# SPDX-License-Identifier: AGPL-3.0-or-later
"""relay_core.integration_service — the integration coordinator (card #AMQQ, B1 of #3MH4).

One object per repository, `IntegrationService(repo, state_root=None, cache_root=None)`, wires
the four phase-1 modules of `docs/TREES-AND-LANDING.md` into the *sole publication path*:

* A1 `relay_core.trees` — canonical identity, `mode` (legacy / queue / paused) and workspaces;
* A2 `relay_core.landq` — the durable verification and publication queue;
* A3 `relay_core.projectconf` / `integration_slots` / `main_release` — the accepted policy, the
  required gate, host admission and the runnable-main release channel;
* A4 `relay_core.reconcile` — automatic AI reconciliation on the weighted High tier.

What the service adds on top of them:

**Accepted policy.** `activate` captures `.relay/project.toml` *from the target tip* and stores
it (`policies` table, `settings.accepted_policy_hash`). Every gate runs that stored policy, never
the candidate's own file (invariant 6: a submission cannot approve its own weaker gate), and the
queue is told the accepted hash so a `ready` job publishes only under the policy in force.

**Gates and admission.** `verifier(job, sha, path)` is the queue's required verifier: a code job
runs `projectconf.run_gate` with `RELAY_JOBS` set to the CPUs host admission granted; a metadata
job runs schema checks on the Board files it changes (cards parse with an id, type and status;
threads only ever grow). Admission is acquired by the run loop *before* the queue picks a job, so
a full host defers work instead of failing it.

**Reconciliation.** `reconcile(context)` enriches the queue's raw context from the canonical
Board — the submitted card *and* the cards of what landed on the target since the base, each as
`{id, title, status, summary, done_means}` plus one intent line per side — passes the accepted
policy, and calls `Reconciler.reconcile_sync` with the Board root so its idempotent card notes
are written. The queue still owns commit creation and runs the required gate on any resolution.

**Handoffs.** The queue's outbox (durable, retried with backoff) is drained into `handoffs`:
one row per delivery, addressed to the *author's session* (the workspace that submitted, or the
session that submitted a Board snapshot), plus one idempotent note on the card thread, plus an
optional `wake` callback B2 supplies to prompt that session. Nothing here ever asks the owner.

**Runnable main.** A landing requests a `MainRelease.update` through a coalescing updater that
runs *outside* the publisher lock (a background thread in the daemon, synchronous in `--once`),
so submissions and publications keep flowing while a release builds. `main_status()` surfaces the
installed sha and how far it lags the target.

**Transitions.** Activation is a registry `mode` plus a publication marker in the Git common
directory (`relay-publication.json`) — separate from the config file, default `legacy`. The
marker is what legacy `scripts/land.py` reads before any of its three publishers moves the
target, under a shared `relay-publication.lock`; `activate`/`pause`/`rollback` take that lock
exclusively, which drains in-flight legacy landings. Cutover creates a human branch at the target
tip and moves HEAD *symbolically* — files and index untouched — then refuses if any other
worktree still has the target checked out. Rollback pauses, drains the publisher, keeps every
job, ref and branch, syncs the human checkout only by a fast-forward git itself accepts, and
fails closed otherwise. Nothing here rewrites the human checkout's index or working files.

State (`state_root` is `$XDG_STATE_HOME/relay`, default `~/.local/state/relay`):

    state_root/integration/<repo-id>/service.sqlite3   policies, transitions, submissions,
                                                       handoffs, events
    state_root/integration/<repo-id>/daemon.lock       one run loop per repository
    state_root/integration/<repo-id>/logs/try-<id>/    try runs
    cache_root/integration/<repo-id>/try/<id>/         disposable try checkouts
    <git common dir>/relay-publication.json            the mode legacy publishers read
    <git common dir>/relay-publication.lock            the transition lock

CLI (`scripts/relay-land`, or `python -m relay_core.integration_service`): `run [--once]`,
`project-init`, `inventory`, `activate [--dry-run]`, `pause`, `rollback`, `try`, `main-status`,
`main-run`, `snapshot`, `handoffs`, `board-submit`, `workspace`. Exit 0 success, 1 usage/config,
2 refused, 3 conflict, 5 gate failure, 7 busy (publisher, admission or transition lock).
"""
from __future__ import annotations

import argparse
try:
    import fcntl
except ImportError:                     # Windows: legacy land.py keeps working there; the
    fcntl = None                        # queue's publication host is Linux/POSIX first.
import json
import os
import re
import secrets
import shlex
import signal
import sqlite3
import subprocess
import sys
import threading
import time
from contextlib import contextmanager
from datetime import datetime, timezone
from pathlib import Path

from . import landq, projectconf, trees

SCHEMA_VERSION = 1
MODES = trees.REPO_MODES                     # legacy, queue, paused
MARKER_NAME = "relay-publication.json"
LOCK_NAME = "relay-publication.lock"
TRANSITION_REF = "refs/relay/transition"
TRY_REF = "refs/relay/try"
DEFAULT_HUMAN_BRANCH = "human"
DEFAULT_INTERVAL = 2.0
DEFAULT_ADMISSION_TIMEOUT = 0.0
DEFAULT_MAIN_RETRY_SECONDS = 60.0            # after a failed main build, before the next try
LEGACY_IDLE_HOURS = 12                       # matches scripts/land.py IDLE_HOURS
HOOK_VERSION = 2
HOOK_REFUSAL = "commit through scripts/land.py; the shared index is never committed here"
HOOK_PROJECT_NAME = "pre-commit.project"
NOTE_AUTHOR = "landq"
KIND_TITLES = {"landed": "landed", "failed": "failed the gate", "conflict": "conflicted",
               "cancelled": "was cancelled", "author_required": "needs its author"}

# The Relay-owned pre-commit hook, version 2 (card #AMQQ). Version 1 (scripts/land.py before
# this card) refused *every* default index, including a linked worktree's private one, so an
# ordinary `git commit` in a Relay workspace could not work. This one refuses only the main
# checkout's shared index — `<common dir>/index`, the one a plain commit reverts other sessions
# from — and chains to a project hook preserved beside it as `pre-commit.project`.
HOOK = r"""#!/bin/sh
# Installed by Relay (scripts/land.py, relay-land). relay-hook-version: HOOK_VERSION_NUMBER
# A commit from the main checkout's shared index reverts whatever landed on the target while
# that index sat there, so git refuses it here. A linked worktree's own index is fine: that is
# a Relay workspace, and its commits are submitted, never pushed onto the target directly.
#     python3 scripts/land.py begin <session> <paths...>      (legacy mode)
#     python3 scripts/land.py commit <session> -m "message"
#     relay-land submit <sha> --request-id <id>               (queue mode)
# Owner's escape hatch: RELAY_ALLOW_SHARED_COMMIT=1 git commit ...
if [ "${RELAY_ALLOW_SHARED_COMMIT:-}" = "1" ]; then
    exit 0
fi

resolve() {
    _d=$(dirname -- "$1")
    _b=$(basename -- "$1")
    if _p=$(cd "$_d" 2>/dev/null && pwd -P); then
        printf '%s/%s\n' "$_p" "$_b"
    else
        printf '%s\n' "$1"
    fi
}

common=$(git rev-parse --git-common-dir)
shared=$(resolve "$common/index")
# Not `--git-path index`: that one honours GIT_INDEX_FILE and would answer with the
# private index we are trying to tell apart from the shared one.
if [ -z "${GIT_INDEX_FILE:-}" ]; then
    mine=$(resolve "$(git rev-parse --absolute-git-dir)/index")
else
    mine=$(resolve "$GIT_INDEX_FILE")
fi

if [ "$mine" = "$shared" ]; then
    if grep -q '"mode": *"queue"' "$common/relay-publication.json" 2>/dev/null; then
        echo "REFUSAL_TEXT (this project publishes through relay-land: work in a Relay workspace and submit)" >&2
    else
        echo "REFUSAL_TEXT" >&2
    fi
    exit 1
fi

# A project hook that was here before Relay's still runs, after this check.
_hooks=$(dirname -- "$0")
if [ -x "$_hooks/pre-commit.project" ]; then
    exec "$_hooks/pre-commit.project" "$@"
fi
exit 0
""".replace("REFUSAL_TEXT", HOOK_REFUSAL).replace("HOOK_VERSION_NUMBER", str(HOOK_VERSION))


class ServiceError(Exception):
    """Base of every refusal; `exit_code` is what the CLI exits with."""
    exit_code = 2


class ServiceUsageError(ServiceError):
    exit_code = 1


class ModeError(ServiceError):
    """The repository is not in the mode the operation needs."""
    exit_code = 2


class TransitionRefused(ServiceError):
    """Activation or rollback found a blocker; nothing was changed."""
    exit_code = 2


class ServiceBusy(ServiceError):
    """A lock (transition, publisher, daemon) is held by another process."""
    exit_code = 7


class GateFailed(ServiceError):
    exit_code = 5


class AdmissionUnavailable(ServiceBusy):
    """Host admission cannot be constructed on this host: nothing that needs capacity runs."""


# --------------------------------------------------------------------------- helpers

def _now() -> str:
    return datetime.now(timezone.utc).isoformat(timespec="microseconds")


def _parse_ts(value: str) -> float:
    try:
        return datetime.fromisoformat(value).timestamp()
    except (TypeError, ValueError):
        return 0.0


def default_state_root() -> Path:
    return landq.default_state_root()


def default_cache_root() -> Path:
    base = os.environ.get("RELAY_CACHE_HOME") or os.environ.get("XDG_CACHE_HOME") \
        or str(Path.home() / ".cache")
    return Path(base) / "relay"


def default_land_root() -> Path:
    override = os.environ.get("RELAY_LAND_ROOT")
    if override:
        return Path(override)
    return default_state_root() / "land"


git = landq.git
git_out = landq.git_out


def common_dir(repo) -> Path:
    out = git_out(repo, "rev-parse", "--git-common-dir")
    path = Path(out)
    if not path.is_absolute():
        path = Path(repo) / path
    return path.resolve()


def _atomic_json(path: Path, value) -> None:
    tmp = path.with_name(".%s.%d.tmp" % (path.name, os.getpid()))
    tmp.write_text(json.dumps(value, indent=1, sort_keys=True) + "\n", encoding="utf-8")
    os.replace(tmp, path)


def publication_marker(repo) -> dict:
    """The publication mode legacy publishers read: `<common dir>/relay-publication.json`,
    or `{"mode": "legacy"}` when the file is absent or unreadable (absent means never
    activated; unreadable is reported in `error` and treated as legacy — the marker is only
    ever written whole, atomically)."""
    path = common_dir(repo) / MARKER_NAME
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
    except FileNotFoundError:
        return {"mode": "legacy", "path": str(path), "present": False}
    except (OSError, ValueError) as exc:
        # A marker that exists but cannot be read is not "legacy": that would put a second
        # publisher back after state corruption. Nothing publishes until someone looks.
        return {"mode": "invalid", "path": str(path), "present": True,
                "error": "publication marker unreadable: %s" % exc}
    if not isinstance(data, dict) or data.get("mode") not in MODES:
        return {"mode": "invalid", "path": str(path), "present": True,
                "error": "publication marker has no valid mode (%r)"
                         % (data.get("mode") if isinstance(data, dict) else data)}
    data.update({"path": str(path), "present": True})
    return data


def _require_posix_locks(what="the publication lock"):
    if fcntl is None:
        raise ServiceError("%s needs POSIX file locks; queue publication is Linux/POSIX first "
                           "and this host gets a clear refusal, not an unlocked fallback" % what)


@contextmanager
def transition_lock(repo, *, exclusive=False, timeout=0.0, what="publication"):
    """`<common dir>/relay-publication.lock`: legacy publishers hold it *shared* around a ref
    swap; activate/pause/rollback hold it *exclusive*, which waits for every in-flight legacy
    landing to finish (that is the drain) and keeps new ones out until the marker says what
    the mode is now. `timeout` seconds of waiting, then ServiceBusy."""
    _require_posix_locks("the %s lock" % what)
    path = common_dir(repo) / LOCK_NAME
    fd = os.open(str(path), os.O_CREAT | os.O_RDWR, 0o644)
    deadline = time.monotonic() + float(timeout)
    flag = fcntl.LOCK_EX if exclusive else fcntl.LOCK_SH
    try:
        while True:
            try:
                fcntl.flock(fd, flag | fcntl.LOCK_NB)
                break
            except (BlockingIOError, OSError):
                if time.monotonic() >= deadline:
                    raise ServiceBusy("the %s lock %s is held by another process (%s); "
                                      "wait for it or pass a longer --wait-seconds"
                                      % (what, path, "a transition is in progress" if not
                                         exclusive else "a legacy landing or another "
                                         "transition is in progress"))
                time.sleep(0.05)
        try:
            yield path
        finally:
            fcntl.flock(fd, fcntl.LOCK_UN)
    finally:
        os.close(fd)


def hooks_dir(repo) -> Path:
    out = git_out(repo, "rev-parse", "--git-path", "hooks")
    path = Path(out)
    if not path.is_absolute():
        path = Path(repo) / path
    return path.resolve()


def hook_state(repo) -> dict:
    """What pre-commit hook the repository has: none, Relay's (which version), or a project's."""
    path = hooks_dir(repo) / "pre-commit"
    record = {"path": str(path), "installed": path.exists(), "relay": False, "version": None,
              "project_hook": (hooks_dir(repo) / HOOK_PROJECT_NAME).exists()}
    if not path.exists():
        return record
    text = path.read_text(encoding="utf-8", errors="replace")
    if HOOK_REFUSAL in text:
        record["relay"] = True
        match = re.search(r"relay-hook-version: (\d+)", text)
        record["version"] = int(match.group(1)) if match else 1
    return record


