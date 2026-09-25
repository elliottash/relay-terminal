#!/usr/bin/env python3
"""Land your hunks on `main` from the shared checkout, without reverting another session.

Several Claude sessions edit this one checkout at once. The danger is never the editor, it is
the commit: a commit built from the shared index, or from a base older than the branch tip,
writes other people's files back to older contents and nobody notices, because every working
tree still shows the new code.

This tool commits only the hunks *you* made, against the tip of `main` as it is at the moment
of the swap:

    python3 scripts/land.py begin <session> <paths...>     # before you edit
    ... edit, build and test in the shared checkout, as usual ...
    python3 scripts/land.py commit <session> -m "message"  # lands, then fixes the index

`begin` snapshots the working copies it is given. `commit` takes, for each path,
base = that snapshot, ours = the version at the current tip of `refs/heads/main`,
theirs = the working copy now, and three-way merges them with real temporary files. The
result is "the tip plus your hunks": another session's uncommitted edits were already in the
snapshot, so they are not in your diff and stay uncommitted in the working tree.

A snapshot only tells your hunks from someone else's while nobody else edits the file after
it was taken. On 2026-09-19 a session held a snapshot of `src/Pane.h` for forty minutes, a
second session edited that header in the meantime, and `commit` read the second session's
work as its own and landed half of it. So:

  * When you `begin` a path another live session already claims, this tool records your
    snapshot as a *marker* in that session's data. At their commit, a hunk that is in
    diff(their snapshot, your marker) was already in the tree before you started, so it is
    theirs; anything else appeared afterwards and is **contested**.
  * At your commit, a path claimed by another live session whose snapshot you have no marker
    for (they began before you) has *every* hunk contested: you cannot tell their later edits
    from yours.
  * `commit` always prints a per-path stat. When a path has contested hunks, or its snapshot
    is older than --stale-minutes, it does **not** land: it prints the numbered hunks and a
    digest and exits 4. Rerun with `--confirm <digest>` to land all of it, or with
    `--exclude-hunk <path>:<n>` (repeatable) to leave hunks out of the commit; excluded hunks
    stay uncommitted in the working tree and land with a later commit.

  * Before the swap, a landing that touches C++ or build files builds the EXACT tree it would
    put on the branch, in its own directory under the session's snapshots. Never the working
    tree: that holds every session's uncommitted code, so it can compile while the tree being
    landed cannot -- which is how a green build of code nobody had written reached `main`
    twice on 2026-09-19. A tree that does not compile is not landed (exit 5).

`who` lists the live sessions, what they claim, how old their snapshots are and how to reach
them (`begin --contact <name>`). `repair <sha> --paths ...` lands a commit that takes back
what `<sha>` did to those paths, keeping whatever landed on them afterwards.

What it refuses to do, and why:

  * It never commits from the shared index, and never runs `git add` there. The shared index
    accumulates entries equal to older commits' blobs as `main` moves, so a plain `git commit`
    silently reverts newer commits. `hook install` makes git refuse that too.
  * It never runs `git checkout`, `git stash`, `git reset`, or writes any working-tree file
    (the single exception is `doctor --fix` on a provably stale path, see below). Restoring a
    path from git over someone's live edit destroys work that was never committed anywhere.
  * It merges with real temporary files, never process substitution: `git merge-file` on a
    /dev/fd pipe exits 0 and applies nothing.
  * It reads the tip once per attempt and passes that same sha to `commit-tree -p` and to
    `update-ref`'s old-value argument, so a branch that moved while you were testing makes the
    swap fail instead of overwriting. On a failed swap it recomputes the merge against the new
    tip and tries again.
  * It gates on `git diff --name-only TIP NEW`: the commit must touch exactly your paths.
  * It never commits `.board/bug_intake.txt`, `.board/feature_intake.txt` or any `*.orig`.

Exit codes: 0 fine, 1 usage or environment error, 2 `doctor` found something, 3 a merge
conflict or a swap that could not be completed, 4 the commit is held for review (contested or
stale hunks), 5 the exact tree does not build. On 3, 4 and 5 nothing was changed.
"""

import argparse
import datetime as _dt
import difflib
import hashlib
import json
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

_USER_DIR = "claude-%d" % os.getuid() if hasattr(os, "getuid") else "claude"


def system_tmp():
    """The machine's temp dir, whatever TMPDIR says. Card #BHJZ: Relay points every session's
    TMPDIR at its own scratch dir (#DVV2), so a root under tempfile.gettempdir() gave each
    session a private registry — `who` saw nobody else and no claim was ever contested. The
    land root has to be the one place every session of this user agrees on."""
    if os.name == "posix":
        return "/tmp"
    for name in ("TEMP", "TMP"):
        if os.environ.get(name):
            return os.environ[name]
    return tempfile.gettempdir()


DEFAULT_ROOT = os.path.join(system_tmp(), _USER_DIR, "land")
# Where a session that started before #BHJZ may still hold its snapshots: the root under its
# own TMPDIR. `commit` and `abandon` look there when the shared root has no such session.
LEGACY_ROOT = os.path.join(tempfile.gettempdir(), _USER_DIR, "land")
DEFAULT_BRANCH = "main"
DEFAULT_STALE_MINUTES = 15
# A session that has not run a land.py command for this long is not editing anything any
# more: `who` and `doctor` call it stale and contest detection ignores it.
IDLE_HOURS = 12
# Card #SZHQ: the verify build used to live in each session's own directory and was never
# removed — 255 of them, 153 GB, on 2026-09-24. It is now a small pool of slots shared by every
# session of one repository, each held under a lock for a whole materialise-and-build, so the
# disk it takes is bounded by the slot count instead of the session count. Every session's tree
# is close to the tip, so a slot another session built last stays incremental.
VERIFY_SLOTS = max(1, int(os.environ.get("RELAY_LAND_VERIFY_SLOTS") or 2))
# A session idle this long is not coming back: `gc` removes its snapshots and its registry
# entry. Stale (IDLE_HOURS) sessions younger than this are still listed and kept.
GC_DAYS = float(os.environ.get("RELAY_LAND_GC_DAYS") or 3)
# A pre-#SZHQ per-session verify directory untouched this long is not mid-build: gc drops it.
LEGACY_VERIFY_MINUTES = 30
NEVER_COMMIT = (".board/bug_intake.txt", ".board/feature_intake.txt")
# Variables that would silently redirect a git command at someone else's index or work tree.
GIT_ENV_STRIP = ("GIT_INDEX_FILE", "GIT_DIR", "GIT_WORK_TREE", "GIT_OBJECT_DIRECTORY",
                 "GIT_COMMON_DIR", "GIT_NAMESPACE")
SWAP_ATTEMPTS = 10
# Above this many hunks on one path, the review prints every hunk's header but only the
# bodies that need reading: the contested ones and the ones being left out.
MAX_HUNK_BODIES = 20
# `who` stays readable with twenty sessions in the registry.
WHO_PATHS = 6

HOOK_REFUSAL = "commit through scripts/land.py; the shared index is never committed here"

HOOK = r"""#!/bin/sh
# Installed by scripts/land.py. A commit from the shared index reverts whatever landed on
# main while that index sat there, so git refuses it here. Land with:
#     python3 scripts/land.py begin <session> <paths...>
#     python3 scripts/land.py commit <session> -m "message"
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

# Not `--git-path index`: that one honours GIT_INDEX_FILE and would answer with the
# private index we are trying to tell apart from the shared one.
shared=$(resolve "$(git rev-parse --absolute-git-dir)/index")
if [ -z "${GIT_INDEX_FILE:-}" ]; then
    mine="$shared"
else
    mine=$(resolve "$GIT_INDEX_FILE")
fi

if [ "$mine" = "$shared" ]; then
    echo "REFUSAL_TEXT" >&2
    exit 1
fi
exit 0
""".replace("REFUSAL_TEXT", HOOK_REFUSAL)


class _Missing:
    """Sentinel: "this argument was not given", where None is a meaningful value."""


_MISSING = _Missing()


class Fail(Exception):
    """A clean, explained stop. `code` becomes the process exit code."""

    def __init__(self, message, code=1):
        super().__init__(message)
        self.code = code


# --------------------------------------------------------------------------- git plumbing

def git(repo, *args, env=None, stdin=None, text=True, check=True):
    """Run one git command with a scrubbed environment and an explicit cwd.

    GIT_INDEX_FILE / GIT_DIR / GIT_WORK_TREE are removed unless this call sets them itself,
    so a stray export in the caller's shell cannot point a command at the shared index.
    """
    environ = {k: v for k, v in os.environ.items() if k not in GIT_ENV_STRIP}
    if env:
        environ.update({k: str(v) for k, v in env.items()})
    proc = subprocess.run(["git", *args], cwd=str(repo), env=environ,
                          input=stdin, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                          text=text)
    if check and proc.returncode != 0:
        err = proc.stderr if text else proc.stderr.decode("utf-8", "replace")
        raise Fail("git %s failed (%d): %s" % (" ".join(args[:3]), proc.returncode,
                                               err.strip()))
    return proc


def git_out(repo, *args, **kw):
    return git(repo, *args, **kw).stdout.strip()


def repo_root(start=None):
    start = Path(start or os.getcwd())
    proc = subprocess.run(["git", "rev-parse", "--show-toplevel"], cwd=str(start),
                          stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True,
                          env={k: v for k, v in os.environ.items() if k not in GIT_ENV_STRIP})
    if proc.returncode != 0:
        raise Fail("not inside a git repository: %s" % start)
    return Path(proc.stdout.strip())


def branch_tip(repo, branch):
    out = git(repo, "rev-parse", "--verify", "--quiet", "refs/heads/%s" % branch, check=False)
    sha = out.stdout.strip()
    if not sha:
        raise Fail("no branch refs/heads/%s in %s" % (branch, repo))
    return sha


def head_branch(repo):
    """The branch HEAD points at, or None on a detached HEAD."""
    out = git(repo, "symbolic-ref", "--quiet", "HEAD", check=False).stdout.strip()
    return out[len("refs/heads/"):] if out.startswith("refs/heads/") else None


def tree_entry(repo, rev, path):
    """(mode, sha) for `path` in `rev`, or None when the tree has no such file."""
    out = git(repo, "ls-tree", "-z", rev, "--", path).stdout
    record = out.split("\0")[0]
    if not record or "\t" not in record:
        return None
    meta = record.split("\t", 1)[0].split()
    if len(meta) != 3 or meta[1] != "blob":
        return None
    return meta[0], meta[2]


def blob_bytes(repo, sha):
    return git(repo, "cat-file", "blob", sha, text=False).stdout


def rev_bytes(repo, rev, path):
    """The bytes of `path` at `rev`, or None when it is not there."""
    entry = tree_entry(repo, rev, path)
    return None if entry is None else blob_bytes(repo, entry[1])


def hash_blob(repo, data):
    return git(repo, "hash-object", "-w", "--stdin", stdin=data, text=False
               ).stdout.decode().strip()


