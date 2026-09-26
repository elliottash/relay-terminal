# SPDX-License-Identifier: AGPL-3.0-or-later
"""Isolated development workspace lifecycle (card #RT3B, docs/TREES-AND-LANDING.md §A1).

Canonical project identity and source-only development workspaces:

* ``register_repo`` records a repository once, keyed by its resolved Git common
  directory, and returns a persistent random repo ID. Registering the same common
  dir again returns the same record; a relocated common dir is *explicit* repair
  (``repair=True``), never a silent second identity. ``mode`` defaults to
  ``legacy``: registering changes nothing about how the project publishes.
* ``resolve_project`` maps any path inside a registered repo — main checkout or
  any linked worktree — back to its canonical record. It never allocates.
* ``TreeManager`` creates, leases, syncs and removes development workspaces under
  ``state_root/trees/<repo-id>/<workspace-id>/`` as Git worktrees of the canonical
  repository. Create is idempotent per active session, serialized cross-process by
  an flock on the repo's trees directory, and leaves both crash windows (registry
  row before worktree, worktree before ``active``) recoverable on the next call
  from the same session.

Sparse exclusions use non-cone patterns written to the worktree's own
``info/sparse-checkout`` (never the shared one, never the porcelain
``git sparse-checkout`` in a linked worktree, which on some Git versions applies
the patterns to the *main* checkout). The canonical Board is always excluded —
a development tree must not look like a second Board (invariant 5) — while
instruction/config files (AGENTS.md, CLAUDE.md, RELAY.md, …) are re-included
last so no exclusion can remove them. Explicit ``includes`` punch specific
paths (e.g. an agent's own evidence) back in.

``sync`` is the only rebase: it requires the lease owner, refuses a dirty tree,
and aborts + reports a conflict rather than leaving a half-finished rebase.
``remove`` requires a released lease, verifies a queue receipt's repo and
submitted SHA against the branch's current tip, refuses dirty/untracked/ignored
content instead of deleting it (no broad ``git clean``, ignored dependency or
user files are never touched), and refuses unlanded work with no receipt.
Pending-submission state lives in A2's queue database; the receipt is the
cross-module proof A1 verifies (docs/TREES-AND-LANDING.md: cross-database links
are IDs plus receipts).

State layout (contract): ``state_root/integration/registry.sqlite3`` holds the
repository and workspace tables (WAL, foreign keys, busy timeout, schema
version); ``state_root/trees/<repo-id>/`` holds the worktrees. ``state_root``
defaults to ``$XDG_STATE_HOME/relay`` (``~/.local/state/relay``) and every
entry point accepts an explicit override for tests. It is never a pane's
temporary directory.
"""
import json
import os
import secrets
import shlex
import sqlite3
import subprocess
import time
from datetime import datetime, timezone
from pathlib import Path

from relay_core.filelock import LOCK_EX, LOCK_UN, flock, open_directory_lock

SCHEMA_VERSION = 1

# Workspace lifecycle: creating -> active -> released -> removed. `init_failed`
# sits beside `active` (durable, retryable); `retained` is a released workspace
# whose branch still holds unlanded work (kept source, cleanable only with a
# receipt that covers its tip).
LIVE_STATUSES = ("creating", "active", "init_failed")

# Instruction/config files an exclusion may never remove: re-included after all
# exclusions so last-match-wins keeps them in every development tree.
PROTECTED_PATHS = ("AGENTS.md", "CLAUDE.md", "RELAY.md", ".gitignore",
                   ".gitattributes", ".gitmodules", ".editorconfig",
                   ".relay/project.toml")

_GIT_TIMEOUT = 120
_CREATE_LOCK_TIMEOUT = 30.0

# Queue (A2) job statuses that mean a submission is still in flight; cleanup of
# a workspace named by one is refused. Durable copies of receipts live in the
# same database's receipts table (docs/TREES-AND-LANDING.md §A2).
_QUEUE_PENDING = ("queued", "preparing", "verifying", "ready", "publishing",
                  "interrupted")

# Repo identity marker inside the Git common dir: it travels with the repo, so
# a whole-repository move is detected as relocation, not a new identity.
_MARKER_NAME = "relay-repo-id"


class TreeError(Exception):
    """Base for every trees refusal; ``exit_code`` is the CLI mapping."""


class TreeUsageError(TreeError):
    """Bad arguments or configuration (CLI exit 1)."""
    exit_code = 1


class NotRegisteredError(TreeError):
    """The path is not inside a registered repository (CLI exit 1)."""
    exit_code = 1


class TreeNotFoundError(TreeError):
    """No such repository or workspace record (CLI exit 1)."""
    exit_code = 1


class TreeRefusedError(TreeError):
    """The operation is refused as unsafe or not allowed (CLI exit 2)."""
    exit_code = 2


class TreeConflictError(TreeError):
    """A competing lease, lock or rebase conflict (CLI exit 3)."""
    exit_code = 3


TreeError.exit_code = 1


# ------------------------------------------------------------------ state root

def state_root_default() -> Path:
    base = (os.environ.get("RELAY_STATE_HOME") or os.environ.get("XDG_STATE_HOME")
            or str(Path.home() / ".local" / "state"))
    return Path(base) / "relay"


def _state_root(state_root) -> Path:
    return Path(state_root) if state_root is not None else state_root_default()


def _registry_path(state_root) -> Path:
    return _state_root(state_root) / "integration" / "registry.sqlite3"


