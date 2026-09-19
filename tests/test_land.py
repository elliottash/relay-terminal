"""scripts/land.py: landing one session's hunks on a shared checkout without reverting anyone.

Every test builds a throw-away git repository and drives the script the way a session would:
`begin` before editing, edits in the working tree, `commit` afterwards. A second "session" is
simulated by editing the same working tree and landing through a private index, exactly as the
real one does.
"""

import datetime
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
# The copy in the shared checkout is the one every session *runs*, so it must be a working
# tool at every moment: a half-finished edit there breaks everyone's `commit`. Develop the
# next version in a scratch file and point the suite at it with RELAY_LAND_SCRIPT, then move
# it into scripts/ once this passes.
LAND = Path(os.environ.get("RELAY_LAND_SCRIPT") or ROOT / "scripts" / "land.py")

TEN_LINES = "".join("line %d\n" % n for n in range(1, 11))
# Hunks three context lines apart merge into one, so the tests that need several
# numbered hunks in one file use this longer fixture and edit lines far apart.
MANY_LINES = "".join("l %d\n" % n for n in range(1, 61))


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
        write(self.repo / "big.txt", MANY_LINES)
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

    def digest(self, proc):
        """The digest a held (or about-to-land) commit printed."""
        found = re.findall(r"digest ([0-9a-f]{12})", proc.stdout)
        self.assertTrue(found, "no digest in output:\n%s" % proc.stdout)
        return found[0]

    def age_snapshot(self, session, minutes, path=None):
        """Backdate a session's snapshot timestamps, to test the staleness gate."""
        meta_file = self.land_root / session / "meta.json"
        meta = json.loads(meta_file.read_text(encoding="utf-8"))
        when = (datetime.datetime.now() - datetime.timedelta(minutes=minutes)
                ).isoformat(timespec="seconds")
        for key, record in meta["paths"].items():
            if path is None or key == path:
                record["at"] = when
        meta_file.write_text(json.dumps(meta), encoding="utf-8")

    def age_session(self, session, hours):
        """Backdate a session's last activity, to test the idle-session rule."""
        registry = self.land_root / "registry.json"
        data = json.loads(registry.read_text(encoding="utf-8"))
        when = (datetime.datetime.now() - datetime.timedelta(hours=hours)
                ).isoformat(timespec="seconds")
        data["sessions"][session]["updated"] = when
        registry.write_text(json.dumps(data), encoding="utf-8")


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
        # Alice's edit came after Bob began, so hers is contested and held until confirmed.
        # By Bob's turn Alice has landed and dropped her claim, so his lands in one step.
        self.land("begin", "alice", "f.txt")
        self.land("begin", "bob", "f.txt")
        edit_line(self.repo / "f.txt", 2, "ALICE")
        held = self.land("commit", "alice", "-m", "alice", expect=4)
        self.land("commit", "alice", "-m", "alice", "--confirm", self.digest(held))
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

    def test_whole_and_paths_accumulate_across_repeated_flags(self):
        # `--whole a --whole b` must take both; argparse's nargs="+" alone keeps only the last.
        edit_line(self.repo / "f.txt", 3, "WHOLE-A")
        write(self.repo / "g.txt", "WHOLE-B\n")
        self.land("begin", "mine", "other.txt")
        write(self.repo / "other.txt", "hello\n")
        out = self.land("commit", "mine", "-m", "x", "--whole", "f.txt", "--whole", "g.txt")
        self.assertIn("WHOLE-A", self.tip_text("f.txt"))
        self.assertEqual(self.tip_text("g.txt"), "WHOLE-B\n")
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

    def test_a_claim_warns_at_begin_and_holds_the_commit_until_confirmed(self):
        self.land("begin", "alice", "f.txt")
        out = self.land("begin", "bob", "f.txt")
        self.assertIn("warning", out.stdout)
        self.assertIn("alice", out.stdout)
        edit_line(self.repo / "f.txt", 2, "BOB")
        held = self.land("commit", "bob", "-m", "bob", expect=4)
        self.assertIn("alice", held.stdout)
        self.assertIn("CONTESTED", held.stdout)
        self.assertEqual(self.tip_text("f.txt"), TEN_LINES)      # nothing landed

        out = self.land("commit", "bob", "-m", "bob", "--confirm", self.digest(held))
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


