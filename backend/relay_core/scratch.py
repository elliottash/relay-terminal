"""Agent scratch on disk: what it takes, what can go, and whether anyone should be told.

Card #SZHQ. Coding agents leave trees behind in the temp directory — Claude Code's per-session
folders under `$TMPDIR/claude-<uid>/`, exported source trees and build directories an agent made
to test something, `relay-*` test workspaces, `tmp*` folders from a test run that was killed —
and nothing ever removes them. On 2026-09-24 one machine had 380 GB of it. On a laptop that is
the whole disk, and the user finds out when something else fails to write.

Three verbs, used by `scripts/relay-scratch`, the `disk-hygiene` skill and the app's monitor:

- `report()`   every entry under the scratch roots: kind, size, idle time, and whether a live
               process is using it;
- `gc()`       remove the entries idle past a threshold that no process uses (dry run unless
               `apply=True`);
- `check()`    one verdict: scratch over its budget, or the disk nearly full, with one line a
               person can read and the bytes `gc` would give back.

Safety rules for `gc`, each one an easy way to delete someone's work: only entries owned by this
user; never through a symlink; never an entry a live process has as its cwd, holds open or names
on its command line; never one written to within `idle_hours`; never land.py's root (it has its
own gc, which knows which snapshots are live); never the shared compiler cache (card #V52P: ccache
evicts its own objects at the `max_size` that cmake/CompilerCache.cmake writes). That cache lives
outside the temp directory, so it is listed as its own kind and counted against the budget.

Card #DVV2 adds the ledger on top: Relay, not the agent, owns scratch. An agent asks for a
directory (`scratch_dir` tool, or `relay-scratch new` for shells and guests), Relay decides where
it lives — one of three classes with fixed homes — records one append-only row per directory, and
ends it. Nothing that must outlive the task is scratch: class `keep` lives in the project and is
promoted, never left in /tmp. `report` reads the ledger first and walks only for what is on disk
but not in it; every `gc` removal and every release is a ledger transition, not a guess. The three
homes mirror `relay::scratchpaths` in `src/AppPaths.h` and `docs/SCRATCH.md`; keep them identical.
"""

from __future__ import annotations

import argparse
import dataclasses
import json
import os
import re
import shutil
import sys
import tempfile
import time
import uuid
from dataclasses import asdict, dataclass, field
from datetime import datetime, timezone
from pathlib import Path

GB = 1024 ** 3
DEFAULT_IDLE_HOURS = 24.0
# Loose temp-dir entries that are agent or test scratch by name. Only directories, only ours.
LOOSE_PREFIXES = ("relay-", "tmp")
# Directories under a root whose children are the real entries (one per session).
CONTAINER_MARK = "-"          # Claude Code names its per-project folders "-home-user-repo"
MANAGED = {"land": "land.py gc"}


@dataclass
class Entry:
    path: str
    kind: str                  # claude-session, claude-scratch, loose, managed, compiler-cache
    bytes: int = 0
    idle_hours: float = 0.0
    in_use: bool = False
    removable: bool = False
    why_kept: str = ""
    ledger_id: str = ""  # set when this entry comes from (or is recorded in) the #DVV2 ledger


@dataclass
class Verdict:
    ok: bool
    line: str
    scratch_bytes: int
    reclaimable_bytes: int
    budget_bytes: int
    free_bytes: int
    min_free_bytes: int
    disk_bytes: int
    roots: list = field(default_factory=list)


def uid_suffix() -> str:
    return "claude-%d" % os.getuid() if hasattr(os, "getuid") else "claude"


def temp_dir() -> Path:
    return Path(tempfile.gettempdir())


def default_roots() -> list[Path]:
    """`RELAY_SCRATCH_ROOTS` (os.pathsep-separated) or the two Relay-owned scratch homes:
    the ledgered class-`scratch` root (card #DVV2) and, until `adopt` has cleared the backlog,
    the pre-ledger Claude Code root under the temp dir."""
    env = os.environ.get("RELAY_SCRATCH_ROOTS")
    if env:
        return [Path(p) for p in env.split(os.pathsep) if p]
    return [scratch_root(), temp_dir() / uid_suffix()]


def compiler_cache_dir() -> Path:
    """The shared ccache dir, found the way cmake/CompilerCache.cmake finds it."""
    env = os.environ.get("RELAY_CCACHE_DIR")
    if env:
        return Path(env)
    xdg = os.environ.get("XDG_CACHE_HOME")
    if xdg:
        return Path(xdg) / "relay" / "ccache"
    if os.name == "nt" and os.environ.get("LOCALAPPDATA"):
        return Path(os.environ["LOCALAPPDATA"]) / "relay" / "ccache"
    return Path.home() / ".cache" / "relay" / "ccache"


def human(n: float) -> str:
    for unit in ("B", "KB", "MB", "GB", "TB"):
        if n < 1024 or unit == "TB":
            return ("%.0f %s" if unit in ("B", "KB") else "%.1f %s") % (n, unit)
        n /= 1024.0
    return "%d B" % n