def install_hook(repo, *, force=False) -> dict:
    """Install (or upgrade to) the version-2 Relay hook. A non-Relay hook already there is
    preserved as `pre-commit.project` and chained, never discarded; an up-to-date Relay hook
    is left alone unless `force`."""
    state = hook_state(repo)
    path = Path(state["path"])
    path.parent.mkdir(parents=True, exist_ok=True)
    if state["installed"] and not state["relay"]:
        project = path.parent / HOOK_PROJECT_NAME
        if project.exists() and project.read_bytes() != path.read_bytes():
            raise ServiceError("cannot preserve the project's pre-commit hook: %s already exists"
                               " and differs; move one of them aside" % project)
        os.replace(str(path), str(project))
        os.chmod(str(project), 0o755)
        state["project_hook"] = True
    elif state["relay"] and state["version"] == HOOK_VERSION and not force:
        return {**state, "changed": False}
    path.write_text(HOOK, encoding="utf-8")
    os.chmod(str(path), 0o755)
    return {**hook_state(repo), "changed": True}


def _toml_value(value) -> str:
    if isinstance(value, bool):
        return "true" if value else "false"
    if isinstance(value, int):
        return str(value)
    if isinstance(value, str):
        return json.dumps(value)
    if isinstance(value, (list, tuple)):
        return "[" + ", ".join(_toml_value(v) for v in value) + "]"
    raise ServiceUsageError("cannot write %r to TOML" % (value,))


def render_config_toml(config: dict) -> str:
    """A `.relay/project.toml` for a (partial) config dict: only what `projectconf` reads."""
    lines = ["version = %d" % int(config.get("version", projectconf.SCHEMA_VERSION))]
    for section in ("project", "workspace", "verification", "resources", "main", "reconcile"):
        table = config.get(section)
        if not isinstance(table, dict) or not table:
            continue
        lines.append("")
        lines.append("[%s]" % section)
        for key, value in table.items():
            if value is None:
                lines.append("# %s = \"...\"   # fill in: no suggestion" % key)
                continue
            if isinstance(value, dict):
                if value:
                    lines.append("%s = {%s}" % (key, ", ".join(
                        "%s = %s" % (k, _toml_value(v)) for k, v in value.items())))
                continue
            lines.append("%s = %s" % (key, _toml_value(value)))
    return "\n".join(lines) + "\n"


def _card_refs(text: str) -> list[str]:
    """Card ids a commit message names: `#AB12`, `(#AB12)`, `Board #AB12`."""
    out = []
    for match in re.finditer(r"(?<![\w/])#([A-Z0-9]{4})\b", text):
        ident = match.group(1)
        if ident not in out:
            out.append(ident)
    return out


def _section(body: str, heading: str) -> str:
    lines = body.splitlines()
    wanted = "## " + heading
    for i, line in enumerate(lines):
        if line.strip() == wanted:
            out = []
            for j in range(i + 1, len(lines)):
                if lines[j].startswith("## ") or lines[j].startswith("# "):
                    break
                out.append(lines[j])
            return "\n".join(out).strip()
    return ""


# --------------------------------------------------------------------------- schema

SCHEMA = """
CREATE TABLE IF NOT EXISTS settings (
    key TEXT PRIMARY KEY,
    value TEXT NOT NULL
);
CREATE TABLE IF NOT EXISTS policies (
    hash TEXT PRIMARY KEY,
    config_json TEXT NOT NULL,
    target_sha TEXT,
    captured_at TEXT NOT NULL,
    source TEXT
);
CREATE TABLE IF NOT EXISTS transitions (
    id INTEGER PRIMARY KEY,
    kind TEXT NOT NULL,
    from_mode TEXT NOT NULL,
    to_mode TEXT NOT NULL,
    target_sha TEXT,
    human_branch TEXT,
    head_before TEXT,
    detail_json TEXT NOT NULL DEFAULT '{}',
    created_at TEXT NOT NULL
);
CREATE TABLE IF NOT EXISTS submissions (
    job_id TEXT PRIMARY KEY,
    session TEXT,
    workspace_id TEXT,
    card TEXT,
    kind TEXT NOT NULL,
    created_at TEXT NOT NULL
);
CREATE TABLE IF NOT EXISTS handoffs (
    id INTEGER PRIMARY KEY,
    delivery_key TEXT NOT NULL UNIQUE,
    job_id TEXT NOT NULL,
    kind TEXT NOT NULL,
    session TEXT,
    workspace_id TEXT,
    card TEXT,
    text TEXT NOT NULL,
    payload_json TEXT NOT NULL,
    created_at TEXT NOT NULL,
    acked_at TEXT
);
CREATE INDEX IF NOT EXISTS handoffs_session ON handoffs(session);
CREATE TABLE IF NOT EXISTS events (
    id INTEGER PRIMARY KEY,
    kind TEXT NOT NULL,
    payload_json TEXT NOT NULL,
    created_at TEXT NOT NULL
);
"""


# --------------------------------------------------------------------------- the service