def _trees_dir(state_root, repo_id) -> Path:
    return _state_root(state_root) / "trees" / repo_id


# ----------------------------------------------------------------------- git

def _clean_env(extra: dict | None = None) -> dict:
    """Environment for child processes with ambient GIT_* scrubbed.

    Callers may run inside a hook, a rebase or another worktree where GIT_DIR,
    GIT_WORK_TREE, GIT_INDEX_FILE and friends would silently redirect every
    command below; nothing this module runs needs inherited Git state.
    """
    env = {k: v for k, v in os.environ.items() if not k.startswith("GIT_")}
    if extra:
        env.update(extra)
    return env


def _git(args, *, cwd=None, git_dir=None, timeout=_GIT_TIMEOUT):
    cmd = ["git"]
    if git_dir is not None:
        cmd += ["--git-dir", str(git_dir)]
    cmd += list(args)
    try:
        proc = subprocess.run(cmd, cwd=cwd, capture_output=True, text=True,
                              timeout=timeout, env=_clean_env())
    except FileNotFoundError:
        raise TreeUsageError("git is not on PATH")
    except subprocess.TimeoutExpired:
        raise TreeRefusedError(f"git {' '.join(args)} timed out after {timeout}s")
    return proc.returncode, proc.stdout, proc.stderr


def _git_ok(args, **kw) -> str:
    rc, out, err = _git(args, **kw)
    if rc != 0:
        raise TreeRefusedError(f"git {' '.join(args)} failed: {err.strip() or out.strip()}")
    return out.strip()


def _common_dir(path) -> Path | None:
    """Resolved absolute Git common dir for any checkout or linked worktree."""
    rc, out, _ = _git(["rev-parse", "--path-format=absolute", "--git-common-dir"],
                      cwd=str(path))
    if rc != 0:  # Git < 2.31: relative output resolved against the given path
        rc, out, _ = _git(["rev-parse", "--git-common-dir"], cwd=str(path))
        if rc != 0:
            return None
        out = out.strip()
        if not os.path.isabs(out):
            out = str(Path(path) / out)
    return Path(os.path.realpath(out.strip()))


def _rev_parse(git_dir, ref) -> str:
    return _git_ok(["rev-parse", "--verify", f"{ref}^{{commit}}"], git_dir=git_dir)


# -------------------------------------------------------------------- database

def _connect(state_root) -> sqlite3.Connection:
    path = _registry_path(state_root)
    path.parent.mkdir(parents=True, exist_ok=True)
    conn = sqlite3.connect(str(path), timeout=5.0)
    conn.row_factory = sqlite3.Row
    conn.execute("PRAGMA journal_mode=WAL")
    conn.execute("PRAGMA foreign_keys=ON")
    conn.execute("PRAGMA busy_timeout=5000")
    version = conn.execute("PRAGMA user_version").fetchone()[0]
    if version > SCHEMA_VERSION:
        conn.close()
        raise TreeUsageError(
            f"registry schema version {version} is newer than this module "
            f"({SCHEMA_VERSION}); upgrade Relay")
    if version == 0:
        conn.executescript(
            """
            CREATE TABLE repositories (
                id          TEXT PRIMARY KEY,
                common_dir  TEXT NOT NULL UNIQUE,
                project_root TEXT NOT NULL DEFAULT '',
                board_root  TEXT NOT NULL DEFAULT '',
                target      TEXT NOT NULL DEFAULT 'main',
                mode        TEXT NOT NULL DEFAULT 'legacy',
                created_at  TEXT NOT NULL,
                updated_at  TEXT NOT NULL
            );
            CREATE TABLE workspaces (
                id          TEXT PRIMARY KEY,
                repo_id     TEXT NOT NULL REFERENCES repositories(id),
                path        TEXT NOT NULL,
                branch      TEXT NOT NULL,
                base_sha    TEXT NOT NULL,
                session     TEXT NOT NULL,
                card        TEXT NOT NULL DEFAULT '',
                owner       TEXT NOT NULL DEFAULT '',
                status      TEXT NOT NULL,
                init_cmd    TEXT NOT NULL DEFAULT '',
                init_error  TEXT NOT NULL DEFAULT '',
                created_at  TEXT NOT NULL,
                updated_at  TEXT NOT NULL
            );
            -- One live workspace per (repo, session): create is idempotent
            -- per active session and a crash window is found, not duplicated.
            CREATE UNIQUE INDEX workspaces_live_session ON workspaces(repo_id, session)
                WHERE status IN ('creating', 'active', 'init_failed');
            """)
        conn.execute(f"PRAGMA user_version={SCHEMA_VERSION}")
        conn.commit()
    return conn


def _now() -> str:
    return datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")


def _repo_record(row: sqlite3.Row) -> dict:
    return {"id": row["id"], "common_dir": row["common_dir"],
            "project_root": row["project_root"], "board_root": row["board_root"],
            "target": row["target"], "mode": row["mode"],
            "created_at": row["created_at"], "updated_at": row["updated_at"]}


def _workspace_record(row: sqlite3.Row) -> dict:
    return {"id": row["id"], "repo_id": row["repo_id"], "path": row["path"],
            "execution_cwd": row["path"], "branch": row["branch"],
            "base_sha": row["base_sha"], "session": row["session"],
            "card": row["card"], "owner": row["owner"], "status": row["status"],
            "created_at": row["created_at"], "updated_at": row["updated_at"],
            "init_error": row["init_error"]}


