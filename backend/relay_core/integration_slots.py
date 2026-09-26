# SPDX-License-Identifier: AGPL-3.0-or-later
"""Bounded host build admission for the integration service (docs/TREES-AND-LANDING.md §A3).

One SQLite ledger at ``state_root/integration/resources.sqlite3`` (WAL, schema-versioned) records
who holds how much of the host.  Verifier processes acquire a slot before building/testing a
candidate; the queue sizes its concurrency from what admission grants.

Guarantees:

* **Reservations die with their holder.**  Every reservation owns an ``flock`` on
  ``locks/<token>.lock``.  The kernel releases it on process death (kill -9, OOM, crash — no
  heartbeat needed, no pid-reuse hole), and any peer reaps the row the next time it looks.
* **Effective capacity is cgroup-aware.**  This process's cgroup v2 ancestry is resolved via
  ``/proc/self/cgroup`` and the *tightest* ancestor ``memory.max`` / ``cpu.max`` quota wins (the
  service lives in a nested scope/slice, so reading only the cgroup root would admit over the
  real cap); v1 fallbacks and host totals cap it further; disk comes from the filesystem holding
  the state root.  Explicit ``limits`` passed at construction *replace* detection (tests,
  embedding) — production passes none.
* **No double subtraction.**  Live memory headroom under the cgroup is computed as
  ``memory.max - max(memory.current, Σ active reservations)``: ``memory.current`` already
  includes what admitted jobs actually use, and the reservation sum estimates what they were
  granted; taking the larger accounts for both admitted jobs and non-ledger consumers without
  charging admitted jobs twice.  On top of that, ``MemAvailable`` must cover the request plus a
  reserve, so a host that is about to swap refuses new builds.
* **Zero or impossible capacity is explicit.**  A request exceeding effective capacity — including
  a zero-capacity dimension — raises :class:`AdmissionRefused` immediately, even with a timeout
  set; it can never fit, so waiting would be a lie.  Contended capacity with ``timeout=0`` raises
  ``AdmissionRefused``; with ``timeout>0`` it waits and then raises :class:`AdmissionTimeout``
  (``exit_code = 7``, matching land.py's "slots busy past --wait-seconds").
* **Landings are prioritized without starving try.**  Waiters register in the ledger; a `land`
  waiter outranks a `try` waiter, except a `try` older than ``starve_seconds`` is promoted, so a
  landing storm cannot starve try runs forever.  Same class: oldest first.

Linux first (contract): non-POSIX hosts get a clear refusal at construction; hosts without
``/proc``/cgroup data may serve CPU/disk but refuse memory requests unless explicit limits name a
memory capacity.

A reservation is **accounting, not an OS cap**: it does not constrain the holder's processes.
Callers must translate their grant into real limits themselves — e.g. B1 passes
``RELAY_JOBS=<granted cpus>`` to scripts/relay-build, which clamps compile parallelism to the
cgroup memory budget.  Admission only guarantees the ledger never grants more than the host
effectively has.

Tradeoffs, stated once: CPU admission is ledger-only (no load-average check — build jobs are
already cgroup-clamped and a load check would flap); disk capacity is a fixed 90% of the
filesystem plus a live free-space reserve rather than a per-repo quota (simple, and the releases'
own keep-pruning is the real consumer); polling at ~1s instead of condition variables across
processes (SQLite has none — the cost is up to one poll interval of latency per grant).
"""
from __future__ import annotations

import fcntl
import os
import sqlite3
import time
import uuid
from dataclasses import dataclass
from pathlib import Path
from typing import Mapping, Sequence

SCHEMA_VERSION = 1

DEFAULT_RESERVE_MEMORY_BYTES = 512 << 20   # keep this much MemAvailable untouched
DEFAULT_RESERVE_DISK_BYTES = 1 << 30       # and this much free disk
DEFAULT_DISK_UTILIZATION = 0.9             # never let the ledger fill more than this fraction
DEFAULT_POLL_INTERVAL = 1.0
DEFAULT_STARVE_SECONDS = 600.0             # a try waiter this old outranks fresh land waiters