def work_bytes(repo, path):
    """The working copy of `path` as bytes, or None when it is absent."""
    full = Path(repo) / path
    if os.path.islink(full):
        return os.readlink(full).encode("utf-8")
    if not os.path.isfile(full):
        return None
    try:
        return full.read_bytes()
    except OSError:
        return None


def work_mode(repo, path, fallback="100644"):
    full = Path(repo) / path
    try:
        info = full.lstat()
    except OSError:
        return fallback
    if os.path.islink(full):
        return "120000"
    return "100755" if info.st_mode & 0o111 else "100644"


def is_ignored(repo, path):
    return git(repo, "check-ignore", "-q", "--", path, check=False).returncode == 0


def norm_path(repo, given):
    """A repo-relative, forward-slash path for something typed on the command line."""
    candidate = Path(given)
    if not candidate.is_absolute():
        candidate = Path(os.getcwd()) / candidate
    try:
        rel = os.path.relpath(os.path.normpath(str(candidate)), str(repo))
    except ValueError:
        raise Fail("path outside the repository: %s" % given)
    if rel.startswith(".."):
        raise Fail("path outside the repository: %s" % given)
    return rel.replace(os.sep, "/")


def excluded(path):
    return path in NEVER_COMMIT or path.endswith(".orig")


# --------------------------------------------------------------------------- merging

def is_binary(data):
    return data is not None and b"\0" in data[:8000]


def merge3(base, ours, theirs, labels):
    """Three-way merge through real temp files. Returns (merged_bytes, conflicted).

    `git merge-file` cannot read /dev/fd pipes: given process substitution it exits 0 and
    applies nothing, which looks exactly like a clean merge. So everything here is a real
    file in a real temp directory.
    """
    if is_binary(base) or is_binary(ours) or is_binary(theirs):
        # No line-based merge is meaningful. Unchanged on one side is still decidable.
        if base == ours:
            return theirs, False
        if base == theirs:
            return ours, False
        return ours, True
    tmp = tempfile.mkdtemp(prefix="land-merge-")
    try:
        mine = Path(tmp) / "ours"
        orig = Path(tmp) / "base"
        other = Path(tmp) / "theirs"
        mine.write_bytes(ours)
        orig.write_bytes(base)
        other.write_bytes(theirs)
        proc = subprocess.run(
            ["git", "merge-file", "-L", labels[0], "-L", labels[1], "-L", labels[2],
             str(mine), str(orig), str(other)],
            stdout=subprocess.PIPE, stderr=subprocess.PIPE, cwd=tmp,
            env={k: v for k, v in os.environ.items() if k not in GIT_ENV_STRIP})
        if proc.returncode < 0 or proc.returncode > 127:
            raise Fail("git merge-file failed: %s" % proc.stderr.decode("utf-8", "replace"))
        return mine.read_bytes(), proc.returncode != 0
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


def unified(old, new, path):
    def lines(data):
        if data is None:
            return []
        return data.decode("utf-8", "replace").splitlines(keepends=True)
    return "".join(difflib.unified_diff(lines(old), lines(new),
                                        fromfile="a/%s" % path, tofile="b/%s" % path))


# --------------------------------------------------------------------------- hunks

def lines_of(data):
    """Bytes to a list of lines that turns back into exactly those bytes.

    `surrogateescape`, not `replace`: a file that is not valid UTF-8 must still round-trip,
    because these lines are spliced back together into the blob that gets committed.
    """
    if data is None:
        return []
    return data.decode("utf-8", "surrogateescape").splitlines(keepends=True)


def bytes_of(lines):
    return "".join(lines).encode("utf-8", "surrogateescape")


class Hunk:
    """One numbered change between a snapshot and a later version of the same file.

    `i1:i2` is the range it replaces in the snapshot's lines (three lines of context each
    side, as in a unified diff), `new` the lines that replace them. Two diffs taken against
    the *same* snapshot describe the same change with the same `key`, which is how a hunk is
    recognised as one that was already in the tree when another session began.
    """

    def __init__(self, i1, i2, j1, j2, base, new, minus, plus, whole=False):
        self.i1, self.i2, self.j1, self.j2 = i1, i2, j1, j2
        self.base, self.new = base, new
        self.minus, self.plus = minus, plus
        self.whole = whole

    @property
    def key(self):
        return (self.i1, self.i2, tuple(self.base), tuple(self.new))

    def header(self):
        if self.whole:
            return "@@ whole file @@"
        return "@@ -%d,%d +%d,%d @@" % (self.i1 + 1, len(self.base),
                                        self.j1 + 1, len(self.new))

    def body(self):
        if self.whole:
            return ["(binary file, or a whole-file add or delete: it cannot be split)\n"]
        out = []
        matcher = difflib.SequenceMatcher(None, self.base, self.new, autojunk=False)
        for tag, i1, i2, j1, j2 in matcher.get_opcodes():
            if tag == "equal":
                out.extend(" " + line for line in self.base[i1:i2])
            else:
                out.extend("-" + line for line in self.base[i1:i2])
                out.extend("+" + line for line in self.new[j1:j2])
        return [line if line.endswith("\n") else line + "\n" for line in out]


def split_hunks(base, new, context=3):
    hunks = []
    matcher = difflib.SequenceMatcher(None, base, new, autojunk=False)
    for group in matcher.get_grouped_opcodes(context):
        i1, i2 = group[0][1], group[-1][2]
        j1, j2 = group[0][3], group[-1][4]
        minus = sum(op[2] - op[1] for op in group if op[0] != "equal")
        plus = sum(op[4] - op[3] for op in group if op[0] != "equal")
        hunks.append(Hunk(i1, i2, j1, j2, base[i1:i2], new[j1:j2], minus, plus))
    return hunks


def path_hunks(snapshot, working):
    """The numbered hunks between a snapshot and the working copy of one path."""
    if snapshot == working:
        return []
    if working is None or is_binary(snapshot) or is_binary(working):
        base = [] if is_binary(snapshot) else lines_of(snapshot)
        return [Hunk(0, len(base), 0, 0, base, [], len(base), 0, whole=True)]
    return split_hunks(lines_of(snapshot), lines_of(working))


def apply_hunks(snapshot, working, hunks, selected):
    """The snapshot with only the selected (1-based) hunks applied to it."""
    if not hunks:
        return snapshot
    if hunks[0].whole:
        return working if 1 in selected else snapshot
    base = lines_of(snapshot)
    out, pos = [], 0
    for number, hunk in enumerate(hunks, 1):
        if number not in selected:
            continue
        out.extend(base[pos:hunk.i1])
        out.extend(hunk.new)
        pos = hunk.i2
    out.extend(base[pos:])
    return bytes_of(out)


def content_digest(tip, plans):
    """A short hash over the tip and the exact bytes each path would be committed with.

    The point is that it stops matching the moment anything that feeds the commit moves: a
    later edit in the working tree, a hunk excluded or put back, or `main` advancing.
    """
    digest = hashlib.sha256()
    digest.update(tip.encode("ascii"))
    for path in sorted(plans):
        content, mode = plans[path]
        digest.update(b"\0")
        digest.update(path.encode("utf-8", "surrogateescape"))
        digest.update(b"\0")
        if content is None:
            digest.update(b"deleted")
        else:
            digest.update(("%s " % mode).encode("ascii"))
            digest.update(hashlib.sha256(content).digest())
    return digest.hexdigest()[:12]


# --------------------------------------------------------------------------- registry

def registry_file(root):
    return Path(root) / "registry.json"


def read_registry(root):
    try:
        data = json.loads(registry_file(root).read_text(encoding="utf-8"))
    except (OSError, ValueError):
        return {"version": 1, "sessions": {}}
    if not isinstance(data, dict) or not isinstance(data.get("sessions"), dict):
        return {"version": 1, "sessions": {}}
    return data


def write_registry(root, data):
    """Atomic: a temp file in the same directory, then os.replace. The only shared state."""
    Path(root).mkdir(parents=True, exist_ok=True)
    handle, tmp = tempfile.mkstemp(dir=str(root), prefix=".registry-")
    try:
        with os.fdopen(handle, "w", encoding="utf-8") as fh:
            json.dump(data, fh, indent=2, sort_keys=True)
            fh.write("\n")
        os.replace(tmp, str(registry_file(root)))
    except BaseException:
        try:
            os.unlink(tmp)
        except OSError:
            pass
        raise


def edit_registry(root, mutate):
    data = read_registry(root)
    mutate(data)
    write_registry(root, data)
    return data


def registered_sessions(root, skip=None):
    """Registered sessions whose snapshot directory still exists."""
    out = {}
    for name, entry in read_registry(root).get("sessions", {}).items():
        if name == skip or not isinstance(entry, dict):
            continue
        if (Path(root) / name).is_dir():
            out[name] = entry
    return out


def session_idle_minutes(entry):
    return age_minutes(entry.get("updated") or entry.get("started"))


def is_idle(entry):
    idle = session_idle_minutes(entry)
    return idle is not None and idle > IDLE_HOURS * 60


def live_sessions(root, skip=None):
    """Registered sessions that have run a land.py command in the last IDLE_HOURS."""
    return {name: entry for name, entry in registered_sessions(root, skip=skip).items()
            if not is_idle(entry)}


def describe_session(name, entry):
    contact = (entry.get("contact") or "").strip()
    return "session %s%s" % (name, " (contact: %s)" % contact if contact else
                             " (no --contact given; you cannot reach it)")


def claimants(root, session, path):
    """Live sessions other than `session` that still claim `path`, oldest begin first."""
    found = [(name, entry) for name, entry in live_sessions(root, skip=session).items()
             if path in (entry.get("claims") or [])]
    found.sort(key=lambda item: item[1].get("started") or "")
    return found


def warn_overlaps(root, session, paths, log):
    clashes = []
    for other, entry in sorted(live_sessions(root, skip=session).items()):
        shared = sorted(set(entry.get("claims") or []) & set(paths))
        if shared:
            clashes.append((other, entry, shared))
    for other, entry, shared in clashes:
        log("warning: %s has also claimed %s — hunks that appeared after it began are "
            "contested and will be held for review" % (describe_session(other, entry),
                                                       ", ".join(shared)))
    return clashes


# --------------------------------------------------------------------------- markers

def markers_file(root, session):
    return session_dir(root, session) / "markers.json"


def read_markers(root, session):
    try:
        data = json.loads(markers_file(root, session).read_text(encoding="utf-8"))
    except (OSError, ValueError):
        return {}
    return data if isinstance(data, dict) else {}


def marker_file(root, session, other, path):
    return session_dir(root, session) / "markers" / other / path


def marker_bytes(root, session, other, path):
    """What `path` held when `other` began, as recorded in `session`'s data, or None."""
    record = ((read_markers(root, session).get(other) or {}).get("paths") or {}).get(path)
    if not isinstance(record, dict):
        return None
    if not record.get("existed"):
        return b""
    dest = marker_file(root, session, other, path)
    return dest.read_bytes() if dest.exists() else None


