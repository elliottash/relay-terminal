"""scripts/land.py: landing one session's hunks on a shared checkout without reverting anyone.

Every test builds a throw-away git repository and drives the script the way a session would:
`begin` before editing, edits in the working tree, `commit` afterwards. A second "session" is
simulated by editing the same working tree and landing through a private index, exactly as the
real one does.
"""

import json
import os
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
LAND = ROOT / "scripts" / "land.py"

TEN_LINES = "".join("line %d\n" % n for n in range(1, 11))


def clean_env(**extra):
    env = {k: v for k, v in os.environ.items()
           if k not in ("GIT_INDEX_FILE", "GIT_DIR", "GIT_WORK_TREE")}
    env.update(extra)
    return env


def git(repo, *args, env=None, check=True):
    proc = subprocess.run(["git", *args], cwd=str(repo), env=clean_env(**(env or {})),
                          stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    if check and proc.returncode != 0:
        raise AssertionError("git %s failed: %s%s" % (" ".join(args), proc.stdout, proc.stderr))
    return proc


def write(path, text):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8")


def edit_line(path, number, text):
    lines = path.read_text(encoding="utf-8").splitlines(keepends=True)
    lines[number - 1] = text + "\n"
    path.write_text("".join(lines), encoding="utf-8")


class LandCase(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        base = Path(self.temp.name)
        self.repo = base / "repo"
        self.land_root = base / "land"
        self.repo.mkdir()
        git(self.repo, "init", "-q", "-b", "main", ".")
        git(self.repo, "config", "user.email", "test@example.invalid")
        git(self.repo, "config", "user.name", "Test")
        git(self.repo, "config", "commit.gpgsign", "false")
        write(self.repo / ".gitignore", "/build/\n")
        write(self.repo / "f.txt", TEN_LINES)
        write(self.repo / "issues/bug_intake.txt", "owner's inbox\n")
        git(self.repo, "add", "-A")
        git(self.repo, "commit", "-q", "-m", "first")

    def tearDown(self):
        self.temp.cleanup()

    # -- driving the script ------------------------------------------------

    def land(self, *args, expect=0, cwd=None):
        proc = subprocess.run(
            [sys.executable, str(LAND), "--root", str(self.land_root), *args],
            cwd=str(cwd or self.repo), env=clean_env(),
            stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        if expect is not None:
            self.assertEqual(proc.returncode, expect,
                             "exit %d\nstdout:\n%s\nstderr:\n%s"
                             % (proc.returncode, proc.stdout, proc.stderr))
        return proc

    # -- a second session, landing the way land.py does --------------------

    def other_session_lands(self, path, message="other session"):
        """Commit the working copy of `path` onto main through a private index."""
        index = Path(self.temp.name) / "other.index"
        if index.exists():
            index.unlink()
        env = {"GIT_INDEX_FILE": str(index)}
        tip = git(self.repo, "rev-parse", "refs/heads/main").stdout.strip()
        git(self.repo, "read-tree", tip, env=env)
        blob = git(self.repo, "hash-object", "-w", "--",
                   str(self.repo / path)).stdout.strip()
        git(self.repo, "update-index", "--add", "--cacheinfo",
            "100644,%s,%s" % (blob, path), env=env)
        tree = git(self.repo, "write-tree", env=env).stdout.strip()
        new = subprocess.run(["git", "commit-tree", tree, "-p", tip, "-m", message],
                             cwd=str(self.repo), env=clean_env(),
                             stdout=subprocess.PIPE, text=True, check=True).stdout.strip()
        git(self.repo, "update-ref", "refs/heads/main", new, tip)
        return new

    # -- helpers -----------------------------------------------------------

    def tip_text(self, path, rev="refs/heads/main"):
        return git(self.repo, "show", "%s:%s" % (rev, path)).stdout

    def tip(self):
        return git(self.repo, "rev-parse", "refs/heads/main").stdout.strip()

    def porcelain(self):
        return git(self.repo, "status", "--porcelain").stdout


class LandingHunks(LandCase):
    def test_only_my_hunks_land_and_the_other_edit_survives(self):
        # Another session is already halfway through the same file.
        edit_line(self.repo / "f.txt", 9, "THEIRS, uncommitted")
        self.land("begin", "mine", "f.txt")
        edit_line(self.repo / "f.txt", 2, "MINE")

        out = self.land("commit", "mine", "-m", "my line")
        sha = out.stdout.strip().splitlines()[-1]

        landed = self.tip_text("f.txt")
        self.assertIn("MINE\n", landed)
        self.assertNotIn("THEIRS", landed)          # their work was never committed
        self.assertEqual(sha, self.tip())

        working = (self.repo / "f.txt").read_text(encoding="utf-8")
        self.assertIn("MINE\n", working)
        self.assertIn("THEIRS, uncommitted\n", working)   # and it is still in the tree

        # The shared index now holds what was landed, so status shows only their edit.
        self.assertEqual(git(self.repo, "diff", "--cached", "--name-only").stdout.strip(), "")
        self.assertEqual(self.porcelain().strip(), "M f.txt")
        unstaged = git(self.repo, "diff", "--", "f.txt").stdout
        self.assertIn("THEIRS", unstaged)
        self.assertNotIn("MINE", unstaged)

    def test_two_sessions_land_different_regions_of_one_file(self):
        self.land("begin", "alice", "f.txt")
        self.land("begin", "bob", "f.txt")
        edit_line(self.repo / "f.txt", 2, "ALICE")
        self.land("commit", "alice", "-m", "alice")
        edit_line(self.repo / "f.txt", 8, "BOB")
        self.land("commit", "bob", "-m", "bob")

        landed = self.tip_text("f.txt")
        self.assertIn("ALICE\n", landed)
        self.assertIn("BOB\n", landed)
        self.assertEqual(len(git(self.repo, "log", "--oneline").stdout.strip().splitlines()), 3)

    def test_main_moving_between_begin_and_commit_is_absorbed(self):
        self.land("begin", "mine", "f.txt")
        edit_line(self.repo / "f.txt", 9, "THEIRS, landed")
        self.other_session_lands("f.txt")
        moved = self.tip()

        edit_line(self.repo / "f.txt", 2, "MINE")
        self.land("commit", "mine", "-m", "mine")

        landed = self.tip_text("f.txt")
        self.assertIn("MINE\n", landed)
        self.assertIn("THEIRS, landed\n", landed)
        self.assertEqual(git(self.repo, "rev-parse", "HEAD~1").stdout.strip(), moved)

    def test_a_real_conflict_aborts_and_changes_nothing(self):
        self.land("begin", "mine", "f.txt")
        edit_line(self.repo / "f.txt", 5, "THEIRS")
        self.other_session_lands("f.txt")
        before_tip = self.tip()
        edit_line(self.repo / "f.txt", 5, "MINE")
        before_work = (self.repo / "f.txt").read_text(encoding="utf-8")

        out = self.land("commit", "mine", "-m", "mine", expect=3)
        self.assertIn("f.txt", out.stderr)
        self.assertIn("conflict", out.stderr)
        self.assertEqual(self.tip(), before_tip)
        self.assertEqual((self.repo / "f.txt").read_text(encoding="utf-8"), before_work)

    def test_a_new_file_is_committed_whole(self):
        self.land("begin", "mine", "new/thing.py")
        write(self.repo / "new/thing.py", "print('hi')\n")
        self.land("commit", "mine", "-m", "new file")
        self.assertEqual(self.tip_text("new/thing.py"), "print('hi')\n")

    def test_a_file_written_before_begin_is_still_a_new_file(self):
        # How a session bootstraps a brand-new script: write it, then claim it.
        write(self.repo / "scripts/tool.py", "one\ntwo\n")
        self.land("begin", "mine", "scripts/tool.py")
        self.land("commit", "mine", "-m", "the tool")
        self.assertEqual(self.tip_text("scripts/tool.py"), "one\ntwo\n")

    def test_a_path_without_begin_is_refused_until_whole(self):
        edit_line(self.repo / "f.txt", 3, "UNCLAIMED")
        out = self.land("commit", "mine", "-m", "x", "--paths", "f.txt", expect=1)
        self.assertIn("begin", out.stderr)

        out = self.land("commit", "mine2", "-m", "x", "--whole", "f.txt", expect=1)
        self.assertIn("begin", out.stderr)      # no session at all yet

        self.land("begin", "mine3", "other.txt")
        write(self.repo / "other.txt", "hello\n")
        out = self.land("commit", "mine3", "-m", "x", "--whole", "f.txt")
        self.assertIn("--whole f.txt commits the entire working copy", out.stdout)
        self.assertIn("UNCLAIMED", out.stdout)              # the hunks are printed
        self.assertIn("UNCLAIMED", self.tip_text("f.txt"))
        self.assertEqual(self.tip_text("other.txt"), "hello\n")

    def test_dry_run_prints_the_merged_diff_and_lands_nothing(self):
        self.land("begin", "mine", "f.txt")
        edit_line(self.repo / "f.txt", 2, "MINE")
        before = self.tip()
        out = self.land("commit", "mine", "-m", "x", "--dry-run")
        self.assertIn("+MINE", out.stdout)
        self.assertEqual(self.tip(), before)

    def test_the_message_may_be_a_file(self):
        message = Path(self.temp.name) / "msg.txt"
        write(message, "subject line\n\nbody\n\nCo-Authored-By: Someone <x@y>\n")
        self.land("begin", "mine", "f.txt")
        edit_line(self.repo / "f.txt", 4, "MINE")
        self.land("commit", "mine", "-m", str(message))
        body = git(self.repo, "log", "-1", "--format=%B").stdout
        self.assertIn("subject line", body)
        self.assertIn("Co-Authored-By: Someone <x@y>", body)

    def test_intake_files_and_orig_files_are_never_committed(self):
        write(self.repo / "issues/bug_intake.txt", "owner's inbox\nsomething new\n")
        write(self.repo / "f.txt.orig", "junk\n")
        out = self.land("begin", "mine", "issues/bug_intake.txt", "f.txt.orig", "f.txt")
        edit_line(self.repo / "f.txt", 6, "MINE")
        self.assertIn("never committed by this tool", out.stdout)

        self.land("commit", "mine", "-m", "mine")
        touched = git(self.repo, "show", "--name-only", "--format=", "HEAD").stdout.split()
        self.assertEqual(touched, ["f.txt"])
        self.assertIn("something new", (self.repo / "issues/bug_intake.txt").read_text())

    def test_ignored_paths_are_refused(self):
        write(self.repo / "build/huge.o", "x\n")
        out = self.land("begin", "mine", "build/huge.o", expect=1)
        self.assertIn("gitignore", out.stdout + out.stderr)

    def test_claims_warn_but_never_block(self):
        self.land("begin", "alice", "f.txt")
        out = self.land("begin", "bob", "f.txt")
        self.assertIn("warning", out.stdout)
        self.assertIn("alice", out.stdout)
        edit_line(self.repo / "f.txt", 2, "BOB")
        out = self.land("commit", "bob", "-m", "bob")          # warned, not blocked
        self.assertIn("alice", out.stdout)
        self.assertIn("BOB", self.tip_text("f.txt"))

        registry = json.loads((self.land_root / "registry.json").read_text())
        self.assertEqual(registry["sessions"]["bob"]["claims"], [])
        self.assertEqual(registry["sessions"]["alice"]["claims"], ["f.txt"])

    def test_a_second_commit_from_the_same_session_works(self):
        self.land("begin", "mine", "f.txt")
        edit_line(self.repo / "f.txt", 2, "FIRST")
        self.land("commit", "mine", "-m", "first")
        edit_line(self.repo / "f.txt", 3, "SECOND")
        self.land("commit", "mine", "-m", "second")
        landed = self.tip_text("f.txt")
        self.assertIn("FIRST\n", landed)
        self.assertIn("SECOND\n", landed)

    def test_abandon_drops_the_session_and_leaves_the_tree(self):
        self.land("begin", "mine", "f.txt")
        edit_line(self.repo / "f.txt", 2, "MINE")
        self.land("abandon", "mine")
        self.assertFalse((self.land_root / "mine").exists())
        self.assertEqual(json.loads((self.land_root / "registry.json").read_text())["sessions"],
                         {})
        self.assertIn("MINE", (self.repo / "f.txt").read_text(encoding="utf-8"))

    def test_the_index_is_left_alone_off_branch(self):
        self.land("begin", "mine", "f.txt")
        edit_line(self.repo / "f.txt", 2, "MINE")
        git(self.repo, "checkout", "-q", "--detach", "HEAD")
        out = self.land("commit", "mine", "-m", "mine")
        self.assertIn("leaving the shared index alone", out.stdout)
        self.assertIn("MINE", self.tip_text("f.txt"))


class Doctor(LandCase):
    def stale_index_entry(self):
        """Leave the index (and working copy) at v1 while main has moved on to v2."""
        write(self.repo / "f.txt", "v1\n")
        git(self.repo, "add", "f.txt")
        git(self.repo, "commit", "-q", "-m", "v1")
        v1 = git(self.repo, "rev-parse", "HEAD:f.txt").stdout.strip()
        write(self.repo / "f.txt", "v2\n")
        self.other_session_lands("f.txt", "v2")
        # The shared index and the working copy are both left holding the older blob.
        git(self.repo, "update-index", "--add", "--cacheinfo", "100644,%s,f.txt" % v1)
        write(self.repo / "f.txt", "v1\n")
        return v1

    def test_doctor_reports_and_fixes_a_stale_staged_entry(self):
        self.stale_index_entry()
        out = self.land("doctor", expect=2)
        self.assertIn("stale staged entry", out.stdout)
        self.assertIn("f.txt", out.stdout)

        out = self.land("doctor", "--fix", expect=2)
        self.assertIn("index and working copy set to HEAD's version", out.stdout)
        self.assertEqual((self.repo / "f.txt").read_text(encoding="utf-8"), "v2\n")
        self.assertEqual(self.land("doctor").returncode, 0)

    def test_doctor_reports_new_staged_content_without_touching_it(self):
        write(self.repo / "f.txt", "someone's uncommitted work\n")
        git(self.repo, "add", "f.txt")
        out = self.land("doctor", expect=2)
        self.assertIn("not in history", out.stdout)
        self.assertEqual((self.repo / "f.txt").read_text(encoding="utf-8"),
                         "someone's uncommitted work\n")
        self.assertIn("f.txt", git(self.repo, "diff", "--cached", "--name-only").stdout)

    def test_doctor_reports_other_branches_orig_files_and_a_detached_head(self):
        git(self.repo, "branch", "side")
        write(self.repo / "f.txt.orig", "junk\n")
        git(self.repo, "checkout", "-q", "--detach", "HEAD")
        out = self.land("doctor", expect=2)
        self.assertIn("branches other than main: side", out.stdout)
        self.assertIn("f.txt.orig", out.stdout)
        self.assertIn("checkout is on a detached HEAD, not main", out.stdout)

    def test_doctor_is_clean_on_a_clean_checkout(self):
        self.assertEqual(self.land("doctor").returncode, 0)


class Hook(LandCase):
    def test_the_hook_refuses_the_shared_index_and_allows_a_private_one(self):
        out = self.land("hook", "install")
        hook = self.repo / ".git/hooks/pre-commit"
        self.assertTrue(os.access(str(hook), os.X_OK))
        self.assertIn("the shared index is never committed here", out.stdout)

        write(self.repo / "f.txt", "changed\n")
        git(self.repo, "add", "f.txt")
        refused = git(self.repo, "commit", "-m", "nope", check=False)
        self.assertNotEqual(refused.returncode, 0)
        self.assertIn("commit through scripts/land.py", refused.stdout + refused.stderr)

        index = Path(self.temp.name) / "private.index"
        env = {"GIT_INDEX_FILE": str(index)}
        git(self.repo, "read-tree", "HEAD", env=env)
        git(self.repo, "add", "f.txt", env=env)
        allowed = git(self.repo, "commit", "-m", "private", env=env, check=False)
        self.assertEqual(allowed.returncode, 0, allowed.stdout + allowed.stderr)

    def test_the_owner_can_override_the_hook(self):
        self.land("hook", "install")
        write(self.repo / "f.txt", "changed\n")
        git(self.repo, "add", "f.txt")
        allowed = git(self.repo, "commit", "-m", "owner",
                      env={"RELAY_ALLOW_SHARED_COMMIT": "1"}, check=False)
        self.assertEqual(allowed.returncode, 0, allowed.stdout + allowed.stderr)

    def test_begin_installs_the_hook_when_it_is_missing(self):
        hook = self.repo / ".git/hooks/pre-commit"
        self.assertFalse(hook.exists())
        self.land("begin", "mine", "f.txt")
        self.assertTrue(hook.exists())
        self.land("begin", "mine", "f.txt")      # idempotent

    def test_a_foreign_hook_is_not_overwritten(self):
        hook = self.repo / ".git/hooks/pre-commit"
        hook.parent.mkdir(parents=True, exist_ok=True)
        write(hook, "#!/bin/sh\nexit 0\n")
        out = self.land("hook", "install", expect=1)
        self.assertIn("already installed", out.stderr)
        self.assertEqual(hook.read_text(encoding="utf-8"), "#!/bin/sh\nexit 0\n")
        self.land("hook", "install", "--force")
        self.assertIn("scripts/land.py", hook.read_text(encoding="utf-8"))


class Safety(LandCase):
    def test_the_script_never_runs_checkout_stash_or_a_pathless_reset(self):
        source = LAND.read_text(encoding="utf-8")
        for forbidden in ('"checkout"', '"stash"', '"reset"', '"clean"'):
            self.assertNotIn(forbidden, source,
                             "land.py must never run git %s on the shared tree" % forbidden)

    def test_merges_use_real_temp_files_not_process_substitution(self):
        source = LAND.read_text(encoding="utf-8")
        self.assertIn("mkdtemp(prefix=\"land-merge-\")", source)
        self.assertNotIn("<(", source)      # no process substitution, ever

    def test_the_private_index_is_never_exported(self):
        source = LAND.read_text(encoding="utf-8")
        self.assertNotIn("os.environ[\"GIT_INDEX_FILE\"]", source)
        self.assertIn("GIT_ENV_STRIP", source)


if __name__ == "__main__":
    unittest.main()
