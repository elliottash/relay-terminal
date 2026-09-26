# SPDX-License-Identifier: AGPL-3.0-or-later
"""Atomic runnable-main releases (docs/TREES-AND-LANDING.md §A3, invariants 6-7).

A *tip channel* keeps an installed, runnable copy of the project's target branch:

    state_root/integration/<repo-id>/tip/run/<sha>/    installed release, immutable once complete
    state_root/integration/<repo-id>/tip/run/current   atomic symlink to a complete release
    cache_root/integration/<repo-id>/tip/source        persistent disposable source worktree
    cache_root/integration/<repo-id>/tip/build         coalesced build directory

``update(sha, config)`` coalesces: it records the requested SHA, takes the channel lock, and
builds whatever the newest request is by the time it holds the lock — a burst of submissions
produces one build, not N.  Publication is all-or-nothing: build and install run into a staging
directory under ``run/``, the release is checked for completeness and smoke-gated, only then
renamed into place and made ``current`` with an atomic symlink swap.  A crash anywhere before the
swap leaves the previous release running; a directory without ``release.json`` is incomplete and
is pruned, never served.

The installed release must stand alone after the disposable source/build is reused (contract):
the completeness check refuses symlinks that escape the install root, and the project config's
``main.install`` commands are responsible for copying *all* runtime assets — for this repository
``cmake --install {build} --prefix {dest}`` (AppPaths prefers ``<exe>/../share/relay`` over the
compiled-in ``RELAY_SOURCE_DIR``).  The source worktree under ``cache_root`` is *persistent*
across builds (coalesced, reusable, disposable), so a binary that still reaches back to its
configure-time source path finds a stable location rather than a deleted temp dir; projects must
still not depend on that — reuse can wipe it.

`repo_id`: B1 passes the canonical A1 registry id.  Without it, the A1 registry is consulted when
importable; otherwise a derived id is used **only** when the caller passes
``allow_derived_repo_id=True`` — production must never silently land releases under a hash-based
identity that the registry does not know.

Linux/POSIX first (contract): other hosts get a clear refusal at construction.

**Live release leases.**  Pruning must never delete a release a program is still running from —
the running binary may import backend modules or read assets from its release directory long
after start.  ``pin()`` selects a complete release and takes a lease on it: a file under
``run/.leases/`` created under a temporary name, ``flock``-ed, and only then renamed into place,
so every visible lease file was locked from birth.  Liveness is the kernel lock, never a PID: the
lock belongs to the open file description, so it survives ``execve`` (the fd is made inheritable)
and is shared by descendants that inherit the fd; it disappears when the last holder exits, and
PID reuse cannot keep a dead lease alive or kill a live one.  Selection and pruning are atomic
with respect to each other: ``pin`` resolves ``current`` and publishes its lease while holding
``run/.prune.lock``, and pruning computes the pinned set under the same lock.  Pruning keeps
``max(keep, 2)`` completed releases plus ``current`` plus every live-pinned release; a dead
lease is detected by acquiring its lock and removed; ``reclaim()`` reclaims a release as soon as
its last runner exits.  ``exec_release`` (``relay-land main-run``) execs the pinned executable
with the lease fd inherited; ``spawn`` starts it as a child that holds the lease.  The channel
build lock is never held for a program's lifetime.  A binary started directly from
``run/<sha>/…`` without ``pin`` is unmanaged and not protected once it falls out of ``keep``.

Tradeoffs, stated once: build/install/smoke commands run sequentially with per-command timeouts
(simple, matches the gate); the smoke gate defaults to "executable exists, has the exec bit, and
no symlink escapes the root" unless the project configures `main.smoke` commands — an unconfigured
smoke gate proves install completeness, not runtime health; actual build time and installed bytes
are recorded (no promised cache hits); pruning keeps ``max(keep, 2)`` completed releases plus the
current one, so disk use is bounded but a rollback target survives.
"""
from __future__ import annotations