def record_marker(root, owner, other, path, data, contact):
    """Leave `other`'s view of `path` in `owner`'s session data.

    `owner` is a session that claimed the path first and is still editing it. At its commit
    this is the line between "already in the tree when `other` started" (its own work) and
    "appeared afterwards" (contested).
    """
    if not session_dir(root, owner).is_dir():
        return
    dest = marker_file(root, owner, other, path)
    dest.parent.mkdir(parents=True, exist_ok=True)
    if data is None:
        if dest.exists():
            dest.unlink()
    else:
        dest.write_bytes(data)
    markers = read_markers(root, owner)
    entry = markers.setdefault(other, {})
    entry["at"] = now()
    entry["contact"] = contact or entry.get("contact") or ""
    entry.setdefault("paths", {})[path] = {"existed": data is not None}
    write_json(markers_file(root, owner), markers)


# --------------------------------------------------------------------------- session state

def session_dir(root, session):
    if not session or "/" in session or session.startswith("."):
        raise Fail("session name must be a short plain name, not %r" % session)
    return Path(root) / session


def snap_path(root, session, path):
    return session_dir(root, session) / "snap" / path


def meta_file(root, session):
    return session_dir(root, session) / "meta.json"


def read_meta(root, session):
    path = meta_file(root, session)
    if not path.exists():
        raise Fail("no session %r under %s — run `land.py begin %s <paths...>` first"
                   % (session, root, session or "<session>"))
    return json.loads(path.read_text(encoding="utf-8"))


def write_json(path, data):
    path.parent.mkdir(parents=True, exist_ok=True)
    handle, tmp = tempfile.mkstemp(dir=str(path.parent), prefix=".json-")
    try:
        with os.fdopen(handle, "w", encoding="utf-8") as fh:
            json.dump(data, fh, indent=2, sort_keys=True)
            fh.write("\n")
        os.replace(tmp, str(path))
    except BaseException:
        try:
            os.unlink(tmp)
        except OSError:
            pass
        raise


def write_meta(root, session, meta):
    write_json(meta_file(root, session), meta)


def take_snapshot(repo, root, session, path, meta, tip, content=_MISSING, base_rev=None):
    """Snapshot `path` and record whether the tip tracked it.

    `content` defaults to the working copy. It is given explicitly in two places: `begin
    --base <rev>`, which snapshots that revision instead, and the end of a commit that left
    some hunks out, where the new snapshot is "the old snapshot plus what was landed" so the
    hunks that were left out are still later-than-the-snapshot next time.
    """
    data = work_bytes(repo, path) if content is _MISSING else content
    dest = snap_path(root, session, path)
    dest.parent.mkdir(parents=True, exist_ok=True)
    if data is None:
        if dest.exists():
            dest.unlink()
    else:
        dest.write_bytes(data)
    record = {
        "existed": data is not None,
        "mode": work_mode(repo, path),
        "tracked_at_begin": tree_entry(repo, tip, path) is not None,
        "at": now(),
    }
    if base_rev:
        record["base_rev"] = base_rev
    meta.setdefault("paths", {})[path] = record
    return data


def snapshot_bytes(root, session, path, record):
    if not record.get("existed"):
        return None
    dest = snap_path(root, session, path)
    return dest.read_bytes() if dest.exists() else None


def now():
    return _dt.datetime.now().isoformat(timespec="seconds")


def age_minutes(stamp):
    """How long ago an ISO timestamp written by `now()` was, in minutes, or None."""
    try:
        when = _dt.datetime.fromisoformat(stamp)
    except (TypeError, ValueError):
        return None
    return max(0.0, (_dt.datetime.now() - when).total_seconds() / 60.0)