def measure(path: Path) -> tuple[int, float]:
    """(bytes on disk, newest mtime) of a tree, without following symlinks."""
    total, newest = 0, 0.0
    try:
        st = os.lstat(path)
        total, newest = getattr(st, "st_blocks", 0) * 512 or st.st_size, st.st_mtime
    except OSError:
        return 0, 0.0
    if not os.path.isdir(path) or os.path.islink(path):
        return total, newest
    stack = [str(path)]
    while stack:
        top = stack.pop()
        try:
            with os.scandir(top) as it:
                for item in it:
                    try:
                        st = item.stat(follow_symlinks=False)
                    except OSError:
                        continue
                    blocks = getattr(st, "st_blocks", None)
                    total += blocks * 512 if blocks is not None else st.st_size
                    newest = max(newest, st.st_mtime)
                    if item.is_dir(follow_symlinks=False):
                        stack.append(item.path)
        except OSError:
            continue
    return total, newest


def paths_in_use() -> list[str]:
    """Every cwd, open file and path-looking argument of this user's live processes (Linux)."""
    proc = Path("/proc")
    if not proc.is_dir():
        return []
    me = os.getuid() if hasattr(os, "getuid") else None
    self_pid = str(os.getpid())
    found: list[str] = []
    for pid in os.listdir(proc):
        if not pid.isdigit() or pid == self_pid:
            continue  # our own cwd and argv are the deleter's, not evidence anyone uses it
        base = proc / pid
        try:
            if me is not None and base.stat().st_uid != me:
                continue
        except OSError:
            continue
        try:
            found.append(os.readlink(base / "cwd"))
        except OSError:
            pass
        try:
            for fd in os.listdir(base / "fd"):
                try:
                    found.append(os.readlink(base / "fd" / fd))
                except OSError:
                    pass
        except OSError:
            pass
        try:
            for arg in (base / "cmdline").read_bytes().split(b"\0"):
                if arg.startswith(b"/"):
                    found.append(arg.decode("utf-8", "replace"))
        except OSError:
            pass
    return found


def _used(path: str, used: list[str]) -> bool:
    prefix = path.rstrip("/") + "/"
    return any(u == path or u.startswith(prefix) for u in used)


def _mine(path: Path) -> bool:
    if not hasattr(os, "getuid"):
        return True
    try:
        return os.lstat(path).st_uid == os.getuid()
    except OSError:
        return False


def candidates(roots: list[Path], include_loose: bool = True) -> list[tuple[Path, str]]:
    out: list[tuple[Path, str]] = []
    seen: set[str] = set()
    home = scratch_root()
    for root in roots:
        if not root.is_dir() or root.is_symlink():
            continue
        for child in sorted(root.iterdir()):
            if child.is_symlink():
                continue
            if root == home:
                # The ledgered scratch home: children are session roots (ledger rows of their
                # own), not the "-" containers or per-card exports the temp dir holds.
                out.append((child, "scratch-home"))
            elif child.name in MANAGED:
                out.append((child, "managed"))
            elif child.is_dir() and child.name.startswith(CONTAINER_MARK):
                for session in sorted(child.iterdir()):
                    if not session.is_symlink():
                        out.append((session, "claude-session"))
            else:
                out.append((child, "claude-scratch"))
            seen.add(str(child))
    if include_loose and not os.environ.get("RELAY_SCRATCH_ROOTS"):
        tmp = temp_dir()
        try:
            children = sorted(tmp.iterdir())
        except OSError:
            children = []
        for child in children:
            if (child.name.startswith(LOOSE_PREFIXES) and str(child) not in seen
                    and child.is_dir() and not child.is_symlink() and _mine(child)
                    and child not in roots):
                out.append((child, "loose"))
    # With RELAY_SCRATCH_ROOTS set (tests, a narrowed report) only an explicitly named cache.
    if not os.environ.get("RELAY_SCRATCH_ROOTS") or os.environ.get("RELAY_CCACHE_DIR"):
        cache = compiler_cache_dir()
        if cache.is_dir() and not cache.is_symlink() and str(cache) not in seen:
            out.append((cache, "compiler-cache"))
    return out


def report(roots: list[Path] | None = None, idle_hours: float = DEFAULT_IDLE_HOURS,
           include_loose: bool = True, ledger: "ScratchLedger | None" = None) -> list[Entry]:
    narrowed = roots is not None or bool(os.environ.get("RELAY_SCRATCH_ROOTS"))
    roots = [Path(r) for r in (roots or default_roots())]
    used = paths_in_use()
    here = os.getcwd()
    now = time.time()
    entries = []
    # Card #DVV2: the ledger first. A narrowed report (explicit roots, or RELAY_SCRATCH_ROOTS,
    # which is how the #SZHQ tests isolate themselves) stays walk-only.
    ledger_rows: list[Row] = []
    if ledger is not None or not narrowed:
        active = ScratchLedger() if ledger is None else ledger
        ledger_rows = [r for r in active.records().values() if r.state in ("live", "released")]
    # Nested rows (a per-purpose dir inside its session's TMPDIR row) are covered by the
    # outermost row; counting both would double every byte.
    outermost: list[Row] = []
    for row in sorted(ledger_rows, key=lambda r: len(r.path)):
        if not any(Path(row.path) == Path(o.path) or Path(o.path) in Path(row.path).parents
                   for o in outermost):
            outermost.append(row)
    covered = [Path(r.path) for r in ledger_rows]
    for path, kind in candidates(roots, include_loose):
        if any(p == path or p in path.parents for p in covered):
            continue  # the walk only finds what is not in the ledger
        size, newest = measure(path)
        entry = Entry(path=str(path), kind=kind, bytes=size,
                      idle_hours=max(0.0, (now - newest) / 3600) if newest else 0.0)
        entry.in_use = _used(str(path), used) or _used(str(path), [here])
        if kind == "managed":
            entry.why_kept = "managed by %s" % MANAGED[path.name]
        elif kind == "compiler-cache":
            entry.why_kept = "ccache evicts it at its max_size"
        elif not _mine(path):
            entry.why_kept = "not owned by this user"
        elif entry.in_use:
            entry.why_kept = "a live process uses it"
        elif entry.idle_hours < idle_hours:
            entry.why_kept = "written %.1fh ago" % entry.idle_hours
        else:
            entry.removable = True
        entries.append(entry)
    for row in outermost:
        path = Path(row.path)
        size, newest = (measure(path) if path.exists() else (row.size, 0.0))
        entry = Entry(path=str(path), kind=row.cls, bytes=size,
                      idle_hours=max(0.0, (now - newest) / 3600) if newest else 0.0,
                      ledger_id=row.id)
        entry.in_use = _used(str(path), used) or _used(str(path), [here])
        if not path.exists() and not path.is_symlink():
            entry.why_kept = "gone from disk"
        elif not _mine(path):
            entry.why_kept = "not owned by this user"
        elif entry.in_use:
            entry.why_kept = "a live process uses it"
        elif row.cls == "keep":
            entry.why_kept = "keep: promote or drop explicitly"
        elif row.cls == "install":
            entry.why_kept = "install: the user removes it"
        elif row.state == "released":
            entry.removable = True  # already ended; the in-use check above is the only guard
        else:
            threshold = idle_hours
            if row.lifetime.startswith("days:"):
                try:
                    threshold = max(idle_hours, float(row.lifetime[5:]) * 24)
                except ValueError:
                    pass
            if entry.idle_hours < threshold:
                entry.why_kept = "written %.1fh ago (lifetime %s)" % (entry.idle_hours, row.lifetime)
            else:
                entry.removable = True
        entries.append(entry)
    entries.sort(key=lambda e: -e.bytes)
    return entries