PRIORITY_LAND = "land"
PRIORITY_TRY = "try"
_PRIORITIES = (PRIORITY_LAND, PRIORITY_TRY)

_CGROUP_V1_HUGE = 1 << 62  # v1 "no limit" is reported as a giant value; treat as unlimited


def _default_state_root() -> Path:
    """$XDG_STATE_HOME/relay (contract placement), default ~/.local/state/relay."""
    base = os.environ.get("RELAY_STATE_HOME") or os.environ.get("XDG_STATE_HOME") \
        or str(Path.home() / ".local" / "state")
    return Path(base) / "relay"


class AdmissionError(RuntimeError):
    """Base for admission failures.  `exit_code` lets a CLI map them to process exits."""
    exit_code = 1


class AdmissionRefused(AdmissionError):
    """Immediate refusal: over capacity now with timeout=0, or a request that can *never* fit
    (including zero effective capacity for a requested dimension)."""
    exit_code = 2


class AdmissionTimeout(AdmissionError):
    """Capacity stayed contended past the caller's timeout (land.py exit 7)."""
    exit_code = 7


@dataclass(frozen=True)
class Capacity:
    cpus: float
    memory_bytes: int
    disk_bytes: int


class _Reservation:
    """Context manager returned by acquire(); `release()` (or process death) frees the slot."""

    def __init__(self, adm: "HostAdmission", token: str, fd: int, repo_id: str, job_id: str,
                 memory_bytes: int, disk_bytes: int, cpus: float, priority: str):
        self._adm, self.token, self._fd = adm, token, fd
        self.repo_id, self.job_id = repo_id, job_id
        self.memory_bytes, self.disk_bytes, self.cpus = memory_bytes, disk_bytes, cpus
        self.priority = priority
        self._released = False

    def release(self) -> None:
        if self._released:
            return
        self._released = True
        try:
            self._adm._delete_row("reservations", self.token)
        finally:
            self._adm._drop_lock(self.token, self._fd)

    def __enter__(self) -> "_Reservation":
        return self

    def __exit__(self, *exc) -> None:
        self.release()

    def __repr__(self) -> str:  # pragma: no cover - debugging aid
        return (f"<reservation {self.token[:8]} {self.repo_id}/{self.job_id} "
                f"cpus={self.cpus} mem={self.memory_bytes} disk={self.disk_bytes}>")