class Contested(LandCase):
    """Two sessions interleaving edits in one file — the fault of 2026-09-19."""

    def test_a_hunk_made_before_the_other_session_began_is_mine_and_lands(self):
        self.land("begin", "alice", "f.txt")
        edit_line(self.repo / "f.txt", 2, "ALICE, before bob started")
        self.land("begin", "bob", "f.txt")          # leaves a marker in alice's data
        out = self.land("commit", "alice", "-m", "alice")     # one step, nothing contested
        self.assertNotIn("CONTESTED", out.stdout)
        self.assertIn("ALICE, before bob started", self.tip_text("f.txt"))

    def test_a_hunk_made_after_the_other_session_began_is_contested(self):
        self.land("begin", "alice", "big.txt")
        edit_line(self.repo / "big.txt", 5, "ALICE, early")
        self.land("begin", "bob", "big.txt")
        edit_line(self.repo / "big.txt", 40, "SOMEONE, later")  # alice's? bob's? unknowable
        held = self.land("commit", "alice", "-m", "alice", expect=4)

        self.assertIn("HELD", held.stdout)
        self.assertIn("1 of 2 hunks contested", held.stdout)
        self.assertRegex(held.stdout, r"hunk 1 of 2 .*yours")
        self.assertRegex(held.stdout, r"hunk 2 of 2 .*CONTESTED")
        self.assertEqual(self.tip_text("big.txt"), MANY_LINES)
        self.assertIn("--confirm", held.stdout)

    def test_the_session_that_began_second_has_every_hunk_contested(self):
        # bob has no marker for alice: he cannot tell her later edits from his own.
        self.land("begin", "alice", "big.txt")
        self.land("begin", "bob", "big.txt")
        edit_line(self.repo / "big.txt", 5, "A")
        edit_line(self.repo / "big.txt", 40, "B")
        held = self.land("commit", "bob", "-m", "bob", expect=4)
        self.assertIn("2 of 2 hunks contested", held.stdout)

    def test_nothing_is_contested_when_the_other_session_has_gone_idle(self):
        self.land("begin", "alice", "f.txt")
        self.land("begin", "bob", "f.txt")
        self.age_session("alice", 13)             # idle beyond IDLE_HOURS
        edit_line(self.repo / "f.txt", 2, "BOB")
        out = self.land("commit", "bob", "-m", "bob")
        self.assertNotIn("CONTESTED", out.stdout)
        self.assertIn("BOB", self.tip_text("f.txt"))

    def test_the_contact_is_shown_in_every_claim_warning_and_in_who(self):
        self.land("begin", "alice", "--contact", "think-cap (Claude peer)", "f.txt")
        out = self.land("begin", "bob", "f.txt")
        self.assertIn("think-cap (Claude peer)", out.stdout)

        edit_line(self.repo / "f.txt", 2, "BOB")
        held = self.land("commit", "bob", "-m", "bob", expect=4)
        self.assertIn("think-cap (Claude peer)", held.stdout)

        out = self.land("who")
        self.assertIn("think-cap (Claude peer)", out.stdout)
        self.assertIn("f.txt", out.stdout)
        self.assertIn("no --contact given", self.land("begin", "carol", "f.txt").stdout)

    def test_a_stale_snapshot_is_held_on_its_own(self):
        self.land("begin", "mine", "f.txt")
        edit_line(self.repo / "f.txt", 2, "MINE")
        self.age_snapshot("mine", 40)
        held = self.land("commit", "mine", "-m", "mine", expect=4)
        self.assertIn("snapshot 40m old", held.stdout)
        self.assertNotIn("CONTESTED", held.stdout)
        self.assertEqual(self.tip_text("f.txt"), TEN_LINES)

        # A wider window makes the same commit land in one step.
        out = self.land("commit", "mine", "-m", "mine", "--stale-minutes", "120")
        self.assertIn("MINE", self.tip_text("f.txt"))
        self.assertNotIn("HELD", out.stdout)

    def test_the_digest_stops_matching_when_the_working_tree_changes(self):
        self.land("begin", "mine", "f.txt")
        edit_line(self.repo / "f.txt", 2, "MINE")
        self.age_snapshot("mine", 40)
        stale = self.digest(self.land("commit", "mine", "-m", "mine", expect=4))

        edit_line(self.repo / "f.txt", 7, "MINE TOO")       # the tree moved under the digest
        out = self.land("commit", "mine", "-m", "mine", "--confirm", stale, expect=4)
        self.assertIn("does not match", out.stderr)
        self.assertEqual(self.tip_text("f.txt"), TEN_LINES)

        fresh = self.digest(out)
        self.assertNotEqual(fresh, stale)
        self.land("commit", "mine", "-m", "mine", "--confirm", fresh)
        landed = self.tip_text("f.txt")
        self.assertIn("MINE\n", landed)
        self.assertIn("MINE TOO\n", landed)

    def test_the_digest_stops_matching_when_main_moves(self):
        self.land("begin", "mine", "f.txt")
        write(self.repo / "g.txt", "MINE\n")
        self.land("begin", "mine", "g.txt")
        self.age_snapshot("mine", 40)
        stale = self.digest(self.land("commit", "mine", "-m", "mine", expect=4))
        write(self.repo / "unrelated.txt", "someone else\n")
        self.other_session_lands("unrelated.txt")
        out = self.land("commit", "mine", "-m", "mine", "--confirm", stale, expect=4)
        self.assertIn("does not match", out.stderr)

    def test_the_conflict_message_suggests_needing_fewer_files(self):
        self.land("begin", "mine", "f.txt")
        edit_line(self.repo / "f.txt", 5, "THEIRS")
        self.other_session_lands("f.txt")
        edit_line(self.repo / "f.txt", 5, "MINE")
        out = self.land("commit", "mine", "-m", "mine", expect=3)
        self.assertIn("need fewer files", out.stderr)