MIN_IDLE_HOURS = 6.0
"""`gc --apply` on the real (non-narrowed) roots never runs below this idle threshold. The
#DVV2 incident: a test ran `--idle-hours 0 --apply` against the default roots and deleted
~3 GB of other sessions' 3-7 h scratch in one line. An agent that really means it passes
`force_idle=True` (CLI `--force-idle`), and the refusal says so."""


def gc(roots: list[Path] | None = None, idle_hours: float = DEFAULT_IDLE_HOURS,
       apply: bool = False, include_loose: bool = True, log=print,
       force_idle: bool = False) -> tuple[int, int]:
    """Remove removable entries. Returns (count, bytes). Rechecks each one just before.

    A removal is also a ledger transition (card #DVV2): the row for a removed path is
    marked reclaimed with the size freed, so `report` and the monitor stay truthful."""
    narrowed = roots is not None or bool(os.environ.get("RELAY_SCRATCH_ROOTS"))
    if apply and not narrowed and idle_hours < MIN_IDLE_HOURS and not force_idle:
        raise ValueError(
            "refusing gc --apply with --idle-hours %.1f on the default roots (floor is %g); "
            "pass force_idle=True / --force-idle if you really mean it" % (idle_hours,
                                                                          MIN_IDLE_HOURS))
    ledger = ScratchLedger() if not narrowed else None
    count = freed = 0
    for entry in report(roots, idle_hours, include_loose, ledger=ledger):
        if not entry.removable:
            continue
        path = Path(entry.path)
        if apply:
            # The world may have moved since the scan: look once more, cheaply.
            if _used(entry.path, paths_in_use()):
                log("kept %s: a process started using it" % path)
                continue
            if path.is_dir() and not path.is_symlink():
                shutil.rmtree(path, ignore_errors=True)
            else:
                try:
                    path.unlink()
                except OSError:
                    pass
            if path.exists():
                log("could not fully remove %s" % path)
        if apply and ledger is not None and entry.ledger_id:
            row = ledger.find(entry.ledger_id)
            if row is not None and row.state in ("live", "released"):
                ledger.append(dataclasses.replace(row, state="reclaimed", at=_now(),
                                                  freed=entry.bytes, note="reclaimed by gc"))
        log("%s %s  %s  idle %.0fh  (%s)" % ("removed" if apply else "would remove",
                                            human(entry.bytes), path, entry.idle_hours,
                                            entry.kind))
        count += 1
        freed += entry.bytes
    return count, freed


def budget_bytes(disk_total: int) -> int:
    env = os.environ.get("RELAY_SCRATCH_BUDGET_GB")
    if env:
        return int(float(env) * GB)
    return int(min(20 * GB, 0.05 * disk_total))


def min_free_bytes(disk_total: int) -> int:
    env = os.environ.get("RELAY_SCRATCH_MIN_FREE_GB")
    if env:
        return int(float(env) * GB)
    return int(max(5 * GB, 0.05 * disk_total))


def check(roots: list[Path] | None = None, idle_hours: float = DEFAULT_IDLE_HOURS,
          include_loose: bool = True) -> Verdict:
    roots_list = [Path(r) for r in (roots or default_roots())]
    entries = report(roots, idle_hours, include_loose)  # roots=None keeps ledger mode
    total = sum(e.bytes for e in entries)
    reclaim = sum(e.bytes for e in entries if e.removable)
    probe = next((r for r in roots_list if r.exists()), temp_dir())
    disk = shutil.disk_usage(probe)
    budget, floor = budget_bytes(disk.total), min_free_bytes(disk.total)
    problems = []
    if total > budget:
        problems.append("agent scratch takes %s (budget %s)" % (human(total), human(budget)))
    if disk.free < floor:
        problems.append("only %s free on the disk holding it" % human(disk.free))
    if problems:
        line = "; ".join(problems) + (". %s can be reclaimed: relay-scratch gc --apply"
                                      % human(reclaim) if reclaim else
                                      ". Nothing is idle enough to reclaim yet.")
    else:
        line = "agent scratch %s of %s budget; %s free" % (human(total), human(budget),
                                                          human(disk.free))
    return Verdict(ok=not problems, line=line, scratch_bytes=total, reclaimable_bytes=reclaim,
                   budget_bytes=budget, free_bytes=disk.free, min_free_bytes=floor,
                   disk_bytes=disk.total, roots=[str(r) for r in roots_list])