class IntegrationService:
    """The coordinator for one repository. Construct freely; locks serialise what must be."""

    def __init__(self, repo, *, state_root=None, cache_root=None, admission=None,
                 reconciler=None, wake=None, admission_timeout=DEFAULT_ADMISSION_TIMEOUT,
                 land_root=None, register=True, main_retry_seconds=DEFAULT_MAIN_RETRY_SECONDS):
        self.repo = Path(repo).resolve()
        if git(self.repo, "rev-parse", "--git-dir", check=False).returncode != 0:
            raise ServiceUsageError("%s is not a git repository" % self.repo)
        self.state_root = Path(state_root) if state_root else default_state_root()
        self.cache_root = Path(cache_root) if cache_root else default_cache_root()
        self.land_root = Path(land_root) if land_root else default_land_root()
        record = trees.resolve_project(self.repo, state_root=self.state_root)
        if record is None:
            if not register:
                raise ServiceUsageError("%s is not a registered repository" % self.repo)
            record = trees.register_repo(self.repo, state_root=self.state_root)
        self.registry = record
        self.repo_id = str(record["id"])
        self.target = str(record.get("target") or "main")
        self.project_root = Path(record.get("project_root") or self.repo)
        self.common_dir = common_dir(self.repo)
        board_root = record.get("board_root")
        if not board_root:
            from . import board as board_mod
            found = board_mod.board_folder(self.project_root)
            board_root = str(found) if found else str(self.project_root / ".board")
        self.board_root = Path(board_root)
        self.root = self.state_root / "integration" / self.repo_id
        self.root.mkdir(parents=True, exist_ok=True)
        self.db_path = self.root / "service.sqlite3"
        self.daemon_lock_path = self.root / "daemon.lock"
        self.logs_root = self.root / "logs"
        self._admission = admission
        self._admission_failed = None
        self._reconciler = reconciler
        self.wake = wake
        self.admission_timeout = float(admission_timeout)
        self._queue = None
        self._release = None
        self._grant = None
        self._updater = None
        self._main_retry_after = 0.0
        self.main_retry_seconds = float(main_retry_seconds)
        self._migrate()

    # ----------------------------------------------------------------- storage

    def _connect(self) -> sqlite3.Connection:
        conn = sqlite3.connect(str(self.db_path), timeout=10.0, isolation_level=None)
        conn.row_factory = sqlite3.Row
        conn.execute("PRAGMA busy_timeout=10000")
        conn.execute("PRAGMA journal_mode=WAL")
        conn.execute("PRAGMA foreign_keys=ON")
        return conn

    @contextmanager
    def _tx(self):
        conn = self._connect()
        try:
            conn.execute("BEGIN IMMEDIATE")
            try:
                yield conn
            except BaseException:
                conn.execute("ROLLBACK")
                raise
            conn.execute("COMMIT")
        finally:
            conn.close()

    def _migrate(self):
        with self._tx() as conn:
            version = conn.execute("PRAGMA user_version").fetchone()[0]
            if version > SCHEMA_VERSION:
                raise ServiceError("service schema %d is newer than this code (%d): %s"
                                   % (version, SCHEMA_VERSION, self.db_path))
            if version < SCHEMA_VERSION:
                for statement in SCHEMA.split(";"):
                    if statement.strip():
                        conn.execute(statement)
                conn.execute("PRAGMA user_version=%d" % SCHEMA_VERSION)

    def _setting(self, key, default=None):
        with self._tx() as conn:
            row = conn.execute("SELECT value FROM settings WHERE key=?", (key,)).fetchone()
        return row["value"] if row else default

    def _set_setting(self, conn, key, value):
        if value is None:
            conn.execute("DELETE FROM settings WHERE key=?", (key,))
        else:
            conn.execute("INSERT OR REPLACE INTO settings(key, value) VALUES (?,?)",
                         (key, str(value)))

    def _event(self, conn, kind, payload):
        conn.execute("INSERT INTO events(kind, payload_json, created_at) VALUES (?,?,?)",
                     (kind, json.dumps(payload, sort_keys=True, default=str), _now()))

    def events(self, *, since_id=0, limit=200) -> list[dict]:
        """Service events newest last: `main_moved`, `handoff`, `transition`, `main_release`."""
        with self._tx() as conn:
            rows = conn.execute("SELECT * FROM events WHERE id>? ORDER BY id LIMIT ?",
                                (int(since_id), int(limit))).fetchall()
        return [{"id": r["id"], "kind": r["kind"], "created_at": r["created_at"],
                 **json.loads(r["payload_json"])} for r in rows]

    # ----------------------------------------------------------------- collaborators

    @property
    def queue(self) -> landq.Queue:
        if self._queue is None:
            self._queue = landq.Queue(self.project_root, state_root=self.state_root,
                                      repo_id=self.repo_id, target=self.target)
        return self._queue

    @property
    def trees(self) -> trees.TreeManager:
        return trees.TreeManager(self.repo_id, state_root=self.state_root)

    @property
    def admission(self):
        """Host admission, constructed once; None (with `_admission_failed` saying why) when
        this host cannot account for capacity — and then nothing that needs capacity runs."""
        if self._admission_failed is not None:
            return None
        if self._admission is None:
            from . import integration_slots
            try:
                self._admission = integration_slots.HostAdmission(state_root=self.state_root)
            except integration_slots.AdmissionError as exc:
                self._admission_failed = str(exc)
        return self._admission

    @property
    def reconciler(self):
        if self._reconciler is None:
            from . import reconcile
            self._reconciler = reconcile.Reconciler(state_root=self.state_root)
        return self._reconciler

    @property
    def release(self):
        if self._release is None:
            from . import main_release
            self._release = main_release.MainRelease(self.project_root, state_root=self.state_root,
                                                     cache_root=self.cache_root,
                                                     repo_id=self.repo_id)
        return self._release

    # ----------------------------------------------------------------- identity / mode

    def registry_record(self) -> dict:
        record = trees.resolve_project(self.repo, state_root=self.state_root)
        if record is not None:
            self.registry = record
            self.target = str(record.get("target") or self.target)
        return self.registry

    def mode_detail(self) -> tuple[str, str]:
        """(mode, reason). The mode in force is the registry's, which activation keeps equal to
        the marker's. When they disagree, or the marker is present but unreadable or unknown,
        nothing publishes: `paused`, with the reason — never `legacy`, which would put a
        second writer back after state corruption."""
        registry = str(self.registry_record().get("mode") or "legacy")
        marker = publication_marker(self.repo)
        marked = marker.get("mode", "legacy")
        if marked == "invalid":
            return "paused", marker.get("error") or "publication marker invalid"
        if registry == marked:
            return registry, ""
        return "paused", ("registry says %s but %s says %s; run `relay-land activate` or "
                          "`relay-land rollback` to make them agree" % (registry, MARKER_NAME, marked))

    def mode(self) -> str:
        return self.mode_detail()[0]

    def target_sha(self) -> str | None:
        proc = git(self.repo, "rev-parse", "--verify", "--quiet", "refs/heads/%s" % self.target,
                   check=False)
        return proc.stdout.strip() or None

    def _head(self, cwd=None) -> dict:
        cwd = cwd or self.project_root
        sym = git(cwd, "symbolic-ref", "--quiet", "HEAD", check=False).stdout.strip()
        sha = git(cwd, "rev-parse", "--verify", "--quiet", "HEAD", check=False).stdout.strip()
        branch = sym[len("refs/heads/"):] if sym.startswith("refs/heads/") else None
        return {"branch": branch, "sha": sha or None, "detached": not sym}

    def worktrees(self) -> list[dict]:
        out = git_out(self.repo, "worktree", "list", "--porcelain")
        entries, current = [], None
        for line in out.splitlines():
            if line.startswith("worktree "):
                current = {"path": line[len("worktree "):], "branch": None, "detached": False,
                           "head": None}
                entries.append(current)
            elif current is not None and line.startswith("HEAD "):
                current["head"] = line[5:]
            elif current is not None and line.startswith("branch "):
                current["branch"] = line[len("branch refs/heads/"):]
            elif current is not None and line == "detached":
                current["detached"] = True
        return entries

    def target_attached(self) -> list[str]:
        return [w["path"] for w in self.worktrees() if w["branch"] == self.target]

    # ----------------------------------------------------------------- accepted policy

    def accepted_policy(self) -> dict | None:
        """`{hash, config, target_sha, captured_at}` of the policy in force, or None before
        activation. Publication requires it; a config file alone enables nothing."""
        hash_ = self._setting("accepted_policy_hash")
        if not hash_:
            return None
        with self._tx() as conn:
            row = conn.execute("SELECT * FROM policies WHERE hash=?", (hash_,)).fetchone()
        if row is None:
            return None
        return {"hash": row["hash"], "config": json.loads(row["config_json"]),
                "target_sha": row["target_sha"], "captured_at": row["captured_at"],
                "source": row["source"]}

    def capture_policy(self, *, revision=None, accept=True, wait_seconds=0.0) -> dict:
        """Load `.relay/project.toml` at `revision` (default: the target tip), normalize it,
        store it, and — with `accept` — make it the policy in force. Explicit: a candidate that
        changes the config still runs the old policy until someone runs this. Takes the
        transition lock exclusively, so no gate is mid-flight under the policy being replaced."""
        with transition_lock(self.repo, exclusive=True, timeout=wait_seconds, what="transition"):
            return self._capture_policy_locked(revision=revision, accept=accept)

    def _capture_policy_locked(self, *, revision=None, accept=True) -> dict:
        revision = revision or self.target_sha()
        if not revision:
            raise TransitionRefused("target branch %s does not exist" % self.target)
        try:
            config = projectconf.load(self.project_root, revision=revision, require_file=True)
        except projectconf.ProjectConfigError as exc:
            raise TransitionRefused("project config refused: %s" % exc) from exc
        hash_ = projectconf.policy_hash(config)
        with self._tx() as conn:
            conn.execute("INSERT OR IGNORE INTO policies(hash, config_json, target_sha, captured_at,"
                         " source) VALUES (?,?,?,?,?)",
                         (hash_, json.dumps(config, sort_keys=True), revision, _now(),
                          ".relay/project.toml@%s" % revision))
            if accept:
                self._set_setting(conn, "accepted_policy_hash", hash_)
                self._event(conn, "policy_accepted", {"hash": hash_, "target_sha": revision})
        return {"hash": hash_, "config": config, "target_sha": revision, "accepted": accept}

    def _require_policy(self) -> dict:
        accepted = self.accepted_policy()
        if accepted is None:
            raise ModeError("no accepted policy is registered for %s: run `relay-land activate`"
                            " (or capture-policy) first" % self.repo_id)
        return accepted

    # ----------------------------------------------------------------- workspaces (B2 API)

    def _require_mode(self, *allowed, what):
        mode = self.mode()
        if mode not in allowed:
            raise ModeError("%s is refused while %s is in %s mode (needs %s)"
                            % (what, self.repo_id, mode, " or ".join(allowed)))
        return mode

    def allocate_workspace(self, session, *, card=None, base=None, includes=(),
                           init=None, retry=False) -> dict:
        """A development workspace for `session` (idempotent per active session), with the
        accepted policy's exclusions, init and quota. Refused outside queue mode: a pane must
        not silently fall back to shared development, and must not allocate for a legacy
        project either."""
        self._require_mode("queue", what="workspace allocation")
        cfg = self._require_policy()["config"]
        ws = cfg["workspace"]
        record = self.trees.create(str(session), card=card, base=base,
                                   excludes=tuple(ws["exclude"]), includes=tuple(includes),
                                   init=init if init is not None else (ws["init"] or None),
                                   max_workspaces=int(ws["max_workspaces"]), retry=retry)
        return self.tree_status(record["id"])

    def release_workspace(self, workspace_id, *, owner) -> dict:
        return self.trees.release(workspace_id, owner=owner)

    def sync_workspace(self, workspace_id, *, owner) -> dict:
        return self.trees.sync(workspace_id, owner=owner)

    def remove_workspace(self, workspace_id) -> dict:
        """Cleanup with the receipt of the workspace's landed job, when there is one."""
        receipt = None
        for job in reversed(self.queue.status()):
            if job.get("workspace_id") == workspace_id and job["status"] == "landed":
                receipt = self.queue.receipt(job["id"])
                break
        return self.trees.remove(workspace_id, receipt=receipt)

    def workspace_tip(self, workspace_id) -> tuple[dict, str]:
        record = self.trees.get(workspace_id)
        tip = git_out(self.repo, "rev-parse", "--verify", "refs/heads/%s" % record["branch"])
        return record, tip

    def tree_status(self, workspace_id) -> dict:
        """The protocol's `tree_status` record for one workspace, plus its latest job."""
        record = self.trees.get(workspace_id)
        proc = git(self.repo, "rev-parse", "--verify", "--quiet",
                   "refs/heads/%s" % record["branch"], check=False)
        tip = proc.stdout.strip() or None
        latest = None
        for job in reversed(self.queue.status()):
            if job.get("workspace_id") == workspace_id:
                latest = self._job_row(job)
                break
        state = record["status"]
        reason = record.get("init_error") or ""
        recoverable = state in ("creating", "init_failed")
        return {"project_root": str(self.project_root), "board_root": str(self.board_root),
                "repo_id": self.repo_id, "workspace_id": record["id"],
                "execution_cwd": record["path"], "branch": record["branch"],
                "base_sha": record["base_sha"], "tip": tip,
                "unlanded": bool(tip and tip != record["base_sha"]),
                "session": record["session"], "card": record["card"], "owner": record["owner"],
                "state": state, "recoverable": recoverable, "reason": reason, "job": latest,
                "mode": self.mode()}

    # ----------------------------------------------------------------- submissions

    def _record_submission(self, job, *, session, workspace_id, card):
        with self._tx() as conn:
            conn.execute("INSERT OR IGNORE INTO submissions(job_id, session, workspace_id, card,"
                         " kind, created_at) VALUES (?,?,?,?,?,?)",
                         (job["id"], session, workspace_id, card, job["kind"], _now()))

    def submit(self, sha=None, *, request_id, workspace_id=None, card=None, session=None,
               selected_tests=(), kind="code") -> dict:
        """Record an immutable submission. With `workspace_id` and no `sha`, the workspace's
        branch tip is what is submitted (and its base is the job's base); the workspace's
        session becomes the handoff address. Refused outside queue mode."""
        self._require_mode("queue", what="submission")
        self._require_policy()
        base = None
        if workspace_id:
            record, tip = self.workspace_tip(workspace_id)
            if sha is None:
                sha = tip
            base = record["base_sha"]
            session = session or record["session"]
            card = card or (record.get("card") or None)
        if sha is None:
            raise ServiceUsageError("submit needs a commit sha or a workspace id")
        job = self.queue.submit(sha, request_id=request_id, workspace_id=workspace_id,
                                card=card, base_sha=base, kind=kind,
                                selected_tests=selected_tests)
        self._record_submission(job, session=session, workspace_id=workspace_id, card=card)
        return job

    def submit_board_snapshot(self, paths, *, session, message=None) -> dict | None:
        """A metadata job from the canonical Board's working copies of `paths`: a commit on the
        target tip holding exactly those files as they are now. Returns None when they already
        match the tip. Later Board edits stay in the canonical Board and become later jobs."""
        self._require_mode("queue", what="a Board snapshot")
        self._require_policy()
        rel = []
        board_rel = os.path.relpath(self.board_root, self.project_root).replace(os.sep, "/")
        for given in paths:
            path = str(given).replace(os.sep, "/")
            if os.path.isabs(path):
                path = os.path.relpath(path, self.project_root).replace(os.sep, "/")
            path = os.path.normpath(path).replace(os.sep, "/")
            if path != board_rel and not path.startswith(board_rel + "/"):
                raise ServiceUsageError("a Board snapshot lands only paths under %s/: %s"
                                        % (board_rel, path))
            if any(seg in ("..", "") for seg in path.split("/")):
                raise ServiceUsageError("path climbs out of the project: %s" % path)
            if path not in rel:
                rel.append(path)
        if not rel:
            raise ServiceUsageError("no Board paths given")
        tip = self.target_sha()
        if not tip:
            raise ModeError("target branch %s does not exist" % self.target)
        index = self.root / ("board-index-%d-%s" % (os.getpid(), secrets.token_hex(4)))
        env = {"GIT_INDEX_FILE": str(index)}
        try:
            git(self.repo, "read-tree", tip, env=env)
            for path in rel:
                full = self.project_root / path
                if full.is_symlink() or not full.is_file():
                    git(self.repo, "update-index", "--force-remove", "--", path, env=env)
                    continue
                blob = git_out(self.repo, "hash-object", "-w", "--", str(full))
                mode = "100755" if os.access(str(full), os.X_OK) else "100644"
                git(self.repo, "update-index", "--add", "--cacheinfo",
                    "%s,%s,%s" % (mode, blob, path), env=env)
            tree = git_out(self.repo, "write-tree", env=env)
        finally:
            try:
                os.unlink(index)
            except OSError:
                pass
        if tree == git_out(self.repo, "rev-parse", tip + "^{tree}"):
            return None
        # Idempotent on content: the same Board bytes on the same tip are the same job, whatever
        # the message or the clock say (a pane retries its end-of-turn sync).
        request_id = "board:%s:%s" % (tip[:12], tree)
        for job in self.queue.status():
            if job["request_id"] == request_id:
                job["existing"] = True
                return job
        text = message or "board: %s" % (session or "snapshot")
        sha = git_out(self.repo, "commit-tree", tree, "-p", tip,
                      stdin="%s\n\nRelay-Board-Snapshot: %s\n" % (text.strip(), session or ""))
        job = self.queue.submit(sha, request_id=request_id, kind="metadata", base_sha=tip)
        cards = sorted({c for p in rel for c in _card_refs(" #" + Path(p).stem.upper())
                        if p.startswith(board_rel + "/threads/")})
        self._record_submission(job, session=session, workspace_id=None,
                                card=cards[0] if len(cards) == 1 else None)
        return job

    def _submission(self, job_id) -> dict:
        with self._tx() as conn:
            row = conn.execute("SELECT * FROM submissions WHERE job_id=?", (job_id,)).fetchone()
        return dict(row) if row else {}

    def _session_for(self, job, sub=None) -> str | None:
        """The author address of a job: the session recorded at submission, else the session
        of the workspace it came from (a submission made through the queue CLI directly)."""
        sub = self._submission(job["id"]) if sub is None else sub
        if sub.get("session"):
            return sub["session"]
        workspace_id = job.get("workspace_id") or sub.get("workspace_id")
        if workspace_id:
            try:
                return self.trees.get(workspace_id).get("session") or None
            except trees.TreeError:
                return None
        return None

    def _job_row(self, job) -> dict:
        sub = self._submission(job["id"])
        return {"id": job["id"], "card": job.get("card"), "workspace_id": job.get("workspace_id"),
                "session": self._session_for(job, sub), "kind": job["kind"], "status": job["status"],
                "reason": job.get("reason"), "age_seconds": job.get("age_seconds"),
                "candidate_sha": job.get("candidate_sha"), "published_sha": job.get("published_sha"),
                "submitted_sha": job["submitted_sha"], "request_id": job["request_id"],
                "created_at": job["created_at"], "updated_at": job["updated_at"]}

    def queue_status(self, *, main=True) -> dict:
        """The protocol's `queue_status`: `repo_id, jobs[]` (+ `main_release`)."""
        jobs = [self._job_row(j) for j in self.queue.status()]
        out = {"repo_id": self.repo_id, "target": self.target, "target_sha": self.target_sha(),
               "mode": self.mode(), "jobs": jobs}
        if main:
            try:
                out["main_release"] = self.main_status()
            except Exception as exc:  # noqa: BLE001 — status never fails on the channel
                out["main_release"] = {"error": str(exc)}
        return out

    # ----------------------------------------------------------------- the verifier

    def verifier(self, job, candidate_sha, candidate_path) -> dict:
        """The queue's required gate, under the accepted policy. Never the candidate's own
        config. A metadata job gets schema checks instead of commands."""
        accepted = self.accepted_policy()
        if accepted is None:
            reason = "no accepted policy registered; publication refused"
            return {"ok": False, "verified": False, "policy_hash": "", "reason": reason,
                    "log": reason + "\n"}
        cfg, hash_ = accepted["config"], accepted["hash"]
        if job["kind"] == "metadata":
            return self._verify_metadata(job, candidate_sha, candidate_path, hash_)
        env = self.gate_env()
        grant = self._grant
        if grant is not None and getattr(grant, "cpus", None):
            env["RELAY_JOBS"] = str(max(1, int(grant.cpus)))
        result = projectconf.run_gate(cfg, candidate_path, selected_tests=job.get("selected_tests")
                                      or (), env=env)
        result["policy_hash"] = hash_
        result["log"] = "%s\n%s" % (" ".join("%s=%s" % kv for kv in sorted(env.items())),
                                    result.get("log", ""))
        return result

    def gate_env(self) -> dict:
        """What a gate gets beyond the scrubbed environment: the *external* build cache. The
        candidate source checkout is disposable and freshly clean (ignored files included, so a
        previous gate's build output or generated module cannot make this one pass); a project
        that wants a warm build puts it under `RELAY_BUILD_DIR` (alias `VERIFY_BUILD`), which
        lives outside the source tree and is bounded by the cache root."""
        build = self.cache_root / "integration" / self.repo_id / "gate-build"
        build.mkdir(parents=True, exist_ok=True)
        return {"RELAY_BUILD_DIR": str(build), "VERIFY_BUILD": str(build)}

    def _verify_metadata(self, job, candidate_sha, candidate_path, policy_hash) -> dict:
        """Schema checks for a Board snapshot: every changed card parses with an id, a known
        type and a status; every changed thread parses and keeps each entry the target had
        (threads are append-only); nothing else under the Board is judged. The queue has
        already refused paths outside the Board, symlinks and submodules."""
        from . import board as board_mod
        tip = job.get("target_sha") or self.target_sha()
        board_rel = os.path.relpath(self.board_root, self.project_root).replace(os.sep, "/")
        changed = git_out(self.repo, "diff-tree", "-r", "--no-renames", "--name-status", tip,
                          candidate_sha).splitlines()
        problems, checked = [], []
        for line in changed:
            if not line.strip():
                continue
            status, _, path = line.partition("\t")
            rel = path[len(board_rel) + 1:] if path.startswith(board_rel + "/") else path
            is_thread = rel.startswith("threads/") or "/threads/" in rel
            if status.startswith("D"):
                if is_thread:
                    problems.append("%s: a thread is never deleted (threads are append-only)" % path)
                else:
                    checked.append("%s: deleted (allowed: cards move and close by policy)" % path)
                continue
            if not path.endswith(".md"):
                checked.append("%s: not a card or thread" % path)
                continue
            blob = git(self.repo, "show", "%s:%s" % (candidate_sha, path), check=False)
            if blob.returncode != 0:
                problems.append("%s: unreadable in the candidate" % path)
                continue
            text = blob.stdout
            if is_thread:
                try:
                    entries = board_mod.parse_thread(text)
                except board_mod.BoardError as exc:
                    problems.append("%s: thread does not parse: %s" % (path, exc))
                    continue
                old = git(self.repo, "show", "%s:%s" % (tip, path), check=False)
                if old.returncode == 0:
                    # Append-only means the old bytes are a prefix of the new bytes: every
                    # entry the target had, unchanged, in the same order, with additions only
                    # after them. Editing an old entry's text is refused, not just dropping it.
                    before = old.stdout
                    if not text.startswith(before):
                        problems.append("%s: thread history changed (old entries must stay "
                                        "byte-for-byte, in order; only additions at the end)" % path)
                        continue
                checked.append("%s: thread ok (%d entries)" % (path, len(entries)))
                continue
            parts = rel.split("/")
            if parts[0] == ".private":
                parts = parts[1:]
            if len(parts) < 2 or parts[-1].upper() in ("BOARD.MD", "README.MD"):
                checked.append("%s: not a card path (not judged)" % path)
                continue
            try:
                card = board_mod.Card.parse(text, path=Path(path))
            except board_mod.BoardError as exc:
                problems.append("%s: card does not parse: %s" % (path, exc))
                continue
            if not card.front:
                checked.append("%s: markdown without front matter (not a card)" % path)
                continue
            known = board_mod.STATUS_FOLDER.get(card.type, {})
            if not card.id:
                problems.append("%s: card has no id" % path)
            elif card.type not in board_mod.CARD_TYPES:
                problems.append("%s: unknown card type %r" % (path, card.type))
            elif not card.status:
                problems.append("%s: card has no status" % path)
            elif card.status not in known:
                problems.append("%s: unknown %s status %r (known: %s)"
                                % (path, card.type, card.status, ", ".join(sorted(known))))
            else:
                checked.append("%s: card #%s ok" % (path, card.id))
        log = "metadata job %s: %d path(s) changed\n%s\n" % (
            job["id"], len(changed), "\n".join(checked + problems))
        if problems:
            return {"ok": False, "verified": False, "policy_hash": policy_hash,
                    "reason": "Board schema check failed: " + "; ".join(problems[:5]), "log": log}
        return {"ok": True, "verified": True, "policy_hash": policy_hash, "log": log,
                "reason": None}

    # ----------------------------------------------------------------- reconciliation

    def card_summary(self, card_id) -> dict | None:
        """`{id, title, status, summary, done_means}` from the canonical Board, or None."""
        from . import board as board_mod
        ident = str(card_id).strip().lstrip("#").upper()
        if not ident:
            return None
        try:
            board = board_mod.Board(self.board_root)
            card = board.card_by_id(ident)
        except (board_mod.BoardError, OSError):
            return None
        if card is None:
            return None
        issue = _section(card.body, "Issue")
        summary = issue.split("\n\n")[0].strip() if issue else ""
        return {"id": ident, "title": card.title, "status": card.status,
                "summary": summary[:1200], "done_means": _section(card.body, "Done means")[:2000]}

    def _target_cards(self, base_sha, target_sha) -> list[str]:
        """Cards of what landed on the target since `base_sha`: `#ID`s named in those commit
        messages, and the cards of `Landq-Job:` trailers the queue knows."""
        if not base_sha or not target_sha or base_sha == target_sha:
            return []
        found = []
        # Landed jobs whose publication lies in base..target: the queue's own record, which
        # covers a fast-forwarded author commit that names no card in its message.
        landed = git(self.repo, "rev-list", "%s..%s" % (base_sha, target_sha), check=False)
        in_range = set(landed.stdout.split())
        for job in self.queue.status():
            if job["status"] == "landed" and job.get("published_sha") in in_range:
                card = str(job.get("card") or "").lstrip("#").upper()
                if card and card not in found:
                    found.append(card)
        proc = git(self.repo, "log", "--format=%s%n%b%n--", "%s..%s" % (base_sha, target_sha),
                   check=False)
        for chunk in proc.stdout.split("\n--\n"):
            for ident in _card_refs(chunk):
                if ident not in found:
                    found.append(ident)
            for match in re.finditer(r"^Landq-Job: (\S+)$", chunk, re.M):
                try:
                    job = self.queue.status(match.group(1))
                except landq.QueueError:
                    continue
                card = str(job.get("card") or "").lstrip("#").upper()
                if card and card not in found:
                    found.append(card)
        return found

    def enrich_context(self, context: dict) -> dict:
        """The queue's raw context plus what the Board knows about both sides and the accepted
        policy: `cards` (dicts, submitted side first), `intents` (one line per side, then the
        Done means), `policy` (the accepted config), `board_root`."""
        context = dict(context)
        accepted = self.accepted_policy()
        submitted_ids = []
        for card in context.get("cards") or ():
            ident = card.get("id") if isinstance(card, dict) else card
            ident = str(ident or "").lstrip("#").upper()
            if ident and ident not in submitted_ids:
                submitted_ids.append(ident)
        target_ids = [i for i in self._target_cards(context.get("base_sha"), context.get("target_sha"))
                      if i not in submitted_ids]
        cards, intents = [], []
        for side, ids in (("submitted", submitted_ids), ("target", target_ids[:6])):
            for ident in ids:
                info = self.card_summary(ident) or {"id": ident, "title": "", "status": "",
                                                     "summary": "", "done_means": ""}
                info["side"] = side
                cards.append(info)
                line = "%s side, card #%s: %s" % (side, ident, info["title"] or "(no title)")
                if info["summary"]:
                    line += " — " + info["summary"]
                intents.append(line)
                if info["done_means"]:
                    intents.append("#%s done means: %s" % (ident, info["done_means"]))
        if not intents:
            intents.append("submitted side: %s (no card named)" % str(context.get("submitted_sha") or "")[:12])
        context["cards"] = cards
        context["intents"] = intents
        context["board_root"] = str(self.board_root)
        if accepted is not None:
            context["policy"] = accepted["config"]
            context["policy_hash"] = accepted["hash"]
        return context

    def reconcile(self, context: dict) -> dict:
        """The queue's reconcile callback: enrich, then the A4 reconciler on the candidate.
        Only a textual merge conflict is reconciled; a gate failure (the queue's optional repair
        round) goes straight back to the author — the reconciler resolves conflicted files, and
        spending a High-tier call on a failing test suite with no conflict to resolve is not
        automatic reconciliation, it is guessing."""
        diagnostics = context.get("diagnostics")
        if isinstance(diagnostics, dict) and diagnostics.get("kind") == "gate_failure":
            return {"status": "author_required",
                    "reason": "gate failure is returned to the author; reconciliation resolves "
                              "merge conflicts only"}
        enriched = self.enrich_context(context)
        result = self.reconciler.reconcile_sync(enriched, board_root=str(self.board_root)
                                                if self.board_root.is_dir() else None)
        return result

    # ----------------------------------------------------------------- the run loop

    def _pickable(self) -> dict | None:
        for job in self.queue.status():
            if job["status"] in landq.PICKABLE:
                return job
        return None

    def _require_admission(self, what):
        """Host admission, or a refusal with the reason it is unavailable. A code gate or a
        main build never runs with capacity unaccounted for."""
        adm = self.admission
        if adm is None:
            raise AdmissionUnavailable("%s refused: host admission is unavailable (%s)"
                                       % (what, self._admission_failed or "not constructed"))
        return adm

    @contextmanager
    def _admitted(self, job):
        """Host admission for one code job; a metadata job (schema checks only) needs none."""
        if job["kind"] != "code":
            yield None
            return
        adm = self._require_admission("the gate for job %s" % job["id"])
        res = self._require_policy()["config"]["resources"]
        grant = adm.acquire(self.repo_id, job["id"], memory_bytes=int(res["memory_bytes"]),
                            disk_bytes=int(res["disk_bytes"]), cpus=float(res["cpus"]),
                            priority="land", timeout=self.admission_timeout)
        try:
            self._grant = grant
            yield grant
        finally:
            self._grant = None
            grant.release()

    def run_once(self, *, main=True) -> dict:
        """One tick: pick the oldest job (under admission), publish it if its gate passes,
        request the runnable-main update, deliver handoffs. Returns what happened.

        The mode and policy check, the gate and the publication happen under the *shared*
        transition lock: `activate`/`pause`/`rollback`/`capture-policy` take it exclusively,
        so a transition drains this tick and no tick starts under a mode or policy that is
        being replaced (lock order everywhere: transition, then publisher)."""
        try:
            with transition_lock(self.repo, exclusive=False, timeout=0, what="transition"):
                return self._run_once_locked(main=main)
        except ServiceBusy as exc:
            if isinstance(exc, AdmissionUnavailable):
                raise
            return {"skipped": "transition in progress: %s" % exc,
                    "delivered": self.deliver_handoffs()}

    def _run_once_locked(self, *, main=True) -> dict:
        mode, reason = self.mode_detail()
        if mode != "queue":
            return {"skipped": "mode %s%s" % (mode, ": %s" % reason if reason else ""),
                    "delivered": self.deliver_handoffs()}
        accepted = self.accepted_policy()
        if accepted is None:
            return {"skipped": "no accepted policy", "delivered": self.deliver_handoffs()}
        job = self._pickable()
        if job is None and any(j["status"] in landq.IN_FLIGHT for j in self.queue.status()):
            # Rows a dead publisher left mid-flight: settle them (landed if the ref already
            # moved, interrupted otherwise) — unless another live publisher holds the lock.
            try:
                self.queue.recover()
            except landq.PublisherBusy:
                return {"idle": True, "busy": self.queue.lock_holder(),
                        "delivered": self.deliver_handoffs()}
            job = self._pickable()
        if job is None:
            return {"idle": True, "delivered": self.deliver_handoffs()}
        from . import integration_slots
        try:
            with self._admitted(job) as grant:
                before = self.target_sha()
                # reconcile_attempts=0: a failed gate is the author's, not a repair round
                # (see `reconcile`); only merge conflicts are reconciled automatically.
                result = self.queue.process_one(self.verifier, reconcile=self.reconcile,
                                                accepted_policy_hash=accepted["hash"],
                                                reconcile_attempts=0)
        except integration_slots.AdmissionError as exc:
            return {"skipped": "admission: %s" % exc, "job_id": job["id"],
                    "delivered": self.deliver_handoffs()}
        except AdmissionUnavailable as exc:
            return {"skipped": str(exc), "job_id": job["id"], "delivered": self.deliver_handoffs()}
        out = {"job": self._job_row(result) if result else None,
               "granted_cpus": getattr(grant, "cpus", None) if grant else None}
        if result and result["status"] == "landed" and result.get("published_sha"):
            with self._tx() as conn:
                self._event(conn, "main_moved", {"repo_id": self.repo_id, "previous_sha": before,
                                                 "sha": result["published_sha"], "job_id": result["id"]})
            if main:
                out["main_release"] = self.request_main_update(result["published_sha"], wait=True)
        elif main and job["kind"] == "code" and self.main_wanted() and self._updater is None:
            out["main_release"] = self.ensure_main()
        out["delivered"] = self.deliver_handoffs()
        return out

    def run(self, *, interval=DEFAULT_INTERVAL, stop=None, main=True, max_ticks=None,
            log=None) -> dict:
        """The daemon: one per repository (`daemon.lock`), ticking every `interval` seconds
        until `stop` (a threading.Event) is set, SIGTERM/SIGINT arrives, or `max_ticks` ran.
        Main releases build on a background updater so publication never waits for a build."""
        _require_posix_locks("the run loop's daemon lock")
        stop = stop or threading.Event()
        log = log or (lambda line: None)
        fd = os.open(str(self.daemon_lock_path), os.O_CREAT | os.O_RDWR, 0o600)
        try:
            try:
                fcntl.flock(fd, fcntl.LOCK_EX | fcntl.LOCK_NB)
            except (BlockingIOError, OSError):
                holder = ""
                try:
                    holder = self.daemon_lock_path.read_text().strip()
                except OSError:
                    pass
                raise ServiceBusy("a run loop for %s is already running (%s)"
                                  % (self.repo_id, holder or "pid unknown"))
            os.ftruncate(fd, 0)
            os.write(fd, ("%d %s\n" % (os.getpid(), _now())).encode())
            self._write_daemon_state({"pid": os.getpid(), "started_at": _now(), "running": True})
            updater = MainUpdater(self, log=log) if main else None
            if updater:
                updater.start()
            self._updater = updater
            ticks, landed, errors = 0, 0, 0
            try:
                while not stop.is_set():
                    ticks += 1
                    try:
                        result = self.run_once(main=False)
                        job = result.get("job")
                        if job and job["status"] == "landed":
                            landed += 1
                        if updater:
                            # Startup, a request lost to a crash, a failed build: the runnable
                            # main follows the target whether or not this tick landed anything.
                            wanted = self.ensure_main(updater=updater)
                            if wanted.get("requested") and not wanted.get("in_progress") \
                                    and not wanted.get("deferred"):
                                log("main release requested for %s" % wanted["requested"][:12])
                        if job:
                            log("job %s: %s%s" % (job["id"], job["status"],
                                                  " (%s)" % job["reason"] if job.get("reason") else ""))
                        elif result.get("skipped"):
                            log("skipped: %s" % result["skipped"])
                    except (landq.QueueError, ServiceError) as exc:
                        errors += 1
                        log("tick %d: %s: %s" % (ticks, exc.__class__.__name__, exc))
                    self._write_daemon_state({"pid": os.getpid(), "running": True, "ticks": ticks,
                                              "landed": landed, "errors": errors,
                                              "last_tick_at": _now()})
                    if max_ticks is not None and ticks >= max_ticks:
                        break
                    stop.wait(interval)
            finally:
                if updater:
                    updater.stop()
                self._updater = None
                self._write_daemon_state({"pid": os.getpid(), "running": False, "ticks": ticks,
                                          "landed": landed, "errors": errors, "stopped_at": _now()})
        finally:
            os.close(fd)
        return {"ticks": ticks, "landed": landed, "errors": errors}

    def _write_daemon_state(self, state):
        try:
            _atomic_json(self.root / "daemon.json", state)
        except OSError:
            pass

    def daemon_state(self) -> dict:
        try:
            state = json.loads((self.root / "daemon.json").read_text())
        except (OSError, ValueError):
            state = {"running": False}
        if state.get("running"):
            fd = os.open(str(self.daemon_lock_path), os.O_CREAT | os.O_RDWR, 0o600)
            try:
                fcntl.flock(fd, fcntl.LOCK_EX | fcntl.LOCK_NB)
                fcntl.flock(fd, fcntl.LOCK_UN)
                state["running"] = False        # the lock is free: the daemon is gone
                state["stale"] = True
            except (BlockingIOError, OSError):
                pass
            finally:
                os.close(fd)
        return state

    # ----------------------------------------------------------------- handoffs

    def handoff_text(self, kind, job, payload, sub) -> str:
        sha = payload.get("published_sha") or payload.get("candidate_sha") or job["submitted_sha"]
        head = "Landing job %s (%s%s) %s." % (
            job["id"], job["submitted_sha"][:12],
            " for card #%s" % str(job["card"]).lstrip("#") if job.get("card") else "",
            KIND_TITLES.get(kind, kind))
        lines = [head]
        if kind == "landed":
            lines.append("Published %s onto %s (target was %s)." % (
                sha[:12], self.target, str(payload.get("target_before") or "")[:12]))
            if job.get("workspace_id"):
                lines.append("Your workspace %s may now be released; its receipt is `relay-land "
                             "receipt %s`." % (job["workspace_id"], job["id"]))
        elif kind == "author_required":
            reconcile = payload.get("reconcile") if isinstance(payload.get("reconcile"), dict) else {}
            if reconcile.get("handoff"):
                lines.append(str(reconcile["handoff"]))
            else:
                lines.append("Reason: %s" % (payload.get("reason") or job.get("reason") or "?"))
                if payload.get("conflicts"):
                    lines.append("Conflicts: " + ", ".join(payload["conflicts"]))
                lines.append("Sync your workspace to the current target, resolve there, run the "
                             "project's checks and submit the new commit. Nothing in your "
                             "workspace was changed.")
            if payload.get("log"):
                lines.append("Gate log: %s" % payload["log"])
        elif kind == "failed":
            lines.append("Reason: %s" % (payload.get("reason") or job.get("reason") or "?"))
            if payload.get("log"):
                lines.append("Gate log: %s" % payload["log"])
            lines.append("Fix it in your workspace and submit a new commit; the target was not "
                         "moved.")
        elif kind == "conflict":
            lines.append("Reason: %s" % (payload.get("reason") or job.get("reason") or "?"))
        elif kind == "cancelled":
            lines.append("Nothing was published.")
        return "\n".join(lines)

    def _deliver(self, record: dict) -> None:
        """Outbox sender: a durable handoff row addressed to the author's session, one
        idempotent note on the card thread, then the optional wake. A raise here (a pane that
        cannot be reached) means the outbox retries the whole delivery with backoff; the row
        and the note are idempotent on the delivery key, so a retry only repeats the wake."""
        job = self.queue.status(record["job_id"])
        sub = self._submission(job["id"])
        kind = record["kind"]
        payload = record["payload"] if isinstance(record.get("payload"), dict) else {}
        session = self._session_for(job, sub)
        workspace_id = job.get("workspace_id") or sub.get("workspace_id")
        card = str(job.get("card") or sub.get("card") or "").lstrip("#").upper() or None
        text = self.handoff_text(kind, job, payload, sub)
        handoff = {"delivery_key": record["delivery_key"], "job_id": job["id"], "kind": kind,
                   "session": session, "workspace_id": workspace_id, "card": card, "text": text,
                   "payload": payload}
        with self._tx() as conn:
            cur = conn.execute(
                "INSERT OR IGNORE INTO handoffs(delivery_key, job_id, kind, session, workspace_id,"
                " card, text, payload_json, created_at) VALUES (?,?,?,?,?,?,?,?,?)",
                (record["delivery_key"], job["id"], kind, session, workspace_id, card, text,
                 json.dumps(payload, sort_keys=True, default=str), _now()))
            row = conn.execute("SELECT id FROM handoffs WHERE delivery_key=?",
                               (record["delivery_key"],)).fetchone()
            handoff["id"] = row["id"] if row else cur.lastrowid
            self._event(conn, "handoff", {"repo_id": self.repo_id, "job_id": job["id"],
                                          "kind": kind, "session": session,
                                          "workspace_id": workspace_id, "card": card,
                                          "handoff_id": handoff["id"]})
        if card:
            self._note_card(card, job["id"], kind, text)
        if self.wake is not None:
            self.wake(handoff)

    def _note_card(self, card, job_id, kind, text):
        from . import board as board_mod
        if not self.board_root.is_dir():
            return
        marker = "<!-- landq:%s:%s -->" % (job_id, kind)
        try:
            board = board_mod.Board(self.board_root)
            if board.card_by_id(card) is None:
                return
            if any(marker in (entry.text or "") for entry in board.thread(card)):
                return
            board.append_thread(card, "%s %s" % (text, marker), NOTE_AUTHOR, kind="note")
        except (board_mod.BoardError, OSError):
            return

    def deliver_handoffs(self, *, limit=50) -> list[dict]:
        """Drain the queue's due outbox rows through `_deliver`. Safe to call any time."""
        results = self.queue.deliver_outbox(self._deliver, limit=limit)
        return [{"delivery_key": r["delivery_key"], "kind": r["kind"], "job_id": r["job_id"],
                 "delivered": r["delivered"], "attempts": r.get("attempts"),
                 "last_error": r.get("last_error")} for r in results]

    def handoffs(self, *, session=None, workspace_id=None, pending_only=True) -> list[dict]:
        sql, args = "SELECT * FROM handoffs", []
        where = []
        if session is not None:
            where.append("session=?")
            args.append(session)
        if workspace_id is not None:
            where.append("workspace_id=?")
            args.append(workspace_id)
        if pending_only:
            where.append("acked_at IS NULL")
        if where:
            sql += " WHERE " + " AND ".join(where)
        with self._tx() as conn:
            rows = conn.execute(sql + " ORDER BY id", args).fetchall()
        out = []
        for r in rows:
            d = dict(r)
            d["payload"] = json.loads(d.pop("payload_json"))
            out.append(d)
        return out

    def ack_handoff(self, handoff_id) -> dict:
        with self._tx() as conn:
            conn.execute("UPDATE handoffs SET acked_at=? WHERE id=? AND acked_at IS NULL",
                         (_now(), int(handoff_id)))
            row = conn.execute("SELECT * FROM handoffs WHERE id=?", (int(handoff_id),)).fetchone()
        if row is None:
            raise ServiceUsageError("no such handoff: %s" % handoff_id)
        d = dict(row)
        d["payload"] = json.loads(d.pop("payload_json"))
        return d

    # ----------------------------------------------------------------- runnable main

    def main_configured(self) -> bool:
        accepted = self.accepted_policy()
        cfg = accepted["config"] if accepted else None
        if cfg is None:
            try:
                cfg = projectconf.load(self.project_root)
            except projectconf.ProjectConfigError:
                return False
        main = cfg["main"]
        return bool(main["executable"] and main["install"])

    def request_main_update(self, sha, *, wait=False) -> dict:
        """Ask for `sha` to become the runnable main. In a daemon the updater coalesces it in
        the background; `wait=True` (the `--once` path) builds now. Never under the publisher
        lock: the queue released it before this is called."""
        if not self.main_configured():
            return {"skipped": "main.executable/main.install not configured", "sha": sha}
        if self._updater is not None and not wait:
            self._updater.request(sha)
            return {"requested": sha, "coalesced": True}
        return self._update_main(sha)

    def _update_main(self, sha) -> dict:
        """Build and install `sha` as the runnable main: under host admission at `try`
        priority (a landing outranks a release build), in a child process whose environment
        carries `RELAY_JOBS` from the grant — the release module reads the environment of the
        process it runs in, and this one must not mutate its own. A failure is recorded with a
        retry time so the loop tries again later without waiting for a new landing."""
        from . import integration_slots
        accepted = self._require_policy()
        res = accepted["config"]["resources"]
        try:
            adm = self._require_admission("the main release build")
            grant = adm.acquire(self.repo_id, "main-%s" % sha[:12], memory_bytes=int(res["memory_bytes"]),
                                disk_bytes=int(res["disk_bytes"]), cpus=float(res["cpus"]),
                                priority="try", timeout=self.admission_timeout)
        except (integration_slots.AdmissionError, AdmissionUnavailable) as exc:
            return self._main_failed(sha, "admission: %s" % exc)
        try:
            env = {k: v for k, v in os.environ.items() if not k.startswith("GIT_")}
            env["RELAY_JOBS"] = str(max(1, int(grant.cpus)))
            env["PYTHONPATH"] = os.pathsep.join(
                p for p in (str(Path(__file__).resolve().parents[1]), env.get("PYTHONPATH", "")) if p)
            argv = [sys.executable, "-m", "relay_core.integration_service", "--repo",
                    str(self.project_root), "--state-root", str(self.state_root),
                    "--cache-root", str(self.cache_root), "main-update", sha]
            timeout = float(accepted["config"]["main"].get("timeout_seconds") or 3600) * 4 + 120
            proc = subprocess.run(argv, env=env, cwd=str(self.project_root), capture_output=True,
                                  text=True, timeout=timeout)
        except (OSError, subprocess.SubprocessError) as exc:
            return self._main_failed(sha, "%s: %s" % (exc.__class__.__name__, exc))
        finally:
            grant.release()
        try:
            record = json.loads(proc.stdout) if proc.stdout.strip() else {}
        except ValueError:
            record = {}
        if proc.returncode != 0 or not isinstance(record, dict) or record.get("error"):
            reason = (record.get("error") if isinstance(record, dict) else None) or \
                (proc.stderr.strip().splitlines() or ["exit %d" % proc.returncode])[-1]
            return self._main_failed(sha, reason)
        self._main_retry_after = 0.0
        record["granted_cpus"] = grant.cpus
        with self._tx() as conn:
            self._event(conn, "main_release", {"repo_id": self.repo_id, "sha": record.get("sha"),
                                               "changed": record.get("changed"),
                                               "duration_seconds": record.get("duration_seconds"),
                                               "granted_cpus": grant.cpus})
        return record

    def _main_failed(self, sha, reason) -> dict:
        self._main_retry_after = time.time() + self.main_retry_seconds
        with self._tx() as conn:
            self._event(conn, "main_release", {"repo_id": self.repo_id, "sha": sha,
                                               "error": str(reason)[:500]})
            self._set_setting(conn, "main_last_error", json.dumps(
                {"sha": sha, "error": str(reason)[:500], "at": _now()}))
        return {"sha": sha, "error": str(reason)}

    def main_update_inprocess(self, sha) -> dict:
        """What the `main-update` child runs: the release module on the accepted config. The
        parent holds the admission grant; this process carries its `RELAY_JOBS`."""
        from . import main_release
        accepted = self._require_policy()
        try:
            record = self.release.update(sha, accepted["config"])
        except main_release.MainReleaseError as exc:
            return {"sha": sha, "error": str(exc)}
        record.pop("status", None)
        return record

    def main_wanted(self) -> str | None:
        """The sha the runnable main should be at: the target tip, when the project is in
        queue mode with an accepted policy and a main contract — else None."""
        if self.mode() != "queue" or self.accepted_policy() is None or not self.main_configured():
            return None
        return self.target_sha()

    def ensure_main(self, *, updater=None) -> dict:
        """Ask for the runnable main to catch up with the target: at daemon start (so an idle
        queue still gets a main), after a landing whose request was lost to a crash, and after a
        failed build once `main_retry_seconds` have passed. Returns what it did."""
        wanted = self.main_wanted()
        if wanted is None:
            return {"skipped": "no runnable-main contract in force"}
        status = self.release.status()
        installed = (status.get("current") or {}).get("sha")
        if installed == wanted:
            return {"installed": wanted, "up_to_date": True}
        if updater is not None:
            state = updater.state()
            if wanted in (state.get("building_sha"), state.get("pending_sha")):
                return {"requested": wanted, "in_progress": True}
        if time.time() < self._main_retry_after:
            return {"requested": wanted, "deferred": "retry after a failed build",
                    "retry_in_seconds": round(self._main_retry_after - time.time(), 1)}
        if updater is not None:
            updater.request(wanted)
            return {"requested": wanted, "coalesced": True}
        return self._update_main(wanted)

    def main_status(self) -> dict:
        """The channel's status plus `installed_sha`, `target_sha` and `lag_commits` behind the
        *target* (the release module counts lag behind the newest request only)."""
        target = self.target_sha()
        if not self.main_configured():
            return {"configured": False, "installed_sha": None, "target_sha": target,
                    "lag_commits": None, "current": None, "error": None}
        status = self.release.status()
        current = status.get("current") or {}
        installed = current.get("sha")
        lag = None
        if installed and target and installed != target:
            proc = git(self.repo, "rev-list", "--count", "%s..%s" % (installed, target), check=False)
            if proc.returncode == 0:
                try:
                    lag = int(proc.stdout.strip())
                except ValueError:
                    lag = None
        elif installed and target:
            lag = 0
        executable = None
        accepted = self.accepted_policy()
        if installed and accepted:
            executable = str(Path(current.get("path", "")) / accepted["config"]["main"]["executable"])
        last_error = self._setting("main_last_error")
        status.update({"configured": True, "installed_sha": installed, "target_sha": target,
                       "lag_commits": lag, "executable": executable,
                       "updater": self._updater.state() if self._updater else None,
                       "last_failure": json.loads(last_error) if last_error else None,
                       "retry_in_seconds": round(max(0.0, self._main_retry_after - time.time()), 1)})
        return status

    def main_executable(self) -> Path:
        status = self.main_status()
        if not status.get("configured"):
            raise ModeError("no runnable-main contract: main.executable/main.install are not configured")
        if not status.get("installed_sha"):
            raise ModeError("no runnable main is installed yet (requested %s, error %s)"
                            % (status.get("requested_sha"), status.get("error")))
        path = Path(status["executable"])
        if not path.exists():
            raise ModeError("installed main is incomplete: %s is missing" % path)
        return path

    # ----------------------------------------------------------------- try

    def try_candidate(self, sha, *, selected_tests=(), workspace_id=None, timeout=None) -> dict:
        """Build the candidate the queue would build for `sha` and run the accepted gate on it in
        a disposable checkout — no ref moves, no job recorded, `try` priority admission. Returns
        the gate result with `candidate_sha`, `target_sha` and the log path."""
        accepted = self._require_policy()
        if workspace_id and not sha:
            _, sha = self.workspace_tip(workspace_id)
        sha = landq.resolve_commit(self.repo, sha)
        tip = self.target_sha()
        if not tip:
            raise ModeError("target branch %s does not exist" % self.target)
        token = secrets.token_hex(6)
        log_dir = self.logs_root / ("try-%s" % token)
        log_dir.mkdir(parents=True, exist_ok=True)
        if landq._is_ancestor(self.repo, sha, tip):
            candidate, note = tip, "already contained in the target"
        elif landq._is_ancestor(self.repo, tip, sha):
            candidate, note = sha, "fast-forward"
        else:
            proc = git(self.repo, "merge-tree", "--write-tree", "--name-only", tip, sha, check=False)
            lines = proc.stdout.splitlines()
            if proc.returncode == 1:
                conflicted = []
                for line in lines[1:]:          # names until the blank line, then messages
                    if not line.strip():
                        break
                    conflicted.append(line.strip())
                (log_dir / "merge.log").write_text(proc.stdout + proc.stderr)
                return {"ok": False, "verified": False, "conflict": True, "candidate_sha": None,
                        "target_sha": tip, "submitted_sha": sha, "conflicts": conflicted,
                        "reason": "merge conflict in %s" % ", ".join(conflicted),
                        "log_path": str(log_dir / "merge.log"), "policy_hash": accepted["hash"]}
            if proc.returncode != 0 or not lines:
                raise ServiceError("git merge-tree failed: %s" % proc.stderr.strip())
            tree = lines[0].strip()
            candidate = git_out(self.repo, "commit-tree", tree, "-p", tip, "-p", sha,
                                stdin="try %s onto %s\n" % (sha[:12], self.target))
            note = "merge"
        ref = "%s/%s" % (TRY_REF, token)
        git(self.repo, "update-ref", ref, candidate)
        wt = self.cache_root / "integration" / self.repo_id / "try" / token
        wt.parent.mkdir(parents=True, exist_ok=True)
        from . import integration_slots
        res = accepted["config"]["resources"]
        grant = None
        try:
            adm = self._require_admission("try")
            grant = adm.acquire(self.repo_id, "try-%s" % token, memory_bytes=int(res["memory_bytes"]),
                                disk_bytes=int(res["disk_bytes"]), cpus=float(res["cpus"]),
                                priority="try",
                                timeout=self.admission_timeout if timeout is None else float(timeout))
            git(self.repo, "worktree", "add", "--detach", "--force", str(wt), candidate)
            env = self.gate_env()
            if grant:
                env["RELAY_JOBS"] = str(max(1, int(grant.cpus)))
            result = projectconf.run_gate(accepted["config"], wt, selected_tests=selected_tests, env=env)
        except integration_slots.AdmissionError as exc:
            raise ServiceBusy("try: %s" % exc) from exc
        finally:
            if grant is not None:
                grant.release()
            git(self.repo, "worktree", "remove", "--force", str(wt), check=False)
            git(self.repo, "worktree", "prune", check=False)
            git(self.repo, "update-ref", "-d", ref, check=False)
        (log_dir / "gate.log").write_text(result.get("log", ""))
        result.update({"candidate_sha": candidate, "target_sha": tip, "submitted_sha": sha,
                       "conflict": False, "note": note, "log_path": str(log_dir / "gate.log"),
                       "policy_hash": accepted["hash"]})
        result.pop("log", None)
        return result

    # ----------------------------------------------------------------- inventory

    def legacy_sessions(self) -> list[dict]:
        """Live land.py sessions (ran a command within LEGACY_IDLE_HOURS) that claim paths in
        this repository, from the shared land root's registry."""
        registry = self.land_root / "registry.json"
        try:
            data = json.loads(registry.read_text(encoding="utf-8"))
        except (OSError, ValueError):
            return []
        out = []
        now = time.time()
        for name, entry in (data.get("sessions") or {}).items():
            if not isinstance(entry, dict):
                continue
            repo = entry.get("repo")
            if repo and Path(repo).resolve() != self.project_root.resolve():
                continue
            stamp = entry.get("updated") or entry.get("started") or ""
            idle = (now - _parse_ts(stamp)) / 60.0 if stamp else None
            if idle is not None and idle > LEGACY_IDLE_HOURS * 60:
                continue
            if not (self.land_root / name).is_dir():
                continue
            claims = entry.get("claims") or []
            out.append({"name": name, "contact": entry.get("contact") or "",
                        "claims": len(claims), "idle_minutes": round(idle, 1) if idle is not None else None,
                        "state": entry.get("state") or ""})
        return out

    def legacy_processes(self) -> list[dict]:
        """Running `land.py` publishers (commit, repair, board-sync, try) whose cwd is inside
        this repository — processes that may hold old code with no marker guard."""
        out = []
        proc_root = Path("/proc")
        if not proc_root.is_dir():
            return out
        me = os.getpid()
        for entry in proc_root.iterdir():
            if not entry.name.isdigit() or int(entry.name) == me:
                continue
            try:
                cmdline = (entry / "cmdline").read_bytes().split(b"\0")
            except OSError:
                continue
            argv = [a.decode("utf-8", "replace") for a in cmdline if a]
            if not any(a.endswith("land.py") for a in argv):
                continue
            verbs = [a for a in argv if a in ("commit", "repair", "board-sync", "try")]
            if not verbs:
                continue
            try:
                cwd = Path(os.readlink(str(entry / "cwd")))
            except OSError:
                continue
            try:
                cwd.resolve().relative_to(self.project_root.resolve())
            except ValueError:
                continue
            out.append({"pid": int(entry.name), "verb": verbs[0], "cmdline": " ".join(argv)[:200]})
        return out

    def _dirty(self) -> dict:
        proc = git(self.project_root, "status", "--porcelain", "--ignored", "--untracked-files=all",
                   check=False)
        staged = unstaged = untracked = ignored = 0
        sample = []
        for line in proc.stdout.splitlines():
            if len(line) < 3:
                continue
            x, y = line[0], line[1]
            if line.startswith("!!"):
                ignored += 1
                continue
            if line.startswith("??"):
                untracked += 1
            else:
                if x not in (" ", "?"):
                    staged += 1
                if y not in (" ", "?"):
                    unstaged += 1
            if len(sample) < 12:
                sample.append(line)
        return {"staged": staged, "unstaged": unstaged, "untracked": untracked, "ignored": ignored,
                "sample": sample}

    def inventory(self) -> dict:
        """Everything a cutover decision needs, without changing anything."""
        registry = self.registry_record()
        marker = publication_marker(self.repo)
        head = self._head()
        tip = self.target_sha()
        config = {"present": False, "error": None, "policy_hash": None}
        if tip:
            try:
                cfg = projectconf.load(self.project_root, revision=tip, require_file=True)
                config.update(present=True, policy_hash=projectconf.policy_hash(cfg))
            except projectconf.ProjectConfigError as exc:
                config["error"] = str(exc)
        accepted = self.accepted_policy()
        jobs = self.queue.status()
        try:
            workspaces = self.trees.list()
        except trees.TreeError:
            workspaces = []
        human = self._setting("human_branch") or DEFAULT_HUMAN_BRANCH
        human_sha = git(self.repo, "rev-parse", "--verify", "--quiet", "refs/heads/%s" % human,
                        check=False).stdout.strip() or None
        return {
            "repo_id": self.repo_id, "project_root": str(self.project_root),
            "board_root": str(self.board_root), "common_dir": str(self.common_dir),
            "target": self.target, "target_sha": tip, "mode": self.mode(),
            "registry_mode": registry.get("mode"), "marker": marker, "head": head,
            "human_branch": {"name": human, "sha": human_sha},
            "target_attached": self.target_attached(), "worktrees": self.worktrees(),
            "dirty": self._dirty(),
            "legacy": {"land_root": str(self.land_root), "sessions": self.legacy_sessions(),
                       "processes": self.legacy_processes()},
            "accepted_policy": ({k: accepted[k] for k in ("hash", "target_sha", "captured_at")}
                                if accepted else None),
            "config": config,
            "queue": {"pending": sum(1 for j in jobs if j["status"] in landq.PICKABLE),
                      "in_flight": sum(1 for j in jobs if j["status"] in landq.IN_FLIGHT),
                      "publisher": self.queue.lock_holder() or None},
            "workspaces": {"total": len(workspaces),
                           "active": sum(1 for w in workspaces if w["status"] in trees.LIVE_STATUSES)},
            "hook": hook_state(self.repo), "daemon": self.daemon_state(),
            "admission": None if self.admission is None else self.admission.status(),
        }

    def _activation_blockers(self, inv, human_branch) -> list[str]:
        blockers = []
        if os.name != "posix":
            blockers.append("publication hosts are POSIX/Linux first")
        if not inv["target_sha"]:
            blockers.append("target branch %s does not exist" % self.target)
        if not inv["config"]["present"]:
            blockers.append("no accepted-able config at the target tip: %s"
                            % (inv["config"]["error"] or ".relay/project.toml is absent"))
        for path in inv["target_attached"]:
            if Path(path).resolve() != self.project_root.resolve():
                blockers.append("worktree %s has %s checked out; detach it first" % (path, self.target))
        for proc in inv["legacy"]["processes"]:
            blockers.append("legacy land.py %s is running (pid %d); wait for it" % (proc["verb"], proc["pid"]))
        head = inv["head"]
        if head["branch"] == self.target:
            human_sha = inv["human_branch"]["sha"] if inv["human_branch"]["name"] == human_branch else \
                git(self.repo, "rev-parse", "--verify", "--quiet", "refs/heads/%s" % human_branch,
                    check=False).stdout.strip()
            if human_sha and human_sha != inv["target_sha"]:
                blockers.append("branch %s already exists at %s, not the target tip %s; pick another"
                                " --human-branch or move it" % (human_branch, human_sha[:12],
                                                                 str(inv["target_sha"])[:12]))
        if inv["daemon"].get("running") and inv["mode"] == "legacy":
            pass  # a daemon in legacy mode idles; nothing to block on
        return blockers

    # ----------------------------------------------------------------- transitions

    def _write_marker(self, mode, **extra):
        marker = {"mode": mode, "repo_id": self.repo_id, "target": self.target,
                  "state_root": str(self.state_root), "since": _now(),
                  "human_branch": self._setting("human_branch") or DEFAULT_HUMAN_BRANCH}
        marker.update(extra)
        _atomic_json(self.common_dir / MARKER_NAME, marker)
        return marker

    def _transition(self, conn, kind, from_mode, to_mode, *, target_sha=None, human_branch=None,
                    head_before=None, detail=None):
        conn.execute("INSERT INTO transitions(kind, from_mode, to_mode, target_sha, human_branch,"
                     " head_before, detail_json, created_at) VALUES (?,?,?,?,?,?,?,?)",
                     (kind, from_mode, to_mode, target_sha, human_branch, head_before,
                      json.dumps(detail or {}, sort_keys=True, default=str), _now()))
        self._event(conn, "transition", {"repo_id": self.repo_id, "kind": kind, "from": from_mode,
                                         "to": to_mode, "target_sha": target_sha,
                                         "human_branch": human_branch})

    def transitions(self) -> list[dict]:
        with self._tx() as conn:
            rows = conn.execute("SELECT * FROM transitions ORDER BY id").fetchall()
        out = []
        for r in rows:
            d = dict(r)
            d["detail"] = json.loads(d.pop("detail_json"))
            out.append(d)
        return out

    def activate(self, *, dry_run=False, human_branch=DEFAULT_HUMAN_BRANCH, wait_seconds=0.0,
                 force_hook=False) -> dict:
        """The cutover: drain legacy writers (exclusive transition lock), check the inventory,
        capture the accepted policy from the target tip, keep a baseline ref, move the human
        checkout to `human_branch` symbolically (files and index untouched), write the marker,
        set the registry mode to `queue`, install the hook. `dry_run` reports the plan and the
        blockers and changes nothing. From `paused`, this resumes."""
        with transition_lock(self.repo, exclusive=True, timeout=wait_seconds, what="transition"):
            inv = self.inventory()
            blockers = self._activation_blockers(inv, human_branch)
            plan = []
            head = inv["head"]
            tip = inv["target_sha"]
            if inv["config"]["present"]:
                plan.append("capture .relay/project.toml@%s as the accepted policy (%s)"
                            % (str(tip)[:12], inv["config"]["policy_hash"][:12]))
            plan.append("record baseline %s/<ts>/target = %s" % (TRANSITION_REF, str(tip)[:12]))
            if head["branch"] == self.target:
                plan.append("create branch %s at %s and point HEAD of %s at it symbolically "
                            "(no file or index change)" % (human_branch, str(tip)[:12], self.project_root))
            elif head["branch"]:
                plan.append("HEAD of %s stays on %s (the target is not checked out there)"
                            % (self.project_root, head["branch"]))
            else:
                plan.append("HEAD of %s is detached at %s; it stays" % (self.project_root, head["sha"]))
            plan.append("write %s with mode=queue; registry mode legacy -> queue" % MARKER_NAME)
            hook = inv["hook"]
            if not hook["installed"]:
                plan.append("install the Relay pre-commit hook (version %d)" % HOOK_VERSION)
            elif hook["relay"] and hook["version"] != HOOK_VERSION:
                plan.append("upgrade the Relay pre-commit hook from version %s to %d"
                            % (hook["version"], HOOK_VERSION))
            elif not hook["relay"]:
                plan.append("preserve the project's pre-commit hook as %s and chain it from Relay's"
                            % HOOK_PROJECT_NAME)
            plan.append("legacy land.py commit/repair refuse; board-sync routes Board snapshots to the queue")
            plan.append("then: `relay-land run` (daemon) publishes; panes allocate workspaces")
            if dry_run:
                return {"dry_run": True, "can_activate": not blockers, "blockers": blockers,
                        "plan": plan, "inventory": inv}
            if blockers:
                raise TransitionRefused("activation refused: " + "; ".join(blockers))
            from_mode = inv["mode"]
            policy = self._capture_policy_locked(revision=tip, accept=True)
            stamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")
            git(self.repo, "update-ref", "%s/%s/target" % (TRANSITION_REF, stamp), tip)
            moved = False
            if head["branch"] == self.target:
                exists = git(self.repo, "rev-parse", "--verify", "--quiet",
                             "refs/heads/%s" % human_branch, check=False).returncode == 0
                if not exists:
                    git(self.repo, "branch", "--", human_branch, tip)
                git(self.project_root, "symbolic-ref", "HEAD", "refs/heads/%s" % human_branch)
                moved = True
            with self._tx() as conn:
                self._set_setting(conn, "human_branch", human_branch)
                self._transition(conn, "activate", from_mode, "queue", target_sha=tip,
                                 human_branch=human_branch, head_before=head["branch"] or head["sha"],
                                 detail={"moved_head": moved, "policy_hash": policy["hash"],
                                         "baseline_ref": "%s/%s/target" % (TRANSITION_REF, stamp)})
            marker = self._write_marker("queue", human_branch=human_branch, policy_hash=policy["hash"])
            trees.configure_repo(self.repo_id, state_root=self.state_root, mode="queue")
            hook = install_hook(self.repo, force=force_hook)
            return {"activated": True, "from_mode": from_mode, "mode": "queue", "target_sha": tip,
                    "human_branch": human_branch, "moved_head": moved, "policy_hash": policy["hash"],
                    "marker": marker, "hook": hook, "plan": plan}

    def pause(self, *, wait_seconds=0.0, reason="") -> dict:
        """Stop publication without leaving queue mode: the run loop idles, submissions and
        workspaces continue, legacy publishers still refuse."""
        with transition_lock(self.repo, exclusive=True, timeout=wait_seconds, what="transition"):
            from_mode = self.mode()
            if from_mode == "legacy":
                raise TransitionRefused("pause is for an activated project; %s is in legacy mode"
                                        % self.repo_id)
            with self._tx() as conn:
                self._transition(conn, "pause", from_mode, "paused", target_sha=self.target_sha(),
                                 detail={"reason": reason})
            marker = self._write_marker("paused", reason=reason)
            trees.configure_repo(self.repo_id, state_root=self.state_root, mode="paused")
            return {"paused": True, "from_mode": from_mode, "mode": "paused", "marker": marker}

    def rollback(self, *, wait_seconds=0.0, keep_head=False) -> dict:
        """Back to legacy publication: pause, drain the publisher (its lock must be free), keep
        every job, ref, workspace and the human branch, put HEAD back on the target only when
        that is safe — same commit, or a fast-forward git performs itself with the checkout's
        own safety checks — and fail closed (staying paused) otherwise."""
        with transition_lock(self.repo, exclusive=True, timeout=wait_seconds, what="transition"):
            from_mode = self.mode()
            if from_mode == "legacy":
                return {"rolled_back": False, "mode": "legacy", "note": "already in legacy mode"}
            # 1. pause first: nothing new starts while we drain.
            if from_mode != "paused":
                with self._tx() as conn:
                    self._transition(conn, "pause", from_mode, "paused",
                                     target_sha=self.target_sha(), detail={"reason": "rollback"})
                self._write_marker("paused", reason="rollback")
                trees.configure_repo(self.repo_id, state_root=self.state_root, mode="paused")
            # 2. drain: the publisher lock free means no prepare/verify/publish is in flight.
            daemon = self.daemon_state()
            if daemon.get("running"):
                raise ServiceBusy("rollback: the run loop (pid %s) is still running; stop it first."
                                  " The project stays paused." % daemon.get("pid"))
            try:
                with self.queue._publisher_lock(timeout=wait_seconds):
                    in_flight = [j for j in self.queue.status() if j["status"] in landq.IN_FLIGHT]
            except landq.PublisherBusy as exc:
                raise ServiceBusy("rollback: %s. The project stays paused." % exc) from exc
            # 3. the human checkout.
            head = self._head()
            human = self._setting("human_branch") or DEFAULT_HUMAN_BRANCH
            tip = self.target_sha()
            synced = "unchanged"
            if keep_head:
                synced = "kept (--keep-head)"
            elif head["branch"] == self.target:
                synced = "already on %s" % self.target
            elif head["branch"] == human and tip:
                human_tip = head["sha"]
                if human_tip == tip:
                    git(self.project_root, "symbolic-ref", "HEAD", "refs/heads/%s" % self.target)
                    synced = "HEAD moved symbolically to %s (same commit; files and index untouched)" % self.target
                elif landq._is_ancestor(self.repo, human_tip, tip):
                    staged = self._fast_forward_checkout(human, human_tip, tip)
                    git(self.project_root, "symbolic-ref", "HEAD", "refs/heads/%s" % self.target)
                    synced = "fast-forwarded %s %s -> %s, then HEAD moved to %s" % (
                        human, human_tip[:12], tip[:12], self.target)
                    if staged:
                        synced += "; staged %d Board path(s) already identical to the target: %s" % (
                            len(staged), ", ".join(staged[:6]))
                else:
                    raise TransitionRefused(
                        "rollback: branch %s holds commits at %s that are not on %s (%s); submit or "
                        "merge them first. The project stays paused." % (human, human_tip[:12],
                                                                          self.target, tip[:12]))
            elif head["branch"] is None:
                raise TransitionRefused("rollback: HEAD of %s is detached at %s; check out %s or %s "
                                        "yourself, or pass --keep-head. The project stays paused."
                                        % (self.project_root, head["sha"], self.target, human))
            else:
                raise TransitionRefused("rollback: HEAD of %s is on %s, neither %s nor %s; pass "
                                        "--keep-head to leave it. The project stays paused."
                                        % (self.project_root, head["branch"], self.target, human))
            with self._tx() as conn:
                self._transition(conn, "rollback", "paused", "legacy", target_sha=tip,
                                 human_branch=human, head_before=head["branch"] or head["sha"],
                                 detail={"synced": synced, "in_flight": [j["id"] for j in in_flight],
                                         "jobs": len(self.queue.status())})
            marker = self._write_marker("legacy", human_branch=human)
            trees.configure_repo(self.repo_id, state_root=self.state_root, mode="legacy")
            return {"rolled_back": True, "from_mode": from_mode, "mode": "legacy", "checkout": synced,
                    "human_branch": human, "jobs_retained": len(self.queue.status()),
                    "in_flight_retained": [j["id"] for j in in_flight], "marker": marker}

    def _fast_forward_checkout(self, human, human_tip, tip) -> list[str]:
        """The explicit sync rollback allows: `git merge --ff-only` in the human checkout, with
        git's own refusal to overwrite local changes. One concession, in the safe direction
        only: a Board path git complains about whose working copy is byte-identical to the
        target's blob (the queue landed that very snapshot) is staged to that blob first, so
        the fast-forward can proceed; the index entry then equals the target, never an older
        commit. Anything else fails closed with the paths git named."""
        board_rel = os.path.relpath(self.board_root, self.project_root).replace(os.sep, "/")
        staged = []
        for round_ in range(2):
            ff = git(self.project_root, "merge", "--ff-only", "--no-edit",
                     "refs/heads/%s" % self.target, check=False)
            if ff.returncode == 0:
                return staged
            named = [line.strip() for line in ff.stderr.splitlines() if line.startswith("\t")]
            fixable = []
            for path in named:
                if not (path == board_rel or path.startswith(board_rel + "/")):
                    continue
                full = self.project_root / path
                if full.is_symlink() or not full.is_file():
                    continue
                blob = git(self.repo, "rev-parse", "--verify", "--quiet", "%s:%s" % (tip, path),
                           check=False).stdout.strip()
                if not blob:
                    continue
                mine = git_out(self.repo, "hash-object", "--", str(full))
                if mine == blob:
                    fixable.append((path, blob))
            if round_ == 1 or not fixable or len(fixable) != len(named):
                detail = ff.stderr.strip().splitlines()
                raise TransitionRefused(
                    "rollback: the target advanced to %s past the human branch %s at %s, and git "
                    "refused to fast-forward the checkout over local changes (%s). Nothing was "
                    "committed, stashed or reset on your behalf: sync those paths yourself (a Board "
                    "path newer than the target is a pending snapshot: `relay-land run --once` lands "
                    "it), then run rollback again. The project stays paused."
                    % (tip[:12], human, human_tip[:12], "; ".join(d for d in detail if d)[:600]))
            for path, blob in fixable:
                mode = "100755" if os.access(str(self.project_root / path), os.X_OK) else "100644"
                git(self.project_root, "update-index", "--add", "--cacheinfo",
                    "%s,%s,%s" % (mode, blob, path))
                staged.append(path)
        return staged

    # ----------------------------------------------------------------- snapshot

    def snapshot(self) -> dict:
        """One record for a status surface: identity, mode, policy, jobs, workspaces, main,
        admission, daemon and pending handoffs."""
        accepted = self.accepted_policy()
        try:
            workspaces = [self.tree_status(w["id"]) for w in self.trees.list()]
        except trees.TreeError:
            workspaces = []
        status = self.queue_status(main=True)
        admission = None
        adm = self.admission
        if adm is not None:
            try:
                admission = adm.status()
            except Exception as exc:  # noqa: BLE001
                admission = {"error": str(exc)}
        return {
            "repo_id": self.repo_id, "project_root": str(self.project_root),
            "board_root": str(self.board_root), "target": self.target,
            "target_sha": status["target_sha"], "mode": status["mode"],
            "marker": publication_marker(self.repo),
            "accepted_policy": ({k: accepted[k] for k in ("hash", "target_sha", "captured_at")}
                                if accepted else None),
            "jobs": status["jobs"], "workspaces": workspaces,
            "main_release": status.get("main_release"), "admission": admission,
            "daemon": self.daemon_state(),
            "handoffs_pending": len(self.handoffs(pending_only=True)),
            "outbox_pending": len(self.queue.outbox()),
        }