def human_age(minutes):
    if minutes is None:
        return "age unknown"
    if minutes < 60:
        return "%dm" % int(minutes)
    if minutes < 60 * 24:
        return "%dh%02dm" % (int(minutes) // 60, int(minutes) % 60)
    return "%dd%02dh" % (int(minutes) // 1440, (int(minutes) % 1440) // 60)


def log_line(root, message):
    Path(root).mkdir(parents=True, exist_ok=True)
    with (Path(root) / "land.log").open("a", encoding="utf-8") as fh:
        fh.write("%s %s\n" % (now(), message))


# --------------------------------------------------------------------------- hook

def hook_file(repo):
    git_dir = git_out(repo, "rev-parse", "--git-dir")
    base = Path(git_dir)
    if not base.is_absolute():
        base = Path(repo) / base
    return base / "hooks" / "pre-commit"


def install_hook(repo, log, force=False):
    target = hook_file(repo)
    target.parent.mkdir(parents=True, exist_ok=True)
    if target.exists() and not force:
        current = target.read_text(encoding="utf-8", errors="replace")
        if HOOK_REFUSAL in current:
            return False
        raise Fail("a different pre-commit hook is already installed at %s; move it aside or "
                   "pass --force" % target)
    target.write_text(HOOK, encoding="utf-8")
    os.chmod(str(target), 0o755)
    log("installed pre-commit hook at %s" % target)
    return True


# --------------------------------------------------------------------------- begin

def cmd_begin(args, log):
    repo = repo_root()
    root = Path(args.root)
    tip = branch_tip(repo, args.branch)

    base_rev = None
    if args.from_head:
        if args.base:
            raise Fail("--base and --from-head are the same argument; pass one")
        base_rev = tip
    elif args.base:
        base_rev = git_out(repo, "rev-parse", "--verify", "%s^{commit}" % args.base)

    paths = []
    for given in args.paths:
        path = norm_path(repo, given)
        if excluded(path):
            log("skipping %s: never committed by this tool" % path)
            continue
        if is_ignored(repo, path):
            log("skipping %s: ignored by .gitignore (build output is never landed)" % path)
            continue
        if path not in paths:
            paths.append(path)
    if not paths:
        raise Fail("nothing to claim")

    auto_gc(root, log)
    directory = session_dir(root, args.session)
    directory.mkdir(parents=True, exist_ok=True)
    try:
        meta = read_meta(root, args.session)
    except Fail:
        meta = {"session": args.session, "repo": str(repo), "branch": args.branch,
                "started": now(), "paths": {}}
    meta["repo"] = str(repo)
    meta["branch"] = args.branch
    meta["updated"] = now()
    if args.contact:
        meta["contact"] = args.contact

    # Who already holds these paths, read before our own claim goes into the registry.
    holders = {path: claimants(root, args.session, path) for path in paths}

    for path in paths:
        content = rev_bytes(repo, base_rev, path) if base_rev else _MISSING
        take_snapshot(repo, root, args.session, path, meta, tip, content=content,
                      base_rev=base_rev)
    write_meta(root, args.session, meta)

    def mutate(data):
        entry = data["sessions"].setdefault(args.session, {})
        entry["repo"] = str(repo)
        entry["branch"] = args.branch
        entry["updated"] = now()
        entry.setdefault("started", meta["started"])
        if args.contact:
            entry["contact"] = args.contact
        claims = set(entry.get("claims") or [])
        claims.update(paths)
        entry["claims"] = sorted(claims)
    edit_registry(root, mutate)

    install_hook(repo, log)

    # Every session that already claims one of these paths gets our snapshot as a marker, so
    # that at *their* commit the tool can say which hunks predate us.
    for path, others in holders.items():
        for other, _entry in others:
            record_marker(root, other, args.session, path,
                          work_bytes(repo, path), args.contact)

    log("session %s claims %d path(s) at tip %s" % (args.session, len(paths), tip[:12]))
    for path in paths:
        record = meta["paths"][path]
        if base_rev:
            state = "snapshot from %s" % base_rev[:12]
        else:
            state = "new file" if not record["existed"] else (
                "snapshot taken" if record["tracked_at_begin"]
                else "untracked, snapshot taken")
        log("  %s (%s)" % (path, state))
    log("snapshots: %s" % (directory / "snap"))
    if base_rev:
        log("--base was given, so your hunks are diff(%s:<path>, working copy) — which may "
            "include another session's uncommitted edits. Every commit of these paths goes "
            "through the --confirm review; read the hunks there." % base_rev[:12])
    for path, others in sorted(holders.items()):
        for other, entry in others:
            log("warning: %s already holds %s (snapshot %s old). Your edits from now on are "
                "contested there and here until one of you lands; talk to them before you "
                "change the same function." % (describe_session(other, entry), path,
                                               human_age(claim_age(root, other, path))))

    log("edit, build and test in %s as usual, then:" % repo)
    log("  python3 scripts/land.py commit %s -m \"message\"" % args.session)
    return 0


def claim_age(root, session, path):
    """How old `session`'s snapshot of `path` is, in minutes, or None."""
    try:
        meta = read_meta(root, session)
    except (Fail, ValueError):
        return None
    record = (meta.get("paths") or {}).get(path) or {}
    return age_minutes(record.get("at") or meta.get("started"))


# --------------------------------------------------------------------------- hunk selection

def parse_numbers(spec, path, flag):
    """"3,7-9" -> {3, 7, 8, 9}."""
    numbers = set()
    for piece in spec.split(","):
        piece = piece.strip()
        if not piece:
            continue
        lo, sep, hi = piece.partition("-")
        try:
            if sep:
                start, stop = int(lo), int(hi)
            else:
                start = stop = int(piece)
        except ValueError:
            raise Fail("%s %s:%s — %r is not a hunk number or a range like 7-9"
                       % (flag, path, spec, piece))
        if start < 1 or stop < start:
            raise Fail("%s %s:%s — %r is not a range of hunk numbers" % (flag, path, spec,
                                                                        piece))
        numbers.update(range(start, stop + 1))
    if not numbers:
        raise Fail("%s %s: no hunk numbers given" % (flag, path))
    return numbers


def parse_selection(repo, values, flag):
    """['src/Pane.h:2,5-7', ...] -> {'src/Pane.h': {2, 5, 6, 7}}, accumulating."""
    out = {}
    for value in values or []:
        if ":" not in value:
            raise Fail("%s wants <path>:<hunks>, for example `%s src/Pane.h:2,5-7` (got %r)"
                       % (flag, flag, value))
        given, _, spec = value.rpartition(":")
        path = norm_path(repo, given)
        out.setdefault(path, set()).update(parse_numbers(spec, path, flag))
    return out


def contested_hunks(root, session, path, snapshot, hunks, holders, forced=False):
    """The hunk numbers on `path` that may be another live session's work.

    A hunk that is also in diff(our snapshot, the marker another session left when it began)
    was in the tree before that session started, so it is ours. Everything else appeared
    afterwards, and nothing in the file can say which of us typed it. When a holder began
    before we did we have no marker for it at all, and then every hunk is contested.
    """
    numbers = set(range(1, len(hunks) + 1))
    if not hunks or not holders:
        return set()
    if forced or hunks[0].whole:
        return numbers
    base = lines_of(snapshot)
    seen = {}
    for other, _entry in holders:
        marker = marker_bytes(root, session, other, path)
        if marker is None or is_binary(marker):
            return numbers
        theirs = {hunk.key for hunk in split_hunks(base, lines_of(marker))}
        for number, hunk in enumerate(hunks, 1):
            if hunk.key in theirs:
                seen[number] = seen.get(number, 0) + 1
    return {n for n in numbers if seen.get(n, 0) < len(holders)}


class PathInfo:
    """Everything `commit` knows about one path, for the stat line and the review."""

    def __init__(self, path):
        self.path = path
        self.whole_path = False
        self.snapshot = None
        self.hunks = []
        self.selected = set()
        self.excluded = set()
        self.contested = set()
        self.holders = []
        self.age = None
        self.stale = False
        self.from_base = None

    @property
    def landing(self):
        return [hunk for number, hunk in enumerate(self.hunks, 1) if number in self.selected]

    def reasons(self):
        why = []
        if self.contested:
            why.append("%d of %d hunks contested" % (len(self.contested), len(self.hunks)))
        if self.stale:
            why.append("snapshot %s old" % human_age(self.age))
        if self.from_base:
            why.append("snapshot taken from %s, not from your own edits" % self.from_base[:12])
        return why

    def stat(self):
        if self.whole_path:
            return "%s: the whole working copy (--whole)" % self.path
        landing = self.landing
        bits = ["%d hunk%s" % (len(landing), "" if len(landing) == 1 else "s"),
                "+%d -%d" % (sum(h.plus for h in landing), sum(h.minus for h in landing)),
                "snapshot %s old" % human_age(self.age)]
        if self.excluded:
            bits.append("%d left out" % len(self.excluded))
        if self.contested:
            bits.append("%d CONTESTED" % len(self.contested))
        if self.from_base:
            bits.append("snapshot from %s" % self.from_base[:12])
        if self.holders:
            bits.append("also claimed by %s" % ", ".join(
                describe_session(name, entry) for name, entry in self.holders))
        return "%s: %s" % (self.path, "; ".join(bits))


def print_review(log, root, branch, infos, plans, held, digest, unselected=()):
    log("")
    log("HELD: nothing was committed, nothing in the working tree was touched.")
    for path in held:
        info = infos[path]
        log("")
        log("%s — %s" % (path, "; ".join(info.reasons()) or "review"))
        for name, entry in info.holders:
            log("  also claimed by %s, its snapshot %s old"
                % (describe_session(name, entry), human_age(claim_age(root, name, path))))
        # Every hunk is numbered, because those numbers are what --exclude-hunk takes. A
        # large landing only prints the bodies that need reading.
        bodies = len(info.hunks) <= MAX_HUNK_BODIES
        for number, hunk in enumerate(info.hunks, 1):
            marks = ["CONTESTED" if number in info.contested else "yours"]
            if number not in info.selected:
                marks.append("LEFT OUT by --exclude-hunk/--only-hunk")
            log("  hunk %d of %d  +%d -%d  %s  %s"
                % (number, len(info.hunks), hunk.plus, hunk.minus, hunk.header(),
                   ", ".join(marks)))
            if bodies or number in info.contested or number not in info.selected:
                sys.stdout.write("".join("    " + line for line in hunk.body()))
        if not bodies:
            log("  (%d hunks, so only the contested and left-out ones are printed in full; "
                "`--dry-run` prints the whole merged diff)" % len(info.hunks))
    log("")
    log("what it would land (digest %s):" % digest)
    for path in sorted(plans):
        log("  %s" % infos[path].stat())
    log("")
    if unselected:
        log("")
        # --only-hunk reads as narrowing the whole landing, and a session that held a
        # digest, added a selection for one path and saw the same digest again read that
        # as the command being honoured (#DT6Z records how that went). Say the opposite
        # outright while the reader is looking at the path list.
        log("  still wholly in this commit — no selection named them: %s"
            % ", ".join(unselected))
    log("")
    log("  land all of it:  rerun with --confirm %s" % digest)
    log("                    (the digest pins the tree printed above, not the flags you")
    log("                     reached it with)")
    log("  leave hunks out: --exclude-hunk <path>:<n>[,<n>-<m>]   (repeatable)")
    log("  or keep a few:   --only-hunk <path>:<n>[,<n>-<m>]      (those hunks of that")
    log("                     one path only — other paths land whole, as listed above)")
    log("Hunks left out are neither committed nor touched: they stay in the working tree and a "
        "later commit picks them up. Either flag prints a new digest.")
    log("The digest covers the tip of %s and the exact bytes of every path, so an edit in the "
        "working tree, a different selection, or %s moving makes it stop matching and you are "
        "asked again." % (branch, branch))
    log("`python3 scripts/land.py who` says who else holds these paths and how to reach them.")


# --------------------------------------------------------------------------- verify

# Paths whose exact landed content decides whether `main` still compiles. On 2026-09-19 two
# commits landed hunks that only built because the other half of somebody else's change was
# sitting in the working tree; the tree that went onto the branch did not compile at all.
CXX_SUFFIXES = (".cpp", ".cc", ".cxx", ".c", ".h", ".hpp", ".hh", ".inl", ".ipp")


def is_cxx_path(path):
    name = path.rsplit("/", 1)[-1]
    if name == "CMakeLists.txt" or path.endswith(".cmake"):
        return True
    return path.startswith(("src/", "engine/", "tests/")) and path.endswith(CXX_SUFFIXES)


def materialise_tree(repo, tree, dest, manifest_file):
    """Write `tree` into `dest`, touching only the files whose blob changed.

    This is the tool's own check, not a place to work: nobody edits here. Files that did not
    change keep their mtime, so the build in `dest`'s sibling build directory stays
    incremental across commits.
    """
    wanted = {}
    for record in git(repo, "ls-tree", "-r", "-z", tree).stdout.split("\0"):
        if not record:
            continue
        meta, _, path = record.partition("\t")
        bits = meta.split()
        if len(bits) == 3 and bits[1] == "blob":
            wanted[path] = [bits[0], bits[2]]
    try:
        have = json.loads(manifest_file.read_text(encoding="utf-8"))
    except (OSError, ValueError):
        have = {}
    if not isinstance(have, dict):
        have = {}

    written = 0
    for path, (mode, sha) in sorted(wanted.items()):
        target = dest / path
        if have.get(path) == [mode, sha] and (target.exists() or target.is_symlink()):
            continue
        target.parent.mkdir(parents=True, exist_ok=True)
        if target.is_symlink():
            target.unlink()
        if mode == "120000":
            os.symlink(blob_bytes(repo, sha).decode("utf-8", "surrogateescape"), str(target))
        else:
            target.write_bytes(blob_bytes(repo, sha))
            os.chmod(str(target), 0o755 if mode == "100755" else 0o644)
        written += 1
    for path in sorted(set(have) - set(wanted)):
        try:
            (dest / path).unlink()
        except OSError:
            pass
    write_json(manifest_file, wanted)
    return written


def first_errors(output, limit=25):
    lines = output.splitlines()
    picked = [line for line in lines if "error" in line.lower()][:limit]
    return picked or lines[-limit:]


def py_compile_landed(plans):
    """Byte-compile the exact bytes of every .py file being landed. Cheap, so always on."""
    targets = {path: content for path, (content, _mode) in plans.items()
               if path.endswith(".py") and content is not None}
    if not targets:
        return None
    tmp = tempfile.mkdtemp(prefix="land-py-")
    try:
        names = []
        for path, content in sorted(targets.items()):
            dest = Path(tmp) / path.replace("/", "__")
            dest.write_bytes(content)
            names.append(str(dest))
        proc = subprocess.run([sys.executable, "-m", "py_compile", *names],
                              stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True,
                              cwd=tmp)
        return None if proc.returncode == 0 else proc.stdout
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


def relay_build_jobs(log, limit_bytes=None):
    """Compile jobs for the verify build, capped by memory like scripts/relay-build.

    The verify build runs wherever land.py runs, including inside an agent pane's
    systemd scope, whose default 8 parallel jobs OOM-killed a pane mid-build
    (card #04EC) -- so the wrapper's cap is loaded rather than copied, and the two
    cannot drift apart. `limit_bytes` overrides the detected limit (tests).
    """
    try:
        import types
        script = Path(__file__).resolve().parent / "relay-build"
        module = types.ModuleType("relay_build_jobs")
        module.__file__ = str(script)
        # exec rather than the import system: the file has no .py extension, so
        # SourceFileLoader's spec path refuses the name (load_module() would work
        # but is deprecated); the wrapper has no import-time side effects anyway.
        exec(compile(script.read_text(), str(script), "exec"), module.__dict__)
        return module.jobs_for_environment(
            os.environ, note=lambda message: log("verify: %s" % message), limit=limit_bytes)
    except Exception as exc:  # the cap is a guard, never a gate
        log("verify: memory job cap unavailable (%s); using RELAY_JOBS or 8" % exc)
        return os.environ.get("RELAY_JOBS") or "8"


def run_verify(repo, root, session, tree, args, log):
    """Build the exact tree this commit would put on the branch.

    Returns (failed step, output) or (None, None). The working tree is never read: that is
    the whole point, because it holds everyone's uncommitted code and proves nothing.
    """
    with verify_slot(repo, root, log) as base:
        return _run_verify_in(repo, base, tree, args, log)


def slot_prefix(repo):
    """Slots are per repository: two projects landing through one root never share a build."""
    return "%s-%s" % (Path(str(repo)).name or "repo",
                      hashlib.sha1(str(repo).encode("utf-8")).hexdigest()[:8])


class verify_slot:
    """Hold one of VERIFY_SLOTS build slots for this repository, waiting if all are busy.

    The lock is an flock on `<slot>.lock`, so a session that dies mid-build frees its slot.
    Without fcntl (Windows) the first slot is used without a lock.
    """

    def __init__(self, repo, root, log):
        self.base = Path(root) / "verify-slots"
        self.prefix = slot_prefix(repo)
        self.log = log
        self.handle = None

    def __enter__(self):
        self.base.mkdir(parents=True, exist_ok=True)
        slots = [self.base / ("%s-%d" % (self.prefix, n)) for n in range(VERIFY_SLOTS)]
        try:
            import fcntl
        except ImportError:
            return slots[0]
        # The most recently used free slot first: it is the one most likely to be warm.
        order = sorted(slots, key=lambda s: -(s / "manifest.json").stat().st_mtime
                       if (s / "manifest.json").exists() else 0)
        for slot in order:
            handle = open(str(slot) + ".lock", "a+")
            try:
                fcntl.flock(handle, fcntl.LOCK_EX | fcntl.LOCK_NB)
            except OSError:
                handle.close()
                continue
            self.handle = handle
            return slot
        slot = order[0]
        self.log("verify: all %d build slot(s) busy; waiting for %s" % (len(slots), slot.name))
        self.handle = open(str(slot) + ".lock", "a+")
        fcntl.flock(self.handle, fcntl.LOCK_EX)
        return slot

    def __exit__(self, *exc):
        if self.handle is not None:
            self.handle.close()
        return False


def _run_verify_in(repo, base, tree, args, log):
    src, build = base / "src", base / "build"
    src.mkdir(parents=True, exist_ok=True)
    build.mkdir(parents=True, exist_ok=True)
    refreshed = materialise_tree(repo, tree, src, base / "manifest.json")
    log("verify: tree %s materialised in %s (%d file(s) refreshed)"
        % (tree[:12], src, refreshed))

    jobs = relay_build_jobs(log)
    steps = []
    if args.verify_cmd:
        steps.append(("verify command", ["sh", "-c", args.verify_cmd]))
    else:
        if not (build / "CMakeCache.txt").exists():
            steps.append(("cmake configure", ["cmake", "-S", str(src), "-B", str(build)]))
        target = [] if args.verify_tests else ["--target", args.verify_target]
        steps.append(("compile", ["cmake", "--build", str(build), "--parallel", jobs]
                      + target))
        if args.verify_tests:
            steps.append(("ctest -R %s" % args.verify_tests,
                          ["ctest", "--test-dir", str(build), "-R", args.verify_tests,
                           "--output-on-failure"]))
    env = {k: v for k, v in os.environ.items() if k not in GIT_ENV_STRIP}
    env["VERIFY_BUILD"] = str(build)
    for label, command in steps:
        log("verify: %s" % " ".join(command))
        proc = subprocess.run(command, cwd=str(src), env=env, stdout=subprocess.PIPE,
                              stderr=subprocess.STDOUT, text=True)
        if proc.returncode != 0:
            return label, proc.stdout
    log("verify: the exact tree builds")
    return None, None


# --------------------------------------------------------------------------- commit

class Conflict(Exception):
    def __init__(self, paths):
        super().__init__(", ".join(paths))
        self.paths = paths


def plan_path(repo, root, session, path, record, tip, whole, theirs=_MISSING):
    """Decide the bytes to commit for one path. Returns (content, mode) or None to skip.

    content is None for a deletion. Raises Conflict for that one path. `theirs` overrides the
    working copy as the right-hand side of the merge: it is the snapshot plus only the hunks
    that were selected, when some of them were excluded from this commit.
    """
    working = work_bytes(repo, path) if theirs is _MISSING else theirs
    tip_entry = tree_entry(repo, tip, path)
    tip_data = None if tip_entry is None else blob_bytes(repo, tip_entry[1])

    if whole:
        if working is None and tip_data is None:
            return None
        if working is not None and working == tip_data:
            return None
        return working, work_mode(repo, path, tip_entry[0] if tip_entry else "100644")

    snapshot = snapshot_bytes(root, session, path, record)
    tracked = record.get("tracked_at_begin", True)

    if working is None:
        if tip_data is None:
            return None                      # gone here and gone there: nothing to do
        if snapshot is not None and snapshot == tip_data:
            return None, work_mode(repo, path, tip_entry[0])   # you deleted it
        raise Conflict([path])               # deleted here, changed on main

    if tip_data is None:
        if not tracked:
            return working, work_mode(repo, path)              # a new file: commit it whole
        raise Conflict([path])               # removed on main while you were editing it

    if snapshot is None:
        if working == tip_data:
            return None
        merged, bad = merge3(b"", tip_data, working,
                             ("main (tip %s)" % tip[:8], "nothing (new file)",
                              "working copy"))
        if bad:
            raise Conflict([path])
        return merged, work_mode(repo, path, tip_entry[0])

    if snapshot == working:
        return None                          # you changed nothing here
    merged, bad = merge3(snapshot, tip_data, working,
                         ("main (tip %s)" % tip[:8], "snapshot at begin", "working copy"))
    if bad:
        raise Conflict([path])
    if merged == tip_data:
        return None
    return merged, work_mode(repo, path, tip_entry[0])


def build_tree(repo, tip, entries):
    """Private index read from `tip`, your blobs in it, and the tree it writes out.

    This tree is exactly what the branch would hold, which is what `verify` builds.
    """
    index = Path(tempfile.mkdtemp(prefix="land-index-")) / "index"
    env = {"GIT_INDEX_FILE": str(index)}
    try:
        git(repo, "read-tree", tip, env=env)
        for path, (blob, mode, deleted) in sorted(entries.items()):
            if deleted:
                git(repo, "update-index", "--force-remove", "--", path, env=env)
            else:
                git(repo, "update-index", "--add", "--cacheinfo",
                    "%s,%s,%s" % (mode, blob, path), env=env)
        return git_out(repo, "write-tree", env=env)
    finally:
        shutil.rmtree(str(index.parent), ignore_errors=True)


def build_commit(repo, tip, entries, message, tree=None):
    """commit-tree the built tree onto `tip`. Returns the sha."""
    if tree is None:
        tree = build_tree(repo, tip, entries)
    return git_out(repo, "commit-tree", tree, "-p", tip, stdin=message)


def set_shared_index(repo, branch, entries, log):
    """Point the shared index at what we just committed, for our paths only.

    Nothing else in the index is touched, no working-tree file is written, and if HEAD is not
    the branch we landed on the shared index is left completely alone.
    """
    on = head_branch(repo)
    if on != branch:
        log("HEAD is %s, not %s: leaving the shared index alone (run this again from %s, or "
            "fix it with `git reset -q HEAD -- <path>` yourself once you are back on it)"
            % (on or "detached", branch, branch))
        return
    for path, (blob, mode, deleted) in sorted(entries.items()):
        if deleted:
            git(repo, "update-index", "--force-remove", "--", path)
        else:
            git(repo, "update-index", "--add", "--cacheinfo",
                "%s,%s,%s" % (mode, blob, path))


def cmd_commit(args, log):
    repo = repo_root()
    root = Path(args.root)
    meta = read_meta(root, args.session)
    auto_gc(root, log)
    branch = args.branch or meta.get("branch", DEFAULT_BRANCH)
    if Path(meta.get("repo", repo)) != Path(repo):
        raise Fail("session %r was started in %s, not %s"
                   % (args.session, meta.get("repo"), repo))

    message = read_message(args.message)
    claimed = dict(meta.get("paths") or {})
    whole = [norm_path(repo, p) for p in (args.whole or [])]

    # A --whole path is an explicit request and is landed whether or not --paths names it too;
    # a landing that silently dropped one (a moved card, 6c68a2a) looked like success.
    wanted = sorted(set(norm_path(repo, p) for p in args.paths) | set(whole)) if args.paths \
        else sorted(set(claimed) | set(whole))
    paths, dropped = [], []
    for path in wanted:
        if excluded(path):
            dropped.append(path)
            continue
        if path not in claimed and path not in whole:
            raise Fail("%s was edited without `begin`, so there is no snapshot to diff "
                       "against. Either `land.py begin %s %s` (it will snapshot the file as "
                       "it is now, so only later edits land), or `land.py begin --base HEAD "
                       "%s %s` if you have already edited it and want the diff against main, "
                       "or pass --whole %s to commit the whole working copy of it."
                       % (path, args.session, path, args.session, path, path))
        if path not in paths:
            paths.append(path)
    for path in dropped:
        log("not committing %s (intake file or *.orig)" % path)
    if not paths:
        raise Fail("nothing to commit")

    excludes = parse_selection(repo, args.exclude_hunk, "--exclude-hunk")
    onlys = parse_selection(repo, args.only_hunk, "--only-hunk")
    both = sorted(set(excludes) & set(onlys))
    if both:
        raise Fail("--exclude-hunk and --only-hunk both name %s; pick one form per path"
                   % ", ".join(both))
    for path in sorted(set(excludes) | set(onlys)):
        if path not in paths:
            raise Fail("%s is not one of this commit's paths (%s)" % (path, ", ".join(paths)))
        if path in whole:
            raise Fail("%s is a --whole path: it has no snapshot, so it has no numbered hunks"
                       % path)

    warn_overlaps(root, args.session, paths, log)

    last_error, verified = None, None
    for attempt in range(1, SWAP_ATTEMPTS + 1):
        tip = branch_tip(repo, branch)          # read once per attempt, used everywhere below
        plans, infos, conflicts = {}, {}, []
        for path in paths:
            record = claimed.get(path, {"existed": False, "tracked_at_begin": False})
            info = PathInfo(path)
            infos[path] = info
            info.holders = claimants(root, args.session, path)
            info.age = age_minutes(record.get("at") or meta.get("started"))
            info.from_base = record.get("base_rev")

            theirs = _MISSING
            if path in whole:
                # No snapshot, so no hunks and no age to be stale: --whole already prints the
                # whole diff it is about to take and says whose code may be in it.
                info.whole_path = True
            else:
                info.stale = info.age is not None and info.age > args.stale_minutes
                working = work_bytes(repo, path)
                info.snapshot = snapshot_bytes(root, args.session, path, record)
                info.hunks = path_hunks(info.snapshot, working)
                if not info.hunks and path not in whole:
                    # Two sessions were caught by this: a file edited BEFORE `begin` snapshots as
                    # already-edited, so there is nothing to land and the silence looked like success.
                    log("  %s: no change since your snapshot. If you edited it before `begin`, run "
                        "`begin %s --base main %s` to snapshot it from the tip instead."
                        % (path, args.session, path))
                numbers = set(range(1, len(info.hunks) + 1))
                asked = onlys.get(path) or excludes.get(path) or set()
                unknown = sorted(asked - numbers)
                if unknown:
                    raise Fail("%s has %d hunk(s) right now, so there is no hunk %s. Run "
                               "commit again without a selection to see them numbered."
                               % (path, len(info.hunks),
                                  ", ".join(str(n) for n in unknown)))
                info.selected = (onlys[path] & numbers) if path in onlys else \
                    (numbers - excludes.get(path, set()))
                info.excluded = numbers - info.selected
                info.contested = contested_hunks(root, args.session, path, info.snapshot,
                                                 info.hunks, info.holders,
                                                 forced=bool(info.from_base))
                if info.excluded:
                    if not info.selected:
                        continue          # every hunk left out: this path is not in the commit
                    theirs = apply_hunks(info.snapshot, working, info.hunks, info.selected)

            try:
                outcome = plan_path(repo, root, args.session, path, record, tip,
                                    path in whole, theirs)
            except Conflict as clash:
                conflicts.extend(clash.paths)
                continue
            if outcome is None:
                continue
            plans[path] = outcome
        if conflicts:
            raise Fail("merge conflict in: %s\nNothing was committed and nothing in the "
                       "working tree was touched. Pull the other session's change into your "
                       "copy by hand (their version is `git show %s:<path>`), then run commit "
                       "again. The cheapest resolution is often to need fewer files "
                       "(restructure so the contested file needs no change), not to merge "
                       "harder." % (", ".join(sorted(conflicts)), branch), code=3)
        if not plans:
            log("nothing to land: every claimed path already matches the tip"
                + (" once the hunks you left out are taken off" if
                   any(i.excluded for i in infos.values()) else ""))
            return 0

        digest = content_digest(tip, plans)
        held = sorted(path for path in plans
                      if infos[path].contested or infos[path].stale or infos[path].from_base)
        # Paths a --exclude-hunk/--only-hunk selection says nothing about: they land whole.
        # The review names them, because "only" reads as narrowing the whole landing (#DT6Z:
        # a session held a digest, added --only-hunk for one path, and read the unchanged
        # digest as its command having been honoured — then filed the tool as broken).
        unselected = sorted(path for path in plans
                            if (excludes or onlys)
                            and path not in excludes and path not in onlys)

        log("onto %s (tip %s), %d path(s), digest %s:"
            % (branch, tip[:12], len(plans), digest))
        for path in sorted(plans):
            log("  %s" % infos[path].stat())

        if args.dry_run:
            for path in sorted(plans):
                log("--- %s" % path)
                sys.stdout.write(unified(rev_bytes(repo, tip, path), plans[path][0], path))
            if held:
                print_review(log, root, branch, infos, plans, held, digest, unselected)
                log("(--dry-run, so nothing was committed either way.)")
            if any(is_cxx_path(path) for path in plans) and not args.no_verify:
                log("(a real commit would also build this exact tree in one of the %d shared "
                    "build slots under %s before the swap.)" % (VERIFY_SLOTS, root / "verify-slots"))
            return 0

        if args.no_verify and held:
            raise Fail("--no-verify is refused while anything is contested or stale (%s). "
                       "Those are exactly the landings that broke the build on 2026-09-19: "
                       "the working tree compiled because the other session's other half was "
                       "sitting in it, and the tree that went onto the branch did not."
                       % ", ".join(held))
        if args.confirm and args.confirm != digest:
            print_review(log, root, branch, infos, plans, held, digest, unselected)
            raise Fail("--confirm %s does not match this commit's digest %s. Something that "
                       "feeds the commit moved since you were shown that digest: a working-tree "
                       "edit, a different --exclude-hunk/--only-hunk selection, or %s advancing. "
                       "Nothing was landed. Read the hunks above and rerun with --confirm %s."
                       % (args.confirm, digest, branch, digest), code=4)
        if held and args.confirm != digest:
            print_review(log, root, branch, infos, plans, held, digest, unselected)
            raise Fail("held for review: %s. Nothing was landed."
                       % ", ".join("%s (%s)" % (path, "; ".join(infos[path].reasons()))
                                   for path in held), code=4)

        for path in sorted(whole):
            if path in plans:
                log("warning: --whole %s commits the entire working copy of that path, "
                    "including anything another session left in it:" % path)
                sys.stdout.write(unified(rev_bytes(repo, tip, path), plans[path][0], path))

        entries = {}
        for path, (content, mode) in plans.items():
            if content is None:
                entries[path] = (None, mode, True)
            else:
                entries[path] = (hash_blob(repo, content), mode, False)

        tree = build_tree(repo, tip, entries)
        cxx = {path: entries[path][0] for path in sorted(entries) if is_cxx_path(path)}
        if args.no_verify:
            log("verify: skipped (--no-verify)")
        else:
            problem = py_compile_landed(plans)
            if problem is not None:
                raise Fail("the exact bytes this commit would land do not compile as Python:\n"
                           "%s\nNothing was landed." % problem.strip(), code=5)
            if cxx and cxx == verified:
                log("verify: every landed C++ blob is the one already built; not rebuilding")
            elif cxx:
                step, output = run_verify(repo, root, args.session, tree, args, log)
                if step is not None:
                    sys.stdout.write("\n".join(first_errors(output)) + "\n")
                    raise Fail("the exact tree this commit would put on %s does not build "
                               "(%s failed). Nothing was landed. The working tree is not the "
                               "same thing: it holds every session's uncommitted code, so it "
                               "can compile while this tree cannot. Look at the errors above; "
                               "a missing declaration usually means you are landing one half "
                               "of somebody else's change (`land.py who`)."
                               % (branch, step), code=5)
                verified = cxx

        new = build_commit(repo, tip, entries, message, tree=tree)
        touched = [line for line in git_out(repo, "diff", "--no-renames", "--name-only", tip, new).splitlines()
                   if line]
        if sorted(touched) != sorted(entries):
            raise Fail("name gate failed: the commit would touch %s but this session's paths "
                       "are %s. Nothing was landed."
                       % (sorted(touched), sorted(entries)), code=3)

        swap = git(repo, "update-ref", "refs/heads/%s" % branch, new, tip, check=False)
        if swap.returncode != 0:
            last_error = swap.stderr.strip()
            log("%s moved under attempt %d (was %s); re-merging against the new tip"
                % (branch, attempt, tip[:12]))
            continue

        log_line(root, "%s %s -> %s (%s) [%s]"
                 % (branch, tip[:12], new[:12], args.session, " ".join(sorted(entries))))
        log("landed %s on %s" % (new, branch))
        for path in sorted(entries):
            log("  %s" % path)

        set_shared_index(repo, branch, entries, log)

        kept = set()
        for path in sorted(entries):
            info = infos.get(path)
            content, base_rev = _MISSING, None
            if info is not None and info.excluded:
                # The new snapshot is the old one plus what was landed, never the working
                # copy: the hunks left out have to stay later-than-the-snapshot so a later
                # commit still sees them.
                content = apply_hunks(info.snapshot, work_bytes(repo, path), info.hunks,
                                      info.selected)
                base_rev = info.from_base
                kept.add(path)
                log("  %s: %d hunk(s) left uncommitted in the working tree; a later commit "
                    "picks them up" % (path, len(info.excluded)))
            take_snapshot(repo, root, args.session, path, meta, new, content=content,
                          base_rev=base_rev)
        meta["updated"] = now()
        write_meta(root, args.session, meta)

        def mutate(data):
            entry = data["sessions"].get(args.session)
            if not entry:
                return
            entry["claims"] = sorted((set(entry.get("claims") or []) - set(entries)) | kept)
            entry["updated"] = now()
            entry["last_commit"] = new
        edit_registry(root, mutate)

        print(new)
        return 0

    raise Fail("%s moved under every one of the %d attempts (last: %s). Nothing was landed; "
               "run commit again." % (branch, SWAP_ATTEMPTS, last_error or "swap refused"),
               code=3)


def read_message(value):
    if value is None:
        raise Fail("a commit message is required (-m TEXT or -m path/to/file)")
    candidate = Path(value)
    try:
        if candidate.is_file():
            value = candidate.read_text(encoding="utf-8")
        elif ("/" in value or value.endswith((".txt", ".md"))) and not any(c.isspace() for c in value):
            # A path to a message file that does not exist would land as the subject line
            # (134068fe, 2026-09-20: a `cat > msg.txt` earlier in an `&&` chain never ran).
            raise Fail(f"-m {value} looks like a message file, and there is no such file: "
                       "write it first, or pass the message text itself")
    except OSError:
        pass
    if not value.strip():
        raise Fail("the commit message is empty")
    return value if value.endswith("\n") else value + "\n"


# --------------------------------------------------------------------------- abandon

def cmd_abandon(args, log):
    root = Path(args.root)
    directory = session_dir(root, args.session)
    if directory.exists():
        shutil.rmtree(str(directory), ignore_errors=True)

    def mutate(data):
        data["sessions"].pop(args.session, None)
    edit_registry(root, mutate)
    log("dropped session %s (snapshots and claims). The working tree was not touched."
        % args.session)
    return 0


# --------------------------------------------------------------------------- gc

def tree_bytes(path):
    total = 0
    for dirpath, dirnames, filenames in os.walk(str(path)):
        for name in filenames:
            try:
                total += os.lstat(os.path.join(dirpath, name)).st_blocks * 512
            except (OSError, AttributeError):
                pass
    return total


def human_bytes(n):
    for unit in ("B", "KB", "MB", "GB", "TB"):
        if n < 1024 or unit == "TB":
            return ("%.0f %s" if unit in ("B", "KB") else "%.1f %s") % (n, unit)
        n /= 1024.0


def newest_mtime(path):
    newest = 0.0
    for dirpath, dirnames, filenames in os.walk(str(path)):
        for name in dirnames + filenames:
            try:
                newest = max(newest, os.lstat(os.path.join(dirpath, name)).st_mtime)
            except OSError:
                pass
    try:
        newest = max(newest, os.lstat(str(path)).st_mtime)
    except OSError:
        pass
    return newest


def collect_garbage(root, log, dry_run=False, quiet=False):
    """Reclaim what no session will read again. Returns (removed count, bytes freed).

    - a pre-#SZHQ `<session>/verify` directory nobody has written for LEGACY_VERIFY_MINUTES;
    - a session idle longer than GC_DAYS: its directory and its registry entry;
    - a session directory with no meta.json and no registry entry (a crashed `begin`).
    Live sessions' snapshots are never touched, and neither is the working tree.
    """
    root = Path(root)
    if not root.is_dir():
        return 0, 0
    registry = read_registry(root).get("sessions", {})
    doomed = []          # (path, why, registry name to drop or None)
    cutoff = _dt.datetime.now().timestamp() - LEGACY_VERIFY_MINUTES * 60
    for directory in sorted(p for p in root.iterdir() if p.is_dir()):
        name = directory.name
        if name == "verify-slots" or name.startswith("."):
            continue
        entry = registry.get(name)
        idle = session_idle_minutes(entry) if isinstance(entry, dict) else None
        if idle is None and not (directory / "meta.json").exists():
            try:
                idle = (_dt.datetime.now().timestamp() - directory.stat().st_mtime) / 60
            except OSError:
                continue
        if idle is not None and idle > GC_DAYS * 24 * 60:
            doomed.append((directory, "idle %s" % human_age(idle), name))
            continue
        legacy = directory / "verify"
        if legacy.is_dir() and newest_mtime(legacy / "manifest.json"
                                            if (legacy / "manifest.json").exists()
                                            else legacy) < cutoff:
            doomed.append((legacy, "per-session verify build (now pooled)", None))

    freed = 0
    for path, why, name in doomed:
        size = tree_bytes(path)
        freed += size
        if not quiet or dry_run:
            log("gc: %s %s (%s, %s)" % ("would remove" if dry_run else "removed",
                                        path, why, human_bytes(size)))
        if not dry_run:
            shutil.rmtree(str(path), ignore_errors=True)
    names = [name for _, _, name in doomed if name]
    if names and not dry_run:
        def mutate(data):
            for name in names:
                data["sessions"].pop(name, None)
        edit_registry(root, mutate)
    if doomed and quiet and not dry_run:
        log("gc: reclaimed %s from %d stale land director%s under %s"
            % (human_bytes(freed), len(doomed), "y" if len(doomed) == 1 else "ies", root))
    return len(doomed), freed


def auto_gc(root, log):
    """Run gc at most once an hour, from the commands every session already runs."""
    stamp = Path(root) / ".last-gc"
    try:
        if _dt.datetime.now().timestamp() - stamp.stat().st_mtime < 3600:
            return
    except OSError:
        pass
    try:
        Path(root).mkdir(parents=True, exist_ok=True)
        stamp.touch()
        collect_garbage(root, log, quiet=True)
    except Exception as exc:   # housekeeping must never fail a begin or a commit
        log("gc: skipped (%s)" % exc)


def cmd_gc(args, log):
    root = Path(args.root)
    count, freed = collect_garbage(root, log, dry_run=args.dry_run)
    if not count:
        log("gc: nothing to reclaim under %s" % root)
    else:
        log("gc: %s %s in %d director%s" % ("would free" if args.dry_run else "freed",
                                           human_bytes(freed), count,
                                           "y" if count == 1 else "ies"))
    log("gc: %s now holds %s" % (root, human_bytes(tree_bytes(root)) if root.exists() else "0 B"))
    return 0


# --------------------------------------------------------------------------- doctor

def older_versions(repo, path, limit=80):
    out = git_out(repo, "log", "--format=%H", "-n", str(limit), "--", path)
    shas = set()
    for commit in out.splitlines():
        entry = tree_entry(repo, commit, path)
        if entry:
            shas.add(entry[1])
    return shas


def cmd_doctor(args, log):
    repo = repo_root()
    branch = args.branch
    findings = 0
    auto_gc(Path(args.root), log)

    on = head_branch(repo)
    if on != branch:
        log("checkout is on %s, not %s (report only)" % (on or "a detached HEAD", branch))
        findings += 1

    staged = git_out(repo, "diff", "--cached", "--name-only", "-z", "HEAD")
    staged = [p for p in staged.split("\0") if p]
    stale, fresh = [], []
    for path in staged:
        index_line = git_out(repo, "ls-files", "-s", "--", path).split()
        index_sha = index_line[1] if len(index_line) > 2 else None
        head_entry = tree_entry(repo, "HEAD", path)
        head_sha = head_entry[1] if head_entry else None
        if index_sha is None or index_sha == head_sha:
            continue
        if index_sha in older_versions(repo, path):
            stale.append((path, index_sha, head_sha, head_entry))
        else:
            fresh.append(path)

    for path, index_sha, head_sha, head_entry in stale:
        findings += 1
        working = work_bytes(repo, path)
        same_as_index = working is not None and \
            git_out(repo, "hash-object", "--", str(Path(repo) / path)) == index_sha
        log("stale staged entry: %s is staged at %s, an older commit's version (HEAD has %s)"
            % (path, index_sha[:12], (head_sha or "no file")[:12]))
        if not args.fix:
            continue
        if head_entry is None:
            log("  not fixing %s: HEAD has no version of it" % path)
            continue
        git(repo, "update-index", "--add", "--cacheinfo",
            "%s,%s,%s" % (head_entry[0], head_entry[1], path))
        if same_as_index:
            # The one case this tool writes a working file: the working copy is byte-for-byte
            # the stale blob that is already in history, so it is provably nobody's edit.
            (Path(repo) / path).write_bytes(blob_bytes(repo, head_entry[1]))
            log("  fixed: index and working copy set to HEAD's version")
        else:
            log("  fixed: index set to HEAD's version; the working copy holds newer content "
                "and was not touched")

    for path in fresh:
        findings += 1
        log("staged content that is not in history: %s (report only — that is someone's "
            "uncommitted work; leave it)" % path)

    branches = [line.strip() for line in
                git_out(repo, "for-each-ref", "--format=%(refname:short)",
                        "refs/heads").splitlines()]
    extra = [b for b in branches if b != branch]
    if extra:
        findings += 1
        log("branches other than %s: %s (report only)" % (branch, ", ".join(extra)))

    trees = []
    current = None
    for line in git_out(repo, "worktree", "list", "--porcelain").splitlines():
        if line.startswith("worktree "):
            current = line[len("worktree "):]
            trees.append(current)
    others = [t for t in trees if Path(t) != Path(repo)]
    if others:
        findings += 1
        log("worktrees other than this checkout: %s (report only)" % ", ".join(others))

    orig = [p for p in git_out(repo, "ls-files", "--others", "--exclude-standard", "-z"
                               ).split("\0") if p.endswith(".orig")]
    if orig:
        findings += 1
        log("untracked *.orig files: %s (report only; never committed)" % ", ".join(orig))

    for name, entry in sorted(registered_sessions(Path(args.root)).items()):
        if not is_idle(entry):
            continue
        findings += 1
        log("stale land session: %s has not run a land.py command for %s (claims: %s). It is "
            "ignored for contest detection; `land.py abandon %s` drops it, which never touches "
            "the working tree."
            % (name, human_age(session_idle_minutes(entry)),
               ", ".join(entry.get("claims") or []) or "none", name))

    if not findings:
        log("clean: nothing staged from an older commit, and nothing else to report")
    return 0 if findings == 0 else 2


# --------------------------------------------------------------------------- who

def cmd_who(args, log):
    root = Path(args.root)
    auto_gc(root, log)
    sessions = registered_sessions(root)
    if not sessions:
        log("no land sessions under %s" % root)
        return 0
    log("land sessions under %s:" % root)
    for name, entry in sorted(sessions.items()):
        idle = session_idle_minutes(entry)
        state = "STALE, idle %s (ignored for contest detection)" % human_age(idle) \
            if is_idle(entry) else "idle %s" % human_age(idle)
        log("%s — %s, contact: %s" % (name, state, entry.get("contact") or "none given"))
        claims = entry.get("claims") or []
        for path in claims[:WHO_PATHS]:
            log("    %s  snapshot %s old" % (path, human_age(claim_age(root, name, path))))
        if len(claims) > WHO_PATHS:
            log("    ... and %d more (%s/%s/meta.json has them all)"
                % (len(claims) - WHO_PATHS, root, name))
    slots = root / "verify-slots"
    log("disk: %s holds %s, of which verify build slots %s (at most %d per repository; "
        "`land.py gc` reclaims stale sessions)"
        % (root, human_bytes(tree_bytes(root)),
           human_bytes(tree_bytes(slots)) if slots.exists() else "0 B", VERIFY_SLOTS))
    return 0


# --------------------------------------------------------------------------- repair

def cmd_repair(args, log):
    """Take back what one commit did to some paths, keeping what landed on them since."""
    repo = repo_root()
    branch = args.branch
    sha = git_out(repo, "rev-parse", "--verify", "%s^{commit}" % args.sha)
    parents = git_out(repo, "rev-list", "--parents", "-n", "1", sha).split()[1:]
    if not parents:
        raise Fail("%s is a root commit: there is no earlier version to go back to" % sha[:12])
    if len(parents) > 1:
        raise Fail("%s is a merge commit; repair only handles a single-parent commit" % sha[:12])
    parent = parents[0]

    paths = []
    for given in args.paths:
        path = norm_path(repo, given)
        if excluded(path):
            log("not repairing %s (intake file or *.orig)" % path)
            continue
        if path not in paths:
            paths.append(path)
    if not paths:
        raise Fail("nothing to repair")

    changed = set(git_out(repo, "diff", "--no-renames", "--name-only", parent, sha).splitlines())
    for path in paths:
        if path not in changed:
            log("warning: %s is not one of the paths %s changed" % (path, sha[:12]))

    message = read_message(args.message) if args.message else (
        "repair: take back %s's changes to %s\n\nEach path is the current tip's version with "
        "that commit's hunks removed (three-way: base %s, ours the tip, theirs %s), so "
        "anything that landed on these paths afterwards is kept. No working-tree file was "
        "touched.\n" % (sha[:12], ", ".join(paths), sha[:12], parent[:12]))

    last_error = None
    for attempt in range(1, SWAP_ATTEMPTS + 1):
        tip = branch_tip(repo, branch)
        plans, conflicts = {}, []
        for path in paths:
            base = rev_bytes(repo, sha, path)            # what <sha> left there
            ours = rev_bytes(repo, tip, path)            # what main has now
            theirs = rev_bytes(repo, parent, path)       # what it held before <sha>
            entry = tree_entry(repo, tip, path) or tree_entry(repo, parent, path)
            mode = entry[0] if entry else "100644"
            if ours is None:
                if theirs is None:
                    continue                             # nothing there either way
                conflicts.append(path)                   # deleted on main since: not ours to
                continue                                 # decide, so abort
            if theirs is None:
                if ours == base:
                    plans[path] = (None, mode)           # <sha> added it; take it back out
                else:
                    conflicts.append(path)               # and someone has edited it since
                continue
            merged, bad = merge3(base if base is not None else b"", ours, theirs,
                                 ("%s (tip of %s)" % (tip[:8], branch),
                                  "%s (the commit being taken back)" % sha[:8],
                                  "%s (before it)" % parent[:8]))
            if bad:
                conflicts.append(path)
                continue
            if merged == ours:
                continue
            plans[path] = (merged, mode)
        if conflicts:
            raise Fail("cannot take back %s cleanly in: %s\nSomething that landed afterwards "
                       "overlaps the hunks being removed. Nothing was changed — not the "
                       "branch, not the index, not the working tree. Fix those paths by hand "
                       "through a normal `begin`/`commit`."
                       % (sha[:12], ", ".join(sorted(conflicts))), code=3)
        if not plans:
            log("nothing to repair: %s already holds no trace of %s in %s"
                % (branch, sha[:12], ", ".join(paths)))
            return 0

        log("would take back %s from %s (tip %s):" % (sha[:12], branch, tip[:12]))
        for path in sorted(plans):
            log("--- %s" % path)
            sys.stdout.write(unified(rev_bytes(repo, tip, path), plans[path][0], path))
        if args.dry_run:
            log("--dry-run: nothing was changed.")
            return 0

        entries = {}
        for path, (content, mode) in plans.items():
            entries[path] = (None, mode, True) if content is None else \
                (hash_blob(repo, content), mode, False)

        new = build_commit(repo, tip, entries, message)
        touched = [line for line in git_out(repo, "diff", "--no-renames", "--name-only", tip, new).splitlines()
                   if line]
        if sorted(touched) != sorted(entries):
            raise Fail("name gate failed: the repair would touch %s but you asked for %s. "
                       "Nothing was landed." % (sorted(touched), sorted(entries)), code=3)

        swap = git(repo, "update-ref", "refs/heads/%s" % branch, new, tip, check=False)
        if swap.returncode != 0:
            last_error = swap.stderr.strip()
            log("%s moved under attempt %d (was %s); recomputing the repair"
                % (branch, attempt, tip[:12]))
            continue

        log_line(Path(args.root), "%s %s -> %s (repair of %s) [%s]"
                 % (branch, tip[:12], new[:12], sha[:12], " ".join(sorted(entries))))
        log("landed %s on %s" % (new, branch))
        for path in sorted(entries):
            log("  %s" % path)

        # The working copy legitimately holds newer foreign code, so the shared index entry
        # would otherwise still point at the pre-repair blob and the next plain `git commit`
        # by anyone would put the bad version back. Index only; never the working copy.
        set_shared_index(repo, branch, entries, log)

        log("the working tree was NOT touched and still holds whatever the other sessions "
            "have in these files. Verify a repair on a clean export of the new tree, never on "
            "the working tree: `git archive %s | tar -x -C <scratch dir>`, then build there."
            % new[:12])
        print(new)
        return 0

    raise Fail("%s moved under every one of the %d attempts (last: %s). Nothing was landed."
               % (branch, SWAP_ATTEMPTS, last_error or "swap refused"), code=3)


# --------------------------------------------------------------------------- hook command

def cmd_hook(args, log):
    repo = repo_root()
    if args.hook_action != "install":
        raise Fail("unknown hook action %r" % args.hook_action)
    if not install_hook(repo, log, force=args.force):
        log("pre-commit hook already installed at %s" % hook_file(repo))
    log("a plain `git commit` from the shared index now fails with: %s" % HOOK_REFUSAL)
    log("owner's escape hatch: RELAY_ALLOW_SHARED_COMMIT=1 git commit ...")
    return 0


# --------------------------------------------------------------------------- CLI

EPILOG = """\
the whole workflow

  python3 scripts/land.py begin mysession --contact "my Claude name" src/Pane.h docs/X.md
  # ... edit, build and test in this checkout, as usual ...
  python3 scripts/land.py commit mysession -m "what I did"

`begin` snapshots those files as they are now. `commit` lands, for each of them, the tip of
main plus the hunks you added since the snapshot -- so another session's uncommitted edits in
the same file are neither committed nor disturbed -- then points the shared index at what it
committed, so `git status` shows only what is still uncommitted.

contested hunks

A snapshot tells your hunks from someone else's only while nobody else edits the file after it
was taken. When another live session claims the same path, `commit` calls a hunk CONTESTED if
it appeared after that session began -- and if that session began before you, it calls every
hunk contested, because nothing in the file can say who typed it. A path with contested hunks,
or whose snapshot is older than --stale-minutes, is held: `commit` prints the numbered hunks
and a digest and exits 4 without landing anything. Then either

  commit mysession -m "..." --confirm DIGEST                 land all of it
  commit mysession -m "..." --exclude-hunk src/Pane.h:2,5-7  leave those hunks out
  commit mysession -m "..." --only-hunk src/Pane.h:1,3       land only those hunks

Hunks left out are neither committed nor touched: they stay in the working tree and a later
commit picks them up. Either selection flag prints a new digest. The digest covers the tip and
the exact bytes of every path, so a working-tree edit, a different selection or main moving
makes it stop matching, and you are asked again.

  -m accepts either the message text or the path to a file holding it.
  --paths p...       land only some of the session's paths.
  --whole p          commit the entire working copy of a path you never ran `begin` on.
  --dry-run          print the merged diff (and the review, if it would be held) and stop.
  --stale-minutes N  hold a path whose snapshot is older than this (default {stale}).

the build gate

When the paths being landed include C++ or build files (src/, engine/, tests/*.cpp,
CMakeLists.txt, *.cmake), `commit` materialises the EXACT tree it is about to put on the branch
into a build slot, <root>/verify-slots/<repo>-<n>/src, and builds it in .../build before the
swap. Only a tree that compiles is landed; otherwise the first errors are printed, nothing is
landed, and it exits 5. A slot is the tool's own check, not a place to work: nobody edits there.
There are RELAY_LAND_VERIFY_SLOTS of them per repository (default 2), shared by every session
and held under a lock for the whole build, so twenty sessions take two build trees of disk, not
twenty (card #SZHQ). Only the files whose blob changed are rewritten, so a slot stays
incremental whoever used it last. Landed .py files are byte-compiled the same way.

disk

`gc` reclaims what nobody will read again: sessions idle longer than RELAY_LAND_GC_DAYS (default
3) and pre-#SZHQ per-session verify builds. It runs by itself at most once an hour from begin,
commit, who and doctor; `gc --dry-run` says what it would take. `who` prints the root's size.

The working tree is deliberately not what gets built: it holds every session's uncommitted
code, so it can compile while the tree you are landing cannot. That is how a green build of
code nobody had written reached main twice on 2026-09-19.

  --verify-cmd "..."   run this instead (cwd = the materialised tree, VERIFY_BUILD in env).
  --verify-tests RE    also build everything and run the ctest cases matching RE.
  --verify-target T    the cmake target the default verify builds (default relay).
  --no-verify          skip it -- refused when any path is contested or stale.

  begin --contact NAME  how to reach you; shown in every claim warning and in `who`.
  begin --base REV p    snapshot REV's version of p instead of the working copy, for a file
  begin --from-head p   you had already edited before claiming it (--from-head is --base tip).
                        Its hunks are then diff(REV:p, working copy), which includes anything
                        another session left in that file, so such a path is always held for
                        the --confirm review. This replaces editing the snapshot by hand.

  python3 scripts/land.py who                 live sessions, their paths, ages and contacts.
  python3 scripts/land.py abandon mysession   drop the snapshots and claims.
  python3 scripts/land.py doctor [--fix]      shared-index and checkout hygiene.
  python3 scripts/land.py hook install        make git refuse a shared-index commit.
  python3 scripts/land.py repair SHA --paths p...
      land a commit that takes back what SHA did to those paths, keeping whatever landed on
      them afterwards, then point the shared index at it. The working tree is never touched,
      so verify a repair on `git archive <new sha>`, never in the checkout.

exit codes: 0 fine, 1 usage or environment error, 2 doctor found something, 3 conflict or a
swap that could not be completed, 4 held for review, 5 the exact tree does not build.
Nothing was changed on 3, 4 or 5.
""".replace("{stale}", str(DEFAULT_STALE_MINUTES))


def build_parser():
    parser = argparse.ArgumentParser(
        prog="land.py",
        description=__doc__.split("\n\n")[0],
        epilog=EPILOG,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--root", default=os.environ.get("RELAY_LAND_ROOT", DEFAULT_ROOT),
                        help="where snapshots and the claims registry live (default %s)"
                             % DEFAULT_ROOT)
    subs = parser.add_subparsers(dest="command", required=True)

    begin = subs.add_parser("begin", help="snapshot paths before you edit them")
    begin.add_argument("session")
    begin.add_argument("paths", nargs="+")
    begin.add_argument("--branch", default=DEFAULT_BRANCH)
    begin.add_argument("--contact", default=None,
                       help="free text saying how to reach this session, e.g. its Claude "
                            "peer name; shown in every claim warning and in `who`")
    begin.add_argument("--base", default=None,
                       help="snapshot this revision's version of each path instead of the "
                            "working copy (for a file you edited before claiming it)")
    begin.add_argument("--from-head", action="store_true",
                       help="shorthand for --base <the current tip of the branch>")
    begin.set_defaults(func=cmd_begin)

    commit = subs.add_parser("commit", help="land your hunks on the branch")
    commit.add_argument("session")
    commit.add_argument("-m", "--message", help="the message, or a file holding it")
    commit.add_argument("--paths", nargs="+", action="extend", default=None, help="land only this subset (repeatable)")
    commit.add_argument("--whole", nargs="+", action="extend", default=None, help="commit a whole working copy, unsnapshotted (repeatable)")
    commit.add_argument("--branch", default=None)
    commit.add_argument("--dry-run", action="store_true")
    commit.add_argument("--confirm", default=None, metavar="DIGEST",
                        help="the digest a held commit printed; lands it unchanged")
    commit.add_argument("--exclude-hunk", action="append", default=None, metavar="PATH:N",
                        help="leave these hunks out of the commit, e.g. src/Pane.h:2,5-7 "
                             "(repeatable, accumulating)")
    commit.add_argument("--only-hunk", action="append", default=None, metavar="PATH:N",
                        help="land only these hunks of that path and leave the rest out "
                             "(repeatable, accumulating)")
    commit.add_argument("--stale-minutes", type=float, default=DEFAULT_STALE_MINUTES,
                        help="hold a path whose snapshot is older than this (default %d)"
                             % DEFAULT_STALE_MINUTES)
    commit.add_argument("--verify-cmd", default=None, metavar="SHELL",
                        help="run this instead of the default cmake build, with cwd = the "
                             "materialised tree and VERIFY_BUILD in the environment")
    commit.add_argument("--verify-tests", default=None, metavar="REGEX",
                        help="also build everything and run the ctest cases matching REGEX")
    commit.add_argument("--verify-target", default=os.environ.get("RELAY_LAND_VERIFY_TARGET",
                                                                  "relay"),
                        help="the cmake target the default verify builds (default relay)")
    commit.add_argument("--no-verify", action="store_true",
                        help="skip the build gate; refused when any path is contested or stale")
    commit.set_defaults(func=cmd_commit)

    who = subs.add_parser("who", help="live sessions, their paths, ages and contacts")
    who.set_defaults(func=cmd_who)

    gc = subs.add_parser("gc", help="reclaim stale sessions and old verify builds "
                                    "(runs by itself at most hourly)")
    gc.add_argument("--dry-run", action="store_true")
    gc.set_defaults(func=cmd_gc)

    abandon = subs.add_parser("abandon", help="drop a session's snapshots and claims")
    abandon.add_argument("session")
    abandon.set_defaults(func=cmd_abandon)

    doctor = subs.add_parser("doctor", help="check the shared index and the checkout")
    doctor.add_argument("--fix", action="store_true")
    doctor.add_argument("--branch", default=DEFAULT_BRANCH)
    doctor.set_defaults(func=cmd_doctor)

    hook = subs.add_parser("hook", help="install the pre-commit hook")
    hook.add_argument("hook_action", choices=["install"], metavar="install")
    hook.add_argument("--force", action="store_true")
    hook.set_defaults(func=cmd_hook)

    repair = subs.add_parser("repair", help="take back what one commit did to some paths")
    repair.add_argument("sha")
    repair.add_argument("--paths", nargs="+", action="extend", required=True)
    repair.add_argument("-m", "--message", default=None,
                        help="the message, or a file holding it (a default is written for you)")
    repair.add_argument("--branch", default=DEFAULT_BRANCH)
    repair.add_argument("--dry-run", action="store_true")
    repair.set_defaults(func=cmd_repair)
    return parser


def adopt_legacy_root(args):
    """A session begun under a per-TMPDIR root (before #BHJZ) keeps its snapshots there;
    finish it from that root rather than refusing every path as edited without `begin`."""
    if (args.command not in ("commit", "abandon") or args.root != DEFAULT_ROOT
            or os.path.normpath(LEGACY_ROOT) == os.path.normpath(DEFAULT_ROOT)):
        return
    session = getattr(args, "session", None)
    if not session or "/" in session or session.startswith("."):
        return
    if meta_file(DEFAULT_ROOT, session).exists() \
            or not meta_file(LEGACY_ROOT, session).exists():
        return
    sys.stderr.write("land.py: session %s was begun under the old per-TMPDIR root %s; using it "
                     "for this %s. New sessions use the shared root %s (#BHJZ).\n"
                     % (session, LEGACY_ROOT, args.command, DEFAULT_ROOT))
    args.root = LEGACY_ROOT


def main(argv=None):
    args = build_parser().parse_args(argv)
    adopt_legacy_root(args)

    def log(message):
        print(message)
    try:
        return args.func(args, log)
    except Fail as failure:
        sys.stderr.write("land.py: %s\n" % failure)
        return failure.code


if __name__ == "__main__":
    sys.exit(main())