# ---------------------------------------------------------------------------
# Card #DVV2: the ledger. Three classes, three fixed homes, one append-only row per
# directory. The same three paths exist as relay::scratchpaths in src/AppPaths.h.
# ---------------------------------------------------------------------------

CLASSES = ("scratch", "keep", "install")
STATES = ("live", "released", "promoted", "reclaimed", "orphaned")
# How long a directory is expected to live: task (until this task closes), session
# (until its session closes), days:N, until-promoted (keep: until moved into the project).
DEFAULT_LIFETIME = {"scratch": "session", "keep": "until-promoted", "install": "user"}
# $HOME folders agents made on this machine before the ledger existed (2026-09-24 survey).
KNOWN_HOME_FOLDERS = ("tmp", "relay-phone", "projects-reorg", "relay-qa", "logs")
KNOWN_HOME_GLOBS = ("relay-stash-archive-*",)


def _xdg_base(override: str, xdg: str, home_default: str) -> Path:
    """$<override> wins, then $XDG_<dir>, then the XDG default under $HOME — matching
    relay::scratchpaths::xdgBase in src/AppPaths.h."""
    for name in (override, xdg):
        value = os.environ.get(name)
        if value:
            return Path(value)
    return Path.home() / home_default


def scratch_root() -> Path:
    """Class `scratch`: disposable trees, per session, under the cache dir — not /tmp."""
    return _xdg_base("RELAY_SCRATCH_HOME", "XDG_CACHE_HOME", ".cache") / "relay" / "scratch"


def tools_root() -> Path:
    """Class `install`: tools an agent installs for the user — never a new top-level $HOME folder."""
    return _xdg_base("RELAY_TOOLS_HOME", "XDG_DATA_HOME", ".local/share") / "relay" / "tools"


def state_root() -> Path:
    return _xdg_base("RELAY_STATE_HOME", "XDG_STATE_HOME", ".local/state") / "relay"


def ledger_path() -> Path:
    return Path(os.environ.get("RELAY_LEDGER") or state_root() / "scratch-ledger.jsonl")


# A session root is named by the first 12 characters of the session key (#H1BS): a pane token is a
# 36-char UUID, and <scratch home>/<uuid>/tmp as TMPDIR left no room under the 108-byte Unix
# socket limit for what programs put there (Chrome's $TMPDIR/com.google.Chrome.XXXXXX/
# SingletonSocket). 12 characters of a UUID still tell panes apart. src/AppPaths.h
# relay::scratchpaths::sessionRoot builds the same name; keep the two in step.
SESSION_NAME_CHARS = 12


def session_root(session: str) -> Path:
    """The session's own subroot of the scratch home (also where its TMPDIR points)."""
    return scratch_root() / (_safe_name(session)[:SESSION_NAME_CHARS] if session else "adhoc")


def keep_root(project: str | Path) -> Path:
    """Class `keep`: work that must outlive the task, in the project, git-ignored via /.relay/."""
    return Path(project).resolve() / ".relay" / "work"


# The machine's temp dir as inherited at import time. Agent sessions repoint os.environ TMPDIR at
# their own ledgered scratch root (that is the point of card #DVV2), so the post-turn sweep cannot
# trust gettempdir() alone: an agent that hardcodes /tmp/foo would escape it. This is captured
# before the first session start — the worker imports this module at startup.
_INHERITED_TMPDIR = os.environ.get("TMPDIR") or ""


def system_tmp() -> Path:
    return Path(_INHERITED_TMPDIR) if _INHERITED_TMPDIR else Path(tempfile.gettempdir())


def _safe_name(text: str) -> str:
    return re.sub(r"[^A-Za-z0-9_.-]+", "-", text).strip("-.")[:60] or "x"


def _slug(purpose: str, id_: str) -> str:
    return _safe_name(purpose)[:40] + "-" + id_


def _now() -> str:
    return datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")