class MainUpdater:
    """Coalescing background builder of the runnable main. `request(sha)` replaces any sha not
    yet built; the thread builds whatever is newest when it wakes, so a burst of landings makes
    one build. Runs outside every lock the publisher holds."""

    def __init__(self, service: IntegrationService, *, log=None):
        self.service = service
        self.log = log or (lambda line: None)
        self._event = threading.Event()
        self._stop = threading.Event()
        self._lock = threading.Lock()
        self._latest = None
        self._building = None
        self._last = None
        self._thread = threading.Thread(target=self._run, name="relay-main-updater", daemon=True)

    def start(self):
        self._thread.start()

    def request(self, sha):
        with self._lock:
            self._latest = sha
        self._event.set()

    def stop(self, timeout=None):
        self._stop.set()
        self._event.set()
        self._thread.join(timeout)

    def state(self) -> dict:
        with self._lock:
            return {"pending_sha": self._latest, "building_sha": self._building,
                    "last": self._last, "alive": self._thread.is_alive()}

    def _run(self):
        while not self._stop.is_set():
            self._event.wait()
            self._event.clear()
            while True:
                with self._lock:
                    sha, self._latest = self._latest, None
                    self._building = sha
                if sha is None:
                    break
                try:
                    result = self.service.request_main_update(sha, wait=True)
                except Exception as exc:  # noqa: BLE001 — the updater never dies on a build
                    result = {"sha": sha, "error": "%s: %s" % (exc.__class__.__name__, exc)}
                with self._lock:
                    self._last = result
                    self._building = None
                self.log("main release %s: %s" % (sha[:12], result.get("error") or
                                                  ("installed" if result.get("changed", True) else "unchanged")))
                if self._stop.is_set():
                    break