# ------------------------------------------------------------- repo identity

def _read_marker(common: Path) -> str | None:
    try:
        marker = (common / _MARKER_NAME).read_text().strip()
    except OSError:
        return None
    return marker or None


def _write_marker(common: Path, repo_id: str):
    try:
        (common / _MARKER_NAME).write_text(repo_id + "\n")
    except OSError:
        pass  # a read-only common dir must not fail registration


def _project_roots(common: Path, repo) -> tuple[str, str]:
    if os.path.basename(common) == ".git" and common.parent.is_dir():
        project_root = str(common.parent)
    else:
        rc, out, _ = _git(["rev-parse", "--show-toplevel"], cwd=str(repo))
        project_root = out.strip() if rc == 0 else ""
    board = Path(project_root) / ".board" if project_root else None
    return project_root, str(board) if board and board.is_dir() else ""


def register_repo(repo, *, state_root=None, target=None, mode=None,
                  repair=False) -> dict:
    """Register a repository's canonical identity and return its record.

    Keyed by the resolved Git common dir: re-registering returns the existing
    record unchanged. A marker file (``relay-repo-id``) inside the common dir
    travels with the repository, so moving the *whole* repo — project root,
    Board and all — is still recognized as the same identity: refused as a
    relocation without ``repair=True``, rebound with it, never a silent second
    identity. A *copy* (the old common dir still exists) is always refused.
    ``target`` defaults to ``main`` and ``mode`` to ``legacy``; registration
    alone never changes how the project publishes.
    """
    common = _common_dir(repo)
    if common is None:
        raise TreeUsageError(f"{repo}: not inside a Git repository")
    project_root, board_root = _project_roots(common, repo)
    marker_id = _read_marker(common)
    conn = _connect(state_root)
    try:
        by_common = conn.execute("SELECT * FROM repositories WHERE common_dir = ?",
                                 (str(common),)).fetchone()
        by_marker = (conn.execute("SELECT * FROM repositories WHERE id = ?",
                                  (marker_id,)).fetchone() if marker_id else None)

        if by_common is not None and by_marker is not None \
                and by_common["id"] != by_marker["id"]:
            raise TreeRefusedError(
                f"{repo}: identity conflict — the common-dir marker says "
                f"{by_marker['id']} but the registry keys this common dir as "
                f"{by_common['id']}; refusing to guess (inspect the registry and "
                f"the {_MARKER_NAME} marker)")
        if by_marker is not None:
            if by_marker["common_dir"] == str(common):
                return _repo_record(by_marker)
            if Path(by_marker["common_dir"]).exists():
                raise TreeRefusedError(
                    f"{repo}: this repository carries the identity marker of "
                    f"{by_marker['id']}, whose common dir {by_marker['common_dir']} "
                    f"still exists — this looks like a copy, and a copy needs its "
                    f"own identity: delete the {_MARKER_NAME} marker inside its "
                    f"Git directory to register it as new")
            if not repair:
                raise TreeRefusedError(
                    f"{repo}: the repository registered as {by_marker['id']} moved "
                    f"(its common dir {by_marker['common_dir']} is gone); "
                    f"relocation is explicit repair — call register_repo(..., "
                    f"repair=True), not a new registration")
            conn.execute("UPDATE repositories SET common_dir = ?, project_root = ?,"
                         " board_root = ?, updated_at = ? WHERE id = ?",
                         (str(common), project_root, board_root, _now(),
                          by_marker["id"]))
            conn.commit()
            _write_marker(common, by_marker["id"])
            row = conn.execute("SELECT * FROM repositories WHERE id = ?",
                               (by_marker["id"],)).fetchone()
            return _repo_record(row)
        if by_common is not None:
            if marker_id is None:
                _write_marker(common, by_common["id"])  # adopt: pre-marker record
            return _repo_record(by_common)
        if marker_id is not None:
            if not repair:
                raise TreeRefusedError(
                    f"{repo}: the common-dir marker names repository {marker_id}, "
                    f"but the registry has no such record; refusing to allocate a "
                    f"second identity — call register_repo(..., repair=True) to "
                    f"rebind the registry to the marker")
            repo_id = marker_id
        else:
            # Repositories registered before the marker existed: a relocated
            # common dir whose project root or Board still matches is the same
            # project, not a new identity.
            stale = conn.execute(
                "SELECT * FROM repositories WHERE"
                " (project_root != '' AND project_root = ?)"
                " OR (board_root != '' AND board_root = ?)",
                (project_root, board_root)).fetchone()
            if stale is not None:
                if not Path(stale["common_dir"]).exists() and repair:
                    conn.execute("UPDATE repositories SET common_dir = ?,"
                                 " project_root = ?, board_root = ?, updated_at = ?"
                                 " WHERE id = ?",
                                 (str(common), project_root, board_root, _now(),
                                  stale["id"]))
                    conn.commit()
                    _write_marker(common, stale["id"])
                    row = conn.execute("SELECT * FROM repositories WHERE id = ?",
                                       (stale["id"],)).fetchone()
                    return _repo_record(row)
                raise TreeRefusedError(
                    f"{repo}: this project is already registered as {stale['id']} "
                    f"with common dir {stale['common_dir']}; the common dir moved, "
                    f"which is explicit repair — call register_repo(..., "
                    f"repair=True), not a new registration")
            repo_id = secrets.token_hex(8)

        now = _now()
        conn.execute(
            "INSERT INTO repositories (id, common_dir, project_root, board_root,"
            " target, mode, created_at, updated_at) VALUES (?,?,?,?,?,?,?,?)",
            (repo_id, str(common), project_root, board_root,
             target or "main", mode or "legacy", now, now))
        conn.commit()
        _write_marker(common, repo_id)
        row = conn.execute("SELECT * FROM repositories WHERE id = ?",
                           (repo_id,)).fetchone()
        return _repo_record(row)
    finally:
        conn.close()


