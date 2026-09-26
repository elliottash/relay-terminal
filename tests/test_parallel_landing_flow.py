# SPDX-License-Identifier: AGPL-3.0-or-later
"""Cross-module acceptance checks for isolated authors and exact verified publication."""
from __future__ import annotations

import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "backend"))
from relay_core import landq, projectconf, trees


class ParallelLandingFlow(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="relay-parallel-flow-")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.repo = self.root / "project"
        self.repo.mkdir()
        self.state = self.root / "state"
        self.git(self.repo, "init", "-b", "main")
        self.git(self.repo, "config", "user.name", "Integration Test")
        self.git(self.repo, "config", "user.email", "integration@example.invalid")
        self.original = "\n".join(f"line {n}" for n in range(40)) + "\n"
        (self.repo / "shared.txt").write_text(self.original)
        (self.repo / ".relay").mkdir()
        (self.repo / ".relay/project.toml").write_text(
            'version = 1\n[project]\ntarget = "main"\n[verification]\n'
            'commands = [["' + sys.executable + '", "-c", '
            '"from pathlib import Path; assert len(Path(\'shared.txt\').read_text().splitlines()) == 40"]]\n'
        )
        self.commit(self.repo, "baseline")
        self.base = self.git(self.repo, "rev-parse", "HEAD")
        self.git(self.repo, "switch", "-c", "human")
        trees.register_repo(self.repo, state_root=self.state)
        self.manager = trees.TreeManager(self.repo, state_root=self.state)
        self.queue = landq.Queue(self.repo, state_root=self.state)
        self.config = projectconf.load(self.repo)
        self.verified = []

    @staticmethod
    def git(cwd, *args):
        env = {k: v for k, v in os.environ.items() if not k.startswith("GIT_")}
        result = subprocess.run(["git", "-c", "core.hooksPath=/dev/null", *args],
                                cwd=cwd, env=env, text=True, capture_output=True)
        if result.returncode:
            raise AssertionError(result.stderr)
        return result.stdout.strip()

    def commit(self, cwd, message):
        self.git(cwd, "add", ".")
        self.git(cwd, "commit", "-m", message)
        return self.git(cwd, "rev-parse", "HEAD")

    def author(self, name, old, new):
        record = self.manager.create(name, base=self.base)
        path = Path(record["path"])
        (path / "shared.txt").write_text(self.original.replace(old, new))
        sha = self.commit(path, name)
        return record, path, sha

    def verify(self, job, candidate, path):
        self.assertEqual(candidate, self.git(path, "rev-parse", "HEAD"))
        result = projectconf.run_gate(self.config, path)
        self.assertTrue(result["ok"], result)
        self.verified.append((job["id"], candidate))
        return result

    def test_two_same_file_authors_land_without_rewriting_either_workspace(self):
        alice, apath, asha = self.author("alice", "line 2\n", "alice change\n")
        bob, bpath, bsha = self.author("bob", "line 35\n", "bob change\n")
        ajob = self.queue.submit(asha, request_id="alice-v1", workspace_id=alice["id"])
        bjob = self.queue.submit(bsha, request_id="bob-v1", workspace_id=bob["id"])
        self.queue.process_one(self.verify)
        self.queue.process_one(self.verify)
        main = self.git(self.repo, "show", "main:shared.txt")
        self.assertIn("alice change", main)
        self.assertIn("bob change", main)
        self.assertEqual(asha, self.git(apath, "rev-parse", "HEAD"))
        self.assertEqual(bsha, self.git(bpath, "rev-parse", "HEAD"))
        self.assertNotIn("bob change", (apath / "shared.txt").read_text())
        self.assertNotIn("alice change", (bpath / "shared.txt").read_text())
        self.assertEqual(self.original, (self.repo / "shared.txt").read_text())
        for job in (ajob, bjob):
            receipt = self.queue.receipt(job["id"])
            self.assertIsNotNone(receipt)
            self.assertIn((job["id"], receipt["published_sha"]), self.verified)
            self.assertTrue(receipt["verified"])

    def test_submission_is_fixed_and_post_submit_edit_stays_in_author_tree(self):
        author, path, sha = self.author("next-edit", "line 2\n", "submitted\n")
        job = self.queue.submit(sha, request_id="fixed-v1", workspace_id=author["id"])
        duplicate = self.queue.submit(sha, request_id="fixed-v1", workspace_id=author["id"])
        self.assertEqual(job["id"], duplicate["id"])
        (path / "shared.txt").write_text((path / "shared.txt").read_text().replace(
            "line 35\n", "not submitted\n"))
        self.queue.process_one(self.verify)
        self.assertNotIn("not submitted", self.git(self.repo, "show", "main:shared.txt"))
        self.assertIn("not submitted", (path / "shared.txt").read_text())
        self.assertEqual(sha, self.queue.receipt(job["id"])["submitted_sha"])

    def test_failed_gate_keeps_main_and_submission_recoverable(self):
        author, path, sha = self.author("broken", "line 2\n", "candidate\n")
        job = self.queue.submit(sha, request_id="broken-v1", workspace_id=author["id"])
        self.queue.process_one(lambda *_: {
            "ok": False, "verified": False, "policy_hash": "test-policy",
            "log": "acceptance gate failed", "reason": "acceptance gate failed"})
        self.assertEqual(self.base, self.git(self.repo, "rev-parse", "main"))
        self.assertIsNone(self.queue.receipt(job["id"]))
        restarted = landq.Queue(self.repo, state_root=self.state)
        self.assertEqual(sha, restarted.status(job["id"])["submitted_sha"])
        self.assertEqual(sha, self.git(path, "rev-parse", "HEAD"))

    def test_installed_cli_layout_works_outside_the_source_checkout(self):
        source = Path(__file__).resolve().parents[1]
        installed = self.root / "install" / "share" / "relay"
        shutil.copytree(source / "backend", installed / "backend",
                        ignore=shutil.ignore_patterns("__pycache__", "*.pyc"))
        (installed / "scripts").mkdir()
        for name in ("relay-tree", "relay-land"):
            shutil.copy2(source / "scripts" / name, installed / "scripts" / name)
            env = {k: v for k, v in os.environ.items()
                   if not k.startswith(("PYTHON", "GIT_"))}
            result = subprocess.run([sys.executable, str(installed / "scripts" / name),
                                     "--help"], cwd=self.root, env=env,
                                    text=True, capture_output=True, timeout=30)
            self.assertEqual(0, result.returncode, result.stderr)
            self.assertIn("usage", result.stdout.lower())

    def test_candidate_cannot_approve_its_own_weaker_gate(self):
        record = self.manager.create("weaker-policy", base=self.base)
        path = Path(record["path"])
        (path / "shared.txt").write_text("broken invariant\n")
        (path / ".relay/project.toml").write_text(
            'version = 1\n[verification]\ncommands = [["' + sys.executable +
            '", "-c", "pass"]]\n')
        sha = self.commit(path, "attempt to bypass the existing gate")
        job = self.queue.submit(sha, request_id="policy-v1", workspace_id=record["id"])
        accepted_policy = projectconf.load(self.repo, revision="main")
        self.queue.process_one(lambda _job, _sha, cwd:
                               projectconf.run_gate(accepted_policy, cwd))
        self.assertEqual(self.base, self.git(self.repo, "rev-parse", "main"))
        self.assertIsNone(self.queue.receipt(job["id"]))
        self.assertEqual("failed", self.queue.status(job["id"])["status"])


if __name__ == "__main__":
    unittest.main()