# --------------------------------------------------------------------------- CLI

SERVICE_VERBS = ("run", "project-init", "inventory", "activate", "pause", "rollback", "try",
                 "main-status", "main-update", "main-run", "snapshot", "handoffs", "board-submit",
                 "workspace", "capture-policy", "events", "hook")

USAGE = """relay-land (service verbs, card #AMQQ):

  run [--once] [--interval S] [--no-main] [--max-ticks N]   the publisher loop
  project-init [--write] [--target BRANCH]     register the repo; suggest/write .relay/project.toml
  inventory                                    what a cutover would meet (JSON)
  activate [--dry-run] [--human-branch NAME] [--wait-seconds N] [--force-hook]
  pause [--reason TEXT]
  rollback [--wait-seconds N] [--keep-head]
  try SHA | --workspace ID [--test NAME ...]   the gate on the candidate, no publication
  main-status | main-run [-- ARGS...]
  snapshot | events [--since ID] | handoffs [--session S] [--workspace ID] [--all] [--ack ID]
  board-submit PATH... --session S [-m MSG]    a Board snapshot as a metadata job
  workspace create SESSION [--card C] [--base REV] | status ID | release ID --owner O | remove ID
  capture-policy [--revision REV] [--no-accept]
  hook install [--force]

Common: --repo PATH, --state-root PATH, --cache-root PATH. JSON on stdout; exit 0 ok, 1 usage,
2 refused, 3 conflict, 5 gate failure, 7 busy.
"""