import contextlib
import fcntl
import json
import os
import secrets
import shutil
import signal
import subprocess
import time
from pathlib import Path
from typing import Mapping, Sequence

from . import projectconf

GIT_TIMEOUT = 120.0
SMOKE_TIMEOUT = 120.0
CMD_OUTPUT_TAIL_BYTES = 256 * 1024  # bounded capture: only the tail of a command's output


class MainReleaseError(RuntimeError):
    """A release that could not be built, completed, verified or published.  The previous
    release (if any) is still current whenever this is raised."""


def _default_root(env_primary: str, env_fallback: str, leaf: str) -> Path:
    base = os.environ.get(env_primary) or os.environ.get(env_fallback) \
        or str(Path.home() / leaf)
    return Path(base) / "relay"


def _state_root() -> Path:
    return _default_root("RELAY_STATE_HOME", "XDG_STATE_HOME", ".local/state")


def _cache_root() -> Path:
    return _default_root("RELAY_CACHE_HOME", "XDG_CACHE_HOME", ".cache")


def _atomic_write(path: Path, text: str) -> None:
    import uuid
    tmp = path.with_name(f".{path.name}.{os.getpid()}.{uuid.uuid4().hex[:12]}.tmp")
    fd = os.open(str(tmp), os.O_WRONLY | os.O_CREAT | os.O_TRUNC, 0o644)
    try:
        with os.fdopen(fd, "w", encoding="utf-8") as fh:
            fh.write(text)
            fh.flush()
            os.fsync(fh.fileno())
        os.replace(tmp, path)
        dirfd = os.open(str(path.parent), os.O_DIRECTORY)
        try:
            os.fsync(dirfd)
        finally:
            os.close(dirfd)
    finally:
        try:
            os.unlink(tmp)
        except FileNotFoundError:
            pass


def _fsync_dir(path: Path) -> None:
    fd = os.open(str(path), os.O_DIRECTORY)
    try:
        os.fsync(fd)
    finally:
        os.close(fd)


def _tree_bytes(path: Path) -> int:
    total = 0
    for dirpath, dirnames, filenames in os.walk(path):
        for name in filenames:
            p = os.path.join(dirpath, name)
            if not os.path.islink(p):
                try:
                    total += os.lstat(p).st_size
                except OSError:
                    pass
    return total


def _substitute(argv: Sequence[str], mapping: Mapping[str, str]) -> list[str]:
    """Contract substitution: a `{placeholder}` is replaced as a whole argv entry or as a
    literal substring; never shell-quoted guesswork."""
    out = []
    for arg in argv:
        for name, value in mapping.items():
            arg = arg.replace("{" + name + "}", value)
        out.append(arg)
    return out


def _scrubbed_env() -> dict[str, str]:
    return {k: v for k, v in os.environ.items() if not k.startswith("GIT_")}


class ReleaseLease:
    """A live pin on one complete release (see the module docstring, "Live release leases").

    `fd` holds an exclusive ``flock`` on `path`; while it (or an inherited copy) is open the
    release is never pruned.  `close()` drops this process's copy and removes the lease file
    only when no other holder — e.g. a spawned child — still has it locked."""

    def __init__(self, path: Path, fd: int, sha: str, release_dir: Path, executable: Path):
        self.path, self.fd, self.sha = path, fd, sha
        self.release_dir, self.executable = release_dir, executable

    def env(self) -> dict[str, str]:
        """Variables a launched program inherits, so it (or a supervisor) can see its pin."""
        return {"RELAY_MAIN_LEASE": str(self.path), "RELAY_MAIN_LEASE_FD": str(self.fd),
                "RELAY_MAIN_SHA": self.sha, "RELAY_MAIN_RELEASE": str(self.release_dir)}

    def close(self) -> None:
        if self.fd < 0:
            return
        os.close(self.fd)
        self.fd = -1
        _reap_lease_file(self.path)

    def __enter__(self) -> "ReleaseLease":
        return self

    def __exit__(self, *exc) -> None:
        self.close()


