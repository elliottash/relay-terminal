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
"""

from __future__ import annotations

import argparse
import json
import os
import shutil
import sys
import tempfile
import time
from dataclasses import asdict, dataclass, field
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
    """`RELAY_SCRATCH_ROOTS` (os.pathsep-separated) or the Claude Code scratch root."""
    env = os.environ.get("RELAY_SCRATCH_ROOTS")
    if env:
        return [Path(p) for p in env.split(os.pathsep) if p]
    return [temp_dir() / uid_suffix()]


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
    found: list[str] = []
    for pid in os.listdir(proc):
        if not pid.isdigit():
            continue
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
    for root in roots:
        if not root.is_dir() or root.is_symlink():
            continue
        for child in sorted(root.iterdir()):
            if child.is_symlink():
                continue
            if child.name in MANAGED:
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
           include_loose: bool = True) -> list[Entry]:
    roots = [Path(r) for r in (roots or default_roots())]
    used = paths_in_use()
    here = os.getcwd()
    now = time.time()
    entries = []
    for path, kind in candidates(roots, include_loose):
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
    entries.sort(key=lambda e: -e.bytes)
    return entries


def gc(roots: list[Path] | None = None, idle_hours: float = DEFAULT_IDLE_HOURS,
       apply: bool = False, include_loose: bool = True, log=print) -> tuple[int, int]:
    """Remove removable entries. Returns (count, bytes). Rechecks each one just before."""
    count = freed = 0
    for entry in report(roots, idle_hours, include_loose):
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
    roots = [Path(r) for r in (roots or default_roots())]
    entries = report(roots, idle_hours, include_loose)
    total = sum(e.bytes for e in entries)
    reclaim = sum(e.bytes for e in entries if e.removable)
    probe = next((r for r in roots if r.exists()), temp_dir())
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
                   disk_bytes=disk.total, roots=[str(r) for r in roots])


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
    c = sub.add_parser("check", help="exit 1 with one line when over budget or low on space")
    c.add_argument("--json", action="store_true")
    args = parser.parse_args(argv)
    roots = [Path(r) for r in args.root] if args.root else None
    loose = not args.no_loose
    verb = args.verb or "report"

    if verb == "check":
        verdict = check(roots, args.idle_hours, loose)
        print(json.dumps(asdict(verdict)) if args.json else verdict.line)
        return 0 if verdict.ok else 1
    if verb == "gc":
        count, freed = gc(roots, args.idle_hours, args.apply, loose)
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
    return 0


if __name__ == "__main__":
    sys.exit(main())