class _Parser(argparse.ArgumentParser):
    def error(self, message):
        self.exit(1, "%s: error: %s\n" % (self.prog, message))


def _build_parser() -> _Parser:
    parser = _Parser(prog="relay-land", usage=USAGE)
    parser.add_argument("--repo", default=None)
    parser.add_argument("--state-root", default=None)
    parser.add_argument("--cache-root", default=None)
    sub = parser.add_subparsers(dest="verb")
    p = sub.add_parser("run")
    p.add_argument("--once", action="store_true")
    p.add_argument("--interval", type=float, default=DEFAULT_INTERVAL)
    p.add_argument("--no-main", action="store_true")
    p.add_argument("--max-ticks", type=int, default=None)
    p.add_argument("--admission-timeout", type=float, default=DEFAULT_ADMISSION_TIMEOUT)
    p = sub.add_parser("project-init")
    p.add_argument("--write", action="store_true")
    p.add_argument("--target", default=None)
    sub.add_parser("inventory")
    p = sub.add_parser("activate")
    p.add_argument("--dry-run", action="store_true")
    p.add_argument("--human-branch", default=DEFAULT_HUMAN_BRANCH)
    p.add_argument("--wait-seconds", type=float, default=0.0)
    p.add_argument("--force-hook", action="store_true")
    p = sub.add_parser("pause")
    p.add_argument("--reason", default="")
    p.add_argument("--wait-seconds", type=float, default=0.0)
    p = sub.add_parser("rollback")
    p.add_argument("--wait-seconds", type=float, default=0.0)
    p.add_argument("--keep-head", action="store_true")
    p = sub.add_parser("try")
    p.add_argument("sha", nargs="?")
    p.add_argument("--workspace", default=None)
    p.add_argument("--test", action="append", default=[])
    p.add_argument("--admission-timeout", type=float, default=None)
    sub.add_parser("main-status")
    p = sub.add_parser("main-update")       # the build child `_update_main` spawns
    p.add_argument("sha")
    p = sub.add_parser("main-run")
    p.add_argument("args", nargs=argparse.REMAINDER)
    sub.add_parser("snapshot")
    p = sub.add_parser("events")
    p.add_argument("--since", type=int, default=0)
    p = sub.add_parser("handoffs")
    p.add_argument("--session", default=None)
    p.add_argument("--workspace", default=None)
    p.add_argument("--all", action="store_true")
    p.add_argument("--ack", default=None)
    p = sub.add_parser("board-submit")
    p.add_argument("paths", nargs="+")
    p.add_argument("--session", required=True)
    p.add_argument("-m", "--message", default=None)
    p = sub.add_parser("workspace")
    ws = p.add_subparsers(dest="ws_verb")
    q = ws.add_parser("create")
    q.add_argument("session")
    q.add_argument("--card", default=None)
    q.add_argument("--base", default=None)
    q = ws.add_parser("status")
    q.add_argument("id")
    q = ws.add_parser("release")
    q.add_argument("id")
    q.add_argument("--owner", required=True)
    q = ws.add_parser("remove")
    q.add_argument("id")
    q = ws.add_parser("list")
    p = sub.add_parser("capture-policy")
    p.add_argument("--revision", default=None)
    p.add_argument("--no-accept", action="store_true")
    p = sub.add_parser("hook")
    p.add_argument("hook_action", choices=["install", "status"])
    p.add_argument("--force", action="store_true")
    return parser