def _iso(ts: float) -> str:
    return datetime.fromtimestamp(ts, timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")


@dataclass
class Row:
    """One ledger record. The file is append-only: a transition appends a new record with
    the same id, and reading takes the last one — so the file is a replayable history and
    a half-written last line (crash mid-append) costs nothing."""

    id: str
    path: str
    cls: str = "scratch"           # JSON key "class"
    purpose: str = ""
    created_by: dict = field(default_factory=dict)  # session, pane, model, card
    created_at: str = ""
    lifetime: str = "session"      # task | session | days:N | until-promoted | user
    state: str = "live"
    size: int = 0                  # bytes at last scan
    at: str = ""                   # when this record was written
    released_at: str = ""
    freed: int = 0                 # bytes freed when reclaimed
    note: str = ""

    def to_record(self) -> dict:
        record = asdict(self)
        record["class"] = record.pop("cls")
        return record

    @staticmethod
    def from_record(record: dict) -> "Row":
        record = dict(record)
        record["cls"] = record.pop("class", record.get("cls", "scratch"))
        fields = {f for f in Row.__dataclass_fields__}
        return Row(**{k: v for k, v in record.items() if k in fields})


class ScratchLedger:
    """The append-only JSONL of every scratch directory Relay owns for this user."""

    def __init__(self, path: Path | None = None):
        self.path = Path(path) if path is not None else ledger_path()

    def records(self) -> dict[str, Row]:
        rows: dict[str, Row] = {}
        try:
            text = self.path.read_text(encoding="utf-8")
        except OSError:
            return rows
        for line in text.splitlines():
            line = line.strip()
            if not line:
                continue
            try:
                record = json.loads(line)
            except json.JSONDecodeError:
                continue  # half-written last line; the previous record for that id stands
            try:
                row = Row.from_record(record)
            except TypeError:
                continue
            rows[row.id] = row  # last record per id wins
        return rows

    def rows(self) -> list[Row]:
        return sorted(self.records().values(), key=lambda r: (r.created_at, r.id))

    def live_rows(self) -> list[Row]:
        return [r for r in self.rows() if r.state == "live"]

    def append(self, row: Row) -> Row:
        self.path.parent.mkdir(parents=True, exist_ok=True)
        with self.path.open("a", encoding="utf-8") as handle:
            handle.write(json.dumps(row.to_record(), ensure_ascii=False) + "\n")
        return row

    def find(self, ref: str) -> Row | None:
        """By row id ('#sc3f' or 'sc3f') or by path."""
        ref = ref.lstrip("#")
        for row in self.rows():
            if row.id == ref or row.path == ref:
                return row
        try:
            resolved = str(Path(ref).resolve())
        except OSError:
            return None
        for row in self.rows():
            if row.path == resolved:
                return row
        return None

    def next_id(self) -> str:
        return "sc" + uuid.uuid4().hex[:5]


def new_dir(cls: str, purpose: str, *, session: str = "", pane: str = "", model: str = "",
            card: str = "", lifetime: str | None = None, project: str | Path | None = None,
            path: Path | None = None, ledger: ScratchLedger | None = None) -> Row:
    """Allocate a ledgered directory. Agents never pick the path themselves; this decides
    where it lives by class, creates it, and records the row."""
    if cls not in CLASSES:
        raise ValueError("class must be one of %s, not %r" % (", ".join(CLASSES), cls))
    purpose = " ".join(str(purpose).split())[:200] or "unspecified"
    ledger = ledger if ledger is not None else ScratchLedger()
    id_ = ledger.next_id()
    if path is None:
        if cls == "scratch":
            base = session_root(session)
        elif cls == "keep":
            base = keep_root(project or os.getcwd())
        else:
            base = tools_root()
        path = base / _slug(purpose, id_)
    path = Path(path)
    path.mkdir(parents=True, exist_ok=True)
    return ledger.append(Row(id=id_, path=str(path), cls=cls, purpose=purpose,
                             created_by={"session": session, "pane": pane, "model": model,
                                         "card": card},
                             created_at=_now(),
                             lifetime=lifetime or DEFAULT_LIFETIME[cls], state="live",
                             at=_now()))


def _safe_delete(path: Path) -> int | None:
    """Delete a tree under the same safety rules as gc. Returns bytes freed, or None
    when the deletion was refused (a live process uses it)."""
    if not path.exists() and not path.is_symlink():
        return 0
    if path.is_symlink() or not _mine(path):
        return None
    if _used(str(path), paths_in_use()) or _used(str(path), [os.getcwd()]):
        return None
    size, _ = measure(path)
    if path.is_dir():
        shutil.rmtree(path, ignore_errors=True)
    else:
        try:
            path.unlink()
        except OSError:
            pass
    if path.exists():
        return None
    return size


def release(ref: str, *, promote_to: str | Path | None = None, drop: bool = False,
            ledger: ScratchLedger | None = None) -> Row:
    """End a ledgered directory. `scratch` is reclaimed (deleted, with the size recorded);
    `keep` must be promoted into the project or explicitly dropped — never silently
    deleted; `install` stays until the user removes it."""
    ledger = ledger if ledger is not None else ScratchLedger()
    row = ledger.find(ref)
    if row is None:
        raise ValueError("no ledger row matches %r" % ref)
    if row.state != "live":
        raise ValueError("%s is already %s" % (row.id, row.state))
    now = _now()
    src = Path(row.path)
    if promote_to is not None:
        dest = Path(promote_to).expanduser()
        if not dest.is_absolute():
            dest = Path(os.getcwd()) / dest
        if temp_dir() == dest.parent or Path.home() == dest.parent:
            raise ValueError("promote into the project, not the top of the temp dir or $HOME")
        if not src.exists():
            raise ValueError("%s no longer exists; nothing to promote" % src)
        dest.parent.mkdir(parents=True, exist_ok=True)
        if dest.exists():
            raise ValueError("%s already exists" % dest)
        shutil.move(str(src), str(dest))
        return ledger.append(dataclasses.replace(row, state="promoted", at=now,
                                                 released_at=now,
                                                 note="promoted to %s" % dest))
    if drop:
        freed = _safe_delete(src)
        if freed is None:
            raise ValueError("a live process uses %s; not dropping it" % src)
        return ledger.append(dataclasses.replace(row, state="reclaimed", at=now,
                                                 released_at=now, freed=freed,
                                                 note="dropped explicitly"))
    if row.cls == "scratch":
        freed = _safe_delete(src)
        if freed is None:
            return ledger.append(dataclasses.replace(row, state="released", at=now,
                                                     released_at=now,
                                                     note="in use; gc will reclaim it"))
        return ledger.append(dataclasses.replace(row, state="reclaimed", at=now,
                                                 released_at=now, freed=freed))
    if row.cls == "keep":
        raise ValueError("keep must be promoted (promote_to=...) or explicitly dropped "
                         "(drop=True); it is never silently deleted")
    raise ValueError("install rows stay until the user removes them")


def end_session(session: str, ledger: ScratchLedger | None = None) -> list[Row]:
    """Session close: reclaim this session's scratch. `keep` rows are deliberately left
    live — the monitor flags them as unpromoted, and the agent is asked, never skipped."""
    ledger = ledger if ledger is not None else ScratchLedger()
    transitions: list[Row] = []
    for row in ledger.live_rows():
        if (row.created_by or {}).get("session") != session or row.cls != "scratch":
            continue
        try:
            transitions.append(release(row.id, ledger=ledger))
        except ValueError as exc:
            transitions.append(ledger.append(dataclasses.replace(
                row, state="released", at=_now(), released_at=_now(), note=str(exc))))
    return transitions


def own_path(path: str | Path, purpose: str = "", *, ledger: ScratchLedger | None = None) -> Row:
    """Record an *existing* path as an application's own state — install-class, lifetime
    user, live — without creating or moving anything (card #WZ3K). This is the supported
    answer when the sweep names something that is not agent scratch at all: a guest
    harness's home files, a tool's own install root. Idempotent: a path a ledger row
    already covers comes back unchanged, so the hand-appended rows of the #WZ3K era and
    a repeated `own` both stay one row."""
    resolved = Path(path).expanduser()
    if not (resolved.exists() or resolved.is_symlink()):
        raise ValueError("%s does not exist; `own` records what is already there" % resolved)
    ledger = ledger if ledger is not None else ScratchLedger()
    row = ledger.find(str(resolved))
    if row is not None:
        return row
    purpose = " ".join(str(purpose).split())[:200] or "unspecified"
    size, newest = measure(resolved)
    return ledger.append(Row(id=ledger.next_id(), path=str(resolved), cls="install",
                             purpose=purpose,
                             created_by={"source": "own"},
                             created_at=_iso(newest) if newest else _now(),
                             lifetime="user", state="live", size=size, at=_now(),
                             note="adopted as an application's own state (card #WZ3K)"))


# Card #27AR: the owner mark every Relay runtime directory carries (src/RuntimeDirs.h) — a file
# named `owner` whose first line is this. A pane's /tmp/relay-XXXXXX, a guest's board-bridge
# socket dir and a tests_run scratch dir are Relay's own state, made whenever a pane opens or a
# run starts, so the post-turn sweep would otherwise name them in whichever turn was running.
RELAY_OWNER_FILE = "owner"
RELAY_OWNER_MAGIC = "relay-owner 1"


def _proc_starttime() -> str | None:
    """Field 22 of /proc/self/stat, the boot ticks this process started at (the #9JYK mark's
    pid-reuse guard); None where there is no /proc."""
    try:
        stat = Path("/proc/self/stat").read_text()
    except OSError:
        return None
    fields = stat[stat.rfind(")") + 2:].split()
    return fields[19] if len(fields) > 19 else None


def mark_relay_owned(directory: str | Path) -> None:
    """Write the RuntimeDirs owner mark into a directory Relay itself made, atomically and
    mode 0600, so the sweep knows it for Relay's own. Best effort: a mark that cannot be
    written leaves the directory named by the sweep, never broken."""
    lines = [RELAY_OWNER_MAGIC, "pid %d" % os.getpid()]
    started = _proc_starttime()
    if started:
        lines.append("starttime %s" % started)
    target = Path(directory) / RELAY_OWNER_FILE
    temp = target.with_name(target.name + ".tmp")
    try:
        fd = os.open(temp, os.O_WRONLY | os.O_CREAT | os.O_TRUNC, 0o600)
        with os.fdopen(fd, "w") as handle:
            handle.write("\n".join(lines) + "\n")
        os.replace(temp, target)
    except OSError:
        pass


def is_relay_owned(path: str | Path) -> bool:
    """A real directory (not a symlink) holding an owner mark: Relay's own runtime state."""
    path = Path(path)
    if path.is_symlink() or not path.is_dir():
        return False
    try:
        with open(path / RELAY_OWNER_FILE, encoding="utf-8", errors="replace") as handle:
            return handle.readline().strip() == RELAY_OWNER_MAGIC
    except OSError:
        return False


def _top_level(home: Path | None, tmp: Path | None):
    """(path, stat) for every entry directly in the temp dirs and $HOME — what the sweep scans."""
    bases = {tmp or temp_dir(), system_tmp(), temp_dir(), home or Path.home()}
    for raw in sorted(str(b) for b in bases):
        try:
            children = list(os.scandir(raw))
        except OSError:
            continue
        for child in children:
            try:
                yield child.path, child.stat(follow_symlinks=False)
            except OSError:
                continue


def top_level_entries(*, home: Path | None = None, tmp: Path | None = None) -> frozenset[str]:
    """The names the sweep would scan, as they stand now: taken at turn start and handed back to
    `unledgered_created_since` as `before`, so only what truly appeared is named (card #NQTD)."""
    return frozenset(path for path, _ in _top_level(home, tmp))


def unledgered_created_since(since: float, *, ledger: ScratchLedger | None = None,
                             home: Path | None = None, tmp: Path | None = None,
                             skip: list[str] | tuple[str, ...] = (),
                             before: frozenset[str] | set[str] | None = None) -> list[str]:
    """Top-level entries of the temp dir and $HOME newer than `since`, owned by this user,
    and not covered by any ledger row — the post-turn sweep's list. Top-level only, so the
    scan stays cheap enough to run on every turn end. `skip` names paths to leave out (the
    guest harness's own home files, card #WZ3K); each one matches exactly as a covered row
    does, by being the entry or a parent of it. `before` is `top_level_entries()` from the
    turn's start: an entry already there is not new, however recent its mtime — a directory's
    mtime moves whenever something inside it is added or removed, which is how `~/.cache`
    came to be named (card #NQTD). A directory carrying Relay's owner mark is Relay's own
    runtime state and is never named (card #27AR)."""
    ledger = ledger if ledger is not None else ScratchLedger()
    covered = [Path(r.path) for r in ledger.records().values()]
    covered += [Path(p) for p in skip]
    me = os.getuid() if hasattr(os, "getuid") else None
    found: list[str] = []
    for raw, st in _top_level(home, tmp):
        if st.st_mtime < since or (me is not None and st.st_uid != me):
            continue
        if before is not None and raw in before:
            continue
        path = Path(raw)
        if any(p == path or p in path.parents for p in covered):
            continue
        if is_relay_owned(path):
            continue
        found.append(raw)
    return sorted(found)


def adopt(apply: bool = False, *, ledger: ScratchLedger | None = None,
          home: Path | None = None) -> list[Row]:
    """Write `orphaned` rows for the pre-ledger backlog: the temp-dir entries the #SZHQ
    walk finds, plus the known agent-made $HOME folders. Orphans are listed for the user
    to keep, promote or reclaim — never deleted automatically."""
    ledger = ledger if ledger is not None else ScratchLedger()
    home = home or Path.home()
    targets: list[tuple[Path, str]] = [(path, "scratch")
                                       for path, _kind in candidates(default_roots())]
    for name in KNOWN_HOME_FOLDERS:
        if (home / name).is_dir():
            targets.append((home / name, "scratch"))
    for pattern in KNOWN_HOME_GLOBS:
        targets.extend((p, "scratch") for p in sorted(home.glob(pattern)) if p.is_dir())
    rows: list[Row] = []
    for path, cls in targets:
        if ledger.find(str(path)) is not None:
            continue
        size, newest = measure(path)
        row = Row(id=ledger.next_id(), path=str(path), cls=cls,
                  purpose="adopted: predates the ledger (card #DVV2 migration)",
                  created_by={"guessed": "path and mtime"},
                  created_at=_iso(newest) if newest else "", lifetime="user",
                  state="orphaned", size=size, at=_now(),
                  note="created %s" % _iso(newest) if newest else "age unknown")
        rows.append(row)
        if apply:
            ledger.append(row)
    return rows


def ledger_summary(ledger: ScratchLedger | None = None) -> dict:
    """Totals by class and by session, unpromoted keeps, and orphans — what
    `relay-scratch ledger` and the app's monitor report."""
    ledger = ledger if ledger is not None else ScratchLedger()
    rows = ledger.rows()
    live = [r for r in rows if r.state == "live"]
    by_class: dict[str, dict] = {}
    by_session: dict[str, dict] = {}
    for row in live:
        for bucket, key in ((by_class, row.cls),
                            (by_session, (row.created_by or {}).get("session") or "unknown")):
            entry = bucket.setdefault(key, {"rows": 0, "bytes": 0})
            entry["rows"] += 1
            entry["bytes"] += max(row.size, 0)
    return {
        "ledger": str(ledger.path),
        "rows": [r.to_record() for r in rows],
        "live": len(live),
        "by_class": by_class,
        "by_session": by_session,
        "unpromoted_keep": [r.to_record() for r in live if r.cls == "keep"],
        "orphans": [r.to_record() for r in rows if r.state == "orphaned"],
    }


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(prog="relay-scratch", description=__doc__.split("\n\n")[0])
    parser.add_argument("--root", action="append", default=None,
                        help="scratch root (repeatable; default $TMPDIR/claude-<uid> plus "
                             "loose relay-*/tmp* folders in $TMPDIR)")
    parser.add_argument("--idle-hours", type=float, default=DEFAULT_IDLE_HOURS)
    parser.add_argument("--no-loose", action="store_true",
                        help="leave relay-* and tmp* folders in the temp directory out")
    sub = parser.add_subparsers(dest="verb")
    rep = sub.add_parser("report", help="what is there, biggest first")
    rep.add_argument("--json", action="store_true")
    rep.add_argument("--limit", type=int, default=25)
    g = sub.add_parser("gc", help="remove idle, unused entries (dry run without --apply)")
    g.add_argument("--apply", action="store_true")
    g.add_argument("--force-idle", action="store_true",
                   help="allow --idle-hours below the %gh floor on the default roots"
                        % MIN_IDLE_HOURS)
    c = sub.add_parser("check", help="exit 1 with one line when over budget or low on space")
    c.add_argument("--json", action="store_true")
    n = sub.add_parser("new", help="allocate a ledgered scratch dir (agents and guests)")
    n.add_argument("--class", dest="cls", choices=CLASSES, default="scratch")
    n.add_argument("--purpose", required=True, help="one line: what this is for")
    n.add_argument("--session", default=os.environ.get("RELAY_SESSION", ""))
    n.add_argument("--pane", default=os.environ.get("RELAY_PANE", ""))
    n.add_argument("--model", default=os.environ.get("RELAY_MODEL", ""))
    n.add_argument("--card", default="")
    n.add_argument("--lifetime", default=None,
                   help="task | session | days:N | until-promoted (default by class)")
    n.add_argument("--project", default=None, help="project dir for --class keep")
    o = sub.add_parser("own", help="record an existing path as an application's own state "
                                   "(install-class, lifetime user; nothing is created or moved)")
    o.add_argument("path", help="the existing path to own")
    o.add_argument("--purpose", default="", help="one line: what it is")
    rel = sub.add_parser("release", help="end a ledgered dir: reclaim, promote or drop")
    rel.add_argument("ref", help="row id or path")
    rel.add_argument("--promote-to", default=None, help="move it into the project first (keep)")
    rel.add_argument("--drop", action="store_true", help="delete it explicitly (keep)")
    a = sub.add_parser("adopt", help="ledger the pre-ledger backlog as orphans (dry run; it "
                                      "walks the real default roots, so a big backlog takes minutes)")
    a.add_argument("--apply", action="store_true", help="write the orphaned rows")
    led = sub.add_parser("ledger", help="the ledger by class and session")
    led.add_argument("--json", action="store_true")
    args = parser.parse_args(argv)
    roots = [Path(r) for r in args.root] if args.root else None
    loose = not args.no_loose
    verb = args.verb or "report"

    if verb == "new":
        try:
            row = new_dir(args.cls, args.purpose, session=args.session, pane=args.pane,
                          model=args.model, card=args.card, lifetime=args.lifetime,
                          project=args.project)
        except ValueError as exc:
            print("relay-scratch new: %s" % exc, file=sys.stderr)
            return 2
        print(row.path)
        return 0
    if verb == "own":
        try:
            row = own_path(args.path, args.purpose)
        except ValueError as exc:
            print("relay-scratch own: %s" % exc, file=sys.stderr)
            return 2
        print("%s %s %s" % (row.id, row.state, row.path))
        return 0
    if verb == "release":
        try:
            row = release(args.ref, promote_to=args.promote_to, drop=args.drop)
        except ValueError as exc:
            print("relay-scratch release: %s" % exc, file=sys.stderr)
            return 2
        print("%s %s %s" % (row.id, row.state, row.path))
        return 0
    if verb == "adopt":
        rows = adopt(apply=args.apply)
        for row in rows:
            print("%s %-9s %9s  %s" % (row.id, row.state, human(row.size), row.path))
        print("%s %d orphan%s%s" % ("ledgered" if args.apply else "would ledger (dry run; "
                                    "--apply writes the rows)",
                                    len(rows), "" if len(rows) == 1 else "s",
                                    "; nothing was deleted" if rows else ""))
        return 0
    if verb == "ledger":
        summary = ledger_summary()
        if args.json:
            print(json.dumps(summary))
            return 0
        for row in summary["rows"]:
            if row["state"] in ("live", "released", "orphaned"):
                print("%s %-9s %-9s %9s  %s  %s" % (
                    row["id"], row["state"], row["class"], human(max(row.get("size", 0), 0)),
                    (row.get("created_by") or {}).get("session", "?")[:12], row["purpose"]))
        for label, bucket in (("class", summary["by_class"]), ("session", summary["by_session"])):
            for key, totals in sorted(bucket.items()):
                print("%-7s %-20s %4d rows  %s" % (label, key[:20], totals["rows"],
                                                   human(totals["bytes"])))
        return 0
    if verb == "check":
        verdict = check(roots, args.idle_hours, loose)
        print(json.dumps(asdict(verdict)) if args.json else verdict.line)
        return 0 if verdict.ok else 1
    if verb == "gc":
        try:
            count, freed = gc(roots, args.idle_hours, args.apply, loose,
                              force_idle=getattr(args, "force_idle", False))
        except ValueError as exc:
            print("relay-scratch gc: %s" % exc, file=sys.stderr)
            return 2
        print("%s %s in %d entr%s" % ("freed" if args.apply else "would free (dry run; "
                                      "--apply removes)", human(freed), count,
                                      "y" if count == 1 else "ies"))
        return 0
    entries = report(roots, args.idle_hours, loose)
    if getattr(args, "json", False):
        print(json.dumps([asdict(e) for e in entries]))
        return 0
    total = sum(e.bytes for e in entries)
    reclaim = sum(e.bytes for e in entries if e.removable)
    for e in entries[:args.limit]:
        print("%9s  %6.0fh  %-15s %-3s %s%s" % (human(e.bytes), e.idle_hours, e.kind,
                                               "gc" if e.removable else "", e.path,
                                               "" if e.removable else "  (%s)" % e.why_kept))
    if len(entries) > args.limit:
        print("... %d more" % (len(entries) - args.limit))
    print("total %s in %d entries; %s reclaimable with `relay-scratch gc --apply`"
          % (human(total), len(entries), human(reclaim)))
    if not os.environ.get("RELAY_SCRATCH_ROOTS"):
        summary = ledger_summary()
        if summary["live"]:
            by = ", ".join("%s in %s" % (human(v["bytes"]), k)
                           for k, v in sorted(summary["by_class"].items()))
            print("ledger: %d live rows (%s); `relay-scratch ledger` lists them"
                  % (summary["live"], by))
    return 0


if __name__ == "__main__":
    sys.exit(main())