def resolve_project(path, *, state_root=None) -> dict | None:
    """Return the registered canonical repo record for any path inside it —
    main checkout or any linked worktree — or None. Never allocates."""
    common = _common_dir(path)
    if common is None:
        return None
    conn = _connect(state_root)
    try:
        row = conn.execute("SELECT * FROM repositories WHERE common_dir = ?",
                           (str(common),)).fetchone()
        return _repo_record(row) if row is not None else None
    finally:
        conn.close()


REPO_MODES = ("legacy", "queue", "paused")


def configure_repo(repo, *, state_root=None, mode=None, target=None) -> dict:
    """Update a registered repository's metadata and return its record.

    This is the supported registry mutation for B1's activation: it changes
    only the ``mode``/``target`` fields. ``mode`` must be one of
    ``legacy``/``queue``/``paused`` and ``target`` must be a valid branch name;
    actual activation (transition locks, draining, publisher start) is B1's
    job, so flipping the record here neither starts nor stops any publisher.
    """
    if mode is None and target is None:
        raise TreeUsageError("configure_repo needs mode and/or target")
    if mode is not None and mode not in REPO_MODES:
        raise TreeUsageError(
            f"mode must be one of {', '.join(REPO_MODES)}, not {mode!r}")
    if target is not None:
        target = str(target)
        if not target.strip():
            raise TreeUsageError("target must be a non-empty branch name")
        rc, _, err = _git(["check-ref-format", "--branch", target])
        if rc != 0:
            raise TreeUsageError(f"target {target!r} is not a valid branch name: "
                                 f"{err.strip()}")
    record = _repo_for(repo, state_root)
    conn = _connect(state_root)
    try:
        conn.execute(
            "UPDATE repositories SET mode = ?, target = ?, updated_at = ? WHERE id = ?",
            (mode if mode is not None else record["mode"],
             target if target is not None else record["target"],
             _now(), record["id"]))
        conn.commit()
        row = conn.execute("SELECT * FROM repositories WHERE id = ?",
                           (record["id"],)).fetchone()
        return _repo_record(row)
    finally:
        conn.close()


def _repo_for(manager_ref, state_root) -> dict:
    ref = str(manager_ref)
    conn = _connect(state_root)
    try:
        row = conn.execute("SELECT * FROM repositories WHERE id = ?", (ref,)).fetchone()
        if row is not None:
            return _repo_record(row)
    finally:
        conn.close()
    record = resolve_project(ref, state_root=state_root)
    if record is not None:
        return record
    raise NotRegisteredError(
        f"{ref}: not a registered repository; call register_repo() first "
        f"(registration never allocates a workspace and legacy mode is unchanged)")


# ------------------------------------------------------------------- locking

class _repo_lock:
    """Cross-process lifecycle serialization: flock on the repo's trees dir.

    Held across an entire create (registry row, worktree, sparse, init) and
    around release/sync/remove, so no session can observe or resume a
    half-finished lifecycle operation. A crashed holder frees the lock via the
    OS; a slow but alive one (long init) is waited out up to ``timeout``.
    """

    def __init__(self, directory: Path, timeout: float = _CREATE_LOCK_TIMEOUT):
        directory.mkdir(parents=True, exist_ok=True)
        self._fd = open_directory_lock(str(directory))
        self._timeout = timeout
        self._locked = False

    def __enter__(self):
        if os.name == "nt":  # the filelock shim has no LOCK_NB on Windows
            flock(self._fd, LOCK_EX)
            self._locked = True
            return self
        deadline = time.monotonic() + self._timeout
        while True:
            try:
                flock(self._fd, LOCK_EX | 4)  # 4 == LOCK_NB
                self._locked = True
                return self
            except OSError:
                if time.monotonic() >= deadline:
                    os.close(self._fd)
                    raise TreeConflictError(
                        "another lifecycle operation holds this repository's "
                        "workspace lock")
                time.sleep(0.1)

    def __exit__(self, *exc):
        if self._locked:
            flock(self._fd, LOCK_UN)
        os.close(self._fd)
        return False


# ------------------------------------------------------------------ sparse

def _sparse_patterns(board_rel: str, excludes, includes) -> list[str]:
    """Non-cone sparse patterns: include all, then exclusions, then explicit
    includes, then protected instruction/config files (last match wins).

    Non-cone sparse semantics differ from gitignore: a negated directory
    pattern does not exclude its contents, so every exclusion emits both
    ``!path`` and ``!path/**``. Verified against git 2.43 in tests."""
    patterns = ["*"]
    for raw in [board_rel, *excludes]:
        pat = str(raw).strip().lstrip("!").rstrip("/")
        if not pat:
            continue
        patterns.append(f"!{pat}")
        patterns.append(f"!{pat}/**")
    for raw in includes:
        pat = str(raw).strip().lstrip("!").rstrip("/")
        if pat:
            patterns.append(pat)
            # Like exclusions, a bare directory pattern does not reach the
            # files below it; emit the recursive form so an excluded parent's
            # `!dir/**` cannot win last-match over the re-inclusion.
            patterns.append(f"{pat}/**")
    for name in PROTECTED_PATHS:
        patterns.append(f"/{name}")
    return patterns


