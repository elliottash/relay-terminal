# SPDX-License-Identifier: AGPL-3.0-or-later
"""Tests for relay_core.trees (card #RT3B, docs/TREES-AND-LANDING.md §A1).

Every test works in a `tempfile.TemporaryDirectory`: a real Git repository with
a local test identity, an independent state root, and (for removal checks) a
minimal A2-shaped queue database. No test creates a worktree of, or mutates,
the development checkout — the no-worktrees rule applies to the real repo, not
to these throwaway repositories of the newly authorized feature.
"""
import json
import os
import shutil
import sqlite3
import subprocess
import sys
import tempfile
import threading
import time
import unittest
from pathlib import Path
from unittest import mock

from relay_core import trees

REPO_ROOT = Path(__file__).resolve().parent.parent
CLI = REPO_ROOT / "scripts" / "relay-tree"


def git(cwd, *args, env_extra=None):
    env = {k: v for k, v in os.environ.items() if not k.startswith("GIT_")}
    if env_extra:
        env.update(env_extra)
    proc = subprocess.run(["git", *args], cwd=str(cwd), capture_output=True,
                          text=True, env=env, timeout=60)
    if proc.returncode != 0:
        raise AssertionError(f"git {' '.join(args)} failed: {proc.stderr}")
    return proc.stdout.strip()