class Selection(LandCase):
    def test_excluding_a_hunk_lands_the_rest_and_a_later_commit_picks_it_up(self):
        self.land("begin", "alice", "big.txt")
        self.land("begin", "bob", "big.txt")
        edit_line(self.repo / "big.txt", 5, "MINE")
        edit_line(self.repo / "big.txt", 40, "NOT MINE")
        held = self.land("commit", "alice", "-m", "alice", expect=4)
        first = self.digest(held)

        chosen = self.land("commit", "alice", "-m", "alice", "--exclude-hunk", "big.txt:2",
                           expect=4)
        second = self.digest(chosen)
        self.assertNotEqual(first, second)
        self.assertIn("LEFT OUT", chosen.stdout)

        out = self.land("commit", "alice", "-m", "alice", "--exclude-hunk", "big.txt:2",
                        "--confirm", second)
        landed = self.tip_text("big.txt")
        self.assertIn("MINE\n", landed)
        self.assertNotIn("NOT MINE", landed)            # left out of the commit
        self.assertIn("NOT MINE", (self.repo / "big.txt").read_text(encoding="utf-8"))
        self.assertIn("left uncommitted in the working tree", out.stdout)

        # The claim survives, because there is still uncommitted work in that file.
        registry = json.loads((self.land_root / "registry.json").read_text())
        self.assertEqual(registry["sessions"]["alice"]["claims"], ["big.txt"])

        # And the excluded hunk is still later-than-the-snapshot, so a later commit takes it.
        self.land("abandon", "bob")                     # nobody contests it any more
        out = self.land("commit", "alice", "-m", "the rest")
        self.assertIn("NOT MINE", self.tip_text("big.txt"))
        self.assertNotIn("HELD", out.stdout)

    def test_only_hunk_takes_ranges_and_lands_just_those(self):
        self.land("begin", "alice", "big.txt")
        self.land("begin", "bob", "big.txt")
        for number, text in ((5, "H1"), (20, "H2"), (35, "H3"), (50, "H4")):
            edit_line(self.repo / "big.txt", number, text)
        held = self.land("commit", "alice", "-m", "alice", "--only-hunk", "big.txt:1,3-4",
                         expect=4)
        self.assertIn("3 hunks", held.stdout)   # 3 of the 4 selected
        self.land("commit", "alice", "-m", "alice", "--only-hunk", "big.txt:1,3-4",
                  "--confirm", self.digest(held))
        landed = self.tip_text("big.txt")
        self.assertIn("H1\n", landed)
        self.assertNotIn("H2", landed)
        self.assertIn("H3\n", landed)
        self.assertIn("H4\n", landed)

    def test_excluding_every_hunk_of_the_only_path_lands_nothing(self):
        self.land("begin", "alice", "f.txt")
        edit_line(self.repo / "f.txt", 2, "MINE")
        out = self.land("commit", "alice", "-m", "alice", "--exclude-hunk", "f.txt:1")
        self.assertIn("nothing to land", out.stdout)
        self.assertEqual(self.tip_text("f.txt"), TEN_LINES)

    def test_a_hunk_number_that_does_not_exist_is_refused(self):
        self.land("begin", "alice", "f.txt")
        edit_line(self.repo / "f.txt", 2, "MINE")
        out = self.land("commit", "alice", "-m", "x", "--exclude-hunk", "f.txt:9", expect=1)
        self.assertIn("no hunk 9", out.stderr)
        out = self.land("commit", "alice", "-m", "x", "--exclude-hunk", "f.txt", expect=1)
        self.assertIn("<path>:<hunks>", out.stderr)
        out = self.land("commit", "alice", "-m", "x", "--only-hunk", "f.txt:1",
                        "--exclude-hunk", "f.txt:1", expect=1)
        self.assertIn("pick one form per path", out.stderr)

    def test_begin_from_head_diffs_against_the_tip_and_is_always_reviewed(self):
        # A session that edited the file before it ever ran `begin`.
        edit_line(self.repo / "big.txt", 5, "EDITED BEFORE BEGIN")
        out = self.land("begin", "mine", "--from-head", "big.txt")
        self.assertIn("snapshot from", out.stdout)
        edit_line(self.repo / "big.txt", 40, "EDITED AFTER BEGIN")

        held = self.land("commit", "mine", "-m", "mine", expect=4)
        self.assertIn("snapshot taken from", held.stdout)
        self.assertRegex(held.stdout, r"hunk 1 of 2")
        self.land("commit", "mine", "-m", "mine", "--confirm", self.digest(held))
        landed = self.tip_text("big.txt")
        self.assertIn("EDITED BEFORE BEGIN\n", landed)
        self.assertIn("EDITED AFTER BEGIN\n", landed)

    def test_begin_base_marks_every_hunk_contested_when_someone_else_claims_it(self):
        self.land("begin", "alice", "big.txt")
        edit_line(self.repo / "big.txt", 5, "SOMEONE")
        base = git(self.repo, "rev-parse", "HEAD").stdout.strip()
        out = self.land("begin", "bob", "--base", base, "big.txt")
        self.assertIn("--confirm review", out.stdout)   # said at begin, not only at commit
        edit_line(self.repo / "big.txt", 40, "BOB")
        held = self.land("commit", "bob", "-m", "bob", expect=4)
        self.assertIn("2 of 2 hunks contested", held.stdout)
        # Bob can still land only what he knows is his.
        chosen = self.land("commit", "bob", "-m", "bob", "--only-hunk", "big.txt:2", expect=4)
        self.land("commit", "bob", "-m", "bob", "--only-hunk", "big.txt:2",
                  "--confirm", self.digest(chosen))
        landed = self.tip_text("big.txt")
        self.assertIn("BOB\n", landed)
        self.assertNotIn("SOMEONE", landed)