def _apply_sparse(path: Path, patterns: list[str]):
    """Write per-worktree sparse patterns and apply them.

    The porcelain ``git sparse-checkout`` is deliberately not used in a linked
    worktree (on git 2.43 it can apply the patterns to the main checkout);
    the per-worktree info/sparse-checkout plus worktree config is the safe
    path and never touches the main checkout or the shared info directory.
    """
    _git_ok(["config", "extensions.worktreeConfig", "true"], cwd=str(path))
    _git_ok(["config", "--worktree", "core.sparseCheckout", "true"], cwd=str(path))
    _git_ok(["config", "--worktree", "core.sparseCheckoutCone", "false"], cwd=str(path))
    sparse = _git_ok(["rev-parse", "--git-path", "info/sparse-checkout"], cwd=str(path))
    sparse_path = Path(sparse)
    if not sparse_path.is_absolute():
        sparse_path = path / sparse_path
    sparse_path.parent.mkdir(parents=True, exist_ok=True)
    sparse_path.write_text("\n".join(patterns) + "\n")
    _git_ok(["read-tree", "--empty"], cwd=str(path))
    _git_ok(["read-tree", "-m", "-u", "HEAD"], cwd=str(path))


# -------------------------------------------------------------- tree manager

class TreeManager:
    """Lifecycle of one repository's isolated development workspaces."""

    def __init__(self, repo, *, state_root=None):
        self.state_root = state_root
        self.repo = _repo_for(repo, state_root)
        self._root = _state_root(state_root)

    # -- helpers -----------------------------------------------------------

    def _conn(self) -> sqlite3.Connection:
        return _connect(self.state_root)

    def _row(self, conn, workspace_id) -> sqlite3.Row:
        row = conn.execute(
            "SELECT * FROM workspaces WHERE id = ? AND repo_id = ?",
            (str(workspace_id), self.repo["id"])).fetchone()
        if row is None:
            raise TreeNotFoundError(
                f"no workspace {workspace_id} in repository {self.repo['id']}")
        return row

    def _record(self, conn, workspace_id) -> dict:
        return _workspace_record(self._row(conn, workspace_id))

    def _branch_tip(self, branch) -> str:
        return _rev_parse(self.repo["common_dir"], f"refs/heads/{branch}")

    def _board_rel(self) -> str:
        board, root = self.repo["board_root"], self.repo["project_root"]
        if board and root:
            return os.path.relpath(board, root)
        return ""

    def _worktree_valid(self, path: Path) -> bool:
        if not path.is_dir():
            return False
        rc, out, _ = _git(["rev-parse", "--is-inside-work-tree"], cwd=str(path))
        return rc == 0 and out.strip() == "true"

    def _drop_partial(self, conn, row):
        """Remove a crash-window `creating` row whose worktree never materialized."""
        path = Path(row["path"])
        if path.exists():
            if self._worktree_valid(path):
                _git(["worktree", "remove", "--force", str(path)],
                     git_dir=self.repo["common_dir"])
        _git(["branch", "-D", row["branch"]], git_dir=self.repo["common_dir"])
        conn.execute("DELETE FROM workspaces WHERE id = ?", (row["id"],))
        conn.commit()

    @staticmethod
    def _normalize_init(init) -> list[str]:
        """Init is an argv array, never a shell string: lists pass through,
        strings are shlex-split, and execution uses no implicit shell."""
        if init is None:
            return []
        if isinstance(init, str):
            return shlex.split(init)
        return [str(arg) for arg in init]

    def _validate_includes(self, includes):
        """Explicit includes may punch holes in exclusions but never re-include
        the canonical Board: a development tree holds at most one Board, the
        real one (invariant 5)."""
        board = self._board_rel()
        if not board:
            return
        for raw in includes:
            pat = str(raw).strip().lstrip("!").lstrip("/").rstrip("/")
            if pat == board or pat.startswith(board + "/"):
                raise TreeRefusedError(
                    f"include {raw!r} re-includes the canonical Board ({board}/); "
                    f"the Board lives only in the main checkout — include your own "
                    f"evidence paths instead")

    def _run_init(self, conn, row, init_argv: list[str], timeout: float) -> dict:
        """Run the init argv in the new tree; failure is durable, not fatal.

        Relay never copies secrets into workspaces; init executes in the new
        tree with the caller's environment (ambient GIT_* scrubbed) and no
        shell. Called with the repository lifecycle lock held."""
        if not init_argv:
            status, error = "active", ""
        else:
            try:
                proc = subprocess.run(init_argv, cwd=row["path"], capture_output=True,
                                      text=True, timeout=timeout, env=_clean_env())
                if proc.returncode == 0:
                    status, error = "active", ""
                else:
                    tail = (proc.stderr or proc.stdout or "").strip()[-2000:]
                    status = "init_failed"
                    error = f"exit {proc.returncode}: {tail}"
            except FileNotFoundError:
                status, error = "init_failed", f"init executable not found: {init_argv[0]}"
            except subprocess.TimeoutExpired:
                status, error = "init_failed", f"init timed out after {timeout}s"
        conn.execute("UPDATE workspaces SET status = ?, init_cmd = ?, init_error = ?,"
                     " updated_at = ? WHERE id = ?",
                     (status, json.dumps(init_argv), error, _now(), row["id"]))
        conn.commit()
        return self._record(conn, row["id"])

    # -- lifecycle ---------------------------------------------------------

    def create(self, session, *, card=None, base=None, excludes=(), init=None,
               max_workspaces=50, includes=(), retry=False, init_timeout=600) -> dict:
        """Create (or idempotently return) the workspace for an active session.

        The repository's cross-process lifecycle lock is held across the whole
        creation — registry row, worktree, sparse checkout *and* init — so a
        concurrent same-session create waits and then sees the finished
        workspace instead of resuming (and resetting) a half-finished one.
        Crash windows are recoverable: a `creating` row from this session is
        either finalized (worktree valid) or cleaned up and recreated, never
        silently dropped. ``init`` is an argv array (strings are shlex-split)
        executed without a shell.
        """
        if not session or not str(session).strip():
            raise TreeUsageError("create needs a non-empty session")
        session = str(session)
        init_argv = self._normalize_init(init)
        self._validate_includes(includes)
        conn = self._conn()
        try:
            lock_timeout = max(_CREATE_LOCK_TIMEOUT, init_timeout + 60)
            with _repo_lock(_trees_dir(self.state_root, self.repo["id"]),
                            timeout=lock_timeout):
                row = conn.execute(
                    "SELECT * FROM workspaces WHERE repo_id = ? AND session = ?"
                    " AND status IN ('creating','active','init_failed')",
                    (self.repo["id"], session)).fetchone()
                if row is not None:
                    return self._resume(conn, row, excludes, includes, init_argv,
                                        retry, init_timeout)

                count = conn.execute(
                    "SELECT COUNT(*) c FROM workspaces WHERE repo_id = ?"
                    " AND status != 'removed'", (self.repo["id"],)).fetchone()["c"]
                if count >= max_workspaces:
                    raise TreeRefusedError(
                        f"workspace quota reached: {count} of at most "
                        f"{max_workspaces} for repository {self.repo['id']}; "
                        f"remove released workspaces first")

                base_sha = _rev_parse(self.repo["common_dir"],
                                      base or self.repo["target"])
                workspace_id = "wt" + secrets.token_hex(8)
                branch = f"relay/tree/{workspace_id}"
                path = _trees_dir(self.state_root, self.repo["id"]) / workspace_id
                now = _now()
                # Registry row first: a crash after this point is found by the
                # next create from this session, never silently dropped.
                conn.execute(
                    "INSERT INTO workspaces (id, repo_id, path, branch, base_sha,"
                    " session, card, owner, status, init_cmd, created_at, updated_at)"
                    " VALUES (?,?,?,?,?,?,?,?,'creating',?,?,?)",
                    (workspace_id, self.repo["id"], str(path), branch, base_sha,
                     session, card or "", session, json.dumps(init_argv), now, now))
                conn.commit()
                try:
                    self._materialize(path, branch, base_sha, excludes, includes)
                except TreeError:
                    self._drop_partial(conn, self._row(conn, workspace_id))
                    raise
                row = self._row(conn, workspace_id)
                return self._run_init(conn, row, init_argv, init_timeout)
        finally:
            conn.close()

    def _resume(self, conn, row, excludes, includes, init_argv, retry,
                init_timeout) -> dict:
        """Idempotent create: an existing live row for this session (lock held)."""
        if row["status"] == "active":
            return _workspace_record(row)
        stored_argv = json.loads(row["init_cmd"]) if row["init_cmd"] else []
        if row["status"] == "init_failed":
            if not retry:
                return _workspace_record(row)
            return self._run_init(conn, row, init_argv or stored_argv, init_timeout)
        # status == creating: a crash between the registry row and `active`.
        path = Path(row["path"])
        if not self._worktree_valid(path):
            self._drop_partial(conn, row)
            raise TreeRefusedError(
                f"recovered an interrupted create for session {row['session']}: the "
                f"worktree never materialized; the stale record was removed — "
                f"call create() again")
        # Worktree exists: re-apply sparse (idempotent) and finish what crashed.
        patterns = _sparse_patterns(self._board_rel(), excludes, includes)
        _apply_sparse(path, patterns)
        return self._run_init(conn, row, init_argv or stored_argv, init_timeout)

    def _materialize(self, path: Path, branch: str, base_sha: str, excludes, includes):
        git_dir = self.repo["common_dir"]
        _git_ok(["worktree", "add", "--no-checkout", "-b", branch, str(path), base_sha],
                git_dir=git_dir)
        patterns = _sparse_patterns(self._board_rel(), excludes, includes)
        _apply_sparse(path, patterns)

    def list(self, *, include_removed=False) -> list[dict]:
        conn = self._conn()
        try:
            sql = "SELECT * FROM workspaces WHERE repo_id = ?"
            if not include_removed:
                sql += " AND status != 'removed'"
            rows = conn.execute(sql + " ORDER BY created_at, id",
                                (self.repo["id"],)).fetchall()
            return [_workspace_record(r) for r in rows]
        finally:
            conn.close()

    def get(self, workspace_id) -> dict:
        conn = self._conn()
        try:
            return self._record(conn, workspace_id)
        finally:
            conn.close()

    def release(self, workspace_id, *, owner) -> dict:
        """Release the lease. A workspace whose branch still holds unlanded work
        is `retained` (kept source); a clean one is `released`."""
        conn = self._conn()
        try:
            with _repo_lock(_trees_dir(self.state_root, self.repo["id"])):
                row = self._row(conn, workspace_id)
                if row["status"] not in ("active", "init_failed"):
                    raise TreeRefusedError(
                        f"workspace {workspace_id} is {row['status']}; only an active "
                        f"lease can be released")
                if row["owner"] != str(owner):
                    raise TreeConflictError(
                        f"workspace {workspace_id} is leased by {row['owner']!r}, "
                        f"not {owner!r}")
                tip = self._branch_tip(row["branch"])
                status = "released" if tip == row["base_sha"] else "retained"
                conn.execute("UPDATE workspaces SET status = ?, owner = '',"
                             " updated_at = ? WHERE id = ?",
                             (status, _now(), row["id"]))
                conn.commit()
                return self._record(conn, workspace_id)
        finally:
            conn.close()

    def sync(self, workspace_id, *, owner) -> dict:
        """Explicitly rebase the workspace branch onto the target's current tip.

        Refuses a competing lease, a released workspace and a dirty tree; a
        conflicting rebase is aborted and reported, never left half-finished.
        There is no implicit rebase anywhere else.
        """
        conn = self._conn()
        try:
            with _repo_lock(_trees_dir(self.state_root, self.repo["id"])):
                row = self._row(conn, workspace_id)
                if row["status"] == "init_failed":
                    raise TreeRefusedError(
                        f"workspace {workspace_id} has a failed init; retry "
                        f"create() first")
                if row["status"] != "active":
                    raise TreeRefusedError(
                        f"workspace {workspace_id} is {row['status']}; sync needs "
                        f"an active lease")
                if row["owner"] != str(owner):
                    raise TreeConflictError(
                        f"workspace {workspace_id} is leased by {row['owner']!r}, "
                        f"not {owner!r}: refusing to sync under a competing lease")
                rc, out, _ = _git(["status", "--porcelain"], cwd=row["path"])
                dirty = [line for line in out.splitlines() if not line.startswith("??")]
                if rc != 0 or dirty:
                    raise TreeRefusedError(
                        f"workspace {workspace_id} has uncommitted work; commit or "
                        f"shelve it before sync: {'; '.join(dirty[:5])}")
                target_sha = _rev_parse(self.repo["common_dir"], self.repo["target"])
                env = _clean_env({"GIT_EDITOR": "true", "GIT_SEQUENCE_EDITOR": "true"})
                proc = subprocess.run(
                    ["git", "rebase", target_sha], cwd=row["path"],
                    capture_output=True, text=True, timeout=_GIT_TIMEOUT, env=env)
                if proc.returncode != 0:
                    subprocess.run(["git", "rebase", "--abort"], cwd=row["path"],
                                   capture_output=True, text=True,
                                   timeout=_GIT_TIMEOUT, env=env)
                    raise TreeConflictError(
                        f"rebase of {row['branch']} onto {self.repo['target']} "
                        f"conflicted and was aborted; the workspace is unchanged: "
                        f"{(proc.stderr or proc.stdout).strip()[-1000:]}")
                conn.execute("UPDATE workspaces SET base_sha = ?, updated_at = ?"
                             " WHERE id = ?", (target_sha, _now(), row["id"]))
                conn.commit()
                return self._record(conn, workspace_id)
        finally:
            conn.close()

    def remove(self, workspace_id, *, receipt=None) -> dict:
        """Remove a workspace whose lease is released and whose work is landed.

        With a receipt: the receipt's repo must be this repo and its submitted
        SHA must equal the branch's current tip (a receipt covers exactly the
        tip it landed; later commits are unlanded work). Without a receipt the
        branch must contain nothing beyond its base. Dirty, untracked *or
        ignored* content refuses the removal with its paths — ignored
        dependency or user files are never deleted by us, and there is no
        broad ``git clean``.
        """
        conn = self._conn()
        try:
            with _repo_lock(_trees_dir(self.state_root, self.repo["id"])):
                row = self._row(conn, workspace_id)
                if row["status"] == "removed":
                    return _workspace_record(row)
                if row["status"] not in ("released", "retained"):
                    raise TreeRefusedError(
                        f"workspace {workspace_id} is {row['status']}; release the "
                        f"lease before cleanup")
                tip = self._branch_tip(row["branch"])
                if receipt is not None:
                    if not isinstance(receipt, dict):
                        raise TreeUsageError(
                            "receipt must be a dict (queue receipt JSON)")
                    if receipt.get("repo_id") != self.repo["id"]:
                        raise TreeRefusedError(
                            f"receipt is for repository {receipt.get('repo_id')!r}, "
                            f"not {self.repo['id']!r}")
                    if receipt.get("submitted_sha") != tip:
                        raise TreeRefusedError(
                            f"receipt submitted_sha {receipt.get('submitted_sha')!r} "
                            f"does not cover the workspace tip {tip!r}; land or "
                            f"revert the extra commits first")
                    self._verify_durable_receipt(receipt)
                elif tip != row["base_sha"]:
                    raise TreeRefusedError(
                        f"workspace {workspace_id} has unlanded work (tip "
                        f"{tip[:12]} is past base {row['base_sha'][:12]}); cleanup "
                        f"needs the queue receipt covering its current tip")
                self._check_no_pending_submissions(workspace_id)
                rc, out, _ = _git(["status", "--porcelain", "--ignored"],
                                  cwd=row["path"])
                leftovers = out.splitlines() if rc == 0 else ["<status failed>"]
                if rc != 0 or leftovers:
                    raise TreeRefusedError(
                        f"workspace {workspace_id} still holds dirty, untracked or "
                        f"ignored files; remove them yourself — cleanup never "
                        f"deletes user data: {'; '.join(leftovers[:8])}")
                _git_ok(["worktree", "remove", str(row["path"])],
                        git_dir=self.repo["common_dir"])
                rc, _, err = _git(["branch", "-d", row["branch"]],
                                  git_dir=self.repo["common_dir"])
                if rc != 0:
                    if receipt is None:
                        raise TreeRefusedError(
                            f"worktree removed but branch {row['branch']} is not "
                            f"merged and there is no receipt to justify deleting "
                            f"it: {err.strip()}")
                    _git_ok(["branch", "-D", row["branch"]],
                            git_dir=self.repo["common_dir"])
                conn.execute("UPDATE workspaces SET status = 'removed', updated_at = ?"
                             " WHERE id = ?", (_now(), row["id"]))
                conn.commit()
                return self._record(conn, workspace_id)
        finally:
            conn.close()

    # -- queue (A2) cross-checks --------------------------------------------

    def _queue_db(self) -> Path:
        return self._root / "integration" / self.repo["id"] / "queue.sqlite3"

    def _open_queue_ro(self) -> sqlite3.Connection:
        """Read-only handle on A2's queue database; any doubt retains the
        workspace rather than guessing."""
        db = self._queue_db()
        try:
            return sqlite3.connect(f"file:{db}?mode=ro", uri=True, timeout=5.0)
        except sqlite3.Error as exc:
            raise TreeRefusedError(
                f"cannot open the queue database {db} read-only ({exc}); "
                f"retaining the workspace on uncertainty")

    def _check_no_pending_submissions(self, workspace_id):
        """Invariant 4: cleanup needs no pending submissions. The queue is a
        separate database owned by A2; a missing database means no queue ever
        saw this repo, but a present-but-unreadable one is uncertainty."""
        if not self._queue_db().exists():
            return
        queue = self._open_queue_ro()
        try:
            tables = {r[0] for r in queue.execute(
                "SELECT name FROM sqlite_master WHERE type = 'table'")}
            if "jobs" not in tables:
                raise TreeRefusedError(
                    "the queue database has no jobs table; retaining the "
                    "workspace on uncertainty")
            cols = {r[1] for r in queue.execute("PRAGMA table_info(jobs)")}
            if not {"workspace_id", "status"} <= cols:
                raise TreeRefusedError(
                    "the queue jobs table lacks workspace_id/status columns; "
                    "retaining the workspace on uncertainty")
            pending = [f"{job}:{status}" for job, status in queue.execute(
                "SELECT id, status FROM jobs WHERE workspace_id = ?",
                (workspace_id,)) if status in _QUEUE_PENDING]
            if pending:
                raise TreeRefusedError(
                    f"workspace {workspace_id} has pending submissions in the "
                    f"queue: {', '.join(pending)}; cancel or land them first")
        finally:
            queue.close()

    def _verify_durable_receipt(self, receipt: dict):
        """The receipt a caller hands over must match a durable queue receipt:
        same job_id, repo and submitted SHA, with a published SHA recorded."""
        job_id = receipt.get("job_id")
        if not job_id:
            raise TreeUsageError("receipt has no job_id")
        if not self._queue_db().exists():
            raise TreeRefusedError(
                f"no queue database holds a durable copy of receipt {job_id}; "
                f"retaining the workspace on uncertainty")
        queue = self._open_queue_ro()
        try:
            tables = {r[0] for r in queue.execute(
                "SELECT name FROM sqlite_master WHERE type = 'table'")}
            if "receipts" not in tables:
                raise TreeRefusedError(
                    "the queue database has no receipts table; retaining the "
                    "workspace on uncertainty")
            cols = {r[1] for r in queue.execute("PRAGMA table_info(receipts)")}
            need = {"job_id", "repo_id", "submitted_sha", "published_sha"}
            if not need <= cols:
                raise TreeRefusedError(
                    "the queue receipts table lacks receipt columns; retaining "
                    "the workspace on uncertainty")
            row = queue.execute(
                "SELECT repo_id, submitted_sha, published_sha FROM receipts"
                " WHERE job_id = ?", (job_id,)).fetchone()
            if row is None:
                raise TreeRefusedError(
                    f"receipt {job_id} is not durable in the queue database; "
                    f"only a persisted, published receipt allows cleanup")
            repo_id, submitted_sha, published_sha = row
            if (repo_id != receipt.get("repo_id")
                    or submitted_sha != receipt.get("submitted_sha")):
                raise TreeRefusedError(
                    f"receipt {job_id} does not match its durable queue copy")
            if not published_sha:
                raise TreeRefusedError(
                    f"receipt {job_id} has no published SHA in the queue "
                    f"database; the submission is not landed")
        finally:
            queue.close()


# ------------------------------------------------------------------- receipt

def parse_receipt(text: str) -> dict:
    """Parse a queue receipt from a JSON string, an @file, or a file path."""
    text = text.strip()
    if text.startswith("@"):
        text = Path(text[1:]).read_text()
    elif os.path.exists(text):
        text = Path(text).read_text()
    try:
        receipt = json.loads(text)
    except json.JSONDecodeError as exc:
        raise TreeUsageError(f"receipt is not JSON: {exc}")
    if not isinstance(receipt, dict):
        raise TreeUsageError("receipt must be a JSON object")
    return receipt
