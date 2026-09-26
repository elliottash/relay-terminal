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

  * Every tool write is also journalled per session token by the backend (under the land
    root, `authors/<token>.jsonl` with both byte images in `blobs/`). At your commit a held
    hunk whose removed+added lines are exactly the change another pane's journal recorded is
    that pane's, not yours: **FOREIGN**. The review names the pane, its card and the time;
    `--confirm` leaves FOREIGN hunks uncommitted in the working tree, and
    `--take-foreign <path>:<n>` (repeatable) is how you land one on purpose — the commit
    message records where it came from. On 2026-09-24 session #6CSN confirmed a review whose
    every hunk was contested and thereby landed #234Z's edit of `Pane.h` as its own; this is
    the fix.

  * `begin` also adopts: when the backend has already begun an auto claim for your own
    RELAY_SESSION_TOKEN on a path (it does so before a pane's first tool write of that path),
    a manual `begin` on the same path takes the pane's earlier snapshot and timestamp, so the
    edits made before the manual begin still land from it.

  * Before the swap, a landing that touches C++ or build files builds the EXACT tree it would
    put on the branch, in its own directory under the session's snapshots. Never the working
    tree: that holds every session's uncommitted code, so it can compile while the tree being
    landed cannot -- which is how a green build of code nobody had written reached `main`
    twice on 2026-09-19. A tree that does not compile is not landed (exit 5).

`who` lists the live sessions, what they claim, how old their snapshots are and how to reach
them (`begin --contact <name>`). `repair <sha> --paths ...` lands a commit that takes back
what `<sha>` did to those paths, keeping whatever landed on them afterwards.

Uncommitted work must not pile up unseen and must not die with its pane (card #FYEY). One
measure everywhere: a session holds `len(path_hunks(snapshot, working copy))` hunks per
claimed path, 0 when the file matches its snapshot.

  * `status --token <token> [--json]` prints what one pane (its RELAY_SESSION_TOKEN, auto and manual
    sessions alike) still holds. The backend refuses to move a pane while the total is above
    zero.
  * `reap --token <token>` runs when the pane closes: a session with uncommitted hunks is
    stamped `reaped` and its card's thread gets a note saying what is waiting and how to
    resume; a clean one is abandoned. `orphans [--json]` lists the reaped and stale sessions
    that still hold hunks, grouped by card, for the GUI's Resume card action; `who` and
    `doctor` fold them into one line instead of listing each.
  * `begin --owner <thread> --card <#ID>` records them in the registry (`who` shows them).
    When the owner thread's stop wrote a cancelled marker under the land root, `commit` and
    `try` refuse: nobody is there to answer a review.
  * `board-sync <token> -m <message> <paths...>` lands the whole working copy of .board/
    paths straight onto the branch — no `begin`, no build gate, no review hold, no claim
    touched — for the backend's end-of-turn board writes.
  * `--only-hunk`/`--exclude-hunk` take a bare path together with `--by <pane-id|#ID>`: the
    selection becomes the hunks the authorship journal attributes to that pane or card, so
    another session's concurrent edit elsewhere in the file does not shift your numbers.

What it refuses to do, and why:

  * It never commits from the shared index, and never runs `git add` there. The shared index
    accumulates entries equal to older commits' blobs as `main` moves, so a plain `git commit`
    silently reverts newer commits. `hook install` makes git refuse that too.
  * It never runs `git checkout`, `git stash`, `git reset`, or writes any working-tree file
    (the exceptions: `doctor --fix` on a provably stale path, see below, and after a landing
    the `links.commits` line of the `#ID` board cards the message names, #MJ76). Restoring a
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
stale hunks), 5 the exact tree does not build, 7 every verify build slot stayed busy past
--wait-seconds. On 3, 4, 5 and 7 nothing was changed.
"""

import argparse
import contextlib
import datetime as _dt
import difflib
import fcntl
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import time
from pathlib import Path

_USER_DIR = "claude-%d" % os.getuid() if hasattr(os, "getuid") else "claude"


def system_tmp():
    """The machine's real temp dir, whatever TMPDIR says (RELAY_LAND_SYSTEM_TMP overrides it,
    for tests). Relay points every session's TMPDIR at its own scratch dir (#DVV2), so
    tempfile.gettempdir() is a different directory for every session."""
    override = os.environ.get("RELAY_LAND_SYSTEM_TMP")
    if override:
        return override
    if os.name == "posix":
        return "/tmp"
    return os.environ.get("TEMP") or os.environ.get("TMP") or tempfile.gettempdir()


# Card #HRF6: the land root is Relay's own state — one place per user, outside the temp dir,
# under a relay-named path. Before #BHJZ it followed each session's TMPDIR (private to one
# pane); #BHJZ made it shared but kept the claude-<uid> name land.py was born with.
DEFAULT_ROOT = os.path.join(
    os.environ.get("XDG_STATE_HOME") or os.path.join(os.path.expanduser("~"), ".local", "state"),
    "relay", "land")


def legacy_roots():
    """Where sessions begun before #HRF6 hold their snapshots: the /tmp/claude-<uid>/land root
    #BHJZ made shared, then the per-TMPDIR roots from before that. The first land.py command
    after the move adopts what it finds into DEFAULT_ROOT; what cannot move is still reached
    by the per-session fallback in main()."""
    roots = [os.path.join(system_tmp(), _USER_DIR, "land"),
             os.path.join(tempfile.gettempdir(), _USER_DIR, "land")]
    out, seen = [], {os.path.normpath(DEFAULT_ROOT)}
    for root in roots:
        norm = os.path.normpath(root)
        if norm not in seen:
            seen.add(norm)
            out.append(root)
    return out


DEFAULT_BRANCH = "main"
DEFAULT_STALE_MINUTES = 15
# A session that has not run a land.py command for this long is not editing anything any
# more: `who` and `doctor` call it stale and contest detection ignores it.
IDLE_HOURS = 12
# Card #SZHQ: the verify build used to live in each session's own directory and was never
# removed — 255 of them, 153 GB, on 2026-09-24. It is now a small pool of slots shared by every
# session of one repository, each held under a lock for a whole materialise-and-build, so the
# disk it takes is bounded by the slot count instead of the session count. Every session's tree
# is close to the tip, so a slot another session built last stays incremental. Card #76QW
# sizes the pool after the machine instead: see verify_slots() near the slot machinery.

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

HOOK_VERSION = 2

# Version 2 (card #AMQQ): refuses only the main checkout's *shared* index (`<common dir>/index`).
# A linked worktree's own index — a Relay workspace — commits normally, since its commits are
# submitted to the queue, never pushed onto the target. A project hook preserved beside this one
# as `pre-commit.project` is chained. Kept byte-identical to relay_core.integration_service.HOOK
# (tests/test_integration_service.py checks); change both together.
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


# ------------------------------------------------------------------ authorship journal
#
# The backend journals every tool write under <land root>/authors/<token>.jsonl, one JSON
# object per line (ts, repo, path, before/after blob shas, pane, token, card, turn_id), with
# the raw bytes of each side in <land root>/blobs/<sha256>. Reading that journal at commit is
# what separates a merely contested hunk from a FOREIGN one: a hunk whose removed and added
# lines are exactly the change another pane recorded is that pane's edit, not yours — the
# case #6CSN could not see and landed as its own. land.py only reads this journal (and gc
# prunes it); a journal that is missing, malformed, or points at an unreadable blob
# attributes nothing, and the hunk stays CONTESTED exactly as before.


def my_token():
    """The RELAY_SESSION_TOKEN of the process running land.py, when it has one."""
    return os.environ.get("RELAY_SESSION_TOKEN", "")


def journal_files(root, token):
    """<token>.jsonl plus its rotations <token>.1.jsonl, <token>.2.jsonl, ..."""
    folder = Path(root) / "authors"
    out = [folder / (token + ".jsonl")]
    for number in range(1, 10):
        out.append(folder / ("%s.%d.jsonl" % (token, number)))
    return [path for path in out if path.exists()]


def read_journal(root, token):
    """Parsed journal entries for one token; malformed or unreadable lines are skipped."""
    entries = []
    for path in journal_files(root, token):
        try:
            text = path.read_text(encoding="utf-8")
        except OSError:
            continue
        for raw in text.splitlines():
            raw = raw.strip()
            if not raw:
                continue
            try:
                entry = json.loads(raw)
            except ValueError:
                continue
            if isinstance(entry, dict):
                entries.append(entry)
    return entries


def journal_blob(root, sha):
    """The raw bytes of one journal blob, or None when it is missing or unreadable."""
    if not isinstance(sha, str) or not sha:
        return None
    try:
        return (Path(root) / "blobs" / sha).read_bytes()
    except OSError:
        return None


def hunk_blocks(hunk):
    """A hunk's content identity: the removed and added lines (with their context), not
    where they sit, so the same edit at a different offset still matches."""
    return (tuple(hunk.base), tuple(hunk.new))


def foreign_hunks(root, repo, path, mine):
    """{hunk blocks: journal entry} for the edits other panes' journals say they made here.

    Every journal whose token is not in `mine` is read; only lines naming this repo and path
    newer than IDLE_HOURS are considered, and only lines whose before/after blobs both read
    (a null `before` is a new file: the empty pre-image). When several lines match the same
    hunk the newest wins. `mine` is the set of tokens that are this session's own (the
    RELAY_SESSION_TOKEN of the process and the token the session recorded at begin): your
    own journal must never make your own hunks foreign.
    """
    found = {}
    try:
        names = sorted((Path(root) / "authors").iterdir())
    except OSError:
        return found
    cutoff = time.time() - IDLE_HOURS * 3600
    for name in names:
        if not name.name.endswith(".jsonl"):
            continue
        token = name.name[: -len(".jsonl")].split(".")[0]
        if not token or token in mine:
            continue
        for entry in read_journal(root, token):
            if entry.get("repo") != str(repo) or entry.get("path") != path:
                continue
            try:
                when = float(entry.get("ts") or 0)
            except (TypeError, ValueError):
                continue
            if when < cutoff:
                continue
            before = b"" if entry.get("before") is None else journal_blob(root, entry.get("before"))
            after = journal_blob(root, entry.get("after"))
            if before is None or after is None or is_binary(before) or is_binary(after):
                continue
            for hunk in split_hunks(lines_of(before), lines_of(after)):
                best = found.get(hunk_blocks(hunk))
                if best is None or when > float(best.get("ts") or 0):
                    found[hunk_blocks(hunk)] = entry
    return found


def latest_own_edit(root, repo, path, token):
    """The newest entry of this token's journal for repo+path, or None."""
    best = None
    for entry in read_journal(root, token):
        if entry.get("repo") != str(repo) or entry.get("path") != path:
            continue
        try:
            when = float(entry.get("ts") or 0)
        except (TypeError, ValueError):
            continue
        if best is None or when > best[0]:
            best = (when, entry)
    return best


def foreign_mark(entry):
    """How a review names the pane behind a FOREIGN hunk."""
    card = (entry.get("card") or "").strip()
    try:
        when = " at %s" % _dt.datetime.fromtimestamp(float(entry.get("ts") or 0)).strftime("%H:%M")
    except (TypeError, ValueError, OSError, OverflowError):
        when = ""
    return "FOREIGN — pane %s%s edited this region%s" % (
        (entry.get("pane") or "another pane").strip() or "another pane",
        " (%s)" % card if card else "",
        when,
    )


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


