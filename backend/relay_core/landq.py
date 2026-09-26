# SPDX-License-Identifier: AGPL-3.0-or-later
"""relay_core.landq — the durable verification and publication queue (card #FW1C).

This is workstream A2 of `docs/TREES-AND-LANDING.md`: one publisher per repository that takes
immutable submitted commits, builds a candidate on the current target tip, has it verified by a
caller-supplied gate, records that verification bound to the candidate's SHA, tree and policy
hash, and then publishes *exactly that commit* with an expected-old-value `update-ref`. The
publishing intent is in SQLite before the ref moves, so a restart can tell a completed
publication from an interrupted one.

    Queue(repo, *, state_root=None)
        .submit(submitted_sha, *, request_id, workspace_id=None, card=None, base_sha=None,
                kind="code", selected_tests=()) -> dict
        .status(job_id=None) -> dict | list[dict]
        .cancel(job_id) -> dict
        .process_one(verifier, *, reconcile=None) -> dict | None
        .recover() -> list[dict]
        .receipt(job_id) -> dict | None

`verifier(job, candidate_sha, candidate_path)` returns `{ok, policy_hash, log, verified,
reason?}` and is called on every candidate the queue builds — there is no default, because the
queue never publishes what nobody verified. `reconcile(context)` returns `{status:
"resolved"|"author_required", candidate_sha?|tree?, trailer?, reason?, ...}` for a merge that
conflicts; a resolved result is a *new* candidate and goes through the verifier again. Neither
callback can move the target or an author's branch: the queue commits through its own
service-owned detached worktree and `commit-tree`, and only ever writes `refs/heads/<target>`
with a compare-and-swap, and `refs/landq/jobs/<job>/…` to retain submissions and candidates.

Job states: `queued -> preparing -> verifying -> ready -> publishing -> landed`, plus
`conflict`, `failed`, `cancelled` and `interrupted`. A process that finds jobs in
`preparing`/`verifying`/`publishing` while it holds `publisher.lock` knows their publisher died:
`recover()` lands the ones whose candidate the target already contains (the crash was after the
ref swap) and marks the rest `interrupted`, which is picked up again and re-verified from
scratch — never with the stale verification pass.

Placement (`state_root` is `$XDG_STATE_HOME/relay`, default `~/.local/state/relay`):

    state_root/integration/<repo-id>/queue.sqlite3      jobs, verifications, receipts, outbox
    state_root/integration/<repo-id>/publisher.lock     the whole prepare/verify/publish transaction
    state_root/integration/<repo-id>/logs/<job-id>/     merge, reconcile and verifier logs
    state_root/integration/<repo-id>/service/candidate  the service-owned detached worktree

Repository identity comes from `relay_core.trees` (workstream A1) when it is importable:
`resolve_project`, then `register_repo`. Until it lands, or when it cannot answer, the id is a
hash of the resolved Git common directory (`identity_source` says which); `repo_id=` overrides
both. B1 (card #AMQQ) takes this module over once it has landed and adds the service loop.
"""
from __future__ import annotations

import asyncio
import hashlib
import inspect
import json
import os
import secrets
import sqlite3
import subprocess
import sys
import time
from contextlib import contextmanager
from datetime import datetime, timezone
from pathlib import Path

if os.name != "nt":
    import fcntl

SCHEMA_VERSION = 1
KINDS = ("code", "metadata")
STATUSES = ("queued", "preparing", "verifying", "ready", "publishing", "landed",
            "conflict", "failed", "cancelled", "interrupted")
TERMINAL = ("landed", "conflict", "failed", "cancelled")
IN_FLIGHT = ("preparing", "verifying", "publishing")
PICKABLE = ("queued", "interrupted", "ready")
REF_NS = "refs/landq/jobs"
METADATA_ROOT = ".board/"
SERVICE_NAME = "Relay landq"
SERVICE_EMAIL = "landq@relay.invalid"
CONFLICT_MARKERS = ("<<<<<<< ", ">>>>>>> ")
# The outbox retries a failed delivery after 5s, 10s, 20s … capped at an hour.
OUTBOX_BACKOFF_BASE = 5.0
OUTBOX_BACKOFF_CAP = 3600.0


class QueueError(Exception):
    """Base of every refusal; `exit_code` is what the CLI exits with."""
    exit_code = 2


class UsageError(QueueError):
    exit_code = 1


class Refused(QueueError):
    exit_code = 2


class ConflictError(QueueError):
    exit_code = 3


class GateFailed(QueueError):
    exit_code = 5


class PublisherBusy(QueueError):
    """Another process holds this repository's publisher lock."""
    exit_code = 7


class TargetAttached(Refused):
    """The target branch is checked out in a worktree; the queue never moves such a branch."""


class InjectedCrash(RuntimeError):
    """Raised by a fault point a test armed (`Queue.faults`); never in production."""


# --------------------------------------------------------------------------- helpers

def _now() -> str:
    return datetime.now(timezone.utc).isoformat(timespec="microseconds")


def _parse(ts: str) -> float:
    return datetime.fromisoformat(ts).timestamp()


def _xdg(override: str, xdg: str, default: str) -> Path:
    for name in (override, xdg):
        value = os.environ.get(name)
        if value:
            return Path(value)
    return Path.home() / default


def default_state_root() -> Path:
    return _xdg("RELAY_STATE_HOME", "XDG_STATE_HOME", ".local/state") / "relay"


def git_env(extra=None) -> dict:
    """The environment every git call runs with: no ambient GIT_* variable at all (an exported
    GIT_INDEX_FILE or GIT_DIR would point the service at somebody's checkout), the service's
    own identity, and no prompts."""
    env = {k: v for k, v in os.environ.items() if not k.startswith("GIT_")}
    env.update({
        "GIT_AUTHOR_NAME": SERVICE_NAME, "GIT_AUTHOR_EMAIL": SERVICE_EMAIL,
        "GIT_COMMITTER_NAME": SERVICE_NAME, "GIT_COMMITTER_EMAIL": SERVICE_EMAIL,
        "GIT_TERMINAL_PROMPT": "0", "GIT_OPTIONAL_LOCKS": "0",
    })
    if extra:
        env.update({k: str(v) for k, v in extra.items()})
    return env