def accepted_config(repo, *, state_root=None) -> dict | None:
    """The accepted policy's normalized config for the registered project containing `repo`,
    or None when the project is unregistered or never activated. Read-only: registers nothing.
    B2's workspace helper reads workspace init/exclusions from here, not from the target's
    file, so a config change on the target cannot change what a pane launches with until it
    is explicitly accepted."""
    record = trees.resolve_project(repo, state_root=state_root)
    if record is None:
        return None
    service = IntegrationService(record.get("project_root") or repo, state_root=state_root,
                                 register=False)
    accepted = service.accepted_policy()
    return accepted["config"] if accepted else None


def resolve_submission(cwd, ref, *, state_root=None, workspace_id=None) -> dict | None:
    """What `relay-land submit REF` means when run from `cwd`: the commit `ref` names *in the
    caller's tree* (HEAD of a workspace, not of the human checkout the service works in), and
    the workspace that tree is — `RELAY_WORKSPACE_ID`/`workspace_id` validated against the
    tree, else the registry record whose path is the tree's top level. Returns None when the
    project is not in queue mode (the caller falls back to the plain queue), a dict
    `{sha, workspace, record, project_root}` otherwise."""
    record = trees.resolve_project(cwd, state_root=state_root)
    if record is None or record.get("mode") != "queue":
        return None
    proc = git(cwd, "rev-parse", "--verify", "--quiet", "--end-of-options",
               "%s^{commit}" % ref, check=False)
    if proc.returncode != 0:
        raise ServiceUsageError("%s is not a commit in %s" % (ref, cwd))
    sha = proc.stdout.strip()
    top = Path(git_out(cwd, "rev-parse", "--show-toplevel")).resolve()
    manager = trees.TreeManager(record["id"], state_root=state_root)
    workspace = None
    wanted = workspace_id or os.environ.get("RELAY_WORKSPACE_ID") or None
    if wanted:
        try:
            workspace = manager.get(wanted)
        except trees.TreeError as exc:
            raise ServiceUsageError("workspace %s: %s" % (wanted, exc)) from exc
        if Path(workspace["path"]).resolve() != top:
            # Named from outside the tree (the canonical checkout, a script): the commit must
            # be the workspace's own, i.e. on its branch — a sha from elsewhere is refused.
            branch = git(cwd, "rev-parse", "--verify", "--quiet",
                         "refs/heads/%s" % workspace["branch"], check=False).stdout.strip()
            if not branch or not landq._is_ancestor(cwd, sha, branch):
                raise ServiceUsageError("commit %s is not on workspace %s's branch %s (and you are "
                                        "not in that tree: %s)" % (sha[:12], wanted,
                                                                   workspace["branch"], top))
    else:
        for candidate in manager.list():
            if Path(candidate["path"]).resolve() == top:
                workspace = candidate
                break
    return {"sha": sha, "workspace": workspace, "record": record,
            "project_root": record.get("project_root") or str(top)}