class Who(LandCase):
    def test_who_lists_sessions_paths_ages_and_contacts(self):
        self.land("begin", "alice", "--contact", "think-cap", "f.txt")
        out = self.land("who")
        self.assertIn("alice", out.stdout)
        self.assertIn("think-cap", out.stdout)
        self.assertIn("f.txt", out.stdout)
        self.assertIn("snapshot", out.stdout)

    def test_who_and_doctor_call_an_idle_session_stale(self):
        self.land("begin", "alice", "f.txt")
        self.age_session("alice", 20)
        out = self.land("who")
        self.assertIn("STALE", out.stdout)
        out = self.land("doctor", expect=2)
        self.assertIn("stale land session: alice", out.stdout)

    def test_who_says_so_when_there_is_nothing(self):
        self.assertIn("no land sessions", self.land("who").stdout)


class Repair(LandCase):
    def landed_pair(self):
        """Land a bad change to line 2, then a good one to line 8, from two sessions."""
        self.land("begin", "bad", "f.txt")
        edit_line(self.repo / "f.txt", 2, "HALF OF SOMEONE ELSE'S CHANGE")
        self.land("commit", "bad", "-m", "the bad commit")
        bad = self.tip()
        self.land("begin", "good", "f.txt")
        edit_line(self.repo / "f.txt", 8, "A LATER, GOOD CHANGE")
        self.land("commit", "good", "-m", "the good commit")
        return bad

    def test_repair_takes_back_one_commit_and_keeps_a_later_one(self):
        bad = self.landed_pair()
        before_work = (self.repo / "f.txt").read_text(encoding="utf-8")

        out = self.land("repair", bad, "--paths", "f.txt")
        landed = self.tip_text("f.txt")
        self.assertNotIn("HALF OF SOMEONE", landed)          # taken back
        self.assertIn("A LATER, GOOD CHANGE\n", landed)      # and the later commit kept
        self.assertIn("line 2\n", landed)

        # The working tree is untouched: it still holds the other session's code.
        self.assertEqual((self.repo / "f.txt").read_text(encoding="utf-8"), before_work)
        self.assertIn("HALF OF SOMEONE", before_work)
        self.assertIn("git archive", out.stdout)

        # The shared index entry points at the repaired blob, not the pre-repair one, so
        # nobody's next plain `git commit` puts it back.
        index_sha = git(self.repo, "ls-files", "-s", "--", "f.txt").stdout.split()[1]
        self.assertEqual(index_sha, git(self.repo, "rev-parse", "HEAD:f.txt").stdout.strip())
        self.assertEqual(git(self.repo, "diff", "--cached", "--name-only").stdout.strip(), "")

    def test_repair_dry_run_changes_nothing(self):
        bad = self.landed_pair()
        before = self.tip()
        out = self.land("repair", bad, "--paths", "f.txt", "--dry-run")
        self.assertIn("-HALF OF SOMEONE", out.stdout)
        self.assertEqual(self.tip(), before)

    def test_repair_aborts_on_a_conflict_and_changes_nothing(self):
        self.land("begin", "bad", "f.txt")
        edit_line(self.repo / "f.txt", 5, "BAD")
        self.land("commit", "bad", "-m", "bad")
        bad = self.tip()
        self.land("begin", "later", "f.txt")
        edit_line(self.repo / "f.txt", 5, "SOMEONE BUILT ON IT")
        self.land("commit", "later", "-m", "later")
        before_tip, before_work = self.tip(), (self.repo / "f.txt").read_text()

        out = self.land("repair", bad, "--paths", "f.txt", expect=3)
        self.assertIn("cannot take back", out.stderr)
        self.assertEqual(self.tip(), before_tip)
        self.assertEqual((self.repo / "f.txt").read_text(), before_work)

    def test_repair_takes_back_a_file_the_commit_added(self):
        self.land("begin", "bad", "added.txt")
        write(self.repo / "added.txt", "should never have landed\n")
        self.land("commit", "bad", "-m", "bad")
        bad = self.tip()
        self.land("repair", bad, "--paths", "added.txt")
        self.assertEqual(git(self.repo, "ls-tree", "refs/heads/main", "--", "added.txt")
                         .stdout.strip(), "")
        self.assertTrue((self.repo / "added.txt").exists())   # the working tree is untouched

    def test_repair_refuses_a_root_commit(self):
        root = git(self.repo, "rev-list", "--max-parents=0", "HEAD").stdout.strip()
        out = self.land("repair", root, "--paths", "f.txt", expect=1)
        self.assertIn("root commit", out.stderr)