class HostAdmission:
    def __init__(self, *, state_root=None, limits: Mapping[str, float] | None = None,
                 reserve_memory_bytes: int = DEFAULT_RESERVE_MEMORY_BYTES,
                 reserve_disk_bytes: int = DEFAULT_RESERVE_DISK_BYTES,
                 disk_utilization: float = DEFAULT_DISK_UTILIZATION,
                 poll_interval: float = DEFAULT_POLL_INTERVAL,
                 starve_seconds: float = DEFAULT_STARVE_SECONDS,
                 cgroup_root="/sys/fs/cgroup", proc_root="/proc", disk_path=None):
        if os.name != "posix":
            raise AdmissionError("host admission is POSIX/Linux first; this host gets a clear "
                                 "refusal instead of an unenforced grant")
        self.state_root = Path(state_root) if state_root else _default_state_root()
        self.root = self.state_root / "integration"
        self.locks = self.root / "locks"
        self.locks.mkdir(parents=True, exist_ok=True)
        self.cgroup_root = Path(cgroup_root)
        self.proc_root = Path(proc_root)
        self.disk_path = Path(disk_path) if disk_path else self.root
        self.limits = dict(limits or {})
        for k in self.limits:
            if k not in ("cpus", "memory_bytes", "disk_bytes"):
                raise AdmissionError(f"unknown limit override {k!r}")
        self.reserve_memory_bytes = int(reserve_memory_bytes)
        self.reserve_disk_bytes = int(reserve_disk_bytes)
        self.disk_utilization = float(disk_utilization)
        self.poll_interval = float(poll_interval)
        self.starve_seconds = float(starve_seconds)
        self._db_path = self.root / "resources.sqlite3"
        self._init_db()

    # ------------------------------------------------------------------ sqlite
    def _connect(self) -> sqlite3.Connection:
        conn = sqlite3.connect(str(self._db_path), timeout=30.0, isolation_level=None)
        conn.execute("PRAGMA journal_mode=WAL")
        conn.execute("PRAGMA busy_timeout=30000")
        conn.execute("PRAGMA synchronous=NORMAL")
        return conn

    def _init_db(self) -> None:
        conn = self._connect()
        try:
            conn.execute("BEGIN IMMEDIATE")
            conn.execute("CREATE TABLE IF NOT EXISTS meta (key TEXT PRIMARY KEY, value TEXT)")
            row = conn.execute("SELECT value FROM meta WHERE key='schema_version'").fetchone()
            if row is None:
                conn.execute("INSERT INTO meta VALUES ('schema_version', ?)",
                             (str(SCHEMA_VERSION),))
            elif int(row[0]) != SCHEMA_VERSION:
                conn.execute("ROLLBACK")
                raise AdmissionError(f"{self._db_path}: ledger schema {row[0]} != "
                                     f"{SCHEMA_VERSION}; refusing to mix versions")
            conn.execute("""CREATE TABLE IF NOT EXISTS reservations(
                token TEXT PRIMARY KEY, repo_id TEXT NOT NULL, job_id TEXT NOT NULL,
                holder_pid INTEGER NOT NULL, memory_bytes INTEGER NOT NULL,
                disk_bytes INTEGER NOT NULL, cpus REAL NOT NULL,
                priority TEXT NOT NULL, created_at REAL NOT NULL)""")
            conn.execute("""CREATE TABLE IF NOT EXISTS waiters(
                token TEXT PRIMARY KEY, repo_id TEXT NOT NULL, job_id TEXT NOT NULL,
                holder_pid INTEGER NOT NULL, memory_bytes INTEGER NOT NULL,
                disk_bytes INTEGER NOT NULL, cpus REAL NOT NULL,
                priority TEXT NOT NULL, created_at REAL NOT NULL)""")
            conn.execute("COMMIT")
        finally:
            conn.close()

    def _delete_row(self, table: str, token: str) -> None:
        conn = self._connect()
        try:
            conn.execute("BEGIN IMMEDIATE")
            conn.execute(f"DELETE FROM {table} WHERE token=?", (token,))
            conn.execute("COMMIT")
        finally:
            conn.close()

    # ------------------------------------------------------------------ liveness (flock)
    def _take_lock(self, token: str) -> int:
        """The holder's side: an exclusive flock on the token's lock file.  Released by the
        kernel on process death, which is what makes reservations crash-safe."""
        fd = os.open(str(self.locks / f"{token}.lock"), os.O_CREAT | os.O_RDWR, 0o600)
        try:
            fcntl.flock(fd, fcntl.LOCK_EX)
        except BaseException:
            os.close(fd)
            raise
        return fd

    def _drop_lock(self, token: str, fd: int | None) -> None:
        if fd is not None:
            try:
                os.close(fd)  # closing drops the flock
            except OSError:
                pass
        try:
            os.unlink(str(self.locks / f"{token}.lock"))
        except FileNotFoundError:
            pass

    def _holder_alive(self, token: str) -> bool:
        """A holder is alive iff its lock file exists and the flock is still held.  If we can
        take the lock ourselves, the holder's process is dead (or never finished acquiring)."""
        path = self.locks / f"{token}.lock"
        if not path.exists():
            return False
        fd = os.open(str(path), os.O_RDWR)
        try:
            try:
                fcntl.flock(fd, fcntl.LOCK_EX | fcntl.LOCK_NB)
            except BlockingIOError:
                return True
            fcntl.flock(fd, fcntl.LOCK_UN)
            return False
        finally:
            os.close(fd)

    def _reap_locked(self, conn: sqlite3.Connection) -> None:
        """Delete rows whose holder is dead.  Caller must hold BEGIN IMMEDIATE."""
        for table in ("reservations", "waiters"):
            tokens = [r[0] for r in conn.execute(f"SELECT token FROM {table}")]
            for token in tokens:
                if not self._holder_alive(token):
                    conn.execute(f"DELETE FROM {table} WHERE token=?", (token,))
                    self._drop_lock(token, None)

    # ------------------------------------------------------------------ capacity detection
    def _read(self, *parts) -> str | None:
        try:
            return Path(*parts).read_text().strip()
        except (OSError, UnicodeDecodeError):
            return None

    def _cgroup_v2_chain(self) -> list[Path]:
        """This process's cgroup v2 ancestry, leaf first, resolved via /proc/self/cgroup —
        the service lives in a nested scope/slice, and the binding limit may sit on any
        ancestor, so reading only the cgroup root would admit over the real cap (the bug
        scripts/relay-build's memory_limit_bytes already solved for builds)."""
        rel = None
        text = self._read(self.proc_root, "self", "cgroup")
        if text is not None:
            for line in text.splitlines():
                if line.startswith("0::"):
                    rel = line.split(":", 2)[2].lstrip("/")
                    break
            if rel is None:
                return []  # no unified hierarchy
        elif not (self.cgroup_root / "cgroup.controllers").exists():
            return []  # no /proc and no v2 mount
        node = self.cgroup_root / rel if rel else self.cgroup_root
        nodes = []
        while True:
            nodes.append(node)
            if node == self.cgroup_root or node.parent == node:
                break
            node = node.parent
        return nodes

    def _cgroup_cpus(self) -> float | None:
        """Effective CPU quota: the *tightest* quota/period on the ancestry (v2), else v1."""
        best = None
        for node in self._cgroup_v2_chain():
            v2 = self._read(node, "cpu.max")
            if not v2:
                continue
            quota, _, period = v2.partition(" ")
            if quota == "max":
                continue
            try:
                cpus = float(quota) / float(period)
            except (ValueError, ZeroDivisionError):
                continue
            best = cpus if best is None else min(best, cpus)
        if best is not None:
            return max(best, 0.0)
        quota, period = (self._read(self.cgroup_root, "cpu", "cpu.cfs_quota_us"),
                         self._read(self.cgroup_root, "cpu", "cpu.cfs_period_us"))
        if quota is None:  # some v1 layouts sit at the root
            quota, period = (self._read(self.cgroup_root, "cpu.cfs_quota_us"),
                             self._read(self.cgroup_root, "cpu.cfs_period_us"))
        if quota and period:
            try:
                q, p = float(quota), float(period)
                if q > 0 and p > 0:
                    return q / p
            except ValueError:
                pass
        return None

    def _cgroup_memory(self) -> tuple[int | None, int | None]:
        """(limit, current) in bytes.  Limit is the tightest `memory.max` on the ancestry;
        current is usage at the node that carries that limit (its subtree includes us), so the
        headroom arithmetic in `_check_live_headroom` compares like with like."""
        chain = self._cgroup_v2_chain()
        limit = None
        limit_node = None
        for node in chain:
            v = self._read(node, "memory.max")
            if v and v != "max":
                try:
                    n = int(v)
                except ValueError:
                    continue
                if n > 0 and (limit is None or n < limit):
                    limit, limit_node = n, node
        current = None
        if chain:
            for node in ([limit_node] if limit_node else [chain[0]]):
                v = self._read(node, "memory.current")
                if v:
                    try:
                        current = int(v)
                    except ValueError:
                        pass
        if limit is None:
            v1 = self._read(self.cgroup_root, "memory", "memory.limit_in_bytes") or \
                 self._read(self.cgroup_root, "memory.limit_in_bytes")
            if v1:
                try:
                    n = int(v1)
                    if 0 < n < _CGROUP_V1_HUGE:
                        limit = n
                except ValueError:
                    pass
        if current is None:
            for name in (("memory", "memory.usage_in_bytes"), ("memory.usage_in_bytes",)):
                v = self._read(self.cgroup_root, *name)
                if v:
                    try:
                        current = int(v)
                    except ValueError:
                        pass
                    break
        return limit, current

    def _meminfo(self) -> tuple[int | None, int | None]:
        """(MemTotal, MemAvailable) in bytes, or None when /proc is absent."""
        text = self._read(self.proc_root, "meminfo")
        if text is None:
            return None, None
        total = available = None
        for line in text.splitlines():
            parts = line.split()
            if len(parts) >= 2 and parts[0] == "MemTotal:":
                total = int(parts[1]) * 1024
            elif len(parts) >= 2 and parts[0] == "MemAvailable:":
                available = int(parts[1]) * 1024
        return total, available

    def _disk_stats(self) -> tuple[int, int]:
        st = os.statvfs(str(self.disk_path))
        return st.f_blocks * st.f_frsize, st.f_bavail * st.f_frsize

    def capacity(self) -> Capacity:
        """Effective capacity: detected cgroup/host values, replaced per dimension by any
        explicit construction override.  Detection never raises; an unknown dimension is a
        refusal at admit time, not a crash here."""
        host_cpus = float(os.cpu_count() or 1)
        cpus = self._cgroup_cpus()
        mem_total, _ = self._meminfo()
        memory, _ = self._cgroup_memory()
        if memory is None:
            memory = mem_total
        elif mem_total is not None:
            memory = min(memory, mem_total)
        disk_total, _ = self._disk_stats()
        disk = int(disk_total * self.disk_utilization)
        if "cpus" in self.limits:
            cpus = float(self.limits["cpus"])
        elif cpus is None:
            cpus = host_cpus
        cpus = min(cpus, host_cpus if "cpus" not in self.limits else cpus)
        if "memory_bytes" in self.limits:
            memory = int(self.limits["memory_bytes"])
        if "disk_bytes" in self.limits:
            disk = int(self.limits["disk_bytes"])
        return Capacity(cpus=float(cpus),
                        memory_bytes=int(memory) if memory is not None else 0,
                        disk_bytes=int(disk))

    # ------------------------------------------------------------------ admission
    def _check_live_headroom(self, conn, cap: Capacity, memory_bytes: int,
                             disk_bytes: int) -> str | None:
        """None when the live host can take this request, else a reason string."""
        rows = conn.execute("SELECT memory_bytes FROM reservations").fetchall()
        reserved_mem = sum(r[0] for r in rows)
        if memory_bytes > 0:
            cg_limit, cg_current = self._cgroup_memory()
            if "memory_bytes" in self.limits:
                cg_limit = int(self.limits["memory_bytes"])
            if cg_limit is not None:
                current = cg_current
                if current is not None:
                    # No double subtraction: current usage already contains admitted jobs'
                    # real consumption; the reservation sum is the grant-side estimate.
                    used = max(current, reserved_mem)
                    if used + memory_bytes > cg_limit:
                        return (f"cgroup memory budget exhausted: used~{used} + "
                                f"requested {memory_bytes} > limit {cg_limit}")
            _, mem_available = self._meminfo()
            if mem_available is not None:
                if memory_bytes + self.reserve_memory_bytes > mem_available:
                    return (f"host memory headroom too low: requested {memory_bytes} + "
                            f"reserve {self.reserve_memory_bytes} > MemAvailable {mem_available}")
            elif "memory_bytes" not in self.limits:
                return "host memory capacity unknown (no /proc, no cgroup, no explicit limit); " \
                       "refusing rather than admitting blind"
        if disk_bytes > 0:
            _, free = self._disk_stats()
            if disk_bytes + self.reserve_disk_bytes > free:
                return (f"disk headroom too low: requested {disk_bytes} + reserve "
                        f"{self.reserve_disk_bytes} > free {free}")
        return None

    def _outranked_locked(self, conn, self_token: str, priority: str,
                          created_at: float, now: float) -> bool:
        """True when some other waiter must be served first: land before try, except a try
        waiter older than starve_seconds is promoted ahead of land.  Ties: oldest first."""
        def rank(prio: str, created: float) -> tuple[int, float]:
            cls = 0 if prio == PRIORITY_LAND else 1
            if prio == PRIORITY_TRY and now - created >= self.starve_seconds:
                cls = 0  # anti-starvation promotion
            return cls, created
        mine = rank(priority, created_at)
        for token, prio, created in conn.execute(
                "SELECT token, priority, created_at FROM waiters WHERE token != ?",
                (self_token,)):
            if rank(prio, created) < mine:
                return True
        return False

    def _never_fits(self, cap: Capacity, memory_bytes: int, disk_bytes: int,
                    cpus: float) -> str | None:
        if memory_bytes > 0 and cap.memory_bytes <= 0:
            return "host memory capacity is unknown or zero (no cgroup limit, no MemTotal, no " \
                   "explicit limit); refusing rather than admitting blind"
        if memory_bytes > cap.memory_bytes:
            return f"requested memory {memory_bytes} can never fit effective capacity " \
                   f"{cap.memory_bytes}"
        if disk_bytes > 0 and (cap.disk_bytes <= 0 or disk_bytes > cap.disk_bytes):
            return f"requested disk {disk_bytes} can never fit effective capacity {cap.disk_bytes}"
        if cpus > 0 and (cap.cpus <= 0 or cpus > cap.cpus):
            return f"requested cpus {cpus} can never fit effective capacity {cap.cpus}"
        return None

    def acquire(self, repo_id: str, job_id: str, *, memory_bytes: int = 0,
                disk_bytes: int = 0, cpus: float = 1.0, priority: str = PRIORITY_LAND,
                timeout: float = 0.0) -> _Reservation:
        """Acquire host capacity, blocking up to `timeout` seconds (0: one attempt).

        Raises AdmissionRefused for a request that can never fit or when timeout=0 finds no
        room; AdmissionTimeout when capacity stayed contended.  Success returns a context
        manager; leaving the block (or dying) releases the reservation.
        """
        if priority not in _PRIORITIES:
            raise AdmissionRefused(f"unknown priority {priority!r} (expected {_PRIORITIES})")
        for name, value in (("memory_bytes", memory_bytes), ("disk_bytes", disk_bytes)):
            if not isinstance(value, int) or value < 0:
                raise AdmissionRefused(f"{name} must be a non-negative int, got {value!r}")
        if not isinstance(cpus, (int, float)) or cpus < 0:
            raise AdmissionRefused(f"cpus must be a non-negative number, got {cpus!r}")

        token = uuid.uuid4().hex
        fd = self._take_lock(token)
        created_at = time.time()
        deadline = time.monotonic() + timeout
        conn = self._connect()
        try:
            conn.execute("BEGIN IMMEDIATE")
            conn.execute(
                "INSERT INTO waiters VALUES (?,?,?,?,?,?,?,?,?)",
                (token, repo_id, job_id, os.getpid(), memory_bytes, disk_bytes,
                 float(cpus), priority, created_at))
            conn.execute("COMMIT")
            first = True
            while True:
                now = time.monotonic()
                if not first and now >= deadline:
                    raise AdmissionTimeout(
                        f"no host slot for {repo_id}/{job_id} within {timeout}s "
                        f"(cpus={cpus}, memory={memory_bytes}, disk={disk_bytes})")
                first = False
                conn.execute("BEGIN IMMEDIATE")
                try:
                    self._reap_locked(conn)
                    cap = self.capacity()
                    never = self._never_fits(cap, memory_bytes, disk_bytes, cpus)
                    if never:
                        raise AdmissionRefused(never)
                    usage = conn.execute(
                        "SELECT COALESCE(SUM(memory_bytes),0), COALESCE(SUM(disk_bytes),0), "
                        "COALESCE(SUM(cpus),0) FROM reservations").fetchone()
                    reason = None
                    if usage[0] + memory_bytes > cap.memory_bytes:
                        reason = f"memory ledger full: {usage[0]}+{memory_bytes} > {cap.memory_bytes}"
                    elif usage[1] + disk_bytes > cap.disk_bytes:
                        reason = f"disk ledger full: {usage[1]}+{disk_bytes} > {cap.disk_bytes}"
                    elif usage[2] + cpus > cap.cpus:
                        reason = f"cpu ledger full: {usage[2]}+{cpus} > {cap.cpus}"
                    elif self._outranked_locked(conn, token, priority, created_at, time.time()):
                        reason = "a higher-priority or older waiter holds the queue"
                    if reason is None:
                        reason = self._check_live_headroom(conn, cap, memory_bytes, disk_bytes)
                    if reason is None:
                        conn.execute(
                            "INSERT INTO reservations VALUES (?,?,?,?,?,?,?,?,?)",
                            (token, repo_id, job_id, os.getpid(), memory_bytes, disk_bytes,
                             float(cpus), priority, created_at))
                        conn.execute("DELETE FROM waiters WHERE token=?", (token,))
                        conn.execute("COMMIT")
                        return _Reservation(self, token, fd, repo_id, job_id,
                                            memory_bytes, disk_bytes, float(cpus), priority)
                    conn.execute("COMMIT")
                except BaseException:
                    if conn.in_transaction:
                        conn.execute("ROLLBACK")
                    raise
                if timeout == 0:
                    raise AdmissionRefused(f"no host slot for {repo_id}/{job_id}: {reason}")
                time.sleep(min(self.poll_interval, max(deadline - time.monotonic(), 0.05)))
        except BaseException:
            # Do not leave a waiter row (or lock) behind on any exit path but success.
            try:
                conn.execute("BEGIN IMMEDIATE")
                conn.execute("DELETE FROM waiters WHERE token=?", (token,))
                conn.execute("COMMIT")
            except sqlite3.Error:
                pass
            self._drop_lock(token, fd)
            raise
        finally:
            conn.close()

    # ------------------------------------------------------------------ status
    def status(self) -> dict:
        """A point-in-time view for the Live tab: capacity, ledger usage, holders, waiters and
        live host headroom.  Also reaps dead holders — reading status is always safe."""
        conn = self._connect()
        try:
            conn.execute("BEGIN IMMEDIATE")
            self._reap_locked(conn)
            rows = {t: conn.execute(
                f"SELECT token, repo_id, job_id, memory_bytes, disk_bytes, cpus, priority, "
                f"created_at FROM {t} ORDER BY created_at").fetchall()
                for t in ("reservations", "waiters")}
            conn.execute("COMMIT")
        finally:
            conn.close()
        now = time.time()

        def entries(rows_):
            return [{"repo_id": r[1], "job_id": r[2], "memory_bytes": r[3],
                     "disk_bytes": r[4], "cpus": r[5], "priority": r[6],
                     "age_seconds": round(now - r[7], 3)} for r in rows_]

        cap = self.capacity()
        res = entries(rows["reservations"])
        usage = {
            "memory_bytes": sum(r["memory_bytes"] for r in res),
            "disk_bytes": sum(r["disk_bytes"] for r in res),
            "cpus": sum(r["cpus"] for r in res),
        }
        _, mem_available = self._meminfo()
        _, disk_free = self._disk_stats()
        return {
            "schema": SCHEMA_VERSION,
            "capacity": {"cpus": cap.cpus, "memory_bytes": cap.memory_bytes,
                         "disk_bytes": cap.disk_bytes},
            "usage": usage,
            "reservations": res,
            "waiters": entries(rows["waiters"]),
            "live": {"mem_available_bytes": mem_available,
                     "cgroup_memory_current_bytes": self._cgroup_memory()[1],
                     "disk_free_bytes": disk_free},
        }