def cli_submit(args, *, cwd=None, out=None) -> int | None:
    """The queue CLI's `submit` in queue mode: route through the service so the author's
    session and card are recorded for handoffs and the ref resolves against the caller's
    tree. Returns None to let the plain queue handle it (legacy mode / unregistered)."""
    cwd = Path(cwd or args.repo or os.getcwd())
    resolved = resolve_submission(cwd, args.sha, state_root=args.state_root,
                                  workspace_id=args.workspace_id)
    if resolved is None:
        return None
    service = IntegrationService(resolved["project_root"], state_root=args.state_root,
                                 cache_root=getattr(args, "cache_root", None), register=False)
    workspace = resolved["workspace"]
    job = service.submit(resolved["sha"], request_id=args.request_id,
                         workspace_id=workspace["id"] if workspace else None,
                         card=args.card, selected_tests=args.test, kind=args.kind)
    job["session"] = service._session_for(job)
    json.dump(job, out or sys.stdout, indent=1, sort_keys=True, default=str)
    (out or sys.stdout).write("\n")
    return 0


def project_init(repo, *, state_root=None, write=False, target=None) -> dict:
    """Register the repository (mode stays legacy), report the config state and, with `write`
    and no config yet, write `projectconf.detect`'s suggestion to `.relay/project.toml` for a
    human to review. Never activates."""
    record = trees.resolve_project(repo, state_root=state_root)
    if record is None:
        record = trees.register_repo(repo, state_root=state_root, target=target)
    elif target and target != record.get("target"):
        record = trees.configure_repo(record["id"], state_root=state_root, target=target)
    root = Path(record.get("project_root") or repo)
    path = root / projectconf._CONFIG_PATH
    out = {"registry": record, "config_path": str(path), "config_present": path.is_file(),
           "written": False, "hook": hook_state(root)}
    if path.is_file():
        try:
            cfg = projectconf.load(root)
            out["policy_hash"] = projectconf.policy_hash(cfg)
        except projectconf.ProjectConfigError as exc:
            out["config_error"] = str(exc)
    else:
        detected = projectconf.detect(root)
        out["suggestion"] = detected
        suggestion = dict(detected.get("suggestion") or {})
        if target:
            suggestion.setdefault("project", {})["target"] = target
        if write:
            text = render_config_toml(suggestion)
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(text, encoding="utf-8")
            out.update(written=True, config_present=True, config_text=text)
            try:
                out["policy_hash"] = projectconf.policy_hash(projectconf.load(root))
            except projectconf.ProjectConfigError as exc:
                out["config_error"] = str(exc)
        else:
            out["config_text"] = render_config_toml(suggestion)
    out["next"] = ("review %s, commit it on %s, then `relay-land activate --dry-run`"
                   % (path, record.get("target") or "main"))
    return out


def main(argv=None, *, out=None, err=None) -> int:
    out = out or sys.stdout
    err = err or sys.stderr
    argv = list(sys.argv[1:] if argv is None else argv)
    parser = _build_parser()
    try:
        args = parser.parse_args(argv)
    except SystemExit as exc:
        return 0 if exc.code == 0 else 1
    if not args.verb:
        out.write(USAGE)
        return 1

    def emit(value):
        json.dump(value, out, indent=1, sort_keys=True, default=str)
        out.write("\n")

    repo = args.repo or os.getcwd()
    try:
        if args.verb == "project-init":
            emit(project_init(repo, state_root=args.state_root, write=args.write, target=args.target))
            return 0
        if args.verb == "hook":
            if args.hook_action == "install":
                emit(install_hook(repo, force=args.force))
            else:
                emit(hook_state(repo))
            return 0
        kw = {"state_root": args.state_root, "cache_root": args.cache_root}
        if getattr(args, "admission_timeout", None) is not None:
            kw["admission_timeout"] = args.admission_timeout
        service = IntegrationService(repo, **kw)
        if args.verb == "run":
            if args.once:
                result = service.run_once(main=not args.no_main)
                emit(result)
                job = result.get("job")
                if job and job["status"] in ("failed",):
                    return 5
                if job and job["status"] == "conflict":
                    return 3
                return 0
            stop = threading.Event()

            def _stop(signum, _frame):
                err.write("relay-land run: signal %d, stopping after this tick\n" % signum)
                stop.set()
            for sig in (signal.SIGTERM, signal.SIGINT):
                try:
                    signal.signal(sig, _stop)
                except (ValueError, OSError):
                    pass
            err.write("relay-land run: %s (%s) every %.1fs, mode %s\n"
                      % (service.repo_id, service.project_root, args.interval, service.mode()))
            emit(service.run(interval=args.interval, stop=stop, main=not args.no_main,
                             max_ticks=args.max_ticks,
                             log=lambda line: err.write("relay-land run: %s\n" % line)))
            return 0
        if args.verb == "inventory":
            emit(service.inventory())
        elif args.verb == "activate":
            result = service.activate(dry_run=args.dry_run, human_branch=args.human_branch,
                                      wait_seconds=args.wait_seconds, force_hook=args.force_hook)
            emit(result)
            if args.dry_run and not result["can_activate"]:
                return 2
        elif args.verb == "pause":
            emit(service.pause(wait_seconds=args.wait_seconds, reason=args.reason))
        elif args.verb == "rollback":
            emit(service.rollback(wait_seconds=args.wait_seconds, keep_head=args.keep_head))
        elif args.verb == "try":
            if not args.sha and not args.workspace:
                raise ServiceUsageError("try needs a sha or --workspace")
            result = service.try_candidate(args.sha, selected_tests=args.test,
                                           workspace_id=args.workspace,
                                           timeout=args.admission_timeout)
            emit(result)
            if result.get("conflict"):
                return 3
            if not result.get("ok"):
                return 5
        elif args.verb == "main-status":
            emit(service.main_status())
        elif args.verb == "main-update":
            record = service.main_update_inprocess(args.sha)
            emit(record)
            return 5 if record.get("error") else 0
        elif args.verb == "main-run":
            exe = service.main_executable()
            extra = list(args.args[1:]) if args.args and args.args[0] == "--" else list(args.args)
            try:
                os.execv(str(exe), [str(exe), *extra])
            except OSError as exc:
                raise ModeError("cannot run the installed main %s: %s" % (exe, exc)) from exc
        elif args.verb == "snapshot":
            emit(service.snapshot())
        elif args.verb == "events":
            emit(service.events(since_id=args.since))
        elif args.verb == "handoffs":
            if args.ack:
                emit(service.ack_handoff(args.ack))
            else:
                emit(service.handoffs(session=args.session, workspace_id=args.workspace,
                                      pending_only=not args.all))
        elif args.verb == "board-submit":
            job = service.submit_board_snapshot(args.paths, session=args.session,
                                                message=args.message)
            emit(job if job else {"submitted": False, "reason": "paths already match the target"})
        elif args.verb == "workspace":
            if args.ws_verb == "create":
                emit(service.allocate_workspace(args.session, card=args.card, base=args.base))
            elif args.ws_verb == "status":
                emit(service.tree_status(args.id))
            elif args.ws_verb == "release":
                emit(service.release_workspace(args.id, owner=args.owner))
            elif args.ws_verb == "remove":
                emit(service.remove_workspace(args.id))
            elif args.ws_verb == "list":
                emit([service.tree_status(w["id"]) for w in service.trees.list()])
            else:
                raise ServiceUsageError("workspace needs create/status/release/remove/list")
        elif args.verb == "capture-policy":
            emit(service.capture_policy(revision=args.revision, accept=not args.no_accept))
        return 0
    except ServiceError as exc:
        emit({"error": str(exc), "kind": exc.__class__.__name__})
        return exc.exit_code
    except landq.QueueError as exc:
        emit({"error": str(exc), "kind": exc.__class__.__name__})
        return exc.exit_code
    except trees.TreeError as exc:
        emit({"error": str(exc), "kind": exc.__class__.__name__})
        return getattr(exc, "exit_code", 1)
    except projectconf.ProjectConfigError as exc:
        emit({"error": str(exc), "kind": "ProjectConfigError"})
        return 1


__all__ = ["IntegrationService", "MainUpdater", "ServiceError", "ServiceUsageError", "ModeError",
           "TransitionRefused", "ServiceBusy", "GateFailed", "AdmissionUnavailable",
           "accepted_config", "resolve_submission", "cli_submit", "publication_marker",
           "transition_lock", "hook_state", "install_hook", "project_init", "render_config_toml",
           "HOOK", "HOOK_VERSION", "HOOK_REFUSAL", "MARKER_NAME", "LOCK_NAME", "SERVICE_VERBS",
           "main"]


if __name__ == "__main__":
    sys.exit(main())