TINY_CMAKE = """cmake_minimum_required(VERSION 3.16)
project(tiny CXX)
add_executable(tiny src/main.cpp)
"""

TINY_MAIN = """#include <cstdio>

int value() { return 1; }

int main() {
    std::printf("%d\\n", value());
    return 0;
}
"""


@unittest.skipUnless(shutil.which("cmake"), "cmake is not installed")
class Verify(LandCase):
    """The build gate: what goes onto the branch is compiled, never the working tree."""

    def setUp(self):
        super().setUp()
        write(self.repo / "CMakeLists.txt", TINY_CMAKE)
        write(self.repo / "src/main.cpp", TINY_MAIN)
        git(self.repo, "add", "-A")
        git(self.repo, "commit", "-q", "-m", "the tiny project")

    def verify(self, *args, **kw):
        return self.land(*args, "--verify-target", "tiny", **kw)

    def half_a_foreign_change(self):
        """Another session adds a helper to main.cpp and leaves it uncommitted."""
        main = self.repo / "src/main.cpp"
        text = main.read_text(encoding="utf-8")
        write(main, text.replace("int value() { return 1; }",
                                 "int value() { return 1; }\n\nint helper() { return 2; }"))

    def test_a_tree_that_does_not_compile_is_refused_while_the_tree_compiles(self):
        # The 2026-09-19 fault exactly: our hunk uses a declaration that exists only in
        # another session's uncommitted edit, so the working tree builds and the branch
        # would not.
        self.half_a_foreign_change()
        self.land("begin", "mine", "src/main.cpp")      # their helper is in my snapshot
        main = self.repo / "src/main.cpp"
        write(main, main.read_text(encoding="utf-8").replace("value()", "value() + helper()"))
        before = self.tip()

        out = self.verify("commit", "mine", "-m", "mine", expect=5)
        self.assertIn("does not build", out.stderr)
        self.assertIn("helper", out.stdout)             # the compiler's own words
        self.assertEqual(self.tip(), before)            # nothing landed
        self.assertIn("helper", main.read_text(encoding="utf-8"))   # tree untouched

    def test_a_tree_that_compiles_lands_and_the_second_verify_is_incremental(self):
        self.land("begin", "mine", "src/main.cpp")
        main = self.repo / "src/main.cpp"
        write(main, main.read_text(encoding="utf-8").replace("return 1;", "return 41 + 1;"))
        out = self.verify("commit", "mine", "-m", "first")
        self.assertIn("the exact tree builds", out.stdout)
        verify_dir = self.land_root / "mine" / "verify"
        self.assertTrue((verify_dir / "build/CMakeCache.txt").exists())

        write(main, main.read_text(encoding="utf-8").replace("return 41 + 1;", "return 7;"))
        out = self.verify("commit", "mine", "-m", "second")
        # Only the one changed file is rewritten, and the build directory is reused, so the
        # configure step does not run again.
        self.assertIn("(1 file(s) refreshed)", out.stdout)
        self.assertNotIn("cmake -S", out.stdout)
        self.assertIn("return 7;", self.tip_text("src/main.cpp"))

    def test_the_verify_directory_is_never_the_working_tree(self):
        self.land("begin", "mine", "src/main.cpp")
        main = self.repo / "src/main.cpp"
        write(main, main.read_text(encoding="utf-8").replace("return 1;", "return 5;"))
        self.verify("commit", "mine", "-m", "mine")
        built = (self.land_root / "mine" / "verify/src/src/main.cpp").read_text()
        self.assertIn("return 5;", built)
        self.assertFalse((self.repo / "build").exists())

    def test_main_moving_during_verify_reuses_the_build(self):
        # A commit is prepared but kept off the branch; the verify command lands it, so the
        # swap fails once and the merge is recomputed. Our own blob is unchanged, so the
        # build is not repeated.
        tip = self.tip()
        write(self.repo / "other.txt", "another session's file\n")
        foreign = self.other_session_lands("other.txt")
        git(self.repo, "update-ref", "refs/heads/main", tip, foreign)
        (self.repo / "other.txt").unlink()

        self.land("begin", "mine", "src/main.cpp")
        main = self.repo / "src/main.cpp"
        write(main, main.read_text(encoding="utf-8").replace("return 1;", "return 9;"))

        flag = Path(self.temp.name) / "moved"
        command = ('if [ ! -e %s ]; then git -C %s update-ref refs/heads/main %s %s; '
                   'touch %s; fi; exit 0' % (flag, self.repo, foreign, tip, flag))
        out = self.land("commit", "mine", "-m", "mine", "--verify-cmd", command)
        self.assertIn("moved under attempt 1", out.stdout)
        self.assertIn("already built; not rebuilding", out.stdout)
        self.assertIn("return 9;", self.tip_text("src/main.cpp"))
        self.assertEqual(self.tip_text("other.txt"), "another session's file\n")

    def test_no_verify_is_refused_when_anything_is_contested(self):
        self.land("begin", "alice", "src/main.cpp")
        self.land("begin", "bob", "src/main.cpp")
        main = self.repo / "src/main.cpp"
        write(main, main.read_text(encoding="utf-8").replace("return 1;", "return 3;"))
        out = self.land("commit", "alice", "-m", "alice", "--no-verify", expect=1)
        self.assertIn("--no-verify is refused", out.stderr)

        self.land("abandon", "bob")
        out = self.land("commit", "alice", "-m", "alice", "--no-verify")
        self.assertIn("skipped (--no-verify)", out.stdout)

    def test_verify_tests_runs_the_matching_ctest_cases(self):
        self.land("begin", "mine", "CMakeLists.txt")
        write(self.repo / "CMakeLists.txt", TINY_CMAKE + "enable_testing()\n"
              "add_test(NAME tiny-runs COMMAND tiny)\n")
        out = self.verify("commit", "mine", "-m", "mine", "--verify-tests", "tiny-runs")
        self.assertIn("ctest", out.stdout)
        self.assertIn("tiny-runs", self.tip_text("CMakeLists.txt"))


class PythonGate(LandCase):
    def test_python_that_does_not_compile_is_refused(self):
        self.land("begin", "mine", "scripts/thing.py")
        write(self.repo / "scripts/thing.py", "def broken(:\n")
        out = self.land("commit", "mine", "-m", "mine", expect=5)
        self.assertIn("do not compile as Python", out.stderr)
        self.assertEqual(git(self.repo, "ls-tree", "refs/heads/main", "--",
                             "scripts/thing.py").stdout.strip(), "")

    def test_python_that_compiles_lands_without_a_build(self):
        self.land("begin", "mine", "scripts/thing.py")
        write(self.repo / "scripts/thing.py", "def fine():\n    return 1\n")
        out = self.land("commit", "mine", "-m", "mine")
        self.assertNotIn("materialised", out.stdout)
        self.assertIn("def fine", self.tip_text("scripts/thing.py"))


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