class TreesTestCase(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        base = Path(self.tmp.name)
        self.state_root = base / "state"
        self.repo = base / "repo"
        self.repo.mkdir()
        git(self.repo, "init", "-q", "-b", "main")
        git(self.repo, "config", "user.email", "trees@test")
        git(self.repo, "config", "user.name", "Trees Test")
        (self.repo / "AGENTS.md").write_text("agents\n")
        (self.repo / "CLAUDE.md").write_text("claude\n")
        (self.repo / "RELAY.md").write_text("relay\n")
        (self.repo / ".relay").mkdir()
        (self.repo / ".relay" / "project.toml").write_text("[project]\n")
        (self.repo / ".board").mkdir()
        (self.repo / ".board" / "card.md").write_text("board\n")
        (self.repo / ".board" / "other.md").write_text("other\n")
        (self.repo / "src").mkdir()
        (self.repo / "src" / "code.py").write_text("x = 1\n")
        (self.repo / "docs" / "qa_evidence").mkdir(parents=True)
        (self.repo / "docs" / "qa_evidence" / "ev.md").write_text("ev\n")
        (self.repo / "docs" / "notes.txt").write_text("notes\n")
        git(self.repo, "add", "-A")
        git(self.repo, "commit", "-qm", "init")
        self.base_sha = git(self.repo, "rev-parse", "HEAD")
        self.record = trees.register_repo(self.repo, state_root=self.state_root)
        self.mgr = trees.TreeManager(self.repo, state_root=self.state_root)

    # -- helpers -----------------------------------------------------------

    def files(self, path):
        return sorted(str(p.relative_to(path))
                      for p in Path(path).rglob("*") if p.is_file())

    def commit_in(self, path, filename, content, message="work"):
        (Path(path) / filename).write_text(content)
        git(path, "add", "-A")
        git(path, "commit", "-qm", message)
        return git(path, "rev-parse", "HEAD")

    def make_queue_db(self):
        db = self.state_root / "integration" / self.record["id"] / "queue.sqlite3"
        db.parent.mkdir(parents=True, exist_ok=True)
        conn = sqlite3.connect(str(db))
        conn.executescript(
            """
            CREATE TABLE jobs (id TEXT PRIMARY KEY, request_id TEXT UNIQUE,
                repo_id TEXT, workspace_id TEXT, card TEXT, kind TEXT,
                base_sha TEXT, submitted_sha TEXT, target_sha TEXT,
                candidate_sha TEXT, candidate_tree TEXT, policy_hash TEXT,
                selected_tests_json TEXT, status TEXT, reason TEXT,
                cancel_requested INTEGER DEFAULT 0,
                created_at TEXT, updated_at TEXT);
            CREATE TABLE receipts (job_id TEXT PRIMARY KEY, repo_id TEXT,
                submitted_sha TEXT, target_before TEXT, published_sha TEXT,
                tree TEXT, policy_hash TEXT, verified INTEGER, landed_at TEXT);
            """)
        conn.commit()
        conn.close()
        return db

    def queue_add(self, table, **cols):
        keys = ", ".join(cols)
        with sqlite3.connect(str(self._queue_db)) as conn:
            conn.execute(f"INSERT INTO {table} ({keys}) VALUES "
                         f"({', '.join('?' for _ in cols)})", tuple(cols.values()))
            conn.commit()

    def make_receipt(self, workspace, *, job_id="job1", published=True):
        tip = git(self.repo, "rev-parse", workspace["branch"])
        receipt = {"job_id": job_id, "repo_id": self.record["id"],
                   "submitted_sha": tip, "target_before": self.base_sha,
                   "published_sha": "f" * 40 if published else "",
                   "tree": "t" * 40, "policy_hash": "p", "verified": True,
                   "landed_at": "2026-09-26T00:00:00Z"}
        self.queue_add("receipts", **receipt)
        return receipt


# ------------------------------------------------------------------ identity

class RegisterResolveTests(TreesTestCase):
    def test_register_returns_contract_fields(self):
        r = self.record
        self.assertTrue(r["id"])
        self.assertEqual(r["common_dir"],
                         os.path.realpath(self.repo / ".git"))
        self.assertEqual(r["project_root"], str(self.repo))
        self.assertEqual(r["board_root"], str(self.repo / ".board"))
        self.assertEqual(r["target"], "main")
        self.assertEqual(r["mode"], "legacy")

    def test_reregister_is_same_identity_no_second_allocation(self):
        again = trees.register_repo(self.repo, state_root=self.state_root)
        self.assertEqual(again["id"], self.record["id"])

    def test_marker_written_inside_common_dir(self):
        marker = self.repo / ".git" / "relay-repo-id"
        self.assertEqual(marker.read_text().strip(), self.record["id"])

    def test_resolve_from_checkout_subdir_and_worktree(self):
        from_subdir = trees.resolve_project(self.repo / "src",
                                            state_root=self.state_root)
        self.assertEqual(from_subdir["id"], self.record["id"])
        ws = self.mgr.create("s-resolve")
        from_tree = trees.resolve_project(ws["path"], state_root=self.state_root)
        self.assertIsNotNone(from_tree)
        self.assertEqual(from_tree["id"], self.record["id"])

    def test_resolve_never_allocates(self):
        other = Path(self.tmp.name) / "other"
        shutil.copytree(self.repo, other, symlinks=True)
        (other / ".git" / "relay-repo-id").unlink()  # a plain clone, no marker
        self.assertIsNone(trees.resolve_project(other,
                                                state_root=self.state_root))
        conn = sqlite3.connect(str(self.state_root / "integration"
                                   / "registry.sqlite3"))
        count = conn.execute("SELECT COUNT(*) FROM repositories").fetchone()[0]
        conn.close()
        self.assertEqual(count, 1)

    def test_register_outside_git_is_usage_error(self):
        with self.assertRaises(trees.TreeUsageError):
            trees.register_repo(self.tmp.name, state_root=self.state_root)

    def test_whole_repo_move_is_repair_not_second_identity(self):
        moved = Path(self.tmp.name) / "moved"
        shutil.move(str(self.repo), moved)
        with self.assertRaises(trees.TreeRefusedError):
            trees.register_repo(moved, state_root=self.state_root)
        fixed = trees.register_repo(moved, state_root=self.state_root,
                                    repair=True)
        self.assertEqual(fixed["id"], self.record["id"])
        self.assertEqual(fixed["common_dir"],
                         os.path.realpath(moved / ".git"))
        self.assertEqual(fixed["project_root"], str(moved))
        # The marker followed the move and was reaffirmed.
        self.assertEqual((moved / ".git" / "relay-repo-id").read_text().strip(),
                         self.record["id"])
        self.assertEqual(trees.resolve_project(moved / "src",
                                               state_root=self.state_root)["id"],
                         self.record["id"])

    def test_repo_copy_is_refused_even_with_repair(self):
        copy = Path(self.tmp.name) / "copy"
        shutil.copytree(self.repo, copy, symlinks=True)
        with self.assertRaises(trees.TreeRefusedError):
            trees.register_repo(copy, state_root=self.state_root)
        with self.assertRaises(trees.TreeRefusedError):
            trees.register_repo(copy, state_root=self.state_root, repair=True)

    def test_tree_manager_requires_registration(self):
        other = Path(self.tmp.name) / "unregistered"
        other.mkdir()
        git(other, "init", "-q", "-b", "main")
        with self.assertRaises(trees.NotRegisteredError):
            trees.TreeManager(other, state_root=self.state_root)


class ConfigureTests(TreesTestCase):
    def test_configure_mode_and_target(self):
        r = trees.configure_repo(self.repo, state_root=self.state_root,
                                 mode="queue", target="main")
        self.assertEqual(r["mode"], "queue")
        self.assertEqual(r["target"], "main")
        r = trees.configure_repo(self.repo, state_root=self.state_root,
                                 mode="paused")
        self.assertEqual(r["mode"], "paused")
        self.assertEqual(r["target"], "main")

    def test_configure_validation(self):
        with self.assertRaises(trees.TreeUsageError):
            trees.configure_repo(self.repo, state_root=self.state_root,
                                 mode="yolo")
        with self.assertRaises(trees.TreeUsageError):
            trees.configure_repo(self.repo, state_root=self.state_root,
                                 target="bad..name")
        with self.assertRaises(trees.TreeUsageError):
            trees.configure_repo(self.repo, state_root=self.state_root)


# --------------------------------------------------------------------- create

class CreateTests(TreesTestCase):
    def test_create_record_and_layout(self):
        ws = self.mgr.create("sess-1", card="#RT3B")
        self.assertEqual(ws["repo_id"], self.record["id"])
        self.assertEqual(ws["status"], "active")
        self.assertEqual(ws["session"], "sess-1")
        self.assertEqual(ws["card"], "#RT3B")
        self.assertEqual(ws["owner"], "sess-1")
        self.assertEqual(ws["base_sha"], self.base_sha)
        expected = (self.state_root / "trees" / self.record["id"] / ws["id"])
        self.assertEqual(Path(ws["path"]), expected)
        self.assertEqual(ws["execution_cwd"], ws["path"])
        self.assertTrue((Path(ws["path"]) / "AGENTS.md").exists())
        self.assertEqual(git(self.repo, "rev-parse", ws["branch"]),
                         self.base_sha)

    def test_create_is_idempotent_per_active_session(self):
        first = self.mgr.create("sess-1")
        second = self.mgr.create("sess-1")
        self.assertEqual(first["id"], second["id"])
        self.assertEqual(len(self.mgr.list()), 1)

    def test_create_requires_session(self):
        with self.assertRaises(trees.TreeUsageError):
            self.mgr.create("")

    def test_same_file_isolation_between_workspaces(self):
        one = self.mgr.create("sess-1")
        two = self.mgr.create("sess-2")
        tip1 = self.commit_in(one["path"], "src/code.py", "x = 100\n")
        self.assertEqual((Path(two["path"]) / "src" / "code.py").read_text(),
                         "x = 1\n")
        self.assertEqual((self.repo / "src" / "code.py").read_text(), "x = 1\n")
        tip2 = self.commit_in(two["path"], "src/code.py", "x = 200\n")
        self.assertNotEqual(tip1, tip2)
        # Both branch tips coexist in the one repository; main never moved.
        self.assertEqual(git(self.repo, "rev-parse", one["branch"]), tip1)
        self.assertEqual(git(self.repo, "rev-parse", two["branch"]), tip2)
        self.assertEqual(git(self.repo, "rev-parse", "main"), self.base_sha)

    def test_quota_refusal(self):
        self.mgr.create("sess-1", max_workspaces=1)
        with self.assertRaises(trees.TreeRefusedError):
            self.mgr.create("sess-2", max_workspaces=1)

    def test_ambient_git_env_is_scrubbed(self):
        with mock.patch.dict(os.environ, {"GIT_DIR": "/nonexistent/.git",
                                          "GIT_WORK_TREE": "/nonexistent",
                                          "GIT_INDEX_FILE": "/nonexistent"}):
            ws = self.mgr.create("sess-env")
            self.assertEqual(ws["status"], "active")
            self.assertIsNotNone(trees.resolve_project(
                ws["path"], state_root=self.state_root))


class SparseTests(TreesTestCase):
    def test_default_excludes_canonical_board_keeps_instructions(self):
        ws = self.mgr.create("sess-1")
        have = self.files(ws["path"])
        self.assertNotIn(".board/card.md", have)
        self.assertNotIn(".board/other.md", have)
        for name in ("AGENTS.md", "CLAUDE.md", "RELAY.md", ".relay/project.toml"):
            self.assertIn(name, have)
        self.assertIn("src/code.py", have)
        # The main checkout is untouched and not sparse.
        self.assertIn(".board/card.md", self.files(self.repo))
        self.assertEqual(git(self.repo, "status", "--porcelain"), "")

    def test_excludes_never_remove_instruction_or_config_files(self):
        ws = self.mgr.create("sess-1", excludes=["*"])
        have = self.files(ws["path"])
        for name in ("AGENTS.md", "CLAUDE.md", "RELAY.md", ".relay/project.toml"):
            self.assertIn(name, have)
        self.assertNotIn("src/code.py", have)
        self.assertNotIn(".board/card.md", have)

    def test_explicit_include_punches_own_evidence_back_in(self):
        ws = self.mgr.create("sess-1", excludes=["docs"],
                             includes=["docs/qa_evidence/"])
        have = self.files(ws["path"])
        self.assertIn("docs/qa_evidence/ev.md", have)
        self.assertNotIn("docs/notes.txt", have)

    def test_include_may_not_reinclude_canonical_board(self):
        with self.assertRaises(trees.TreeRefusedError):
            self.mgr.create("sess-1", includes=[".board/card.md"])
        with self.assertRaises(trees.TreeRefusedError):
            self.mgr.create("sess-1", includes=[".board"])

    def test_worktree_status_clean_after_sparse(self):
        ws = self.mgr.create("sess-1", excludes=["src"])
        self.assertEqual(git(ws["path"], "status", "--porcelain"), "")


class InitTests(TreesTestCase):
    def test_init_argv_runs_without_shell(self):
        ws = self.mgr.create("sess-1", init=["/bin/echo", "hello"])
        self.assertEqual(ws["status"], "active")
        self.assertEqual(ws["init_error"], "")

    def test_init_string_is_shlex_split_no_shell_features(self):
        # A redirection would need a shell; as argv it is literal text.
        ws = self.mgr.create("sess-1", init="/bin/echo hi '>' out.txt")
        self.assertEqual(ws["status"], "active")
        self.assertNotIn("out.txt", self.files(ws["path"]))

    def test_init_failure_is_durable_and_retryable(self):
        ws = self.mgr.create("sess-1", init=["/bin/sh", "-c", "exit 3"])
        self.assertEqual(ws["status"], "init_failed")
        self.assertIn("exit 3", ws["init_error"])
        again = self.mgr.create("sess-1", init=["/bin/echo", "ok"])
        self.assertEqual(again["id"], ws["id"])
        self.assertEqual(again["status"], "init_failed")  # no retry, no rerun
        retried = self.mgr.create("sess-1", init=["/bin/echo", "ok"], retry=True)
        self.assertEqual(retried["id"], ws["id"])
        self.assertEqual(retried["status"], "active")
        self.assertEqual(retried["init_error"], "")

    def test_init_missing_executable_fails_durably(self):
        ws = self.mgr.create("sess-1", init=["/nonexistent/bin"])
        self.assertEqual(ws["status"], "init_failed")
        self.assertIn("not found", ws["init_error"])


class RecoveryTests(TreesTestCase):
    def test_crash_after_worktree_recovers_on_next_create(self):
        real = trees.TreeManager._materialize

        def crash_after(self_, path, branch, base_sha, excludes, includes):
            real(self_, path, branch, base_sha, excludes, includes)
            raise KeyboardInterrupt("simulated crash")

        with mock.patch.object(trees.TreeManager, "_materialize", crash_after):
            with self.assertRaises(KeyboardInterrupt):
                self.mgr.create("sess-1")
        ws = self.mgr.create("sess-1")
        self.assertEqual(ws["status"], "active")
        self.assertTrue((Path(ws["path"]) / "AGENTS.md").exists())
        self.assertEqual(len(self.mgr.list()), 1)

    def test_crash_before_worktree_is_cleaned_then_recreatable(self):
        def crash(self_, path, branch, base_sha, excludes, includes):
            raise KeyboardInterrupt("simulated crash")

        with mock.patch.object(trees.TreeManager, "_materialize", crash):
            with self.assertRaises(KeyboardInterrupt):
                self.mgr.create("sess-1")
        with self.assertRaises(trees.TreeRefusedError):
            self.mgr.create("sess-1")  # reports the recovery, stale row removed
        ws = self.mgr.create("sess-1")
        self.assertEqual(ws["status"], "active")
        self.assertEqual(len(self.mgr.list()), 1)

    def test_concurrent_same_session_create_runs_init_once(self):
        counter = Path(self.tmp.name) / "init-count"
        script = ("import time, pathlib; time.sleep(0.4); "
                  f"p = pathlib.Path({str(counter)!r}); "
                  "p.write_text(str(int(p.read_text() or '0') + 1) "
                  "if p.exists() else '1')")
        results = {}

        def run(name):
            results[name] = self.mgr.create("sess-1",
                                            init=[sys.executable, "-c", script])

        first = threading.Thread(target=run, args=("first",))
        first.start()
        time.sleep(0.1)  # the first create is inside its init
        run("second")    # must wait out the lock, not resume mid-init
        first.join()
        self.assertEqual(results["first"]["id"], results["second"]["id"])
        self.assertEqual(results["second"]["status"], "active")
        self.assertEqual(counter.read_text(), "1")


# ------------------------------------------------------------ release / sync

class ReleaseTests(TreesTestCase):
    def test_release_clean_workspace(self):
        ws = self.mgr.create("sess-1")
        done = self.mgr.release(ws["id"], owner="sess-1")
        self.assertEqual(done["status"], "released")
        self.assertEqual(done["owner"], "")

    def test_release_with_unlanded_work_is_retained(self):
        ws = self.mgr.create("sess-1")
        self.commit_in(ws["path"], "src/code.py", "x = 2\n")
        done = self.mgr.release(ws["id"], owner="sess-1")
        self.assertEqual(done["status"], "retained")

    def test_release_wrong_owner_is_conflict(self):
        ws = self.mgr.create("sess-1")
        with self.assertRaises(trees.TreeConflictError):
            self.mgr.release(ws["id"], owner="someone-else")


class SyncTests(TreesTestCase):
    def test_sync_rebases_onto_moved_target(self):
        ws = self.mgr.create("sess-1")
        self.commit_in(ws["path"], "src/code.py", "x = 2\n")
        new_main = self.commit_in(self.repo, "docs/notes.txt", "notes v2\n")
        synced = self.mgr.sync(ws["id"], owner="sess-1")
        self.assertEqual(synced["base_sha"], new_main)
        self.assertEqual((Path(ws["path"]) / "docs" / "notes.txt").read_text(),
                         "notes v2\n")
        self.assertEqual((Path(ws["path"]) / "src" / "code.py").read_text(),
                         "x = 2\n")

    def test_sync_refuses_competing_lease(self):
        ws = self.mgr.create("sess-1")
        with self.assertRaises(trees.TreeConflictError):
            self.mgr.sync(ws["id"], owner="sess-2")

    def test_sync_refuses_dirty_work(self):
        ws = self.mgr.create("sess-1")
        (Path(ws["path"]) / "src" / "code.py").write_text("x = dirty\n")
        with self.assertRaises(trees.TreeRefusedError):
            self.mgr.sync(ws["id"], owner="sess-1")

    def test_sync_refuses_after_release(self):
        ws = self.mgr.create("sess-1")
        self.mgr.release(ws["id"], owner="sess-1")
        with self.assertRaises(trees.TreeRefusedError):
            self.mgr.sync(ws["id"], owner="sess-1")

    def test_sync_conflict_aborts_rebase(self):
        ws = self.mgr.create("sess-1")
        self.commit_in(ws["path"], "src/code.py", "x = 2\n")
        self.commit_in(self.repo, "src/code.py", "x = 99\n")
        with self.assertRaises(trees.TreeConflictError):
            self.mgr.sync(ws["id"], owner="sess-1")
        self.assertEqual(git(ws["path"], "status", "--porcelain"), "")
        git_dir = git(ws["path"], "rev-parse", "--git-dir")
        self.assertFalse((Path(git_dir) / "REBASE_HEAD").exists())
        # The workspace kept its own work; the rebase was not left behind.
        self.assertEqual((Path(ws["path"]) / "src" / "code.py").read_text(),
                         "x = 2\n")


# -------------------------------------------------------------------- remove

class RemoveTests(TreesTestCase):
    def setUp(self):
        super().setUp()
        self._queue_db = self.make_queue_db()

    def landed_workspace(self, *, dirty=None):
        job_id = f"job-{dirty or 'clean'}"
        ws = self.mgr.create("sess-1")
        tip = self.commit_in(ws["path"], "src/code.py", "x = 2\n")
        ws = self.mgr.release(ws["id"], owner="sess-1")
        self.assertEqual(ws["status"], "retained")
        if dirty == "ignored":
            (Path(ws["path"]) / ".gitignore").write_text("*.log\n")
            git(ws["path"], "add", ".gitignore")
            git(ws["path"], "commit", "-qm", "ignore logs")
            (Path(ws["path"]) / "debug.log").write_text("log\n")
        elif dirty == "tracked":
            (Path(ws["path"]) / "src" / "code.py").write_text("x = dirty\n")
        elif dirty == "untracked":
            (Path(ws["path"]) / "scratch.txt").write_text("scratch\n")
        ws = self.mgr.get(ws["id"])
        receipt = self.make_receipt(ws, job_id=job_id)
        return ws, receipt, tip

    def test_remove_landed_workspace_with_receipt(self):
        ws, receipt, _ = self.landed_workspace()
        done = self.mgr.remove(ws["id"], receipt=receipt)
        self.assertEqual(done["status"], "removed")
        self.assertFalse(Path(ws["path"]).exists())
        rc = subprocess.run(["git", "branch", "--list", ws["branch"]],
                            cwd=self.repo, capture_output=True, text=True)
        self.assertEqual(rc.stdout.strip(), "")
        self.assertEqual(self.mgr.list(), [])
        self.assertEqual(len(self.mgr.list(include_removed=True)), 1)

    def test_remove_is_idempotent(self):
        ws, receipt, _ = self.landed_workspace()
        self.mgr.remove(ws["id"], receipt=receipt)
        again = self.mgr.remove(ws["id"], receipt=receipt)
        self.assertEqual(again["status"], "removed")

    def test_remove_requires_released_lease(self):
        ws = self.mgr.create("sess-1")
        with self.assertRaises(trees.TreeRefusedError):
            self.mgr.remove(ws["id"])

    def test_remove_refuses_unlanded_work_without_receipt(self):
        ws = self.mgr.create("sess-1")
        self.commit_in(ws["path"], "src/code.py", "x = 2\n")
        self.mgr.release(ws["id"], owner="sess-1")
        with self.assertRaises(trees.TreeRefusedError):
            self.mgr.remove(ws["id"])

    def test_remove_empty_workspace_without_receipt(self):
        ws = self.mgr.create("sess-1")
        self.mgr.release(ws["id"], owner="sess-1")
        done = self.mgr.remove(ws["id"])
        self.assertEqual(done["status"], "removed")

    def test_remove_verifies_receipt_repo(self):
        ws, receipt, _ = self.landed_workspace()
        receipt = dict(receipt, repo_id="other-repo")
        with self.assertRaises(trees.TreeRefusedError):
            self.mgr.remove(ws["id"], receipt=receipt)

    def test_remove_verifies_receipt_covers_current_tip(self):
        ws, receipt, _ = self.landed_workspace()
        self.commit_in(ws["path"], "src/code.py", "x = 3\n")  # tip moved on
        with self.assertRaises(trees.TreeRefusedError):
            self.mgr.remove(ws["id"], receipt=receipt)

    def test_remove_requires_durable_published_receipt(self):
        ws = self.mgr.create("sess-1")
        tip = self.commit_in(ws["path"], "src/code.py", "x = 2\n")
        self.mgr.release(ws["id"], owner="sess-1")
        ghost = {"job_id": "ghost", "repo_id": self.record["id"],
                 "submitted_sha": tip}
        with self.assertRaises(trees.TreeRefusedError):  # not in receipts table
            self.mgr.remove(ws["id"], receipt=ghost)
        durable = self.make_receipt(ws, published=False)
        with self.assertRaises(trees.TreeRefusedError):  # no published SHA
            self.mgr.remove(ws["id"], receipt=durable)

    def test_remove_refuses_pending_submissions(self):
        ws, receipt, _ = self.landed_workspace()
        self.queue_add("jobs", id="job1", request_id="req1",
                       repo_id=self.record["id"], workspace_id=ws["id"],
                       card="", kind="code", base_sha=self.base_sha,
                       submitted_sha=receipt["submitted_sha"], target_sha="",
                       candidate_sha="", candidate_tree="", policy_hash="",
                       selected_tests_json="[]", status="verifying", reason="",
                       created_at="", updated_at="")
        with self.assertRaises(trees.TreeRefusedError):
            self.mgr.remove(ws["id"], receipt=receipt)
        conn = sqlite3.connect(str(self._queue_db))
        conn.execute("UPDATE jobs SET status = 'landed' WHERE id = 'job1'")
        conn.commit()
        conn.close()
        done = self.mgr.remove(ws["id"], receipt=receipt)
        self.assertEqual(done["status"], "removed")

    def test_remove_refuses_dirty_untracked_and_ignored(self):
        for kind in ("tracked", "untracked", "ignored"):
            with self.subTest(kind=kind):
                ws, receipt, _ = self.landed_workspace(dirty=kind)
                with self.assertRaises(trees.TreeRefusedError):
                    self.mgr.remove(ws["id"], receipt=receipt)
                self.assertTrue(Path(ws["path"]).exists())  # nothing deleted

    def test_remove_succeeds_once_user_cleans_up(self):
        ws, receipt, _ = self.landed_workspace(dirty="untracked")
        with self.assertRaises(trees.TreeRefusedError):
            self.mgr.remove(ws["id"], receipt=receipt)
        (Path(ws["path"]) / "scratch.txt").unlink()  # the user cleans up
        done = self.mgr.remove(ws["id"], receipt=receipt)
        self.assertEqual(done["status"], "removed")

    def test_remove_without_queue_db_and_without_receipt(self):
        (self._queue_db).unlink()
        ws = self.mgr.create("sess-1")
        self.mgr.release(ws["id"], owner="sess-1")
        done = self.mgr.remove(ws["id"])
        self.assertEqual(done["status"], "removed")


# ----------------------------------------------------------------------- CLI

class CliTests(TreesTestCase):
    def run_cli(self, *args, cwd=None):
        return subprocess.run(
            [sys.executable, str(CLI), *args,
             "--state-root", str(self.state_root)],
            cwd=str(cwd or self.repo), capture_output=True, text=True,
            timeout=120)

    def test_usage_error_exit_1(self):
        proc = self.run_cli("create")  # --session is required
        self.assertEqual(proc.returncode, 1)

    def test_resolve_unregistered_exit_1(self):
        other = Path(self.tmp.name) / "elsewhere"
        other.mkdir()
        git(other, "init", "-q", "-b", "main")
        proc = self.run_cli("resolve", str(other))
        self.assertEqual(proc.returncode, 1)
        self.assertIn("not inside a registered repository", proc.stderr)

    def test_full_lifecycle_exit_codes(self):
        proc = self.run_cli("resolve", str(self.repo))
        self.assertEqual(proc.returncode, 0, proc.stderr)
        self.assertEqual(json.loads(proc.stdout)["id"], self.record["id"])

        proc = self.run_cli("create", "cli-1", "--repo", str(self.repo),
                            "--exclude", "docs", "--card", "#RT3B")
        self.assertEqual(proc.returncode, 0, proc.stderr)
        ws = json.loads(proc.stdout)
        self.assertEqual(ws["status"], "active")

        proc = self.run_cli("list", "--repo", self.record["id"])
        self.assertEqual(proc.returncode, 0)
        self.assertEqual(len(json.loads(proc.stdout)), 1)

        proc = self.run_cli("get", ws["id"], "--repo", str(self.repo))
        self.assertEqual(proc.returncode, 0)
        self.assertEqual(json.loads(proc.stdout)["id"], ws["id"])

        # Wrong owner: conflict, exit 3.
        proc = self.run_cli("release", ws["id"], "--owner", "nope",
                            "--repo", str(self.repo))
        self.assertEqual(proc.returncode, 3)
        proc = self.run_cli("release", ws["id"], "--owner", "cli-1",
                            "--repo", str(self.repo))
        self.assertEqual(proc.returncode, 0, proc.stderr)
        self.assertEqual(json.loads(proc.stdout)["status"], "released")

        # Sync after release: refused, exit 2.
        proc = self.run_cli("sync", ws["id"], "--owner", "cli-1",
                            "--repo", str(self.repo))
        self.assertEqual(proc.returncode, 2)

        proc = self.run_cli("remove", ws["id"], "--repo", str(self.repo))
        self.assertEqual(proc.returncode, 0, proc.stderr)
        self.assertEqual(json.loads(proc.stdout)["status"], "removed")

    def test_cli_remove_with_receipt_file(self):
        self._queue_db = self.make_queue_db()
        ws = self.mgr.create("cli-2")
        self.commit_in(ws["path"], "src/code.py", "x = 5\n")
        ws = self.mgr.release(ws["id"], owner="cli-2")
        receipt = self.make_receipt(ws)
        receipt_file = Path(self.tmp.name) / "receipt.json"
        receipt_file.write_text(json.dumps(receipt))
        proc = self.run_cli("remove", ws["id"], "--receipt", f"@{receipt_file}",
                            "--repo", str(self.repo))
        self.assertEqual(proc.returncode, 0, proc.stderr)
        self.assertEqual(json.loads(proc.stdout)["status"], "removed")

    def test_cli_register_and_configure(self):
        other = Path(self.tmp.name) / "second"
        other.mkdir()
        git(other, "init", "-q", "-b", "main")
        proc = self.run_cli("register", str(other), "--target", "main")
        self.assertEqual(proc.returncode, 0, proc.stderr)
        self.assertEqual(json.loads(proc.stdout)["mode"], "legacy")
        proc = self.run_cli("configure", "--repo", str(other), "--mode", "queue")
        self.assertEqual(proc.returncode, 0, proc.stderr)
        self.assertEqual(json.loads(proc.stdout)["mode"], "queue")
        proc = self.run_cli("configure", "--repo", str(other), "--mode", "bogus")
        self.assertEqual(proc.returncode, 1)


if __name__ == "__main__":
    unittest.main()