def git(cwd, *args, env=None, stdin=None, check=True) -> subprocess.CompletedProcess:
    proc = subprocess.run(["git", *args], cwd=str(cwd), env=git_env(env), input=stdin,
                          stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    if check and proc.returncode != 0:
        raise Refused("git %s failed (%d): %s" % (" ".join(args[:2]), proc.returncode,
                                                  proc.stderr.strip()))
    return proc


def git_out(cwd, *args, **kw) -> str:
    return git(cwd, *args, **kw).stdout.strip()


def _is_ancestor(repo, ancestor, descendant) -> bool:
    proc = git(repo, "merge-base", "--is-ancestor", ancestor, descendant, check=False)
    if proc.returncode in (0, 1):
        return proc.returncode == 0
    raise Refused("git merge-base failed: %s" % proc.stderr.strip())


def resolve_commit(repo, rev) -> str:
    """The full sha of `rev`, which must name a commit that exists in `repo`."""
    if not rev or not isinstance(rev, str) or rev.startswith("-"):
        raise UsageError("a commit sha is required")
    proc = git(repo, "rev-parse", "--verify", "--quiet", "--end-of-options", rev + "^{commit}",
               check=False)
    if proc.returncode != 0:
        raise Refused("%s is not a commit in %s" % (rev, repo))
    return proc.stdout.strip()


def repo_identity(repo, *, state_root, repo_id=None, target=None) -> dict:
    """`{id, common_dir, project_root, target, identity_source}` for `repo`.

    A1's registry is the source of truth when `relay_core.trees` can be imported and answers;
    `repo_id=` overrides it (tests, and a caller that already resolved the project), and a
    hash of the common directory stands in until A1 exists, so a queue is usable now and the
    fallback ids are stable across restarts."""
    common = Path(git_out(repo, "rev-parse", "--git-common-dir"))
    if not common.is_absolute():
        common = Path(repo) / common
    common = common.resolve()
    top = git(repo, "rev-parse", "--show-toplevel", check=False)
    project_root = top.stdout.strip() if top.returncode == 0 else str(Path(repo).resolve())
    record = {"id": repo_id, "common_dir": str(common), "project_root": project_root,
              "target": target, "identity_source": "explicit"}
    if repo_id is None:
        try:
            from relay_core import trees  # noqa: WPS433 — A1; absent only during development
        except ImportError:
            trees = None
        if trees is None:
            # Development-only: a stable hash of the common dir until A1's registry exists.
            record["id"] = hashlib.sha256(str(common).encode("utf-8")).hexdigest()[:16]
            record["identity_source"] = "fallback-hash: no trees module"
        else:
            # The registry is the identity. A failure here is reported, never papered over
            # with a second id: that would fork the queue after a relocation or corruption.
            try:
                found = None
                resolve = getattr(trees, "resolve_project", None)
                if resolve is not None:
                    found = resolve(repo, state_root=state_root)
                if not found:
                    found = trees.register_repo(repo, state_root=state_root)
                record["id"] = str(found["id"])
            except QueueError:
                raise
            except Exception as exc:  # noqa: BLE001 — surfaced as a refusal with its cause
                raise Refused("repository identity unavailable from relay_core.trees for %s: "
                              "%s: %s" % (repo, exc.__class__.__name__, exc)) from exc
            record["identity_source"] = "trees"
            if target is None and found.get("target"):
                record["target"] = str(found["target"])
    if record["target"] is None:
        record["target"] = "main"
    if "/" in record["id"] or record["id"] in ("", ".", ".."):
        raise UsageError("repository id %r is not a path component" % record["id"])
    return record


# --------------------------------------------------------------------------- the queue

SCHEMA = """
CREATE TABLE IF NOT EXISTS jobs (
    id TEXT PRIMARY KEY,
    request_id TEXT NOT NULL UNIQUE,
    repo_id TEXT NOT NULL,
    workspace_id TEXT,
    card TEXT,
    kind TEXT NOT NULL,
    base_sha TEXT,
    submitted_sha TEXT NOT NULL,
    target TEXT NOT NULL,
    target_sha TEXT,
    candidate_sha TEXT,
    candidate_tree TEXT,
    policy_hash TEXT,
    selected_tests_json TEXT NOT NULL DEFAULT '[]',
    status TEXT NOT NULL,
    reason TEXT,
    cancel_requested INTEGER NOT NULL DEFAULT 0,
    published_sha TEXT,
    attempts INTEGER NOT NULL DEFAULT 0,
    created_at TEXT NOT NULL,
    updated_at TEXT NOT NULL
);
CREATE INDEX IF NOT EXISTS jobs_status ON jobs(status);
CREATE TABLE IF NOT EXISTS verifications (
    id INTEGER PRIMARY KEY,
    job_id TEXT NOT NULL REFERENCES jobs(id),
    candidate_sha TEXT NOT NULL,
    candidate_tree TEXT NOT NULL,
    target_sha TEXT NOT NULL,
    policy_hash TEXT NOT NULL,
    ok INTEGER NOT NULL,
    verified INTEGER NOT NULL,
    reason TEXT,
    log_path TEXT,
    created_at TEXT NOT NULL
);
CREATE INDEX IF NOT EXISTS verifications_job ON verifications(job_id);
CREATE TABLE IF NOT EXISTS receipts (
    job_id TEXT PRIMARY KEY REFERENCES jobs(id),
    repo_id TEXT NOT NULL,
    submitted_sha TEXT NOT NULL,
    target TEXT NOT NULL,
    target_before TEXT,
    published_sha TEXT NOT NULL,
    tree TEXT NOT NULL,
    policy_hash TEXT,
    verified INTEGER NOT NULL,
    landed_at TEXT NOT NULL
);
CREATE TABLE IF NOT EXISTS outbox (
    id INTEGER PRIMARY KEY,
    delivery_key TEXT NOT NULL UNIQUE,
    job_id TEXT NOT NULL REFERENCES jobs(id),
    kind TEXT NOT NULL,
    payload_json TEXT NOT NULL,
    attempts INTEGER NOT NULL DEFAULT 0,
    next_attempt_at TEXT NOT NULL,
    last_error TEXT,
    delivered_at TEXT,
    created_at TEXT NOT NULL
);
"""


class Queue:
    """One repository's queue. Construct as many as you like; the publisher lock serialises them."""

    def __init__(self, repo, *, state_root=None, repo_id=None, target=None,
                 lock_timeout=0.0, busy_timeout=10.0):
        self.repo = Path(repo).resolve()
        if not self.repo.exists():
            raise UsageError("no such repository: %s" % self.repo)
        if git(self.repo, "rev-parse", "--git-dir", check=False).returncode != 0:
            raise UsageError("%s is not a git repository" % self.repo)
        self.state_root = Path(state_root) if state_root else default_state_root()
        self.identity = repo_identity(self.repo, state_root=self.state_root,
                                      repo_id=repo_id, target=target)
        self.repo_id = self.identity["id"]
        self.target = self.identity["target"]
        self.root = self.state_root / "integration" / self.repo_id
        self.root.mkdir(parents=True, exist_ok=True)
        self.db_path = self.root / "queue.sqlite3"
        self.lock_path = self.root / "publisher.lock"
        self.logs_root = self.root / "logs"
        self.service_dir = self.root / "service"
        self.lock_timeout = float(lock_timeout)
        self.busy_timeout = float(busy_timeout)
        # Test hook: names in this set raise InjectedCrash when the publisher reaches them.
        self.faults = set()
        self._lock_fd = None
        self._migrate()

    # ----------------------------------------------------------------- storage

    def _connect(self) -> sqlite3.Connection:
        conn = sqlite3.connect(str(self.db_path), timeout=self.busy_timeout, isolation_level=None)
        conn.row_factory = sqlite3.Row
        conn.execute("PRAGMA busy_timeout=%d" % int(self.busy_timeout * 1000))
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
                raise Refused("queue schema %d is newer than this code (%d): %s"
                              % (version, SCHEMA_VERSION, self.db_path))
            if version < SCHEMA_VERSION:
                # Not executescript: it commits the open transaction first.
                for statement in SCHEMA.split(";"):
                    if statement.strip():
                        conn.execute(statement)
                conn.execute("PRAGMA user_version=%d" % SCHEMA_VERSION)

    @staticmethod
    def _job(row) -> dict | None:
        if row is None:
            return None
        job = dict(row)
        job["selected_tests"] = json.loads(job.pop("selected_tests_json") or "[]")
        job["cancel_requested"] = bool(job["cancel_requested"])
        job["age_seconds"] = round(max(0.0, time.time() - _parse(job["created_at"])), 3)
        return job

    def _load(self, conn, job_id) -> dict:
        job = self._job(conn.execute("SELECT * FROM jobs WHERE id=?", (job_id,)).fetchone())
        if job is None:
            raise UsageError("no such job: %s" % job_id)
        return job

    def _set(self, conn, job_id, **fields):
        fields["updated_at"] = _now()
        cols = ", ".join("%s=?" % k for k in fields)
        conn.execute("UPDATE jobs SET %s WHERE id=?" % cols, (*fields.values(), job_id))

    def _notify(self, conn, job_id, kind, payload):
        conn.execute(
            "INSERT OR IGNORE INTO outbox(delivery_key, job_id, kind, payload_json, "
            "next_attempt_at, created_at) VALUES (?,?,?,?,?,?)",
            ("%s:%s" % (job_id, kind), job_id, kind, json.dumps(payload, sort_keys=True),
             _now(), _now()))

    def _log(self, job_id, name, text):
        d = self.logs_root / job_id
        d.mkdir(parents=True, exist_ok=True)
        path = d / name
        path.write_text(text if text is not None else "")
        return str(path)

    # ----------------------------------------------------------------- public API

    def submit(self, submitted_sha, *, request_id, workspace_id=None, card=None, base_sha=None,
               kind="code", selected_tests=()) -> dict:
        """Record a submission. Idempotent on `request_id`: the same request with the same sha
        returns the existing job; the same request with a different sha is refused."""
        if not request_id or not isinstance(request_id, str):
            raise UsageError("request_id is required")
        if kind not in KINDS:
            raise UsageError("kind must be one of %s" % ", ".join(KINDS))
        sha = resolve_commit(self.repo, submitted_sha)
        base = resolve_commit(self.repo, base_sha) if base_sha else None
        tests = [str(t) for t in (selected_tests or ())]
        job_id = secrets.token_hex(8)
        # Retain the submission before the row exists: a ref for an unrecorded job costs a
        # few bytes; a recorded job whose commit gc took would be unrecoverable.
        ref = "%s/%s/submitted" % (REF_NS, job_id)
        git(self.repo, "update-ref", ref, sha)
        now = _now()
        try:
            with self._tx() as conn:
                conn.execute(
                    "INSERT INTO jobs(id, request_id, repo_id, workspace_id, card, kind, base_sha,"
                    " submitted_sha, target, selected_tests_json, status, created_at, updated_at)"
                    " VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?)",
                    (job_id, request_id, self.repo_id, workspace_id, card, kind, base, sha,
                     self.target, json.dumps(tests), "queued", now, now))
        except sqlite3.IntegrityError:
            git(self.repo, "update-ref", "-d", ref, check=False)
            with self._tx() as conn:
                existing = self._job(conn.execute("SELECT * FROM jobs WHERE request_id=?",
                                                  (request_id,)).fetchone())
            if existing and existing["submitted_sha"] == sha and existing["kind"] == kind:
                existing["existing"] = True
                return existing
            raise Refused("request %s already submitted %s as job %s" % (
                request_id, existing["submitted_sha"][:12] if existing else "?",
                existing["id"] if existing else "?"))
        return self.status(job_id)

    def status(self, job_id=None):
        with self._tx() as conn:
            if job_id is None:
                rows = conn.execute("SELECT * FROM jobs ORDER BY rowid").fetchall()
                return [self._job(r) for r in rows]
            return self._load(conn, job_id)

    def cancel(self, job_id) -> dict:
        """Cancel a job that has not been published. A job in flight gets `cancel_requested`
        and stops before its publishing intent is recorded; a job that has already landed is
        returned as `landed` — a publication is never undone."""
        with self._tx() as conn:
            job = self._load(conn, job_id)
            if job["status"] in PICKABLE:
                self._set(conn, job_id, status="cancelled", reason="cancelled by request",
                          cancel_requested=1)
                self._notify(conn, job_id, "cancelled", {"job_id": job_id})
            elif job["status"] in IN_FLIGHT:
                self._set(conn, job_id, cancel_requested=1)
            # Terminal states are left as they are; `landed` stays landed.
            return self._load(conn, job_id)

    def receipt(self, job_id) -> dict | None:
        with self._tx() as conn:
            row = conn.execute("SELECT * FROM receipts WHERE job_id=?", (job_id,)).fetchone()
            if row is None:
                return None
            receipt = dict(row)
            receipt["verified"] = bool(receipt["verified"])
            return receipt

    def verifications(self, job_id) -> list[dict]:
        with self._tx() as conn:
            rows = conn.execute("SELECT * FROM verifications WHERE job_id=? ORDER BY id",
                                (job_id,)).fetchall()
        out = []
        for r in rows:
            d = dict(r)
            d["ok"] = bool(d["ok"])
            d["verified"] = bool(d["verified"])
            out.append(d)
        return out

    def outbox(self, *, pending_only=True) -> list[dict]:
        with self._tx() as conn:
            sql = "SELECT * FROM outbox"
            if pending_only:
                sql += " WHERE delivered_at IS NULL"
            rows = conn.execute(sql + " ORDER BY id").fetchall()
        out = []
        for r in rows:
            d = dict(r)
            d["payload"] = json.loads(d.pop("payload_json"))
            out.append(d)
        return out

    def deliver_outbox(self, sender, *, limit=50, now=None) -> list[dict]:
        """Hand every due notification to `sender(record)`; a raise schedules a retry with
        exponential backoff and is never a reason to touch a job or a ref."""
        wall = time.time() if now is None else now
        due = [r for r in self.outbox() if _parse(r["next_attempt_at"]) <= wall][:limit]
        results = []
        for record in due:
            try:
                sender(record)
            except Exception as exc:  # noqa: BLE001 — retried, and reported
                attempts = record["attempts"] + 1
                delay = min(OUTBOX_BACKOFF_CAP, OUTBOX_BACKOFF_BASE * (2 ** (attempts - 1)))
                next_at = datetime.fromtimestamp(wall + delay, timezone.utc).isoformat(
                    timespec="microseconds")
                with self._tx() as conn:
                    conn.execute("UPDATE outbox SET attempts=?, next_attempt_at=?, last_error=?"
                                 " WHERE id=?", (attempts, next_at, str(exc)[:2000], record["id"]))
                results.append({**record, "delivered": False, "attempts": attempts,
                                "last_error": str(exc), "next_attempt_at": next_at})
            else:
                with self._tx() as conn:
                    conn.execute("UPDATE outbox SET delivered_at=?, attempts=attempts+1"
                                 " WHERE id=?", (_now(), record["id"]))
                results.append({**record, "delivered": True})
        return results

    def recover(self) -> list[dict]:
        """Settle the jobs a dead publisher left in flight. Needs the publisher lock: while
        this process holds it nobody else can legitimately be mid-flight, so every
        `preparing`/`verifying`/`publishing` row is an interruption."""
        with self._publisher_lock():
            return self._recover_locked()

    def process_one(self, verifier, *, reconcile=None, max_attempts=5,
                    accepted_policy_hash=None) -> dict | None:
        """Prepare, verify and publish the oldest pickable job. Returns its final record, or
        None when the queue is empty. Holds the publisher lock throughout.

        A job found already `ready` is published from its recorded verification only when
        `accepted_policy_hash` names the policy that verification was bound to and the target
        has not moved; otherwise it is rebuilt and verified again, because the policy in force
        may have changed since the pass was recorded."""
        if verifier is None or not callable(verifier):
            raise Refused("a verifier is required: the queue never publishes unverified work")
        with self._publisher_lock():
            self._recover_locked()
            with self._tx() as conn:
                row = conn.execute(
                    "SELECT * FROM jobs WHERE status IN (%s) ORDER BY rowid LIMIT 1"
                    % ",".join("?" * len(PICKABLE)), PICKABLE).fetchone()
                job = self._job(row)
            if job is None:
                return None
            job_id = job["id"]
            for _attempt in range(max_attempts):
                outcome = self._attempt(job_id, verifier, reconcile, accepted_policy_hash)
                if outcome != "retry":
                    break
            else:
                with self._tx() as conn:
                    self._set(conn, job_id, status="queued",
                              reason="target moved %d times while publishing; will retry"
                              % max_attempts)
            return self.status(job_id)

    # ----------------------------------------------------------------- locking

    @contextmanager
    def _publisher_lock(self, timeout=None):
        if self._lock_fd is not None:
            yield  # re-entrant within one Queue object
            return
        wait = self.lock_timeout if timeout is None else float(timeout)
        fd = os.open(str(self.lock_path), os.O_CREAT | os.O_RDWR, 0o600)
        deadline = time.monotonic() + wait
        try:
            while True:
                try:
                    if os.name == "nt":
                        from relay_core.filelock import flock, LOCK_EX  # blocking on Windows
                        flock(fd, LOCK_EX)
                    else:
                        fcntl.flock(fd, fcntl.LOCK_EX | fcntl.LOCK_NB)
                    break
                except (BlockingIOError, OSError):
                    if time.monotonic() >= deadline:
                        raise PublisherBusy("another process is publishing %s (%s): %s" % (
                            self.repo_id, self.lock_path, self.lock_holder() or "pid unknown"))
                    time.sleep(0.05)
            os.ftruncate(fd, 0)
            os.write(fd, ("%d %s\n" % (os.getpid(), _now())).encode())
            self._lock_fd = fd
            try:
                yield
            finally:
                self._lock_fd = None
                if os.name != "nt":
                    fcntl.flock(fd, fcntl.LOCK_UN)
        finally:
            os.close(fd)

    def lock_holder(self) -> str:
        try:
            return self.lock_path.read_text().strip()
        except OSError:
            return ""

    # ----------------------------------------------------------------- git plumbing

    def _tip(self) -> str:
        proc = git(self.repo, "rev-parse", "--verify", "--quiet", "refs/heads/%s" % self.target,
                   check=False)
        if proc.returncode != 0:
            raise Refused("target branch %s does not exist in %s" % (self.target, self.repo))
        return proc.stdout.strip()

    def target_attached(self) -> str | None:
        """The path of a worktree that has the target branch checked out, or None."""
        out = git_out(self.repo, "worktree", "list", "--porcelain")
        path = None
        for line in out.splitlines():
            if line.startswith("worktree "):
                path = line[len("worktree "):]
            elif line == "branch refs/heads/%s" % self.target and path:
                return path
        return None

    def _retain(self, job_id, name, sha):
        git(self.repo, "update-ref", "%s/%s/%s" % (REF_NS, job_id, name), sha)

    def _tree_of(self, commit) -> str:
        return git_out(self.repo, "rev-parse", commit + "^{tree}")

    def _service_checkout(self, sha) -> Path:
        """Check `sha` out, detached, in the service-owned worktree and return its path. The
        worktree belongs to the queue: it is reset hard and cleaned of untracked files between
        jobs (ignored build output is left for an incremental verifier)."""
        wt = self.service_dir / "candidate"
        if (wt / ".git").exists():
            reset = git(wt, "checkout", "--detach", "--force", sha, check=False)
            if reset.returncode == 0:
                git(wt, "clean", "-fd", "-q")
                return wt
            # A damaged worktree is thrown away; it holds nothing of anyone's.
        if wt.exists():
            import shutil
            shutil.rmtree(wt, ignore_errors=True)
        git(self.repo, "worktree", "prune")
        wt.parent.mkdir(parents=True, exist_ok=True)
        git(self.repo, "worktree", "add", "--detach", "--force", str(wt), sha)
        return wt

    def _commit(self, tree, parents, message) -> str:
        args = ["commit-tree", tree]
        for p in parents:
            args += ["-p", p]
        return git_out(self.repo, *args, stdin=message)

    def _message(self, job, *, reconciled_from=None, trailer=None) -> str:
        head = "Land %s onto %s" % (job["submitted_sha"][:12], self.target)
        if job.get("card"):
            card = job["card"] if str(job["card"]).startswith("#") else "#%s" % job["card"]
            head += " (%s)" % card
        lines = [head, "", "Landq-Job: %s" % job["id"], "Landq-Submitted: %s" % job["submitted_sha"],
                 "Landq-Kind: %s" % job["kind"]]
        if reconciled_from:
            lines.append("Reconciled-From: %s %s" % reconciled_from)
        if trailer:
            for t in (trailer if isinstance(trailer, (list, tuple)) else [trailer]):
                t = str(t).strip()
                if t and t not in lines:
                    lines.append(t)
        return "\n".join(lines) + "\n"

    def _merge_tree(self, tip, submitted):
        """(tree, conflicted_paths, output) from `git merge-tree --write-tree`."""
        proc = git(self.repo, "merge-tree", "--write-tree", "--name-only", tip, submitted,
                   check=False)
        out = proc.stdout
        if proc.returncode not in (0, 1):
            hint = " (git >= 2.38 is required for merge-tree --write-tree)" if (
                proc.returncode == 129 or "usage" in proc.stderr) else ""
            raise Refused("git merge-tree failed (%d)%s: %s" % (proc.returncode, hint,
                                                                proc.stderr.strip()))
        lines = out.splitlines()
        tree = lines[0].strip() if lines else ""
        if not tree:
            raise Refused("git merge-tree produced no tree: %s" % proc.stderr.strip())
        conflicted = []
        if proc.returncode == 1:
            for line in lines[1:]:
                if not line.strip():
                    break
                conflicted.append(line.strip())
        return tree, conflicted, out + proc.stderr

    def _changed_paths(self, tip, tree) -> list[dict]:
        """Raw diff-tree entries between the target tip and a tree: mode, status and path."""
        out = git(self.repo, "diff-tree", "-r", "--no-renames", "-z", "--raw", tip, tree).stdout
        parts = out.split("\0")
        entries = []
        i = 0
        while i + 1 < len(parts) and parts[i]:
            meta = parts[i].lstrip(":").split(" ")
            src_mode, dst_mode, _src, _dst, status = meta[0], meta[1], meta[2], meta[3], meta[4]
            entries.append({"src_mode": src_mode, "dst_mode": dst_mode, "status": status,
                            "path": parts[i + 1]})
            i += 2
        return entries

    def _check_metadata(self, tip, tree):
        """A metadata job may only change canonical Board files. Anything else — a code path,
        a symlink (which could point outside `.board/`), a gitlink, a path that climbs — is a
        refusal, and the candidate is never verified or published."""
        bad = []
        for entry in self._changed_paths(tip, tree):
            path = entry["path"]
            segments = path.split("/")
            if not path.startswith(METADATA_ROOT) or any(s in ("", ".", "..") for s in segments):
                bad.append("%s: outside %s" % (path, METADATA_ROOT))
            elif entry["dst_mode"] in ("120000", "160000"):
                bad.append("%s: mode %s (symlink or submodule)" % (path, entry["dst_mode"]))
        if bad:
            raise ConflictError("metadata job touches more than the Board: " + "; ".join(bad[:10]))

    def _conflict_markers(self, tree, tip, submitted) -> list[str]:
        """Paths in `tree` that differ from both sides and still hold merge markers."""
        changed = set()
        for entry in self._changed_paths(tip, tree):
            changed.add(entry["path"])
        if not changed:
            return []
        args = ["grep", "-l", "-I", "-e", CONFLICT_MARKERS[0], "-e", CONFLICT_MARKERS[1],
                tree, "--", *sorted(changed)]
        proc = git(self.repo, *args, check=False)
        if proc.returncode not in (0, 1):
            raise Refused("git grep failed: %s" % proc.stderr.strip())
        found = []
        for line in proc.stdout.splitlines():
            # "<tree>:<path>"
            _, _, path = line.partition(":")
            if path:
                found.append(path)
        return found

    # ----------------------------------------------------------------- recovery

    def _recover_locked(self) -> list[dict]:
        touched = []
        with self._tx() as conn:
            rows = conn.execute("SELECT * FROM jobs WHERE status IN (?,?,?) ORDER BY rowid",
                                IN_FLIGHT).fetchall()
            jobs = [self._job(r) for r in rows]
        if not jobs:
            return touched
        tip = None
        for job in jobs:
            job_id = job["id"]
            landed = False
            if job["status"] == "publishing" and job.get("candidate_sha"):
                if tip is None:
                    tip = self._tip()
                if _is_ancestor(self.repo, job["candidate_sha"], tip):
                    landed = True
            with self._tx() as conn:
                if landed:
                    self._land(conn, job, published=job["candidate_sha"],
                               reason="recovered: the target already contained the published"
                                      " candidate")
                else:
                    self._set(conn, job_id, status="interrupted", candidate_sha=None,
                              candidate_tree=None, policy_hash=None,
                              reason="interrupted while %s; will be rebuilt and reverified"
                              % job["status"])
                touched.append(self._load(conn, job_id))
        return touched

    def _land(self, conn, job, *, published, reason=None):
        """Receipt + landed + notification, in the caller's transaction."""
        tree = self._tree_of(published)
        verification = conn.execute(
            "SELECT * FROM verifications WHERE job_id=? AND candidate_sha=? AND ok=1"
            " ORDER BY id DESC LIMIT 1", (job["id"], published)).fetchone()
        verified = bool(verification and verification["verified"])
        policy = verification["policy_hash"] if verification else job.get("policy_hash")
        conn.execute(
            "INSERT OR IGNORE INTO receipts(job_id, repo_id, submitted_sha, target, target_before,"
            " published_sha, tree, policy_hash, verified, landed_at) VALUES (?,?,?,?,?,?,?,?,?,?)",
            (job["id"], self.repo_id, job["submitted_sha"], self.target, job.get("target_sha"),
             published, tree, policy, int(verified), _now()))
        self._set(conn, job["id"], status="landed", published_sha=published, reason=reason)
        self._notify(conn, job["id"], "landed", {
            "job_id": job["id"], "card": job.get("card"), "workspace_id": job.get("workspace_id"),
            "published_sha": published, "target_before": job.get("target_sha"),
            "target": self.target})

    # ----------------------------------------------------------------- one attempt

    def _attempt(self, job_id, verifier, reconcile, accepted_policy_hash=None) -> str:
        """One prepare/verify/publish pass. Returns "done" or "retry" (the target moved under a
        verified candidate: everything is rebuilt against the new tip)."""
        attached = self.target_attached()
        if attached:
            raise TargetAttached("refs/heads/%s is checked out in %s; the queue never moves a "
                                 "branch a workspace has checked out" % (self.target, attached))
        with self._tx() as conn:
            job = self._load(conn, job_id)
            if job["cancel_requested"]:
                self._set(conn, job_id, status="cancelled", reason="cancelled by request")
                self._notify(conn, job_id, "cancelled", {"job_id": job_id})
                return "done"
            if job["status"] not in PICKABLE:
                return "done"
        tip = self._tip()

        # A verified candidate against this very tip, under the policy the caller says is in
        # force, can go straight to publication. Anything else is verified again.
        if (job["status"] == "ready" and job["target_sha"] == tip and job["candidate_sha"]
                and accepted_policy_hash is not None
                and job["policy_hash"] == str(accepted_policy_hash)):
            if self._verified_row(job) is not None:
                return self._publish(job_id, tip)

        with self._tx() as conn:
            self._set(conn, job_id, status="preparing", target_sha=tip, candidate_sha=None,
                      candidate_tree=None, policy_hash=None, reason=None,
                      attempts=job["attempts"] + 1)
            job = self._load(conn, job_id)
        submitted = job["submitted_sha"]

        if _is_ancestor(self.repo, submitted, tip):
            with self._tx() as conn:
                job = self._load(conn, job_id)
                self._land(conn, job, published=tip,
                           reason="nothing to publish: the target already contains %s"
                           % submitted[:12])
            return "done"

        reconciled_from = None
        trailer = None
        fast_forward = _is_ancestor(self.repo, tip, submitted)
        if fast_forward:
            tree = self._tree_of(submitted)
            self._log(job_id, "merge.log", "fast-forward %s -> %s\n" % (tip, submitted))
        else:
            tree, conflicted, output = self._merge_tree(tip, submitted)
            self._log(job_id, "merge.log", output)
            if conflicted:
                resolution = self._reconcile(job, tip, tree, conflicted, output, reconcile)
                if resolution is None:
                    return "done"
                tree, trailer = resolution
                reconciled_from = (tip, submitted)
        if job["kind"] == "metadata":
            try:
                self._check_metadata(tip, tree)
            except ConflictError as exc:
                with self._tx() as conn:
                    self._set(conn, job_id, status="conflict", reason=str(exc))
                    self._notify(conn, job_id, "conflict", {"job_id": job_id, "reason": str(exc)})
                return "done"
        if fast_forward:
            candidate = submitted  # the author's exact commit is what lands
        else:
            candidate = self._commit(tree, [tip, submitted],
                                     self._message(job, reconciled_from=reconciled_from,
                                                   trailer=trailer))
        self._retain(job_id, "candidate", candidate)

        # Verify: the candidate, checked out where the verifier can build it.
        with self._tx() as conn:
            self._set(conn, job_id, status="verifying", candidate_sha=candidate,
                      candidate_tree=tree)
            job = self._load(conn, job_id)
        path = self._service_checkout(candidate)
        try:
            result = verifier(job, candidate, str(path))
            if not isinstance(result, dict) or "ok" not in result:
                raise Refused("verifier returned %r, not a result dict" % (result,))
        except InjectedCrash:
            raise
        except Exception as exc:  # noqa: BLE001 — a verifier that dies is a failed gate, not a loop
            reason = "verifier raised %s: %s" % (exc.__class__.__name__, exc)
            log_path = self._log(job_id, "verify-%d.log" % job["attempts"], reason + "\n")
            with self._tx() as conn:
                self._set(conn, job_id, status="failed", reason=reason)
                self._notify(conn, job_id, "failed", {"job_id": job_id, "candidate_sha": candidate,
                                                      "reason": reason, "log": log_path})
            return "done"
        policy = str(result.get("policy_hash") or "")
        log_path = self._log(job_id, "verify-%d.log" % job["attempts"], str(result.get("log", "")))
        ok = bool(result["ok"])
        verified = bool(result.get("verified", ok))
        reason = result.get("reason")
        tampered = self._workspace_drift(path, candidate)
        if tampered:
            # A pass over different bytes is not a pass over this commit.
            ok, verified = False, False
            reason = "verifier changed the candidate workspace (%s); the result cannot be " \
                     "bound to %s" % (tampered, candidate[:12])
        with self._tx() as conn:
            conn.execute(
                "INSERT INTO verifications(job_id, candidate_sha, candidate_tree, target_sha,"
                " policy_hash, ok, verified, reason, log_path, created_at)"
                " VALUES (?,?,?,?,?,?,?,?,?,?)",
                (job_id, candidate, tree, tip, policy, int(ok), int(verified),
                 str(reason) if reason else None, log_path, _now()))
            if not ok:
                self._set(conn, job_id, status="failed", policy_hash=policy,
                          reason=str(reason or "verification failed"))
                self._notify(conn, job_id, "failed", {"job_id": job_id, "candidate_sha": candidate,
                                                      "reason": str(reason or "verification failed"),
                                                      "log": log_path})
                return "done"
            self._set(conn, job_id, status="ready", policy_hash=policy, reason=None)
        return self._publish(job_id, tip)

    def _workspace_drift(self, path, candidate) -> str:
        """What differs between the service worktree and `candidate` after the verifier ran:
        "" when HEAD and every tracked file are still exactly the candidate."""
        head = git(path, "rev-parse", "--verify", "--quiet", "HEAD", check=False).stdout.strip()
        if head != candidate:
            return "HEAD is %s" % (head[:12] or "missing")
        changed = git(path, "status", "--porcelain", "--untracked-files=no", "--ignored=no",
                      check=False).stdout.splitlines()
        if changed:
            paths = sorted(line[3:] for line in changed)
            return "tracked files modified: %s" % ", ".join(paths[:10])
        return ""

    def dispose_workspace(self) -> dict:
        """Remove the service worktree (one per repository, rebuilt on demand). Refs, logs,
        receipts and the database stay. For a caller that wants no source checkout retained
        between jobs."""
        wt = self.service_dir / "candidate"
        removed = False
        if wt.exists():
            proc = git(self.repo, "worktree", "remove", "--force", str(wt), check=False)
            if proc.returncode != 0:
                import shutil
                shutil.rmtree(wt, ignore_errors=True)
            git(self.repo, "worktree", "prune")
            removed = True
        return {"path": str(wt), "removed": removed}

    def _verified_row(self, job):
        with self._tx() as conn:
            return conn.execute(
                "SELECT * FROM verifications WHERE job_id=? AND candidate_sha=? AND"
                " candidate_tree=? AND target_sha=? AND ok=1 ORDER BY id DESC LIMIT 1",
                (job["id"], job["candidate_sha"], job["candidate_tree"],
                 job["target_sha"])).fetchone()

    def _reconcile(self, job, tip, conflicted_tree, conflicted, output, reconcile):
        """Run the reconcile callback on a conflicted merge. Returns (tree, trailer) for a
        resolution that is a new candidate, or None after recording the `conflict` outcome."""
        job_id = job["id"]
        submitted = job["submitted_sha"]

        def give_up(reason, extra=None):
            with self._tx() as conn:
                self._set(conn, job_id, status="conflict", reason=reason)
                payload = {"job_id": job_id, "card": job.get("card"),
                           "workspace_id": job.get("workspace_id"), "conflicts": conflicted,
                           "reason": reason, "target_sha": tip, "submitted_sha": submitted}
                if extra:
                    payload.update(extra)
                self._notify(conn, job_id, "author_required", payload)
            return None

        if reconcile is None:
            return give_up("merge conflict in %s" % ", ".join(conflicted))

        # Materialise the conflicted result (markers and all) where the reconciler can edit it.
        staged = self._commit(conflicted_tree, [tip, submitted],
                              "conflicted merge for landq job %s (not a candidate)\n" % job_id)
        self._retain(job_id, "conflicted", staged)
        path = self._service_checkout(staged)
        context = {
            "repo": str(self.repo), "repo_id": self.repo_id, "job_id": job_id,
            "base_sha": job.get("base_sha") or git_out(self.repo, "merge-base", tip, submitted),
            "target_sha": tip, "submitted_sha": submitted, "candidate_path": str(path),
            "cards": [job["card"]] if job.get("card") else [], "intents": {},
            "conflicts": list(conflicted), "diagnostics": output,
            "policy": job.get("policy_hash"),
        }
        try:
            result = reconcile(context)
            if inspect.isawaitable(result):
                try:
                    asyncio.get_running_loop()
                except RuntimeError:
                    result = asyncio.run(result)
                else:
                    raise Refused("reconcile returned an awaitable inside a running event loop;"
                                  " wrap it in a synchronous adapter")
        except QueueError:
            raise
        except Exception as exc:  # noqa: BLE001 — a broken reconciler is a conflict, not a crash
            return give_up("reconcile raised %s: %s" % (exc.__class__.__name__, exc))
        self._log(job_id, "reconcile.json",
                  json.dumps(result if isinstance(result, dict) else {"result": str(result)},
                             indent=1, default=str))
        if not isinstance(result, dict) or result.get("status") != "resolved":
            reason = (result or {}).get("reason") if isinstance(result, dict) else None
            return give_up(reason or "reconcile: author required", {"reconcile": result if
                                                                       isinstance(result, dict)
                                                                       else str(result)})
        # Where did the resolution go? A commit or tree the reconciler names, else the tree it
        # edited in place. Either way the queue makes the commit; nothing the reconciler
        # produced is published as-is.
        if result.get("candidate_sha"):
            tree = self._tree_of(resolve_commit(self.repo, str(result["candidate_sha"])))
        elif result.get("tree"):
            proc = git(self.repo, "rev-parse", "--verify", "--quiet", str(result["tree"]) + "^{tree}",
                       check=False)
            if proc.returncode != 0:
                return give_up("reconcile named a tree that does not exist")
            tree = proc.stdout.strip()
        else:
            git(path, "add", "-A", "--", ".")
            tree = git_out(path, "write-tree")
        markers = self._conflict_markers(tree, tip, submitted)
        if markers:
            return give_up("reconcile left conflict markers in %s" % ", ".join(markers))
        if tree == conflicted_tree:
            return give_up("reconcile changed nothing")
        return tree, result.get("trailer")

    def _publish(self, job_id, tip) -> str:
        """Compare-and-swap the target to the verified candidate. Intent goes to SQLite first;
        a crash after the swap is recognised by `recover()` from the ref itself."""
        attached = self.target_attached()
        if attached:
            raise TargetAttached("refs/heads/%s is checked out in %s" % (self.target, attached))
        with self._tx() as conn:
            job = self._load(conn, job_id)
            if job["status"] != "ready":
                return "done"
            if job["cancel_requested"]:
                self._set(conn, job_id, status="cancelled", reason="cancelled before publication")
                self._notify(conn, job_id, "cancelled", {"job_id": job_id})
                return "done"
            candidate = job["candidate_sha"]
            if self._tree_of(candidate) != job["candidate_tree"]:
                raise Refused("candidate %s no longer has tree %s" % (candidate[:12],
                                                                     job["candidate_tree"][:12]))
            if job["target_sha"] != tip:
                raise Refused("candidate was verified against %s, not %s" % (
                    job["target_sha"][:12], tip[:12]))
            row = conn.execute(
                "SELECT id FROM verifications WHERE job_id=? AND candidate_sha=? AND"
                " candidate_tree=? AND target_sha=? AND policy_hash=? AND ok=1",
                (job_id, candidate, job["candidate_tree"], tip, job["policy_hash"])).fetchone()
            if row is None:
                raise Refused("no verification bound to candidate %s" % candidate[:12])
            # The durable intent: status + candidate + expected old value, before the ref moves.
            self._set(conn, job_id, status="publishing")
        self._fault("before_update_ref")
        swap = git(self.repo, "update-ref", "refs/heads/%s" % self.target, candidate, tip,
                   check=False)
        if swap.returncode != 0:
            now_tip = self._tip()
            with self._tx() as conn:
                if now_tip != tip:
                    self._set(conn, job_id, status="queued", candidate_sha=None,
                              candidate_tree=None, policy_hash=None,
                              reason="target moved from %s to %s during publication; rebuilding"
                              % (tip[:12], now_tip[:12]))
                    outcome = "retry"
                else:
                    self._set(conn, job_id, status="interrupted",
                              reason="update-ref refused: %s" % swap.stderr.strip())
                    outcome = "done"
            return outcome
        self._fault("after_update_ref")
        with self._tx() as conn:
            job = self._load(conn, job_id)
            self._land(conn, job, published=candidate)
        return "done"

    def _fault(self, name):
        if name in self.faults:
            raise InjectedCrash(name)


# --------------------------------------------------------------------------- CLI

USAGE = """relay-land: submit work to a repository's landing queue and read it back (card #FW1C).

  relay-land submit SHA --request-id ID [--workspace-id W] [--card C] [--base-sha B]
                        [--kind code|metadata] [--test NAME ...]
  relay-land status [JOB]            every job, or one
  relay-land cancel JOB
  relay-land receipt JOB
  relay-land recover                 settle what a dead publisher left in flight
  relay-land outbox [--all]          pending (or every) notification

Common options: --repo PATH (default: cwd), --state-root PATH, --repo-id ID, --target BRANCH.
Output is JSON. Exit 0 success, 1 usage, 2 refused, 3 conflict, 5 gate failure, 7 the
publisher is busy. Submit succeeds as soon as the job is recorded; `run` is B1's (card #AMQQ).
"""


def _parse_args(argv):
    import argparse
    parser = argparse.ArgumentParser(prog="relay-land", usage=USAGE, add_help=True)
    parser.add_argument("--repo", default=None)
    parser.add_argument("--state-root", default=None)
    parser.add_argument("--repo-id", default=None)
    parser.add_argument("--target", default=None)
    sub = parser.add_subparsers(dest="verb")
    p = sub.add_parser("submit")
    p.add_argument("sha")
    p.add_argument("--request-id", required=True)
    p.add_argument("--workspace-id")
    p.add_argument("--card")
    p.add_argument("--base-sha")
    p.add_argument("--kind", default="code", choices=KINDS)
    p.add_argument("--test", action="append", default=[])
    p = sub.add_parser("status")
    p.add_argument("job", nargs="?")
    p = sub.add_parser("cancel")
    p.add_argument("job")
    p = sub.add_parser("receipt")
    p.add_argument("job")
    sub.add_parser("recover")
    p = sub.add_parser("outbox")
    p.add_argument("--all", action="store_true")
    return parser, parser.parse_args(argv)


def main(argv=None, *, out=None) -> int:
    out = out or sys.stdout
    argv = list(sys.argv[1:] if argv is None else argv)
    try:
        parser, args = _parse_args(argv)
    except SystemExit as exc:
        return 0 if exc.code == 0 else 1
    if not args.verb:
        print(USAGE, file=out)
        return 1

    def emit(value):
        json.dump(value, out, indent=1, sort_keys=True, default=str)
        out.write("\n")

    try:
        queue = Queue(args.repo or os.getcwd(), state_root=args.state_root,
                      repo_id=args.repo_id, target=args.target)
        if args.verb == "submit":
            emit(queue.submit(args.sha, request_id=args.request_id,
                              workspace_id=args.workspace_id, card=args.card,
                              base_sha=args.base_sha, kind=args.kind, selected_tests=args.test))
        elif args.verb == "status":
            emit(queue.status(args.job))
        elif args.verb == "cancel":
            emit(queue.cancel(args.job))
        elif args.verb == "receipt":
            receipt = queue.receipt(args.job)
            if receipt is None:
                emit({"error": "no receipt for job %s" % args.job})
                return 2
            emit(receipt)
        elif args.verb == "recover":
            emit(queue.recover())
        elif args.verb == "outbox":
            emit(queue.outbox(pending_only=not args.all))
        return 0
    except QueueError as exc:
        emit({"error": str(exc), "kind": exc.__class__.__name__})
        return exc.exit_code


if __name__ == "__main__":
    sys.exit(main())