def duration(seconds):
    """'45s', '4m12s', '1h03m': a wait measured in seconds, for the slot-queue lines."""
    seconds = int(seconds or 0)
    if seconds < 60:
        return "%ds" % seconds
    if seconds < 3600:
        return "%dm%02ds" % (seconds // 60, seconds % 60)
    return "%dh%02dm" % (seconds // 3600, (seconds % 3600) // 60)


def log_line(root, message):
    Path(root).mkdir(parents=True, exist_ok=True)
    with (Path(root) / "land.log").open("a", encoding="utf-8") as fh:
        fh.write("%s %s\n" % (now(), message))


# --------------------------------------------------------------------------- hook

def hook_file(repo):
    """The pre-commit hook every worktree of `repo` runs: `git rev-parse --git-path hooks`
    honours core.hooksPath and, from the main checkout, is the common hooks directory."""
    out = git_out(repo, "rev-parse", "--git-path", "hooks")
    base = Path(out)
    if not base.is_absolute():
        base = Path(repo) / base
    return base / "pre-commit"


def hook_version(text):
    match = re.search(r"relay-hook-version: (\d+)", text)
    return int(match.group(1)) if match else 1


def install_hook(repo, log, force=False):
    """Install the Relay hook, or upgrade an older Relay one in place. A hook that is not
    Relay's is refused unless `force`, and then preserved as `pre-commit.project` beside it
    and chained — never discarded (`relay-land activate` does the same at cutover)."""
    target = hook_file(repo)
    target.parent.mkdir(parents=True, exist_ok=True)
    if target.exists():
        current = target.read_text(encoding="utf-8", errors="replace")
        if HOOK_REFUSAL in current:
            if hook_version(current) >= HOOK_VERSION and not force:
                return False
            log("upgrading the Relay pre-commit hook from version %d to %d"
                % (hook_version(current), HOOK_VERSION))
        else:
            if not force:
                raise Fail("a different pre-commit hook is already installed at %s; move it "
                           "aside or pass --force, which keeps it as pre-commit.project and "
                           "runs it after Relay's check" % target)
            project = target.parent / "pre-commit.project"
            if project.exists() and project.read_bytes() != target.read_bytes():
                raise Fail("cannot preserve the project's pre-commit hook: %s already exists and "
                           "differs; move one of them aside" % project)
            os.replace(str(target), str(project))
            os.chmod(str(project), 0o755)
            log("preserved the project's pre-commit hook as %s; Relay's chains to it" % project)
    target.write_text(HOOK, encoding="utf-8")
    os.chmod(str(target), 0o755)
    log("installed pre-commit hook at %s" % target)
    return True


# ------------------------------------------------------------- publication mode (#AMQQ)

PUBLICATION_MARKER = "relay-publication.json"
PUBLICATION_LOCK = "relay-publication.lock"


def common_dir(repo):
    out = git_out(repo, "rev-parse", "--git-common-dir")
    base = Path(out)
    if not base.is_absolute():
        base = Path(repo) / base
    return base.resolve()


def publication_marker(repo):
    """`<common dir>/relay-publication.json`, written by `relay-land activate/pause/rollback`
    (relay_core.integration_service): the mode this repository publishes in. Absent means
    `legacy` — this script is the publisher. Anything else means the integration service is,
    and every ref swap here must refuse."""
    path = common_dir(repo) / PUBLICATION_MARKER
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
    except FileNotFoundError:
        return {"mode": "legacy", "present": False}
    except (OSError, ValueError):
        return {"mode": "legacy", "present": True}
    if not isinstance(data, dict) or data.get("mode") not in ("legacy", "queue", "paused"):
        return {"mode": "legacy", "present": True}
    return data


def require_legacy_publisher(repo, what, marker=None):
    marker = marker if marker is not None else publication_marker(repo)
    mode = marker.get("mode", "legacy")
    if mode == "legacy":
        return marker
    since = marker.get("since") or "?"
    if what == "board-sync":
        raise Fail("this repository has published through relay-land since %s (mode %s); "
                   "board-sync no longer moves refs/heads/%s. In queue mode it submits the "
                   "Board snapshot to the queue instead; in paused mode nothing publishes "
                   "until `relay-land activate` or `relay-land rollback`."
                   % (since, mode, marker.get("target") or DEFAULT_BRANCH), code=2)
    raise Fail("this repository has published through relay-land since %s (mode %s): "
               "land.py %s no longer moves refs/heads/%s. Work in a Relay workspace and submit "
               "its commit (`relay-land submit <sha> --request-id <id>`), or `relay-land "
               "rollback` to return to legacy publication. Your working tree and claims are "
               "untouched." % (since, mode, what, marker.get("target") or DEFAULT_BRANCH), code=2)


@contextlib.contextmanager
def legacy_publication(repo, what):
    """Hold the shared transition lock around a ref swap, and re-check the marker under it: a
    `relay-land activate` running at the same time takes the lock exclusively (waiting for
    this to finish — that is the drain) and flips the marker before it lets go, so a swap that
    started in legacy mode never lands after the cutover."""
    require_legacy_publisher(repo, what)
    path = common_dir(repo) / PUBLICATION_LOCK
    fd = os.open(str(path), os.O_CREAT | os.O_RDWR, 0o644)
    try:
        try:
            fcntl.flock(fd, fcntl.LOCK_SH | fcntl.LOCK_NB)
        except (BlockingIOError, OSError):
            raise Fail("a publication-mode transition (relay-land activate/pause/rollback) holds "
                       "%s; nothing was landed. Run `relay-land inventory` and try again when it "
                       "is over." % path, code=7)
        require_legacy_publisher(repo, what)
        yield
    finally:
        try:
            fcntl.flock(fd, fcntl.LOCK_UN)
        finally:
            os.close(fd)


# --------------------------------------------------------------------------- begin

def find_auto_claim(root, repo, token, path, skip):
    """The live auto session with this pane token that claims `path`, oldest begin first."""
    for name, entry in claimants(root, skip, path):
        if not token or entry.get("token") != token or not entry.get("auto"):
            continue
        if entry.get("repo") == str(repo):
            return name, entry
    return None


def merge_auto_claim(root, name, path):
    """Fold an auto-claimed path into the session that just began over it.

    The path leaves the auto session's claims — it is the same pane's claim now, under a
    session with a human driving it — and an auto session with nothing left to claim is
    dropped whole.
    """
    def mutate(data):
        entry = data["sessions"].get(name)
        if not entry:
            return
        claims = set(entry.get("claims") or [])
        claims.discard(path)
        if claims:
            entry["claims"] = sorted(claims)
            entry["updated"] = now()
        else:
            del data["sessions"][name]
    edit_registry(root, mutate)
    try:
        meta = read_meta(root, name)
    except (Fail, ValueError):
        return
    if path in (meta.get("paths") or {}):
        del meta["paths"][path]
    if meta.get("paths"):
        write_meta(root, name, meta)
    else:
        shutil.rmtree(session_dir(root, name), ignore_errors=True)


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
    token = my_token()
    if token:
        meta["token"] = token
    if args.auto:
        meta["auto"] = True
    if args.card:
        meta["card"] = args.card
    if args.owner:
        meta["owner"] = args.owner

    # An auto claim under our own RELAY_SESSION_TOKEN is this pane's tool-made claim (the
    # backend begins one before a pane's first tool write of a path). Beginning by hand over
    # it adopts the pane's earlier snapshot and timestamp instead of snapshotting the
    # now-edited working copy, so the edits the pane made before this begin still land from
    # it. The claim then belongs to this session: the path leaves the auto session, and an
    # auto session left with no claims is dropped.
    adopted = {}
    if token and not base_rev:
        for path in paths:
            found = find_auto_claim(root, repo, token, path, skip=args.session)
            if found is None:
                continue
            name, _entry = found
            try:
                theirs = ((read_meta(root, name).get("paths") or {}).get(path)) or {}
            except (Fail, ValueError):
                theirs = {}
            if not theirs:
                continue                # no readable record of its snapshot: adopt nothing
            snap = snapshot_bytes(root, name, path, theirs)
            if snap is None and theirs.get("existed"):
                continue                # its snapshot is gone: fall back to snapshotting afresh
            merge_auto_claim(root, name, path)
            take_snapshot(repo, root, args.session, path, meta, tip, content=snap)
            meta["paths"][path]["adopted_from"] = name
            if "tracked_at_begin" in theirs:
                meta["paths"][path]["tracked_at_begin"] = theirs["tracked_at_begin"]
            if theirs.get("at"):
                meta["paths"][path]["at"] = theirs["at"]
            adopted[path] = name

    # Who already holds these paths, read before our own claim goes into the registry.
    holders = {path: claimants(root, args.session, path) for path in paths}

    for path in paths:
        if path in adopted:
            continue
        content = rev_bytes(repo, base_rev, path) if base_rev else _MISSING
        take_snapshot(repo, root, args.session, path, meta, tip, content=content,
                      base_rev=base_rev)
        # Nothing was adopted here, so the snapshot we just took is of a working copy this
        # pane may already have edited through its tools. Its own journal is the only
        # witness: warn when it says so, because those edits predate the snapshot and will
        # never land from it.
        if token and not base_rev:
            mine = latest_own_edit(root, repo, path, token)
            if mine is not None and work_bytes(repo, path) != rev_bytes(repo, tip, path):
                when = _dt.datetime.fromtimestamp(mine[0]).strftime("%H:%M")
                log("warning: %s: you edited this at %s; those edits will not land from this "
                    "snapshot (it was taken after them); use `begin --base main` — here: "
                    "`begin %s --base main %s` — to land them too" % (path, when,
                                                                     args.session, path))
    write_meta(root, args.session, meta)

    def mutate(data):
        entry = data["sessions"].setdefault(args.session, {})
        entry["repo"] = str(repo)
        entry["branch"] = args.branch
        entry["updated"] = now()
        entry.setdefault("started", meta["started"])
        if args.contact:
            entry["contact"] = args.contact
        if token:
            entry["token"] = token
        if args.auto:
            entry["auto"] = True
        if args.card:
            entry["card"] = args.card
        if args.owner:
            entry["owner"] = args.owner
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
        elif path in adopted:
            state = ("adopted this pane's auto claim from session %s (its snapshot and "
                     "timestamp; edits made before this begin still land)" % adopted[path])
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
        self.foreign = {}          # hunk number -> the journal entry behind it
        self.taken = set()         # hunk numbers landed on purpose with --take-foreign
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
        if self.foreign:
            why.append("%d of %d hunks FOREIGN (another pane's edit; lands only with "
                       "--take-foreign)" % (len(self.foreign), len(self.hunks)))
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
        if self.foreign:
            bits.append("%d FOREIGN" % len(self.foreign))
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
            if number in info.foreign:
                marks = [foreign_mark(info.foreign[number])]
                if number in info.taken:
                    marks.append("yours to land (--take-foreign)")
                elif number not in info.selected:
                    marks.append("held back: a FOREIGN hunk lands only with --take-foreign")
            else:
                marks = ["CONTESTED" if number in info.contested else "yours"]
                if number not in info.selected:
                    marks.append("LEFT OUT by --exclude-hunk/--only-hunk")
            log("  hunk %d of %d  +%d -%d  %s  %s"
                % (number, len(info.hunks), hunk.plus, hunk.minus, hunk.header(),
                   ", ".join(marks)))
            if (bodies or number in info.contested or number in info.foreign
                    or number not in info.selected):
                sys.stdout.write("".join("    " + line for line in hunk.body()))
        if not bodies:
            log("  (%d hunks, so only the contested, FOREIGN and left-out ones are printed "
                "in full; `--dry-run` prints the whole merged diff)" % len(info.hunks))
        if info.foreign and not info.taken:
            log("  FOREIGN hunks are another pane's edits sitting in your working tree; "
                "--confirm does not land them. To land one on purpose: --take-foreign "
                "%s:%s" % (info.path, ",".join(str(n) for n in sorted(info.foreign))))
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
    log("  take a foreign:  --take-foreign <path>:<n>[,<n>-<m>]   (another pane's hunk, named")
    log("                     above; landed on purpose, and the commit message records whose")
    log("                     edit it was)")
    log("Hunks left out are neither committed nor touched: they stay in the working tree and a "
        "later commit picks them up. A foreign hunk (another pane's, named above) is left out "
        "by --confirm until you take it. Any of these flags prints a new digest.")
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
        data = json.loads(manifest_file.read_text(encoding="utf-8"))
    except (OSError, ValueError):
        data = {}
    if not isinstance(data, dict):
        data = {}
    # The manifest names the tree it materialised too (`who` prints it as the slot's last
    # tree); a manifest from before #76QW is the file map alone and still reads as one.
    have = data["files"] if isinstance(data.get("files"), dict) else data

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
    write_json(manifest_file, {"tree": tree, "files": wanted})
    return written


def manifest_tree(manifest_file):
    """The tree a slot's manifest.json was last materialised from, or None."""
    try:
        data = json.loads(Path(manifest_file).read_text(encoding="utf-8"))
    except (OSError, ValueError):
        return None
    if isinstance(data, dict) and isinstance(data.get("tree"), str):
        return data["tree"]
    return None


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


def relay_build_module():
    """scripts/relay-build loaded as a module, for the environment caps and configure
    flags it computes, or None when it cannot be read or run.

    exec rather than the import system: the file has no .py extension, so
    SourceFileLoader's spec path refuses the name (load_module() would work
    but is deprecated); the wrapper has no import-time side effects anyway.
    """
    try:
        import types
        script = Path(__file__).resolve().parent / "relay-build"
        module = types.ModuleType("relay_build_module")
        module.__file__ = str(script)
        exec(compile(script.read_text(), str(script), "exec"), module.__dict__)
        return module
    except Exception:
        return None


def relay_build_jobs(log, limit_bytes=None):
    """Compile jobs for the verify build, capped by memory like scripts/relay-build.

    The verify build runs wherever land.py runs, including inside an agent pane's
    systemd scope, whose default 8 parallel jobs OOM-killed a pane mid-build
    (card #04EC) -- so the wrapper's cap is loaded rather than copied, and the two
    cannot drift apart. `limit_bytes` overrides the detected limit (tests).
    """
    module = relay_build_module()
    if module is not None:
        try:
            return module.jobs_for_environment(
                os.environ, note=lambda message: log("verify: %s" % message),
                limit=limit_bytes)
        except Exception as exc:  # the cap is a guard, never a gate
            log("verify: memory job cap unavailable (%s); using RELAY_JOBS or 8" % exc)
    else:
        log("verify: scripts/relay-build unreadable; using RELAY_JOBS or 8")
    return os.environ.get("RELAY_JOBS") or "8"


def relay_configure_args(log):
    """The cmake flags scripts/relay-build itself configures a developer build with.

    A verify slot is configured the same way (card #76QW), so what `try` builds is
    what a developer builds. Loaded from the wrapper so the two cannot drift; the
    literal list only ever answers when the wrapper cannot be read.
    """
    module = relay_build_module()
    if module is not None:
        flags = getattr(module, "CONFIGURE_ARGS", None)
        if isinstance(flags, list) and all(isinstance(f, str) for f in flags):
            return list(flags)
        log("verify: scripts/relay-build defines no CONFIGURE_ARGS; using the literal default")
    else:
        log("verify: scripts/relay-build unreadable; using the literal default")
    return ["-DCMAKE_BUILD_TYPE=RelWithDebInfo"]


def memory_for_slots(meminfo="/proc/meminfo"):
    """Bytes of memory the slot pool may count on: the host's MemTotal, else 2 GiB on
    platforms without /proc/meminfo.

    The pool is shared by every session on the host, so it is sized after the host,
    never after the calling pane's cgroup: an agent pane under an 8 GiB MemoryMax used
    to size it to one slot for everyone (card #3MH4). The cgroup cap still bounds each
    build's --parallel (build_jobs), which is where it belongs. MemTotal rather than
    MemAvailable, so every caller computes the same pool at any moment."""
    try:
        with open(meminfo, encoding="utf-8") as handle:
            for line in handle:
                if line.startswith("MemTotal:"):
                    return int(line.split()[1]) * 1024
    except (OSError, ValueError, IndexError):
        pass
    return 2 * 1024 ** 3


def verify_slots(root, env=None, disk_free_bytes=None, mem_bytes=None):
    """How many verify slots this machine can hold (card #76QW).

    RELAY_LAND_VERIFY_SLOTS wins outright. Otherwise a slot holds a whole tree plus its
    build (about 3 GB of disk) and a parallel build plus its tests (about 8 GB of
    memory), so the machine's spare disk and memory bound the pool, clamped to [1, 4]:
    at most 4 concurrent heavy builds on one host, always at least one. `root` is the
    land root (the pool lives on its filesystem); disk_free_bytes and mem_bytes inject
    the measurements for tests.
    """
    env = os.environ if env is None else env
    try:
        asked = int(env.get("RELAY_LAND_VERIFY_SLOTS") or 0)
    except ValueError:
        asked = 0
    if asked > 0:
        return asked
    if disk_free_bytes is None:
        disk_free_bytes = shutil.disk_usage(str(root)).free
    if mem_bytes is None:
        mem_bytes = memory_for_slots()
    gigabyte = 1024 ** 3
    return max(1, min(4, disk_free_bytes // (3 * gigabyte), mem_bytes // (8 * gigabyte)))


def run_verify(repo, root, session, tree, args, log):
    """Build the exact tree this commit would put on the branch.

    Returns (failed step, output, tests failed) or (None, None, False). The working
    tree is never read: that is the whole point, because it holds everyone's
    uncommitted code and proves nothing.
    """
    with verify_slot(repo, root, log, session=session, tree=tree,
                     wait_seconds=getattr(args, "wait_seconds", None)) as base:
        return _run_verify_in(repo, base, tree, args, log)


def slot_prefix(repo):
    """Slots are per repository: two projects landing through one root never share a build."""
    return "%s-%s" % (Path(str(repo)).name or "repo",
                      hashlib.sha1(str(repo).encode("utf-8")).hexdigest()[:8])


def configure_flag_value(flags, name):
    """The value one -D<name>=<value> configure flag carries, or None when absent."""
    prefix = "-D%s=" % name
    for flag in flags:
        if flag.startswith(prefix):
            return flag[len(prefix):]
    return None


def cache_build_type(cache):
    """CMAKE_BUILD_TYPE as a configured slot's CMakeCache.txt records it, '' or None when
    the cache does not say."""
    try:
        for line in Path(cache).read_text(encoding="utf-8").splitlines():
            if line.startswith("CMAKE_BUILD_TYPE:"):
                return line.split("=", 1)[1].strip()
    except OSError:
        return None
    return None


class verify_slot:
    """Hold one of the verify_slots() build slots for this repository, waiting if all are
    busy.

    The lock is an flock on `<slot>.lock`, so a session that dies mid-build frees its
    slot; while it holds one, the lock file also records who is building (`who` prints
    it). Without fcntl (Windows) the first slot is used without a lock.
    """

    def __init__(self, repo, root, log, session=None, tree=None, wait_seconds=None):
        self.base = Path(root) / "verify-slots"
        self.prefix = slot_prefix(repo)
        self.log = log
        self.session, self.tree = session, tree
        self.wait_seconds = wait_seconds      # None waits for as long as it takes
        self.handle = None

    def _note_holder(self):
        """Record who holds the slot in its lock file, already flocked by __enter__."""
        if self.handle is None:
            return
        self.handle.seek(0)
        self.handle.truncate()
        self.handle.write(json.dumps({"pid": os.getpid(), "session": self.session,
                                      "since": time.time(), "tree": self.tree}) + "\n")
        self.handle.flush()

    def __enter__(self):
        self.base.mkdir(parents=True, exist_ok=True)
        slots = [self.base / ("%s-%d" % (self.prefix, n))
                 for n in range(verify_slots(self.base))]
        try:
            import fcntl
        except ImportError:
            self.slot = slots[0]
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
            self.handle, self.slot = handle, slot
            self._note_holder()
            return slot
        # All busy. Wait for whichever slot frees first, saying so at once on stderr --
        # stdout may be a pipe that shows nothing until exit, and a silent wait reads as a
        # hang (card #3MH4: a session was stopped three times while queued here).
        started = time.time()
        announced = None
        while True:
            for slot in order:
                handle = open(str(slot) + ".lock", "a+")
                try:
                    fcntl.flock(handle, fcntl.LOCK_EX | fcntl.LOCK_NB)
                except OSError:
                    handle.close()
                    continue
                self.handle, self.slot = handle, slot
                self._note_holder()
                self.say("verify: got build slot %s after %s"
                         % (slot.name, duration(time.time() - started)))
                return slot
            waited = time.time() - started
            if self.wait_seconds is not None and waited >= self.wait_seconds:
                raise Fail("all %d verify build slot(s) stayed busy for %s (--wait-seconds %s): "
                           "%s. Nothing was landed; run it again later."
                           % (len(slots), duration(waited), self.wait_seconds,
                              self.holders(slots)), code=7)
            if announced is None or time.time() - announced >= 60:
                self.say("verify: all %d build slot(s) busy, waited %s so far: %s"
                         % (len(slots), duration(waited), self.holders(slots)))
                announced = time.time()
            time.sleep(1)

    def say(self, message):
        try:
            sys.stderr.write(message + "\n")
            sys.stderr.flush()
        except OSError:
            pass

    @staticmethod
    def holders(slots):
        """'slot 0: session x (pid n, 4m12s)' for each slot, as its lock file names it."""
        parts = []
        for slot in slots:
            held = read_slot_holder(str(slot) + ".lock")
            if held is None:
                parts.append("%s: freeing" % slot.name)
                continue
            session, pid, seconds = held
            parts.append("%s: %s (pid %s%s)" % (slot.name, session or "?", pid,
                                                 ", %s" % duration(seconds) if seconds else ""))
        return "; ".join(parts)

    def __exit__(self, *exc):
        if self.handle is not None:
            try:
                self.handle.seek(0)
                self.handle.truncate()        # the slot reads free again in `who`
            except OSError:
                pass
            self.handle.close()
        return False


def _run_verify_in(repo, base, tree, args, log):
    """Materialise `tree` in the slot and build it.

    Returns (failed step, output, tests failed): (None, None, False) when it all built,
    otherwise the label and output of the first failing step, with `tests failed` set
    when that step is the ctest run -- its output is returned as a tail, because a full
    ctest log is noise where a compile log is signal.
    """
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
        flags = relay_configure_args(log)
        wanted_type = configure_flag_value(flags, "CMAKE_BUILD_TYPE")
        cache = build / "CMakeCache.txt"
        why = None
        if not cache.exists():
            why = "fresh slot"
        elif wanted_type is not None and cache_build_type(cache) != wanted_type:
            why = ("the slot's CMAKE_BUILD_TYPE is %r, the developer build is %r"
                   % (cache_build_type(cache), wanted_type))
        if why is not None:
            log("verify: configuring (%s)" % why)
            steps.append(("cmake configure",
                          ["cmake", "-S", str(src), "-B", str(build)] + flags))
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
            if label.startswith("ctest"):
                return label, "\n".join(proc.stdout.splitlines()[-60:]), True
            return label, proc.stdout, False
    log("verify: the exact tree builds")
    return None, None, False


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


def session_selection(repo, session, meta, args, log):
    """The claimed paths and hunk selections one landing uses, exactly as commit parses them.

    Everything between a session's meta and its merge attempts: --whole joins the claimed
    set, --paths narrows it, intake files and *.orig drop out with a log line, and the
    --exclude-hunk/--only-hunk/--take-foreign selections are validated -- `--by <pane id or
    #card>` turns a bare `PATH` into the journal-matched hunks at tree time. Raises the same
    Fail refusals commit raises. Returns (claimed, paths, whole, excludes, onlys, takes,
    excludes_bare, onlys_bare) -- the `_bare` sets hold the paths whose numbers come from
    `--by` and are only resolved once their hunks exist.
    """
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
                       % (path, session, path, session, path, path))
        if path not in paths:
            paths.append(path)
    for path in dropped:
        log("not committing %s (intake file or *.orig)" % path)
    if not paths:
        raise Fail("nothing to commit")

    def split_selection(values, flag):
        """`{path: numbers}` for PATH:N values, plus the bare PATHs left for --by."""
        numbered, bare = [], []
        for value in values or []:
            spec = str(value).strip()
            (numbered if ":" in spec else bare).append(spec)
        return parse_selection(repo, numbered, flag), \
            sorted(set(norm_path(repo, spec) for spec in bare))

    excludes, excludes_bare = split_selection(args.exclude_hunk, "--exclude-hunk")
    onlys, onlys_bare = split_selection(args.only_hunk, "--only-hunk")
    bare = sorted(set(excludes_bare) | set(onlys_bare))
    by = getattr(args, "by", None)
    if bare and not by:
        raise Fail("%s: name the hunks positionally (%s:%s) or give --by <pane id or #card> "
                   "to pick them from the authorship journal"
                   % (", ".join(bare), bare[0], "1,2"))
    both = sorted((set(excludes) | set(excludes_bare)) & (set(onlys) | set(onlys_bare)))
    if both:
        raise Fail("--exclude-hunk and --only-hunk both name %s; pick one form per path"
                   % ", ".join(both))
    for path in sorted(set(excludes) | set(onlys) | set(bare)):
        if path not in paths:
            raise Fail("%s is not one of this commit's paths (%s)" % (path, ", ".join(paths)))
        if path in whole:
            raise Fail("%s is a --whole path: it has no snapshot, so it has no numbered hunks"
                       % path)
    takes = parse_selection(repo, args.take_foreign, "--take-foreign")
    for path in sorted(takes):
        if path not in paths:
            raise Fail("%s is not one of this commit's paths (%s)" % (path, ", ".join(paths)))
        if path in whole:
            raise Fail("%s is a --whole path: it has no snapshot, so it has no numbered hunks"
                       % path)
        if path in excludes and takes[path] & excludes[path]:
            raise Fail("--exclude-hunk and --take-foreign both name %s:%s; pick one"
                       % (path, ",".join(str(n) for n in sorted(takes[path] & excludes[path]))))
        if path in onlys and not takes[path] <= onlys[path]:
            raise Fail("--only-hunk already leaves %s:%s out, so --take-foreign cannot land "
                       "it" % (path, ",".join(str(n) for n in sorted(takes[path] - onlys[path]))))
    return (claimed, paths, whole, excludes, onlys, takes, excludes_bare, onlys_bare)


def session_tree(repo, root, session, meta, args, tip, log=None, selection=None):
    """The landing tree for `session` against `tip`: one attempt of cmd_commit's computation.

    This is commit's per-path loop through the merge-conflict check, verbatim: each path's
    PathInfo (hunks, selections, contested/foreign marks) and its plan against `tip`,
    honouring --paths/--whole and --exclude-hunk/--only-hunk/--take-foreign and the safety
    refusals. Raises Fail (code 3) when any path conflicts. Returns (plans, infos,
    conflicts); `conflicts` is always empty when the call returns.
    """
    if log is None:
        log = lambda message: print(message, file=sys.stderr)
    if selection is None:
        selection = session_selection(repo, session, meta, args, log)
    (claimed, paths, whole, excludes, onlys, takes, excludes_bare, onlys_bare) = selection
    branch = args.branch or meta.get("branch", DEFAULT_BRANCH)
    plans, infos, conflicts = {}, {}, []
    for path in paths:
        record = claimed.get(path, {"existed": False, "tracked_at_begin": False})
        info = PathInfo(path)
        infos[path] = info
        info.holders = claimants(root, session, path)
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
            info.snapshot = snapshot_bytes(root, session, path, record)
            info.hunks = path_hunks(info.snapshot, working)
            if path in onlys_bare or path in excludes_bare:
                # `--only-hunk PATH --by <pane|#card>`: the hunks whose removed+added
                # blocks the authorship journal attributes to that pane or card, wherever
                # they sit now -- numbering may have moved under a concurrent edit.
                flag = "--only-hunk" if path in onlys_bare else "--exclude-hunk"
                if path in onlys_bare:
                    onlys[path] = set(journal_hunk_numbers(
                        root, repo, path, getattr(args, "by", None), info.hunks, flag))
                else:
                    excludes[path] = set(journal_hunk_numbers(
                        root, repo, path, getattr(args, "by", None), info.hunks, flag))
            if not info.hunks and path not in whole:
                # Two sessions were caught by this: a file edited BEFORE `begin` snapshots as
                # already-edited, so there is nothing to land and the silence looked like success.
                log("  %s: no change since your snapshot. If you edited it before `begin`, run "
                    "`begin %s --base main %s` to snapshot it from the tip instead."
                    % (path, session, path))
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
            info.contested = contested_hunks(root, session, path, info.snapshot,
                                             info.hunks, info.holders,
                                             forced=bool(info.from_base))
            # FOREIGN: a held hunk whose removed+added lines are exactly the change
            # another pane's authorship journal recorded for this path is that pane's
            # edit sitting in your working tree (#6CSN confirmed such a review and
            # landed #234Z's hunk as its own). Only held paths need it — an
            # uncontested path has no review to read past. No journal, an unreadable
            # blob, or no match, and the hunk stays merely CONTESTED, as today.
            if info.contested or info.stale or info.from_base:
                mine = {t for t in (my_token(), meta.get("token")) if t}
                found = foreign_hunks(root, repo, path, mine)
                for number, hunk in enumerate(info.hunks, 1):
                    entry = found.get(hunk_blocks(hunk))
                    if entry is not None:
                        info.foreign[number] = entry
            info.taken = (takes.get(path) or set()) & set(info.foreign)
            stray_takes = sorted((takes.get(path) or set()) - set(info.foreign))
            if stray_takes:
                raise Fail("--take-foreign %s:%s — hunk %s of %s is not FOREIGN right now "
                           "(yours, or merely contested, or the numbering moved). Run "
                           "commit again without --take-foreign to see the hunks numbered."
                           % (path, ",".join(str(n) for n in stray_takes),
                              ",".join(str(n) for n in stray_takes), path))
            if info.foreign:
                # A FOREIGN hunk is somebody else's edit: --confirm leaves it in the
                # working tree. Leaving it out rides the --exclude-hunk machinery, so
                # the digest, the merge and the snapshot left behind all treat it as
                # "still to land later".
                info.selected -= set(info.foreign) - info.taken
                info.excluded = numbers - info.selected
            if info.excluded:
                if not info.selected:
                    continue          # every hunk left out: this path is not in the commit
                theirs = apply_hunks(info.snapshot, working, info.hunks, info.selected)

        try:
            outcome = plan_path(repo, root, session, path, record, tip,
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
    return plans, infos, conflicts


def refused_cancelled(root, meta, session):
    """A session whose owner thread was stopped has a cancelled marker under
    `<root>/cancelled/`; the backend wrote it, and `commit`/`try` refuse to land its
    hunks -- they may be half of an edit the thread never finished."""
    owner = meta.get("owner")
    if not owner:
        return
    marker = Path(root) / "cancelled" / str(owner)
    if marker.exists():
        raise Fail("session %s's owner thread %s was cancelled (marker %s); refusing to "
                   "land its hunks -- begin a new session, or delete the marker if the "
                   "thread is really alive" % (session, owner, marker))


def cmd_commit(args, log):
    repo = repo_root()
    root = Path(args.root)
    meta = read_meta(root, args.session)
    refused_cancelled(root, meta, args.session)
    auto_gc(root, log)
    branch = args.branch or meta.get("branch", DEFAULT_BRANCH)
    if Path(meta.get("repo", repo)) != Path(repo):
        raise Fail("session %r was started in %s, not %s"
                   % (args.session, meta.get("repo"), repo))

    message = read_message(args.message)
    selection = session_selection(repo, args.session, meta, args, log)
    (claimed, paths, whole, excludes, onlys, takes, excludes_bare, onlys_bare) = selection

    warn_overlaps(root, args.session, paths, log)

    marker = publication_marker(repo)
    if marker.get("mode", "legacy") != "legacy":
        if not args.dry_run:
            require_legacy_publisher(repo, "commit", marker)
        log("note: this repository publishes through relay-land (mode %s); a real commit "
            "would be refused" % marker["mode"])

    last_error, verified = None, None
    for attempt in range(1, SWAP_ATTEMPTS + 1):
        tip = branch_tip(repo, branch)          # read once per attempt, used everywhere below
        plans, infos, conflicts = session_tree(
            repo, root, args.session, meta, args, tip, log, selection)
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
                    "build slots under %s before the swap.)" % (verify_slots(root), root / "verify-slots"))
            return 0

        if args.no_verify and held:
            raise Fail("--no-verify is refused while anything is contested, FOREIGN or "
                       "stale (%s). Those are exactly the landings that broke the build on "
                       "2026-09-19: the working tree compiled because the other session's "
                       "other half was sitting in it, and the tree that went onto the "
                       "branch did not." % ", ".join(held))
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
                step, output, tests_failed = run_verify(repo, root, args.session, tree,
                                                        args, log)
                if step is not None:
                    if tests_failed:
                        sys.stdout.write(output + "\n")
                    else:
                        sys.stdout.write("\n".join(first_errors(output)) + "\n")
                    raise Fail("the exact tree this commit would put on %s does not build "
                               "(%s failed). Nothing was landed. The working tree is not the "
                               "same thing: it holds every session's uncommitted code, so it "
                               "can compile while this tree cannot. Look at the errors above; "
                               "a missing declaration usually means you are landing one half "
                               "of somebody else's change (`land.py who`)."
                               % (branch, step), code=5)
                verified = cxx

        taken_notes = [(path, number, infos[path].foreign[number])
                       for path in sorted(infos) for number in sorted(infos[path].taken)]
        commit_message = message
        if taken_notes:
            # One trailer per taken hunk: the commit message says whose edit it carries.
            commit_message = "%s\n\n%s" % (
                message.rstrip("\n"),
                "\n".join("Relay-take-foreign: %s:%d from pane %s"
                          % (path, number, (entry.get("pane") or "unknown pane").strip())
                          for path, number, entry in taken_notes))
        new = build_commit(repo, tip, entries, commit_message, tree=tree)
        touched = [line for line in git_out(repo, "diff", "--no-renames", "--name-only", tip, new).splitlines()
                   if line]
        if sorted(touched) != sorted(entries):
            raise Fail("name gate failed: the commit would touch %s but this session's paths "
                       "are %s. Nothing was landed."
                       % (sorted(touched), sorted(entries)), code=3)

        with legacy_publication(repo, "commit"):
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
                for number in sorted(set(info.foreign) & info.excluded):
                    log("    hunk %d %s; it stays in the working tree until its pane lands "
                        "it, or you take it with --take-foreign %s:%d"
                        % (number, foreign_mark(info.foreign[number]), path, number))
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

        if not getattr(args, "no_cards", False):
            try:
                append_commit_to_cards(repo, new, commit_message, log)
            except Exception as exc:  # the commit already landed; card bookkeeping is best effort
                log("cards: could not record %s (%s)" % (new[:12], exc))
        for path, number, entry in taken_notes:
            note_taken_on_card(repo, path, number, entry, new, args.session, log)

        print(new)
        return 0

    raise Fail("%s moved under every one of the %d attempts (last: %s). Nothing was landed; "
               "run commit again." % (branch, SWAP_ATTEMPTS, last_error or "swap refused"),
               code=3)


# --------------------------------------------------------------------------- try

def cmd_try(args, log):
    """Build and optionally test the exact tree `commit` would land -- without landing it.

    The working tree holds every session's uncommitted code, so a green build there proves
    nothing about what a landing would put on the branch (card #76QW). `try` materialises
    tip + this session's claimed hunks -- the very merge `commit` would swap in -- into a
    verify slot and builds it there, without moving a ref or touching the shared index or
    the working tree. `--commit`/`--tree` skip the merge and materialise that revision
    instead, for post-land checks and for a Try-it run.

    Nothing lands, so nothing is held: contested or stale paths are built like any other,
    and their stat line still says what they are. A conflict still stops the try (exit 3).

    stdout is a machine-readable summary, in this order: `tip <sha>` (or `commit <sha>` in
    --commit/--tree mode), one stat line per claimed path in the tree, `src <slot>/src`,
    `build <slot>/build`, `binary <slot>/build/<target>` (nothing when --verify-cmd built
    something else). With --print-binary, stdout is the binary path alone. Everything else
    -- logs, compiler errors, ctest tails -- goes to stderr. Exits: 0 built (and tested),
    1 usage, 3 the merge conflicts, 5 the build (or py-compile or --verify-cmd) failed,
    6 --tests failed. A failed try leaves the slot's tree in place for inspection.
    """
    repo = repo_root()
    root = Path(args.root)
    if args.commit and args.tree:
        raise Fail("--commit and --tree are two ways to name one revision; give one", code=1)
    meta = read_meta(root, args.session)
    refused_cancelled(root, meta, args.session)
    auto_gc(root, log)
    branch = args.branch or meta.get("branch", DEFAULT_BRANCH)
    if Path(meta.get("repo", repo)) != Path(repo):
        raise Fail("%s was begun in %s, not %s" % (args.session, meta.get("repo"), repo),
                   code=1)
    log = lambda message: sys.stderr.write(message + "\n")  # stdout stays machine-readable

    def say(line):
        if not args.print_binary:
            sys.stdout.write(line + "\n")

    target = args.target
    if args.commit or args.tree:
        given = args.commit or args.tree
        kind = "commit" if args.commit else "tree"
        try:
            revision = git_out(repo, "rev-parse", "--verify", "%s^{%s}" % (given, kind))
            tree = git_out(repo, "rev-parse", "--verify", "%s^{tree}" % revision)
        except Fail:
            raise Fail("%s does not name a %s in %s" % (given, kind, repo), code=1)
        say("commit %s" % revision)
    else:
        tip = branch_tip(repo, branch)
        plans, infos, _conflicts = session_tree(repo, root, args.session, meta, args, tip,
                                                log)
        say("tip %s" % tip)
        for path in sorted(plans):
            say(infos[path].stat())
        problem = py_compile_landed(plans)
        if problem is not None:
            raise Fail("the exact bytes this commit would land do not compile as Python:\n"
                       "%s\nNothing was landed and nothing was touched." % problem.strip(),
                       code=5)
        entries = {}
        for path, (content, mode) in plans.items():
            if content is None:
                entries[path] = (None, mode, True)
            else:
                entries[path] = (hash_blob(repo, content), mode, False)
        tree = build_tree(repo, tip, entries)

    verify = argparse.Namespace(verify_cmd=args.verify_cmd, verify_tests=args.tests,
                                verify_target=target)
    binary = None
    with verify_slot(repo, root, log, session=args.session, tree=tree,
                     wait_seconds=args.wait_seconds) as base:
        step, output, tests_failed = _run_verify_in(repo, base, tree, verify, log)
        if step is None:
            say("src %s" % (base / "src"))
            say("build %s" % (base / "build"))
            if not args.verify_cmd:
                binary = base / "build" / target
                say("binary %s" % binary)
        elif tests_failed:
            sys.stderr.write(output + "\n")
            raise Fail("the tests failed (%s) in the tree this try built on %s. Nothing was "
                       "landed and nothing was touched; the failing tree is still in %s."
                       % (step, branch, base / "build"), code=6)
        else:
            sys.stderr.write("\n".join(first_errors(output)) + "\n")
            raise Fail("the tree this try built does not compile (%s failed). Nothing was "
                       "landed and nothing was touched. The working tree is not the same "
                       "thing: it holds every session's uncommitted code, so it can compile "
                       "while this tree cannot. Look at the errors above; a missing "
                       "declaration usually means you are landing one half of somebody "
                       "else's change (`land.py who`). The failing tree is still in %s."
                       % (step, base / "src"), code=5)
    if args.print_binary and binary is not None:
        print(binary)
    return 0


# Card links (#MJ76): a landed commit whose message names a card as `#ID` is appended to that
# card's `links.commits`, kept in commit order, so the card's commit sequence fills itself and
# QA's "newest hash" is its last entry. The card file is written in the working tree *after* the
# swap and is not part of the commit it records; it lands with the session's next commit.
CARD_REF_RE = re.compile(r"#([0-9A-HJKMNP-TV-Z]{4})(?![0-9A-Za-z])")


def note_taken_on_card(repo, path, number, entry, sha, session, log):
    """Landing a FOREIGN hunk with --take-foreign (decision 4 of #WNKN) leaves a note on that
    hunk's card thread, so the pane that wrote the edit sees where it went. Written in the
    working tree after the swap, like the card links, and never a failure of the commit."""
    card = str(entry.get("card") or "").lstrip("#").strip()
    if not card:
        return
    try:
        thread = Path(repo) / ".board" / "threads" / ("%s.md" % card)
        thread.parent.mkdir(parents=True, exist_ok=True)
        stamp = _dt.datetime.now(_dt.timezone.utc).strftime("%Y%m%dT%H%M%SZ")
        suffix = hashlib.sha256(("%s %s %d" % (stamp, path, number))
                                .encode("utf-8")).hexdigest()[:2]
        note = ("hunk %s:%d (pane %s's edit) landed by session %s in commit %s via land.py "
                "--take-foreign" % (path, number, (entry.get("pane") or "unknown pane").strip(),
                                    session, sha[:12]))
        with thread.open("a", encoding="utf-8") as fh:
            if thread.stat().st_size:
                fh.write("\n")
            fh.write("<!-- relay:entry %s-%s author=land.py kind=note -->\n%s\n"
                     % (stamp, suffix, note))
        log("cards: noted the take on #%s" % card)
    except Exception as exc:  # noqa: BLE001 - the commit already landed
        log("cards: could not note the take on #%s (%s)" % (card, exc))


def append_commit_to_cards(repo, sha, message, log):
    """Append `sha` to `links.commits` of every card `message` names as `#ID`. Best effort:
    no board, no backend or a card another writer changed meanwhile is logged and skipped,
    never a failure -- the commit has already landed."""
    ids = sorted(set(CARD_REF_RE.findall(message)))
    if not ids:
        return []
    backend = str(Path(repo) / "backend")
    if backend not in sys.path:
        sys.path.insert(0, backend)
    try:
        from relay_core import board as B
    except Exception as exc:        # noqa: BLE001 - a checkout without the backend
        log("cards: not recording %s on %s (%s)" % (sha[:12], ", ".join(ids), exc))
        return []
    folder = B.board_folder(repo)
    if folder is None:
        return []
    board = B.Board(folder, repo)
    by_id = {card.id: card for card in board.cards() if card.id in ids}
    done = []
    for card_id in ids:
        card = by_id.get(card_id)
        if card is None:
            continue
        for _attempt in range(3):
            base = B.file_hash(card.path)
            links = dict(B.card_links(card))
            commits = [str(c) for c in (links.get("commits") or [])] \
                if isinstance(links.get("commits"), list) else []
            merged = B.normalize_commits(Path(repo), commits + [sha], drop_unresolved=True)
            if merged == commits:
                break
            links["commits"] = merged
            card.set("links", links)
            try:
                board.save(card, base_hash=base)
            except B.BoardConflict:
                card = board.card_by_id(card_id)
                if card is None:
                    break
                continue
            done.append(card_id)
            break
    if done:
        log("cards: recorded %s on %s (links.commits)" % (sha[:12], ", ".join("#" + i for i in done)))
    return done


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
    - a session directory with no meta.json and no registry entry (a crashed `begin`);
    - authorship journal lines older than GC_DAYS, and blobs of that age that no
      surviving line names (attributions that old can no longer be claimed by anyone).
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
        if name in ("verify-slots", "authors", "blobs") or name.startswith("."):
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

    # The authorship journal ages with the sessions it can still attribute. Lines older
    # than GC_DAYS are dropped (a journal file left with none is unlinked), and a blob of
    # that age goes unless some surviving line still names it.
    freed = 0
    old_lines = 0
    files = []           # blobs to remove as single files
    if (root / "authors").is_dir():
        old = _dt.datetime.now().timestamp() - GC_DAYS * 86400
        needed = set()
        for journal in sorted((root / "authors").glob("*.jsonl")):
            try:
                raw_lines = journal.read_text(encoding="utf-8").splitlines()
            except OSError:
                continue
            kept, dropped = [], 0
            for raw in raw_lines:
                try:
                    entry = json.loads(raw)
                    when = float(entry.get("ts") or 0)
                except (ValueError, AttributeError):
                    kept.append(raw)          # unreadable lines are not ours to judge
                    continue
                if when < old:
                    dropped += 1
                else:
                    kept.append(raw)
                    needed.add(entry.get("before"))
                    needed.add(entry.get("after"))
            if not dropped:
                continue
            try:
                was = journal.stat().st_size
            except OSError:
                continue
            old_lines += dropped
            if not quiet or dry_run:
                log("gc: %s %d old authorship journal line%s from %s"
                    % ("would prune" if dry_run else "pruned", dropped,
                       "" if dropped == 1 else "s", journal))
            if not dry_run:
                if kept:
                    journal.write_text("\n".join(kept) + "\n", encoding="utf-8")
                    freed += was - sum(len(raw) + 1 for raw in kept)
                else:
                    journal.unlink(missing_ok=True)
                    freed += was
        if (root / "blobs").is_dir():
            for blob in sorted((root / "blobs").glob("*")):
                if blob.name in needed:
                    continue
                try:
                    if _dt.datetime.now().timestamp() - blob.stat().st_mtime < GC_DAYS * 86400:
                        continue
                except OSError:
                    continue
                files.append((blob, "old authorship blob"))

    for path, why, name in doomed:
        size = tree_bytes(path)
        freed += size
        if not quiet or dry_run:
            log("gc: %s %s (%s, %s)" % ("would remove" if dry_run else "removed",
                                        path, why, human_bytes(size)))
        if not dry_run:
            shutil.rmtree(str(path), ignore_errors=True)
    for path, why in files:
        try:
            size = path.stat().st_size
        except OSError:
            continue
        freed += size
        if not quiet or dry_run:
            log("gc: %s %s (%s, %s)" % ("would remove" if dry_run else "removed",
                                        path, why, human_bytes(size)))
        if not dry_run:
            try:
                path.unlink()
            except OSError:
                pass
    names = [name for _, _, name in doomed if name]
    if names and not dry_run:
        def mutate(data):
            for name in names:
                data["sessions"].pop(name, None)
        edit_registry(root, mutate)
    if (doomed or files) and quiet and not dry_run:
        log("gc: reclaimed %s from %d stale land director%s under %s"
            % (human_bytes(freed), len(doomed), "y" if len(doomed) == 1 else "ies", root))
    return len(doomed) + len(files) + old_lines, freed


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
        if entry.get("reaped") or not is_idle(entry):
            continue
        findings += 1
        log("stale land session: %s has not run a land.py command for %s (claims: %s). It is "
            "ignored for contest detection; `land.py abandon %s` drops it, which never touches "
            "the working tree."
            % (name, human_age(session_idle_minutes(entry)),
               ", ".join(entry.get("claims") or []) or "none", name))

    holding = reaped_holding(repo, Path(args.root))
    if holding:
        findings += 1
        log("%d reaped session(s) with uncommitted hunks; see `land.py orphans`" % len(holding))

    if not findings:
        log("clean: nothing staged from an older commit, and nothing else to report")
    return 0 if findings == 0 else 2


# --------------------------------------------------------------------------- who

def read_slot_holder(lock_path):
    """Who holds a verify slot: (session, pid, seconds held) read from its lock file, when
    that names a live process. A lock file left behind by a crashed or killed build reads
    as free: the flock it was written under is gone, and so is the pid."""
    try:
        note = json.loads(Path(lock_path).read_text(encoding="utf-8"))
        pid = int(note["pid"])
    except (OSError, ValueError, KeyError, TypeError):
        return None
    try:
        os.kill(pid, 0)
    except ProcessLookupError:
        return None
    except PermissionError:
        pass
    since = note.get("since")
    held = time.time() - since if isinstance(since, (int, float)) else None
    return note.get("session"), pid, held


def cmd_who(args, log):
    root = Path(args.root)
    auto_gc(root, log)
    sessions = registered_sessions(root)
    if not sessions:
        log("no land sessions under %s" % root)
        return 0
    log("land sessions under %s:" % root)
    try:
        holding = set(reaped_holding(repo_root(), root))
    except Fail:
        holding = set()             # not in a checkout: nothing to fold, list everything
    reaped = 0
    for name, entry in sorted(sessions.items()):
        if name in holding:
            reaped += 1
            continue
        idle = session_idle_minutes(entry)
        state = "STALE, idle %s (ignored for contest detection)" % human_age(idle) \
            if is_idle(entry) else "idle %s" % human_age(idle)
        extra = ""
        if entry.get("card"):
            extra += ", card %s" % entry["card"]
        if entry.get("owner"):
            extra += ", owner %s" % entry["owner"]
        log("%s — %s, contact: %s%s" % (name, state, entry.get("contact") or "none given", extra))
        claims = entry.get("claims") or []
        for path in claims[:WHO_PATHS]:
            log("    %s  snapshot %s old" % (path, human_age(claim_age(root, name, path))))
        if len(claims) > WHO_PATHS:
            log("    ... and %d more (%s/%s/meta.json has them all)"
                % (len(claims) - WHO_PATHS, root, name))
    if reaped:
        log("%d reaped session(s) with uncommitted hunks; see `land.py orphans`" % reaped)
    slots = root / "verify-slots"
    log("disk: %s holds %s, of which verify build slots %s (at most %d per repository; "
        "`land.py gc` reclaims stale sessions)"
        % (root, human_bytes(tree_bytes(root)),
           human_bytes(tree_bytes(slots)) if slots.exists() else "0 B", verify_slots(root)))
    if slots.is_dir():
        for slot in sorted(p for p in slots.iterdir() if p.is_dir()):
            last = manifest_tree(slot / "manifest.json")
            tail = ", last tree %s" % last[:12] if last else ", no tree yet"
            holder = read_slot_holder(str(slot) + ".lock")
            if holder is None:
                log("    slot %s: free%s" % (slot.name, tail))
            else:
                session, pid, held = holder
                log("    slot %s: session %s (pid %s), held %s%s"
                    % (slot.name, session or "unknown", pid,
                       human_age(held / 60) if held is not None else "an unknown time",
                       tail))
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

    if not args.dry_run:
        require_legacy_publisher(repo, "repair")

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

        with legacy_publication(repo, "repair"):
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


# ------------------------------------------------- uncommitted work (card #FYEY)

def reaped_holding(repo, root):
    """Registry names of `reaped` sessions of `repo` that still hold uncommitted hunks --
    exactly the sessions `orphans` lists, in name order. `who` and `doctor` fold them
    into one line instead of listing each."""
    out = []
    for name, entry in sorted(registered_sessions(root).items()):
        if not entry.get("reaped") or entry.get("repo") != str(repo):
            continue
        if any(hunks for _path, hunks, _age in session_hunks(repo, root, name, entry)):
            out.append(name)
    return out


def session_hunks(repo, root, name, entry):
    """Per-claim hunk counts for one registry entry: [(path, hunks, age_minutes)].

    The same measure everywhere on this card: len(path_hunks(snapshot, working copy)) per
    claimed path -- 0 when the file matches its snapshot, a whole-file hunk when it was
    deleted or is new -- and the snapshot's age in minutes next to it.
    """
    try:
        meta = read_meta(root, name)
    except Fail:
        meta = {}
    paths_meta = meta.get("paths") or {}
    out = []
    for path in sorted(entry.get("claims") or []):
        record = paths_meta.get(path) or {}
        snapshot = None
        try:
            snapshot = snapshot_bytes(root, name, path, record)
        except (Fail, OSError):
            pass
        hunks = len(path_hunks(snapshot, work_bytes(repo, path)))
        out.append((path, hunks, age_minutes(record.get("at"))))
    return out


def uncommitted_total(claims):
    return sum(claim["hunks"] for claim in claims)


def repo_of(entry):
    """The repo a registry entry was begun in, or None when it names none."""
    repo = entry.get("repo")
    return Path(str(repo)) if repo else None


def reap_card_id(entry, args):
    """The card a reap note goes to: --card, else a #ID in the session's contact, else none."""
    if getattr(args, "card", None):
        return args.card
    entry_card = entry.get("card")
    if entry_card:
        return entry_card
    found = CARD_REF_RE.search(entry.get("contact") or "")
    return "#%s" % found.group(1) if found else ""


def journal_blocks(root, repo, path, by):
    """The hunk blocks -- (removed lines, added lines) tuples -- the authorship journal
    attributes to pane id or card `by` on `path`, and how many journal lines cover the
    path at all (any author). The same matcher FOREIGN attribution uses: every journal is
    read, only lines naming this repo and path newer than IDLE_HOURS are considered, and
    only lines whose before/after blobs both read and are text (a null `before` is a new
    file: the empty pre-image)."""
    blocks, covered, matched = set(), 0, 0
    authors_dir = Path(root) / "authors"
    try:
        names = sorted(authors_dir.iterdir())
    except OSError:
        return blocks, covered, matched
    cutoff = time.time() - IDLE_HOURS * 3600
    for name in names:
        if not name.name.endswith(".jsonl"):
            continue
        token = name.name[: -len(".jsonl")].split(".")[0]
        if not token:
            continue
        for entry in read_journal(root, token):
            if entry.get("repo") != str(repo) or entry.get("path") != path:
                continue
            covered += 1
            try:
                when = float(entry.get("ts") or 0)
            except (TypeError, ValueError):
                continue
            if when < cutoff:
                continue
            if by.startswith("#"):
                if (entry.get("card") or "") != by:
                    continue
            elif (entry.get("pane") or "").strip() != by:
                continue
            before = b"" if entry.get("before") is None else journal_blob(root, entry.get("before"))
            after = journal_blob(root, entry.get("after"))
            if before is None or after is None or is_binary(before) or is_binary(after):
                continue
            matched += 1
            for hunk in split_hunks(lines_of(before), lines_of(after)):
                blocks.add(hunk_blocks(hunk))
    return blocks, covered, matched


def journal_hunk_numbers(root, repo, path, by, hunks, flag):
    """Which of `hunks` (1-based) match the journal blocks of `by`. Refuses when no journal
    covers the path at all, or none covers it by `by`: positional numbers are the fallback."""
    blocks, covered, matched = journal_blocks(root, repo, path, by)
    if not covered:
        raise Fail("no authorship journal covers %s, so --by %s has nothing to match; name "
                   "the hunks positionally instead (%s %s:N)"
                   % (path, by, flag, path))
    if not matched:
        raise Fail("no journal entry by %s covers %s, so --by %s has nothing to match; name "
                   "the hunks positionally instead (%s %s:N)"
                   % (by, path, by, flag, path))
    return [number for number, hunk in enumerate(hunks, 1) if hunk_blocks(hunk) in blocks]


def thread_note(repo, card, note, log):
    """Append one `author=land.py kind=note` entry to a card's thread, in the format
    note_taken_on_card uses for --take-foreign. Best effort: the reap itself is done."""
    card_id = str(card or "").lstrip("#").strip()
    if not card_id:
        return
    try:
        thread = Path(repo) / ".board" / "threads" / ("%s.md" % card_id)
        thread.parent.mkdir(parents=True, exist_ok=True)
        stamp = _dt.datetime.now(_dt.timezone.utc).strftime("%Y%m%dT%H%M%SZ")
        suffix = hashlib.sha256(("%s %s" % (stamp, note)).encode("utf-8")).hexdigest()[:2]
        prefix = "\n" if thread.exists() and thread.stat().st_size else ""
        with thread.open("a", encoding="utf-8") as fh:
            fh.write("%s<!-- relay:entry %s-%s author=land.py kind=note -->\n%s\n"
                     % (prefix, stamp, suffix, note))
    except Exception as exc:
        log("cards: could not note #%s (%s)" % (card_id, exc))


def contact_pane(contact):
    """The pane id in a `pane <id>` contact line (how the backend writes auto claims)."""
    # No "<" + "(" in this file, ever: the Safety test greps for process substitution.
    text = contact or ""
    start = text.find("pane <")
    if start < 0:
        return ""
    end = text.find(">", start + 6)
    return text[start + 6:end] if end > start else ""


def session_pane(entry, meta):
    """The pane id a session belongs to: its owner thread, else a `pane <id>` contact."""
    owner = (meta or {}).get("owner") or entry.get("owner")
    if owner:
        return owner
    return contact_pane(entry.get("contact")) or "unknown"


def cmd_status(args, log):
    """Every session registered with this token, and what it still holds uncommitted.

    stdout: one JSON object `{"token", "sessions": [{"session", "auto", "card",
    "claims": [{"path", "hunks", "snapshot_age_minutes"}]}], "uncommitted"}` when --json,
    the same as readable text otherwise. Exits 0 even when the token holds nothing.
    """
    repo = repo_root()
    root = Path(args.root)
    sessions = []
    for name in sorted(registered_sessions(root)):
        entry = registered_sessions(root)[name]
        if entry.get("token") != args.token or repo_of(entry) != Path(repo):
            continue
        claims = [{"path": path, "hunks": hunks,
                   "snapshot_age_minutes": round(age, 1) if age is not None else 0.0}
                  for path, hunks, age in session_hunks(repo, root, name, entry)]
        sessions.append({"session": name, "auto": bool(entry.get("auto")),
                         "card": entry.get("card") or "", "claims": claims})
    payload = {"token": args.token, "sessions": sessions,
               "uncommitted": sum(uncommitted_total(s["claims"]) for s in sessions)}
    if args.json:
        print(json.dumps(payload))
        return 0
    log("token %s, %d uncommitted hunk(s)" % (args.token, payload["uncommitted"]))
    for session in sessions:
        log("  %s%s%s" % (session["session"], " (auto)" if session["auto"] else "",
                          " %s" % session["card"] if session["card"] else ""))
        for claim in session["claims"]:
            log("    %s: %d hunk(s), snapshot %s"
                % (claim["path"], claim["hunks"], human_age(claim["snapshot_age_minutes"])))
    return 0


def cmd_board_sync(args, log):
    """Land the whole working copy of `.board/` paths, with no build gate and no session.

    The pane's own board writes: cards are per-file and threads append-only, so `--whole`
    semantics are right and there is nothing to hold for review. No `begin` is needed and no
    claim is created or disturbed. Prints the new sha (or `nothing to land` when every path
    already matches the tip) and exits 0.
    """
    repo = repo_root()
    root = Path(args.root)
    branch = args.branch or DEFAULT_BRANCH
    paths = []
    for given in args.paths:
        path = norm_path(repo, given)
        if excluded(path):
            raise Fail("board-sync never lands %s (intake file or *.orig)" % path)
        if path != ".board" and not path.startswith(".board/"):
            raise Fail("board-sync lands only paths under .board/: %s is not" % path)
        if path not in paths:
            paths.append(path)
    message = read_message(args.message)
    marker = publication_marker(repo)
    if marker.get("mode") == "queue":
        return board_sync_via_queue(repo, paths, args.token, message, marker, log)
    require_legacy_publisher(repo, "board-sync", marker)
    last_error = None
    for attempt in range(1, SWAP_ATTEMPTS + 1):
        tip = branch_tip(repo, branch)
        entries = {}
        for path in paths:
            working = work_bytes(repo, path)
            if working == rev_bytes(repo, tip, path):
                continue            # already on the tip: nothing to land for this path
            mode = work_mode(repo, path) or "100644"
            entries[path] = (hash_blob(repo, working) if working is not None else None,
                             mode, working is None)
        if not entries:
            log("nothing to land")
            return 0
        tree = build_tree(repo, tip, entries)
        new = build_commit(repo, tip, entries, message, tree=tree)
        touched = [line for line in git_out(repo, "diff", "--no-renames", "--name-only",
                                            tip, new).splitlines() if line]
        if sorted(touched) != sorted(entries):
            raise Fail("name gate failed: the commit would touch %s but board-sync's paths "
                       "are %s. Nothing was landed."
                       % (sorted(touched), sorted(entries)), code=3)
        with legacy_publication(repo, "board-sync"):
            swap = git(repo, "update-ref", "refs/heads/%s" % branch, new, tip, check=False)
        if swap.returncode != 0:
            last_error = swap.stderr.strip()
            log("%s moved under attempt %d (was %s); retrying against the new tip"
                % (branch, attempt, tip[:12]))
            continue
        log_line(root, "%s %s -> %s (board-sync %s) [%s]"
                 % (branch, tip[:12], new[:12], args.token, " ".join(sorted(entries))))
        set_shared_index(repo, branch, entries, log)
        print(new)
        return 0
    raise Fail("%s moved under every one of the %d attempts (last: %s). Nothing was landed; "
               "run board-sync again." % (branch, SWAP_ATTEMPTS, last_error or "swap refused"),
               code=3)


def board_sync_via_queue(repo, paths, token, message, marker, log):
    """Queue mode (#AMQQ): the Board snapshot is a metadata job for the integration service,
    which is the repository's only publisher now. Prints the job id, or `nothing to land`."""
    backend = Path(repo) / "backend"
    if str(backend) not in sys.path:
        sys.path.insert(0, str(backend))
    try:
        from relay_core import integration_service
    except ImportError as exc:
        raise Fail("this repository publishes through relay-land (mode queue) but "
                   "relay_core.integration_service cannot be imported from %s: %s"
                   % (backend, exc), code=2)
    try:
        service = integration_service.IntegrationService(
            repo, state_root=marker.get("state_root") or None)
        job = service.submit_board_snapshot(paths, session=token, message=message)
    except (integration_service.ServiceError, Exception) as exc:  # noqa: BLE001
        code = getattr(exc, "exit_code", 2)
        raise Fail("board-sync via the queue failed: %s: %s" % (exc.__class__.__name__, exc),
                   code=code if isinstance(code, int) else 2)
    if job is None:
        log("nothing to land")
        return 0
    log("submitted Board snapshot %s as landq job %s (kind metadata); the service publishes "
        "it once its schema check passes" % (job["submitted_sha"][:12], job["id"]))
    print(job["submitted_sha"])
    return 0


def cmd_reap(args, log):
    """Split a token's sessions into the dirty (kept, noted, `reaped`-stamped) and the clean
    (abandoned: snapshots and registry entry dropped). Never keyed on idle time, and it
    never touches another token's sessions."""
    repo = repo_root()
    root = Path(args.root)
    kept, abandoned = [], []
    for name in sorted(registered_sessions(root)):
        entry = registered_sessions(root)[name]
        if entry.get("token") != args.token or repo_of(entry) != Path(repo):
            continue
        dirty = [(path, hunks, age)
                 for path, hunks, age in session_hunks(repo, root, name, entry) if hunks]
        if not dirty:
            shutil.rmtree(session_dir(root, name), ignore_errors=True)

            def drop(data, name=name):
                data["sessions"].pop(name, None)
            edit_registry(root, drop)
            abandoned.append(name)
            log_line(root, "reap: abandoned %s (clean)" % name)
            continue
        stamp = now()
        try:
            meta = read_meta(root, name)
        except Fail:
            meta = {}
        meta["reaped"] = stamp
        write_meta(root, name, meta)

        def mark(data, stamp=stamp, name=name):
            entry = data["sessions"].get(name)
            if entry:
                entry["reaped"] = stamp
        edit_registry(root, mark)
        paths = ", ".join(path for path, _hunks, _age in dirty)
        age = max((age for _path, _hunks, age in dirty if age is not None), default=0.0)
        card = reap_card_id(entry, args)
        note = ("%d hunk(s) uncommitted in %s (snapshot %s), pane %s; resume with "
                "`land.py orphans`"
                % (sum(hunks for _path, hunks, _age in dirty), paths, human_age(age),
                   session_pane(entry, meta)))
        if card:
            thread_note(repo, card, note, log)
        log("reap: kept %s (%s)" % (name, note))
        kept.append(name)
    log("reap: %d session(s) kept, %d abandoned for token %s"
        % (len(kept), len(abandoned), args.token))
    return 0


def cmd_orphans(args, log):
    """Reaped or stale sessions that still hold uncommitted hunks, grouped by card.

    Pane liveness is not visible to land.py, so `reaped or stale` is the whole rule: a kept
    session shows up here until its hunks land or its claims drop.
    """
    repo = repo_root()
    root = Path(args.root)
    groups = {}
    for name in sorted(registered_sessions(root)):
        entry = registered_sessions(root)[name]
        if repo_of(entry) != Path(repo):
            continue
        if not (entry.get("reaped") or is_idle(entry)):
            continue
        claims = [{"path": path, "hunks": hunks,
                   "snapshot_age_minutes": round(age, 1) if age is not None else 0.0}
                  for path, hunks, age in session_hunks(repo, root, name, entry) if hunks]
        if not claims:
            continue
        card = reap_card_id(entry, argparse.Namespace(card=None))
        groups.setdefault(card, []).append({
            "session": name, "token": entry.get("token") or "",
            "pane": contact_pane(entry.get("contact")) or entry.get("owner") or "",
            "owner": entry.get("owner") or "", "claims": claims})
    payload = {"cards": [{"card": card, "sessions": groups[card]} for card in groups]}
    if args.json:
        print(json.dumps(payload))
        return 0
    if not payload["cards"]:
        log("no orphaned uncommitted work")
        return 0
    for group in payload["cards"]:
        log(group["card"] or "no card")
        for session in group["sessions"]:
            label = " %s" % session["pane"] if session["pane"] else ""
            log("  %s%s (token %s)" % (session["session"], label, session["token"] or "none"))
            for claim in session["claims"]:
                log("    %s (%d hunk(s), snapshot %s)"
                    % (claim["path"], claim["hunks"], human_age(claim["snapshot_age_minutes"])))
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
  commit mysession -m "..." --only-hunk src/Pane.h --by 3f4a20ad
                                       land the hunks the authorship journal attributes to
                                       that pane (or card, --by #234Z)

Hunks left out are neither committed nor touched: they stay in the working tree and a later
commit picks them up. A bare path with --by <pane-id|#ID> selects by authorship -- the same
matcher that names FOREIGN hunks, over the journals under <root>/authors -- instead of by
position, so another session's concurrent edit elsewhere in the file cannot shift your
numbers; a numbered selection keeps its positional meaning. Either selection flag prints a
new digest. The digest covers the tip and
the exact bytes of every path, so a working-tree edit, a different selection or main moving
makes it stop matching, and you are asked again.

foreign hunks

Every tool write is also journalled, per session token, under the land root
(authors/<token>.jsonl, both byte images in blobs/). At your commit, a held hunk whose
removed and added lines are exactly the change another pane's journal recorded for that path
is FOREIGN: the review names the pane, its card and the time, because that hunk is not yours
-- on 2026-09-24 session #6CSN confirmed a review whose every hunk was contested and thereby
landed #234Z's edit as its own. --confirm leaves FOREIGN hunks in the working tree (they land
when their pane commits, or with a later commit of yours once that pane is gone), and

  commit mysession -m "..." --take-foreign src/Pane.h:2   land that pane's hunk on purpose

lands one deliberately: the commit message records whose edit it was, and a note goes to that
hunk's card thread. --no-verify stays refused while any FOREIGN hunk is held.

auto-begin and the pane token

Before a pane's first tool write of a path, the backend claims it with
`begin <RELAY_SESSION_TOKEN> <path> --auto --contact "pane <id>"`, so the pane's edits are
snapshotted from before the first keystroke. A manual `begin` on that same path from a
process with the same RELAY_SESSION_TOKEN adopts the claim: its earlier snapshot and
timestamp become yours, the edits made before your begin still land, and the auto claim is
gone (if you never begin manually, commit that token session instead). Without a claim to
adopt, `begin` warns when your own journal says you edited the path before claiming it: that
edit predates the snapshot and will not land from it -- `begin --base main` is the way to
land it.

  -m accepts either the message text or the path to a file holding it.
  --paths p...       land only some of the session's paths.
  --whole p          commit the entire working copy of a path you never ran `begin` on.
  --dry-run          print the merged diff (and the review, if it would be held) and stop.
  --no-cards         do not append the landed sha to the `#ID` cards the message names.
  --stale-minutes N  hold a path whose snapshot is older than this (default {stale}).

the build gate

When the paths being landed include C++ or build files (src/, engine/, tests/*.cpp,
CMakeLists.txt, *.cmake), `commit` materialises the EXACT tree it is about to put on the branch
into a build slot, <root>/verify-slots/<repo>-<n>/src, and builds it in .../build before the
swap. Only a tree that compiles is landed; otherwise the first errors are printed, nothing is
landed, and it exits 5. A slot is the tool's own check, not a place to work: nobody edits there.
There are at most four of them per repository, sized after the machine's spare disk and memory
(RELAY_LAND_VERIFY_SLOTS pins the count) and shared by every session, each held under a lock for
the whole build, so twenty sessions take a few build trees of disk, not twenty (cards #SZHQ,
#76QW). A slot is configured exactly like a developer build (scripts/relay-build's
CONFIGURE_ARGS), and only the files whose blob changed are rewritten, so a slot stays
incremental whoever used it last. Landed .py files are byte-compiled the same way.

disk

`gc` reclaims what nobody will read again: sessions idle longer than RELAY_LAND_GC_DAYS (default
3), pre-#SZHQ per-session verify builds, and authorship journal lines and blobs that old. It
runs by itself at most once an hour from begin, commit, who and doctor; `gc --dry-run` says
what it would take. `who` prints the root's size.

The working tree is deliberately not what gets built: it holds every session's uncommitted
code, so it can compile while the tree you are landing cannot. That is how a green build of
code nobody had written reached main twice on 2026-09-19.

try: build your landing without landing it

`try <session>` builds the exact tree `commit` would land -- the tip plus your claimed hunks,
contested or stale included, because nothing lands and so nothing is held -- in a verify slot,
and moves nothing: no refs, no shared index, no working tree. It is what to run when the
working tree is full of other sessions' edits and you want to know whether YOUR change builds
(#76QW's #234Z scenario: someone else's broken edit in the working tree must not be able to
fail your check), and after landing, with --commit, to check what is now on the branch. Its
stdout is a machine-readable summary, in this order:

  tip <sha>                       the tip merged against, or `commit <sha>` in --commit/--tree
                                  mode (a revision materialised instead of merged).
  <stat line>                     one per claimed path in the tree, as commit's review prints
                                  them.
  src <slot>/src                  the materialised tree.
  build <slot>/build              the build directory (ctest runs there with --tests).
  binary <slot>/build/<target>    the target's output; omitted when --verify-cmd built
                                  something else.

Everything else -- logs, compiler errors, the ctest tail -- goes to stderr.

  --paths P...           try only this subset of the session's paths.
  --tests RE             build everything, then run the ctest cases matching RE.
  --target T             the cmake target (default relay, or RELAY_LAND_VERIFY_TARGET).
  --verify-cmd "..."     run this instead of the cmake build (cwd = the materialised tree,
                         VERIFY_BUILD in the environment).
  --commit SHA           materialise that revision instead of merging (--tree SHA names a
  --tree SHA             tree); for post-land checks and a Try-it run.
  --print-binary         print ONLY the binary path on stdout; logs go to stderr.

`try` exits 0 when it built (and tested), 1 on usage, 3 when the merge conflicts, 5 when the
build (a py-compile, or --verify-cmd) fails, 6 when a --tests run fails, 7 when every verify
slot stayed busy past --wait-seconds. A failed try lands
nothing and touches nothing, and leaves the slot's tree in place for inspection.

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

uncommitted work and the end of a pane

A pane's uncommitted hunks are counted the same way everywhere: len(path_hunks(snapshot,
working copy)) per claimed path. `status --token <token> [--json]` prints what one pane still holds
(auto and manual sessions of its RELAY_SESSION_TOKEN, with the total) -- the backend refuses
to move a pane while the total is above zero. `begin --card #ID --owner <thread>` records
them in the registry (`who` shows them); when the owner's stop wrote a cancelled marker,
`commit` and `try` refuse. When the pane closes, the backend runs detached:

  python3 scripts/land.py reap --token <token>
      a session with uncommitted hunks is stamped `reaped` and its card thread gets a note
      (what is waiting, snapshot age, `resume with land.py orphans`); a clean session is
      abandoned. Never keyed on idle time; exit 0 either way.

  python3 scripts/land.py orphans [--json]
      the reaped and stale sessions that still hold hunks, grouped by card -- the resume
      list behind the GUI's Resume card action. `who` and `doctor` fold those sessions into
      one line instead of listing each.

  python3 scripts/land.py board-sync <token> -m "board: <pane> <turn>" <paths...>
      land the whole working copy of .board/ paths straight onto the branch for the
      backend's end-of-turn board writes: no `begin`, no build gate, no review hold, no
      card links, no claim created or disturbed. Prints the new sha, or `nothing to land`
      when the paths already match the tip.

exit codes: 0 fine, 1 usage or environment error, 2 doctor found something, 3 conflict or a
swap that could not be completed, 4 held for review, 5 the exact tree does not build, 6 a
`try --tests` run failed, 7 every verify slot stayed busy past --wait-seconds. Nothing was
changed on 3, 4, 5, 6 or 7.
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
    begin.add_argument("--auto", action="store_true",
                       help="record this claim as the pane's own (the backend begins one "
                            "before a pane's first tool write of a path); a later manual "
                            "`begin` from a process with the same RELAY_SESSION_TOKEN "
                            "adopts its snapshot")
    begin.add_argument("--card", default=None, metavar="#ID",
                       help="the board card this session works on, e.g. #234Z; shown by "
                            "`who` and used to group `orphans` and `reap` notes")
    begin.add_argument("--owner", default=None, metavar="THREAD",
                       help="the thread id that owns this pane; when its stop wrote a "
                            "cancelled marker, `commit` and `try` refuse until a live "
                            "owner takes the session over")
    begin.set_defaults(func=cmd_begin)

    commit = subs.add_parser("commit", help="land your hunks on the branch")
    commit.add_argument("session")
    commit.add_argument("-m", "--message", help="the message, or a file holding it")
    commit.add_argument("--paths", nargs="+", action="extend", default=None, help="land only this subset (repeatable)")
    commit.add_argument("--whole", nargs="+", action="extend", default=None, help="commit a whole working copy, unsnapshotted (repeatable)")
    commit.add_argument("--branch", default=None)
    commit.add_argument("--dry-run", action="store_true")
    commit.add_argument("--no-cards", action="store_true",
                        help="do not append the landed sha to links.commits of the #ID cards the message names")
    commit.add_argument("--confirm", default=None, metavar="DIGEST",
                        help="the digest a held commit printed; lands it unchanged")
    commit.add_argument("--exclude-hunk", action="append", default=None, metavar="PATH:N",
                        help="leave these hunks out of the commit, e.g. src/Pane.h:2,5-7 "
                             "(repeatable, accumulating)")
    commit.add_argument("--only-hunk", action="append", default=None, metavar="PATH:N",
                        help="land only these hunks of that path and leave the rest out "
                             "(repeatable, accumulating)")
    commit.add_argument("--take-foreign", dest="take_foreign", action="append", default=None,
                        metavar="PATH:N",
                        help="land a FOREIGN hunk (another pane's edit, named in the held "
                             "review) on purpose; the commit message records whose edit it "
                             "was (repeatable, same parser as --only-hunk)")
    commit.add_argument("--by", default=None, metavar="PANE|#ID",
                        help="with --only-hunk/--exclude-hunk, a bare PATH selects the "
                             "hunks the authorship journal attributes to that pane id or "
                             "card (e.g. --only-hunk src/Pane.h --by 3f4a20ad); a numbered "
                             "selection keeps its positional meaning")
    commit.add_argument("--stale-minutes", type=float, default=DEFAULT_STALE_MINUTES,
                        help="hold a path whose snapshot is older than this (default %d)"
                             % DEFAULT_STALE_MINUTES)
    commit.add_argument("--verify-cmd", default=None, metavar="SHELL",
                        help="run this instead of the default cmake build, with cwd = the "
                             "materialised tree and VERIFY_BUILD in the environment")
    commit.add_argument("--wait-seconds", type=int, default=None, metavar="N",
                        help="give up with exit 7, landing nothing, when every verify build "
                             "slot is still busy after N seconds (default: wait; the wait "
                             "names each slot's holder on stderr once a minute)")
    commit.add_argument("--verify-tests", default=None, metavar="REGEX",
                        help="also build everything and run the ctest cases matching REGEX")
    commit.add_argument("--verify-target", default=os.environ.get("RELAY_LAND_VERIFY_TARGET",
                                                                  "relay"),
                        help="the cmake target the default verify builds (default relay)")
    commit.add_argument("--no-verify", action="store_true",
                        help="skip the build gate; refused when any path is contested, "
                             "FOREIGN or stale")
    commit.set_defaults(func=cmd_commit)

    try_cmd = subs.add_parser(
        "try", help="build (and test) the exact tree commit would land, landing nothing",
        description="Build the tip plus this session's claimed hunks -- the exact merge "
                    "`commit` would land -- in a verify slot, without moving a ref or "
                    "touching the shared index or the working tree. See the try section "
                    "of --help for the output format and exit codes.")
    try_cmd.add_argument("session")
    try_cmd.add_argument("--paths", nargs="+", action="extend", default=None,
                         metavar="P", help="try only this subset of the session's paths")
    try_cmd.add_argument("--tests", default=None, metavar="REGEX",
                         help="after building everything, run the ctest cases matching "
                              "REGEX in the slot (a failing test exits 6)")
    try_cmd.add_argument("--target", default=os.environ.get("RELAY_LAND_VERIFY_TARGET")
                         or "relay", metavar="T",
                         help="the cmake target to build (default relay, or "
                              "RELAY_LAND_VERIFY_TARGET)")
    try_cmd.add_argument("--wait-seconds", type=int, default=None, metavar="N",
                         help="give up with exit 7, landing nothing, when every verify build "
                              "slot is still busy after N seconds (default: wait; the wait "
                              "names each slot's holder on stderr once a minute)")
    try_cmd.add_argument("--verify-cmd", default=None, metavar="SHELL",
                         help="run this instead of the default cmake build, with cwd = the "
                              "materialised tree and VERIFY_BUILD in the environment")
    try_cmd.add_argument("--commit", default=None, metavar="SHA",
                         help="skip the merge and materialise this revision instead "
                              "(post-land checks, Try-it)")
    try_cmd.add_argument("--tree", default=None, metavar="SHA",
                         help="the same, naming a tree sha")
    try_cmd.add_argument("--print-binary", dest="print_binary", action="store_true",
                         help="print only the built binary's path on stdout; logs go to "
                              "stderr")
    try_cmd.add_argument("--branch", default=None)
    try_cmd.add_argument("--whole", nargs="+", action="extend", default=None, metavar="P",
                         help="include the entire working copy of a path you never ran "
                              "`begin` on, as commit would")
    try_cmd.add_argument("--exclude-hunk", action="append", default=None, metavar="PATH:N",
                         help="try without these hunks (repeatable, as in commit)")
    try_cmd.add_argument("--only-hunk", action="append", default=None, metavar="PATH:N",
                         help="try with only these hunks (repeatable, as in commit)")
    try_cmd.add_argument("--by", default=None, metavar="PANE|#ID",
                         help="with --only-hunk/--exclude-hunk, a bare PATH selects the "
                              "hunks the authorship journal attributes to that pane id or "
                              "card (as in commit)")
    try_cmd.add_argument("--stale-minutes", type=float, default=DEFAULT_STALE_MINUTES,
                         help="as in commit (default %d)" % DEFAULT_STALE_MINUTES)
    try_cmd.set_defaults(func=cmd_try, take_foreign=None)

    who = subs.add_parser("who", help="live sessions, their paths, ages and contacts")
    who.set_defaults(func=cmd_who)

    status_cmd = subs.add_parser(
        "status", help="one pane's sessions, their uncommitted hunks and the total",
        description="Every session registered with one pane's token (auto and manual): "
                    "its claims and how many hunks each still holds against its snapshot, "
                    "plus the total over all of them. The pane-move gate refuses to move "
                    "a pane while the total is above zero. Exits 0 with the report either "
                    "way; exit 1 on an error.")
    status_cmd.add_argument("--token", required=True, metavar="TOKEN",
                            help="the pane's RELAY_SESSION_TOKEN")
    status_cmd.add_argument("--json", action="store_true",
                            help="print one JSON object instead of readable text")
    status_cmd.set_defaults(func=cmd_status)

    board_sync = subs.add_parser(
        "board-sync", help="land the working copy of .board/ paths whole, no gate",
        description="Land the entire working copy of .board/ paths straight onto the "
                    "branch: no `begin` needed, no build gate, no review hold, no "
                    "contested or stale check, no card links, no claim touched. For the "
                    "backend's end-of-turn board writes. Prints the new sha, or "
                    "`nothing to land` when the paths already match the tip.")
    board_sync.add_argument("token", metavar="TOKEN",
                            help="the pane's RELAY_SESSION_TOKEN, for the landing log")
    board_sync.add_argument("-m", "--message", required=True,
                            help="the commit message")
    board_sync.add_argument("paths", nargs="+", metavar="PATH",
                            help="repo-relative paths, all under .board/ (the intake "
                                 "files are refused)")
    board_sync.add_argument("--branch", default=DEFAULT_BRANCH)
    board_sync.set_defaults(func=cmd_board_sync)

    reap_cmd = subs.add_parser(
        "reap", help="note a pane's uncommitted work, free its clean sessions",
        description="For every session of one pane's token: a session with uncommitted "
                    "hunks is stamped `reaped` and its card's thread gets a note saying "
                    "what is waiting and how to resume (`land.py orphans`); a clean "
                    "session is abandoned. Never keyed on idle time. The backend runs "
                    "this detached when a pane closes or a worker exits.")
    reap_cmd.add_argument("--token", required=True, metavar="TOKEN",
                          help="the pane's RELAY_SESSION_TOKEN")
    reap_cmd.add_argument("--card", default=None, metavar="#ID",
                          help="the card to note on when the session itself names none "
                               "(--card at begin, else a #ID in its contact)")
    reap_cmd.set_defaults(func=cmd_reap)

    orphans_cmd = subs.add_parser(
        "orphans", help="reaped or stale sessions still holding uncommitted hunks",
        description="Every session that was reaped or went stale and still has "
                    "uncommitted hunks, grouped by card — the resume list behind the "
                    "GUI's Resume card action.")
    orphans_cmd.add_argument("--json", action="store_true",
                             help="print one JSON object instead of readable text")
    orphans_cmd.set_defaults(func=cmd_orphans)

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


def reclaim_legacy_root(legacy, log):
    """After adoption a legacy root holds only verify build slots nothing can use again —
    every build now goes to DEFAULT_ROOT's slots. Reclaim builds nobody has written for
    LEGACY_VERIFY_MINUTES (an in-flight verify writes continuously, so a live build is never
    touched), then drop the root once only bookkeeping files are left."""
    legacy = Path(legacy)
    if not legacy.is_dir():
        return
    if any(p.is_dir() for p in legacy.iterdir()
           if p.name != "verify-slots" and not p.name.startswith(".")):
        return                      # sessions that could not be moved still live here
    slots = legacy / "verify-slots"
    cutoff = _dt.datetime.now().timestamp() - LEGACY_VERIFY_MINUTES * 60
    if slots.is_dir():
        freed = 0
        for build in sorted(slots.iterdir()):
            try:
                if newest_mtime(build) >= cutoff:
                    continue        # a verify from before the move may still be running
                freed += tree_bytes(build)
                if build.is_dir():
                    shutil.rmtree(str(build), ignore_errors=True)
                else:
                    build.unlink()
            except OSError:
                pass
        if freed:
            log("reclaimed %s of idle verify builds from the old land root %s (#HRF6)"
                % (human_bytes(freed), legacy))
    for stray in list(legacy.iterdir()):
        try:
            if stray.is_file():
                stray.unlink()
        except OSError:
            pass
    for empty in (slots, legacy):
        try:
            empty.rmdir()
        except OSError:
            pass


def adopt_legacy_sessions(args, log):
    """#HRF6: move sessions begun under a claude-named root into the Relay-owned root,
    snapshots, markers and all, so `who` and contest detection see them again. Best effort:
    a session that cannot move (a cross-device root, a racing adoption) stays where it is
    and is still reached by adopt_legacy_root below."""
    root = Path(args.root)
    for legacy in legacy_roots():
        legacy = Path(legacy)
        if not legacy.is_dir():
            continue
        registry = read_registry(legacy).get("sessions", {})
        moved, entries = [], {}
        for directory in sorted(p for p in legacy.iterdir() if p.is_dir()):
            if directory.name == "verify-slots" or directory.name.startswith("."):
                continue
            if (root / directory.name).exists():
                continue            # a newer begin under the new root wins
            try:
                root.mkdir(parents=True, exist_ok=True)
                os.rename(str(directory), str(root / directory.name))
            except OSError:
                continue
            moved.append(directory.name)
            if isinstance(registry.get(directory.name), dict):
                entries[directory.name] = registry[directory.name]
        if moved:
            try:
                if entries:
                    def add(data, entries=entries):
                        for name, entry in entries.items():
                            data["sessions"].setdefault(name, entry)
                    edit_registry(root, add)
                def drop(data, moved=moved):
                    for name in moved:
                        data["sessions"].pop(name, None)
                edit_registry(legacy, drop)
            except OSError as exc:
                log("adopt: moved %d session(s) out of %s but could not rewrite its registry "
                    "(%s); each session's next `begin` re-registers it" % (len(moved), legacy, exc))
            log("adopted %d session%s from the old land root %s into %s (#HRF6)"
                % (len(moved), "" if len(moved) == 1 else "s", legacy, root))
        reclaim_legacy_root(legacy, log)


def adopt_legacy_root(args):
    """A session that could not be moved out of a legacy root (cross-device, or a racing
    adoption) still holds its snapshots there: finish it from that root rather than refusing
    every path as edited without `begin`."""
    if args.command not in ("commit", "abandon") or args.root != DEFAULT_ROOT:
        return
    session = getattr(args, "session", None)
    if not session or "/" in session or session.startswith("."):
        return
    if meta_file(DEFAULT_ROOT, session).exists():
        return
    for legacy in legacy_roots():
        if meta_file(legacy, session).exists():
            sys.stderr.write("land.py: session %s still lives under the old land root %s; "
                             "using it for this %s (#HRF6). New sessions use %s.\n"
                             % (session, legacy, args.command, DEFAULT_ROOT))
            args.root = legacy
            return


def main(argv=None):
    args = build_parser().parse_args(argv)

    def log(message):
        print(message)
    if os.path.abspath(args.root) == os.path.abspath(DEFAULT_ROOT):
        adopt_legacy_sessions(args, log)

    def log(message):
        print(message)
    try:
        return args.func(args, log)
    except Fail as failure:
        sys.stderr.write("land.py: %s\n" % failure)
        return failure.code


if __name__ == "__main__":
    sys.exit(main())