def _reap_lease_file(path: Path) -> bool:
    """Remove `path` if nobody holds its lock; True when the lease is (now) dead."""
    try:
        fd = os.open(str(path), os.O_RDWR)
    except FileNotFoundError:
        return True
    try:
        try:
            fcntl.flock(fd, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError:
            return False  # some live process (or its descendant) still holds it
        try:
            os.unlink(path)
        except FileNotFoundError:
            pass
        return True
    finally:
        os.close(fd)


class MainRelease:
    def __init__(self, repo, *, state_root=None, cache_root=None, repo_id: str | None = None,
                 allow_derived_repo_id: bool = False, git_timeout: float = GIT_TIMEOUT):
        if os.name != "posix":
            raise MainReleaseError("main releases are POSIX/Linux first; this host gets a clear "
                                   "refusal instead of a non-atomic fallback")
        self.repo = Path(repo)
        self.state_root = Path(state_root) if state_root else _state_root()
        self.cache_root = Path(cache_root) if cache_root else _cache_root()
        self.git_timeout = float(git_timeout)
        self.common_dir = self._resolve_common_dir()
        self.repo_id = repo_id or self._registry_repo_id(allow_derived_repo_id)
        base = self.state_root / "integration" / self.repo_id / "tip"
        self.tip_dir = base
        self.run_dir = base / "run"
        cache = self.cache_root / "integration" / self.repo_id / "tip"
        self.source_dir = cache / "source"
        self.build_dir = cache / "build"

    # ------------------------------------------------------------------ identity / git
    def _git(self, *args, cwd=None, check=True) -> subprocess.CompletedProcess:
        proc = subprocess.run(
            ["git", "-C", str(cwd or self.repo), *args],
            stdout=subprocess.PIPE, stderr=subprocess.PIPE,
            timeout=self.git_timeout, env=_scrubbed_env())
        if check and proc.returncode != 0:
            raise MainReleaseError(f"git {' '.join(args)} failed: "
                                   f"{proc.stderr.decode('utf-8', errors='replace').strip()}")
        return proc

    def _resolve_common_dir(self) -> Path:
        proc = subprocess.run(
            ["git", "-C", str(self.repo), "rev-parse", "--path-format=absolute",
             "--git-common-dir"],
            stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=self.git_timeout,
            env=_scrubbed_env())
        if proc.returncode != 0:
            raise MainReleaseError(f"{self.repo}: not a git repository: "
                                   f"{proc.stderr.decode('utf-8', errors='replace').strip()}")
        return Path(proc.stdout.decode().strip()).resolve()

    def _registry_repo_id(self, allow_derived: bool) -> str:
        """Canonical identity comes from A1's registry (keyed by the resolved git common dir).
        A hash-derived id is a test/standalone escape hatch that must be opted into loudly."""
        try:
            from . import trees  # A1; may not exist yet
        except ImportError:
            trees = None
        if trees is not None:
            try:
                rec = trees.resolve_project(self.repo, state_root=self.state_root)
                if rec and rec.get("id"):
                    return rec["id"]
            except Exception:
                pass  # fall through to the explicit paths below
        if not allow_derived:
            raise MainReleaseError(
                "no repo_id given and the A1 registry has no canonical id for "
                f"{self.repo}; pass repo_id= from relay_core.trees.register_repo() "
                "(derived hashes are for tests only: allow_derived_repo_id=True)")
        import hashlib
        return "derived-" + hashlib.sha256(str(self.common_dir).encode()).hexdigest()[:16]

    # ------------------------------------------------------------------ commands
    def _run(self, argv: Sequence[str], *, cwd: Path, timeout: float, what: str) -> str:
        """Run one build/install/smoke command.  Output spills to a file — a noisy build must
        not OOM the service; only a bounded tail is kept for errors/smoke records."""
        import tempfile
        with tempfile.TemporaryFile(prefix="relay-main-") as sink:
            proc = subprocess.Popen(list(argv), cwd=str(cwd), env=_scrubbed_env(),
                                    stdout=sink, stderr=subprocess.STDOUT,
                                    start_new_session=True)
            try:
                proc.wait(timeout=timeout)
            except subprocess.TimeoutExpired:
                try:
                    os.killpg(proc.pid, signal.SIGKILL)
                except (ProcessLookupError, PermissionError):
                    pass
                proc.wait()
                raise MainReleaseError(f"{what} timed out after {timeout}s: {' '.join(argv)}")
            size = sink.seek(0, os.SEEK_END)
            sink.seek(max(0, size - CMD_OUTPUT_TAIL_BYTES))
            text = sink.read().decode("utf-8", errors="replace")
        if proc.returncode != 0:
            raise MainReleaseError(f"{what} exited {proc.returncode}: {' '.join(argv)}\n"
                                   f"{text[-4000:]}")
        return text

    # ------------------------------------------------------------------ update
    def update(self, sha: str, config: Mapping) -> dict:
        """Build and publish `sha` as the tip release; coalesces with concurrent requests.

        Returns a record with `sha`, `path`, `changed`, `duration_seconds`, `bytes` and the
        channel `status`.  Raises MainReleaseError on any failure; `current` is untouched then.
        """
        cfg = projectconf.normalize_config(config, origin="<main_release config>")
        main = cfg["main"]
        if not main["executable"]:
            raise MainReleaseError("main.executable is not configured; the project has no "
                                   "runnable-main release contract")
        if not main["install"]:
            raise MainReleaseError("main.install is empty; nothing would populate {dest}, so "
                                   "no complete release can be promised")
        full = self._git("rev-parse", "--verify", f"{sha}^{{commit}}").stdout.decode().strip()

        self.run_dir.mkdir(parents=True, exist_ok=True)
        self.build_dir.mkdir(parents=True, exist_ok=True)
        self.source_dir.parent.mkdir(parents=True, exist_ok=True)
        _atomic_write(self.tip_dir / "requested.json",
                      json.dumps({"sha": full, "requested_at": time.time()}))

        lock_path = self.tip_dir / "main.lock"
        lock_fd = os.open(str(lock_path), os.O_CREAT | os.O_RDWR, 0o600)
        try:
            fcntl.flock(lock_fd, fcntl.LOCK_EX)  # one publisher per channel; later writers coalesce
            try:
                requested = self._requested_sha() or full
            except MainReleaseError:
                requested = full
            return self._publish(requested, cfg)
        finally:
            os.close(lock_fd)

    def _requested_sha(self) -> str | None:
        try:
            data = json.loads((self.tip_dir / "requested.json").read_text())
            sha = data.get("sha")
            if sha:
                return self._git("rev-parse", "--verify", f"{sha}^{{commit}}"
                                 ).stdout.decode().strip()
        except (OSError, ValueError, KeyError, subprocess.SubprocessError, MainReleaseError):
            pass
        return None

    def _current_sha(self) -> str | None:
        link = self.run_dir / "current"
        try:
            target = os.readlink(link)
        except OSError:
            return None
        sha = os.path.basename(target.rstrip("/"))
        if self._release_record(sha) is None:
            return None  # points at an incomplete release: treat as no current
        return sha

    def _release_record(self, sha: str) -> dict | None:
        try:
            rec = json.loads((self.run_dir / sha / "release.json").read_text())
        except (OSError, ValueError):
            return None
        if rec.get("sha") != sha:
            return None
        return rec

    def _sync_source(self, sha: str) -> None:
        """Point the persistent cache worktree at `sha`.  The worktree is disposable and ours;
        --force discards leftover tracked-file edits from interrupted builds (ignored build
        outputs survive, which is what makes rebuilds incremental)."""
        if not (self.source_dir / ".git").exists():
            if self.source_dir.exists():
                shutil.rmtree(self.source_dir)
            self._git("worktree", "add", "--detach", str(self.source_dir), sha)
        else:
            self._git("checkout", "--detach", "--force", sha, cwd=self.source_dir)

    def _check_complete(self, dest: Path, executable: str) -> None:
        exe = dest / executable
        if not exe.is_file():
            raise MainReleaseError(f"installed release is missing its executable {executable}")
        if not os.access(str(exe), os.X_OK):
            raise MainReleaseError(f"installed executable {executable} is not executable")
        root = os.path.realpath(str(dest))
        for dirpath, dirnames, filenames in os.walk(dest):
            for name in list(dirnames) + filenames:
                p = os.path.join(dirpath, name)
                if os.path.islink(p):
                    raw = os.readlink(p)
                    if os.path.isabs(raw):
                        # even a link into {dest} itself breaks once the staged tree is
                        # renamed into run/<sha>: installs must use relative links
                        raise MainReleaseError(
                            f"installed release uses an absolute symlink {p} -> {raw}; "
                            "symlinks inside a release must be relative so they survive "
                            "the staging rename")
                    target = os.path.realpath(p)
                    if not (target == root or target.startswith(root + os.sep)):
                        raise MainReleaseError(
                            f"installed release is not self-contained: {p} links outside "
                            f"{dest} (-> {raw}); install commands must copy every runtime "
                            "asset into {dest}")

    def _publish(self, sha: str, cfg: Mapping) -> dict:
        main = cfg["main"]
        current = self._current_sha()
        if current == sha:
            status = self.status()
            return {"sha": sha, "path": str(self.run_dir / sha), "changed": False,
                    "duration_seconds": 0.0,
                    "bytes": (status.get("current") or {}).get("bytes"),
                    "status": status}

        existing = self._release_record(sha)
        if existing is not None:
            # A complete retained release (e.g. a rollback target): immutable, so reuse it —
            # flip `current`, never rebuild over it.
            self._flip_current(sha)
            _atomic_write(self.tip_dir / "status.json", json.dumps({
                "current": sha, "requested_sha": sha, "last_error": None,
                "last_build": {"duration_seconds": 0.0, "bytes": existing.get("bytes"),
                               "reused": True},
                "keep": max(int(main["keep"]), 2),
                "updated_at": time.time()}, indent=2))
            return {"sha": sha, "path": str(self.run_dir / sha), "changed": True,
                    "reused": True, "duration_seconds": 0.0,
                    "bytes": existing.get("bytes"), "status": self.status()}

        started = time.monotonic()
        staging = self.run_dir / f".stage-{sha[:12]}-{os.getpid()}"
        self._prune_staging()
        try:
            self._sync_source(sha)
            mapping = {"source": str(self.source_dir), "build": str(self.build_dir),
                       "dest": str(staging)}
            for argv in main["build"]:
                self._run(_substitute(argv, mapping), cwd=self.source_dir,
                          timeout=main["timeout_seconds"], what="main.build")
            staging.mkdir(parents=True, exist_ok=True)
            for argv in main["install"]:
                self._run(_substitute(argv, mapping), cwd=self.source_dir,
                          timeout=main["timeout_seconds"], what="main.install")

            self._check_complete(staging, main["executable"])
            smoke_mapping = {"dest": str(staging),
                             "executable": str(staging / main["executable"])}
            smoke_results = []
            for argv in main["smoke"]:
                out = self._run(_substitute(argv, smoke_mapping), cwd=staging,
                                timeout=min(SMOKE_TIMEOUT, main["timeout_seconds"]),
                                what="main.smoke")
                smoke_results.append({"argv": _substitute(argv, smoke_mapping), "ok": True,
                                      "output_tail": out[-2000:]})

            duration = time.monotonic() - started
            nbytes = _tree_bytes(staging)
            record = {"sha": sha, "installed_at": time.time(),
                      "duration_seconds": round(duration, 3), "bytes": nbytes,
                      "policy_hash": projectconf.policy_hash(cfg),
                      "executable": main["executable"], "smoke": smoke_results}
            _atomic_write(staging / "release.json", json.dumps(record, indent=2))

            final = self.run_dir / sha
            if final.exists():
                shutil.rmtree(final)  # an incomplete leftover with this name
            os.rename(staging, final)
            _fsync_dir(self.run_dir)
            self._flip_current(sha)
            self._prune_releases(keep=max(int(main["keep"]), 2))
            _atomic_write(self.tip_dir / "status.json", json.dumps({
                "current": sha, "requested_sha": sha, "last_error": None,
                "last_build": {"duration_seconds": record["duration_seconds"],
                               "bytes": nbytes},
                "keep": max(int(main["keep"]), 2),
                "updated_at": time.time()}, indent=2))
            return {"sha": sha, "path": str(final), "changed": True,
                    "duration_seconds": record["duration_seconds"], "bytes": nbytes,
                    "status": self.status()}
        except Exception as exc:
            shutil.rmtree(staging, ignore_errors=True)
            self._record_error(exc)
            if isinstance(exc, MainReleaseError):
                raise
            raise MainReleaseError(str(exc)) from exc

    def _flip_current(self, sha: str) -> None:
        """Atomic: a new symlink renamed over `current`.  Readers see the old release or the
        new one, never a missing or partial one."""
        tmp = self.run_dir / f".current.{os.getpid()}.tmp"
        try:
            os.unlink(tmp)
        except FileNotFoundError:
            pass
        os.symlink(sha, tmp)
        os.replace(tmp, self.run_dir / "current")
        _fsync_dir(self.run_dir)

    def _record_error(self, exc: BaseException) -> None:
        try:
            prev = {}
            status_path = self.tip_dir / "status.json"
            if status_path.exists():
                prev = json.loads(status_path.read_text())
            prev.update({"last_error": f"{type(exc).__name__}: {exc}",
                         "updated_at": time.time()})
            _atomic_write(status_path, json.dumps(prev, indent=2))
        except Exception:
            pass  # error recording must never mask the real failure

    def _prune_staging(self) -> None:
        for entry in self.run_dir.iterdir():
            if entry.name.startswith(".stage-") and entry.is_dir() and not entry.is_symlink():
                shutil.rmtree(entry, ignore_errors=True)

    # ------------------------------------------------------------------ live leases
    @property
    def leases_dir(self) -> Path:
        return self.run_dir / ".leases"

    @contextlib.contextmanager
    def _prune_lock(self):
        """Serialises release selection for a lease against pruning (short critical sections
        only — never held while a program runs or a build is in progress)."""
        self.run_dir.mkdir(parents=True, exist_ok=True)
        fd = os.open(str(self.run_dir / ".prune.lock"), os.O_CREAT | os.O_RDWR, 0o600)
        try:
            fcntl.flock(fd, fcntl.LOCK_EX)
            yield
        finally:
            os.close(fd)

    def pin(self, sha: str | None = None) -> ReleaseLease:
        """Lease a complete release (default: `current`) so pruning keeps it while any holder
        of the returned lease's fd lives.  Raises MainReleaseError when there is none."""
        with self._prune_lock():
            chosen = sha or self._current_sha()
            if not chosen:
                raise MainReleaseError("no runnable main is installed yet; nothing to run")
            rec = self._release_record(chosen)
            if rec is None:
                raise MainReleaseError(f"release {chosen} is not a complete installed release")
            release_dir = self.run_dir / chosen
            exe_rel = str(rec.get("executable") or "")
            exe = release_dir / exe_rel
            root = os.path.realpath(release_dir)
            if (not exe_rel or os.path.isabs(exe_rel)
                    or not os.path.realpath(exe).startswith(root + os.sep)):
                raise MainReleaseError(f"release {chosen} records no executable inside it")
            self.leases_dir.mkdir(parents=True, exist_ok=True)
            name = f"{chosen}.{os.getpid()}.{secrets.token_hex(6)}"
            tmp = self.leases_dir / f".{name}.tmp"
            final = self.leases_dir / f"{name}.lease"
            fd = os.open(str(tmp), os.O_CREAT | os.O_EXCL | os.O_RDWR, 0o600)
            try:
                fcntl.flock(fd, fcntl.LOCK_EX | fcntl.LOCK_NB)  # fresh inode: cannot block
                os.write(fd, json.dumps({"sha": chosen, "pid": os.getpid(),
                                         "created_at": time.time()}).encode())
                os.rename(tmp, final)  # visible only once locked, so scanners never misread it
            except BaseException:
                os.close(fd)
                with contextlib.suppress(FileNotFoundError):
                    os.unlink(tmp)
                raise
            return ReleaseLease(final, fd, chosen, release_dir, exe)

    def leases(self, *, reap: bool = False) -> list[dict]:
        """Live leases.  With `reap`, dead lease files are removed (done under the prune lock by
        pruning; safe anywhere because a lease file is locked before it becomes visible)."""
        out = []
        if not self.leases_dir.is_dir():
            return out
        for entry in sorted(self.leases_dir.glob("*.lease")):
            try:
                fd = os.open(str(entry), os.O_RDONLY)
            except FileNotFoundError:
                continue
            try:
                try:
                    fcntl.flock(fd, fcntl.LOCK_EX | fcntl.LOCK_NB)
                except BlockingIOError:
                    try:
                        meta = json.loads(os.read(fd, 4096).decode() or "{}")
                    except ValueError:
                        meta = {}
                    out.append({"sha": meta.get("sha") or entry.name.split(".", 1)[0],
                                "pid": meta.get("pid"), "created_at": meta.get("created_at"),
                                "path": str(entry)})
                    continue
                if reap:
                    with contextlib.suppress(FileNotFoundError):
                        os.unlink(entry)
            finally:
                os.close(fd)
        if reap:
            for stale in self.leases_dir.glob(".*.tmp"):
                _reap_lease_file(stale)  # a pin interrupted between create and rename
        return out

    def exec_release(self, args: Sequence[str] = (), *, env: Mapping[str, str] | None = None,
                     sha: str | None = None):
        """Replace this process with the pinned release's executable (`relay-land main-run`).
        The lease fd is inherited across exec, so the release stays pinned for exactly the
        program's lifetime.  Raises MainReleaseError (lease released) if exec fails."""
        lease = self.pin(sha)
        try:
            os.set_inheritable(lease.fd, True)
            full_env = dict(os.environ if env is None else env)
            full_env.update(lease.env())
            os.execve(str(lease.executable), [str(lease.executable), *args], full_env)
        except OSError as exc:
            lease.close()
            raise MainReleaseError(f"cannot run the installed main {lease.executable}: {exc}") \
                from exc

    def spawn(self, args: Sequence[str] = (), *, sha: str | None = None,
              env: Mapping[str, str] | None = None, **popen_kw) -> subprocess.Popen:
        """Start the pinned release's executable as a child that holds the lease (`pass_fds`).
        The parent's copy is closed once the child is running; the pin lasts until the child and
        any descendant that inherited the fd exit.  `proc.release_lease` describes it."""
        lease = self.pin(sha)
        try:
            full_env = dict(os.environ if env is None else env)
            full_env.update(lease.env())
            pass_fds = tuple(popen_kw.pop("pass_fds", ())) + (lease.fd,)
            proc = subprocess.Popen([str(lease.executable), *args], env=full_env,
                                    pass_fds=pass_fds, **popen_kw)
        except (OSError, subprocess.SubprocessError) as exc:
            lease.close()
            raise MainReleaseError(f"cannot run the installed main {lease.executable}: {exc}") \
                from exc
        proc.release_lease = {"sha": lease.sha, "path": str(lease.path),
                              "executable": str(lease.executable)}
        lease.close()  # the child's inherited copy keeps the lock; the file stays
        return proc

    def reclaim(self, *, keep: int | None = None) -> dict:
        """Prune releases that are neither kept, current nor live-pinned — e.g. once the last
        program running an old release exits.  Skips (never waits) while an update holds the
        channel lock; that update prunes when it finishes."""
        if keep is None:
            try:
                keep = int(json.loads((self.tip_dir / "status.json").read_text()).get("keep", 2))
            except (OSError, ValueError, TypeError):
                keep = 2
        if not self.run_dir.is_dir():
            return {"reclaimed": [], "skipped": None}
        lock_fd = os.open(str(self.tip_dir / "main.lock"), os.O_CREAT | os.O_RDWR, 0o600)
        try:
            try:
                fcntl.flock(lock_fd, fcntl.LOCK_EX | fcntl.LOCK_NB)
            except BlockingIOError:
                return {"reclaimed": [], "skipped": "an update is in progress and prunes itself"}
            return {"reclaimed": self._prune_releases(keep=max(int(keep), 2)), "skipped": None}
        finally:
            os.close(lock_fd)

    def _prune_releases(self, *, keep: int) -> list[str]:
        with self._prune_lock():
            return self._prune_releases_locked(keep=keep)

    def _prune_releases_locked(self, *, keep: int) -> list[str]:
        current = self._current_sha()
        pinned = {lease["sha"] for lease in self.leases(reap=True)}
        removed: list[str] = []
        releases = []
        for entry in self.run_dir.iterdir():
            # `current` is a symlink; is_dir() follows it, so exclude symlinks explicitly or
            # the pointed-at release would be counted twice toward `keep`.
            if entry.is_symlink() or not entry.is_dir() or entry.name.startswith("."):
                continue
            rec = self._release_record(entry.name)
            if rec is None:
                if entry.name != current and entry.name not in pinned:
                    shutil.rmtree(entry, ignore_errors=True)  # incomplete: never served
                continue
            releases.append((rec.get("installed_at", 0.0), entry.name))
        releases.sort(reverse=True)
        kept = 0
        for _, name in releases:
            if name == current or kept < keep:
                kept += 1
                continue
            if name in pinned:
                continue  # a live program runs from it; kept beyond `keep` until it exits
            shutil.rmtree(self.run_dir / name, ignore_errors=True)
            removed.append(name)
        return removed

    # ------------------------------------------------------------------ status
    def status(self) -> dict:
        """Channel view: current release, newest requested sha, lag in commits, last error.

        `lag_commits` is computed live (`git rev-list --count current..requested`) so a busy
        channel shows how far the runnable main trails what was asked for (invariant 7)."""
        current = self._current_sha()
        requested = self._requested_sha()
        lag = None
        if current and requested and current != requested:
            proc = self._git("rev-list", "--count", f"{current}..{requested}", check=False)
            if proc.returncode == 0:
                try:
                    lag = int(proc.stdout.decode().strip())
                except ValueError:
                    lag = None
            # not an ancestor relationship we can count (e.g. history rewrite): lag unknown
        last_error, last_build = None, None
        try:
            saved = json.loads((self.tip_dir / "status.json").read_text())
            last_error = saved.get("last_error")
            last_build = saved.get("last_build")
        except (OSError, ValueError):
            pass
        live = self.leases()
        pinned = {lease["sha"] for lease in live}
        releases = []
        if self.run_dir.is_dir():
            for entry in sorted(self.run_dir.iterdir()):
                if entry.is_symlink() or not entry.is_dir() or entry.name.startswith("."):
                    continue
                rec = self._release_record(entry.name)
                releases.append({"sha": entry.name, "path": str(entry),
                                 "complete": rec is not None, "current": entry.name == current,
                                 "pinned": entry.name in pinned,
                                 "bytes": (rec or {}).get("bytes"),
                                 "installed_at": (rec or {}).get("installed_at")})
        current_rec = self._release_record(current) if current else None
        return {
            "channel": "tip",
            "repo_id": self.repo_id,
            "current": ({"sha": current, "path": str(self.run_dir / current),
                         "installed_at": (current_rec or {}).get("installed_at"),
                         "bytes": (current_rec or {}).get("bytes")} if current else None),
            "requested_sha": requested,
            "lag_commits": lag,
            "error": last_error,
            "last_build": last_build,
            "releases": releases,
            "leases": live,
        }
