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
  * It never commits `issues/bug_intake.txt`, `issues/feature_intake.txt` or any `*.orig`.

Exit codes: 0 fine, 1 usage or environment error, 2 `doctor` found something, 3 a merge
conflict or a swap that could not be completed (nothing was changed).
"""

import argparse
import datetime as _dt
import difflib
import json
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

DEFAULT_ROOT = "/tmp/claude-1000/land"
DEFAULT_BRANCH = "main"
NEVER_COMMIT = ("issues/bug_intake.txt", "issues/feature_intake.txt")
# Variables that would silently redirect a git command at someone else's index or work tree.
GIT_ENV_STRIP = ("GIT_INDEX_FILE", "GIT_DIR", "GIT_WORK_TREE", "GIT_OBJECT_DIRECTORY",
                 "GIT_COMMON_DIR", "GIT_NAMESPACE")
SWAP_ATTEMPTS = 10

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


def live_sessions(root, skip=None):
    """Registered sessions whose snapshot directory still exists."""
    out = {}
    for name, entry in read_registry(root).get("sessions", {}).items():
        if name == skip or not isinstance(entry, dict):
            continue
        if (Path(root) / name).is_dir():
            out[name] = entry
    return out


def warn_overlaps(root, session, paths, log):
    clashes = []
    for other, entry in live_sessions(root, skip=session).items():
        shared = sorted(set(entry.get("claims") or []) & set(paths))
        if shared:
            clashes.append((other, shared))
    for other, shared in clashes:
        log("warning: session %r has also claimed %s (not a blocker; the commit merges "
            "against the tip, so land small and land often)" % (other, ", ".join(shared)))
    return clashes


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


def write_meta(root, session, meta):
    path = meta_file(root, session)
    path.parent.mkdir(parents=True, exist_ok=True)
    handle, tmp = tempfile.mkstemp(dir=str(path.parent), prefix=".meta-")
    with os.fdopen(handle, "w", encoding="utf-8") as fh:
        json.dump(meta, fh, indent=2, sort_keys=True)
        fh.write("\n")
    os.replace(tmp, str(path))


def take_snapshot(repo, root, session, path, meta, tip):
    """Copy the working copy of `path` aside and record whether the tip tracked it."""
    data = work_bytes(repo, path)
    dest = snap_path(root, session, path)
    dest.parent.mkdir(parents=True, exist_ok=True)
    if data is None:
        if dest.exists():
            dest.unlink()
    else:
        dest.write_bytes(data)
    meta.setdefault("paths", {})[path] = {
        "existed": data is not None,
        "mode": work_mode(repo, path),
        "tracked_at_begin": tree_entry(repo, tip, path) is not None,
    }


def snapshot_bytes(root, session, path, record):
    if not record.get("existed"):
        return None
    dest = snap_path(root, session, path)
    return dest.read_bytes() if dest.exists() else None


def now():
    return _dt.datetime.now().isoformat(timespec="seconds")


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

    for path in paths:
        take_snapshot(repo, root, args.session, path, meta, tip)
    write_meta(root, args.session, meta)

    def mutate(data):
        entry = data["sessions"].setdefault(args.session, {})
        entry["repo"] = str(repo)
        entry["branch"] = args.branch
        entry["updated"] = now()
        entry.setdefault("started", meta["started"])
        claims = set(entry.get("claims") or [])
        claims.update(paths)
        entry["claims"] = sorted(claims)
    edit_registry(root, mutate)

    install_hook(repo, log)
    warn_overlaps(root, args.session, paths, log)

    log("session %s claims %d path(s) at tip %s" % (args.session, len(paths), tip[:12]))
    for path in paths:
        record = meta["paths"][path]
        state = "new file" if not record["existed"] else (
            "snapshot taken" if record["tracked_at_begin"] else "untracked, snapshot taken")
        log("  %s (%s)" % (path, state))
    log("snapshots: %s" % (directory / "snap"))
    log("edit, build and test in %s as usual, then:" % repo)
    log("  python3 scripts/land.py commit %s -m \"message\"" % args.session)
    return 0


# --------------------------------------------------------------------------- commit

class Conflict(Exception):
    def __init__(self, paths):
        super().__init__(", ".join(paths))
        self.paths = paths


def plan_path(repo, root, session, path, record, tip, whole):
    """Decide the bytes to commit for one path. Returns (content, mode) or None to skip.

    content is None for a deletion. Raises Conflict for that one path.
    """
    working = work_bytes(repo, path)
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


def build_commit(repo, tip, entries, message):
    """Private index from `tip`, your blobs in it, commit-tree onto `tip`. Returns the sha."""
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
        tree = git_out(repo, "write-tree", env=env)
    finally:
        shutil.rmtree(str(index.parent), ignore_errors=True)
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
    branch = args.branch or meta.get("branch", DEFAULT_BRANCH)
    if Path(meta.get("repo", repo)) != Path(repo):
        raise Fail("session %r was started in %s, not %s"
                   % (args.session, meta.get("repo"), repo))

    message = read_message(args.message)
    claimed = dict(meta.get("paths") or {})
    whole = [norm_path(repo, p) for p in (args.whole or [])]

    wanted = [norm_path(repo, p) for p in args.paths] if args.paths else \
        sorted(set(claimed) | set(whole))
    paths, dropped = [], []
    for path in wanted:
        if excluded(path):
            dropped.append(path)
            continue
        if path not in claimed and path not in whole:
            raise Fail("%s was edited without `begin`, so there is no snapshot to diff "
                       "against. Either `land.py begin %s %s` (it will snapshot the file as "
                       "it is now, so only later edits land) or pass --whole %s to commit the "
                       "whole working copy of it."
                       % (path, args.session, path, path))
        if path not in paths:
            paths.append(path)
    for path in dropped:
        log("not committing %s (intake file or *.orig)" % path)
    if not paths:
        raise Fail("nothing to commit")

    warn_overlaps(root, args.session, paths, log)

    last_error = None
    for attempt in range(1, SWAP_ATTEMPTS + 1):
        tip = branch_tip(repo, branch)          # read once per attempt, used everywhere below
        plans, conflicts = {}, []
        for path in paths:
            record = claimed.get(path, {"existed": False, "tracked_at_begin": False})
            try:
                outcome = plan_path(repo, root, args.session, path, record, tip,
                                    path in whole)
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
                       "again." % (", ".join(sorted(conflicts)), branch), code=3)
        if not plans:
            log("nothing to land: every claimed path already matches the tip")
            return 0

        if args.dry_run:
            log("would commit onto %s (tip %s):" % (branch, tip[:12]))
            for path, (content, _mode) in sorted(plans.items()):
                log("--- %s" % path)
                sys.stdout.write(unified(rev_bytes(repo, tip, path), content, path))
            return 0

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

        new = build_commit(repo, tip, entries, message)
        touched = [line for line in git_out(repo, "diff", "--name-only", tip, new).splitlines()
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

        tip_now = new
        for path in sorted(entries):
            take_snapshot(repo, root, args.session, path, meta, tip_now)
        meta["updated"] = now()
        write_meta(root, args.session, meta)

        def mutate(data):
            entry = data["sessions"].get(args.session)
            if not entry:
                return
            entry["claims"] = sorted(set(entry.get("claims") or []) - set(entries))
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

    if not findings:
        log("clean: nothing staged from an older commit, and nothing else to report")
    return 0 if findings == 0 else 2


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

  python3 scripts/land.py begin mysession src/Pane.h docs/ARCHITECTURE.md
  # ... edit, build and test in this checkout, as usual ...
  python3 scripts/land.py commit mysession -m "what I did"

`begin` snapshots those files as they are now. `commit` lands, for each of them, the tip of
main plus the hunks you added since the snapshot -- so another session's uncommitted edits in
the same file are neither committed nor disturbed -- then points the shared index at what it
committed, so `git status` shows only what is still uncommitted.

  -m accepts either the message text or the path to a file holding it.
  --paths p...   land only some of the session's paths.
  --whole p      commit the entire working copy of a path you never ran `begin` on.
  --dry-run      print the merged diff and stop.

  python3 scripts/land.py abandon mysession   drop the snapshots and claims.
  python3 scripts/land.py doctor [--fix]      shared-index and checkout hygiene.
  python3 scripts/land.py hook install        make git refuse a shared-index commit.

exit codes: 0 fine, 1 usage or environment error, 2 doctor found something, 3 conflict or a
swap that could not be completed (in which case nothing was changed).
"""


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
    begin.set_defaults(func=cmd_begin)

    commit = subs.add_parser("commit", help="land your hunks on the branch")
    commit.add_argument("session")
    commit.add_argument("-m", "--message", help="the message, or a file holding it")
    commit.add_argument("--paths", nargs="+", action="extend", default=None, help="land only this subset (repeatable)")
    commit.add_argument("--whole", nargs="+", action="extend", default=None, help="commit a whole working copy, unsnapshotted (repeatable)")
    commit.add_argument("--branch", default=None)
    commit.add_argument("--dry-run", action="store_true")
    commit.set_defaults(func=cmd_commit)

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
    return parser


def main(argv=None):
    args = build_parser().parse_args(argv)

    def log(message):
        print(message)
    try:
        return args.func(args, log)
    except Fail as failure:
        sys.stderr.write("land.py: %s\n" % failure)
        return failure.code


if __name__ == "__main__":
    sys.exit(main())
