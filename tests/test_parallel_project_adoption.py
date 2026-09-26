# SPDX-License-Identifier: AGPL-3.0-or-later
"""Adopt an unrelated Python project using the shipped relay-land CLI only."""
from __future__ import annotations

import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time
import unittest


CLI = Path(__file__).resolve().parents[1] / "scripts" / "relay-land"


@unittest.skipUnless(sys.platform.startswith("linux"), "queue publication is Linux-first")
class ParallelProjectAdoption(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix="relay-second-project-")
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        self.repo = self.root / "independent-python-project"
        self.repo.mkdir()
        self.state = self.root / "state"
        self.env = {k: v for k, v in os.environ.items()
                    if not k.startswith(("GIT_", "PYTHON"))}
        self.env.update(XDG_CACHE_HOME=str(self.root / "cache"),
                        RELAY_LAND_ROOT=str(self.root / "legacy-land"))
        self.git(self.repo, "init", "-b", "main")
        self.git(self.repo, "config", "user.name", "Second Project Test")
        self.git(self.repo, "config", "user.email", "second@example.invalid")
        (self.repo / "app.py").write_text(
            "#!/usr/bin/env python3\n"
            "import json, pathlib, sys, time\n"
            "root = pathlib.Path(__file__).resolve().parent.parent\n"
            "data = (pathlib.Path(__file__).resolve().parent / 'data.txt').read_text().splitlines()\n"
            "release = root / 'release.json'\n"
            "print(json.dumps({'sha': json.loads(release.read_text())['sha'] if release.exists() "
            "else None, 'data': data}), flush=True)\n"
            "if '--hold' in sys.argv: time.sleep(30)\n")
        (self.repo / "data.txt").write_text(
            "first=base\n" + "".join(f"spacer={n}\n" for n in range(38)) + "second=base\n")
        (self.repo / ".gitignore").write_text("local-build/\n")
        (self.repo / "buildinstall.py").write_text(
            "import pathlib, shutil, sys\n"
            "mode, dest, *rest = sys.argv[1:]\n"
            "dest = pathlib.Path(dest)\n"
            "if mode == 'build':\n"
            "    dest.mkdir(parents=True, exist_ok=True)\n"
            "    (dest / 'built.txt').write_text('built\\n')\n"
            "elif mode == 'install':\n"
            "    assert (pathlib.Path(rest[0]) / 'built.txt').is_file()\n"
            "    target = dest / 'bin'\n"
            "    target.mkdir(parents=True, exist_ok=True)\n"
            "    for name in ('app.py', 'data.txt'): shutil.copy2(name, target / name)\n"
            "    (target / 'app.py').chmod(0o755)\n")
        (self.repo / "tests").mkdir()
        (self.repo / "tests/test_app.py").write_text(
            "import pathlib, unittest\n"
            "class DataTest(unittest.TestCase):\n"
            "    def test_data(self):\n"
            "        lines = pathlib.Path('data.txt').read_text().splitlines()\n"
            "        self.assertEqual(len(lines), 40)\n"
            "        self.assertTrue(all('=' in line and 'BAD' not in line for line in lines))\n")
        (self.repo / ".board").mkdir()
        (self.repo / ".board/BOARD.md").write_text("# Independent project Board\n")
        self.git(self.repo, "add", ".")
        self.git(self.repo, "commit", "-m", "Initial independent Python application")

    def git(self, cwd, *args):
        proc = subprocess.run(["git", *args], cwd=cwd, env=self.env, text=True,
                              capture_output=True, timeout=30)
        self.assertEqual(proc.returncode, 0, proc.stderr)
        return proc.stdout.strip()

    def cli(self, *args, expected=0):
        command = [sys.executable, str(CLI), "--repo", str(self.repo),
                   "--state-root", str(self.state), *map(str, args)]
        started = time.monotonic()
        proc = subprocess.run(command, cwd=self.root, env=self.env, text=True,
                              capture_output=True, timeout=90)
        self.assertEqual(proc.returncode, expected,
                         f"{' '.join(command)}\nstdout: {proc.stdout}\nstderr: {proc.stderr}")
        try:
            result = json.loads(proc.stdout)
        except json.JSONDecodeError as exc:
            self.fail(f"CLI did not return JSON: {proc.stdout!r}: {exc}")
        return result, time.monotonic() - started

    def author(self, name, line, replacement):
        workspace, _ = self.cli("workspace", "create", name)
        path = Path(workspace["execution_cwd"])
        data = path / "data.txt"
        data.write_text(data.read_text().replace(line, replacement))
        self.git(path, "add", "data.txt")
        self.git(path, "commit", "-m", f"{name} edits data")
        return workspace, path, self.git(path, "rev-parse", "HEAD")

    def test_independent_project_cli_adoption(self):
        init, _ = self.cli("project-init")
        self.assertEqual(init["registry"]["mode"], "legacy")
        self.assertFalse(init["config_present"])
        (self.repo / ".relay").mkdir()
        (self.repo / ".relay/project.toml").write_text(
            'version = 1\n[project]\ntarget = "main"\n'
            '[workspace]\nexclude = [".board"]\nmax_workspaces = 4\n'
            '[verification]\ncommands = [["python3", "-m", "unittest", "discover", "-s", "tests", "-v"]]\n'
            'timeout_seconds = 30\n'
            '[resources]\nmemory_bytes = 1048576\ndisk_bytes = 1048576\ncpus = 1\n'
            '[main]\nbuild = [["python3", "buildinstall.py", "build", "{build}"]]\n'
            'install = [["python3", "buildinstall.py", "install", "{dest}", "{build}"]]\n'
            'executable = "bin/app.py"\n'
            'smoke = [["{executable}"]]\nkeep = 2\n'
            '[reconcile]\nenabled = false\n')
        self.git(self.repo, "add", ".relay/project.toml")
        self.git(self.repo, "commit", "-m", "Configure verification and installed main")
        baseline = self.git(self.repo, "rev-parse", "HEAD")
        ready, _ = self.cli("activate", "--dry-run")
        self.assertTrue(ready["can_activate"], ready["blockers"])
        activated, _ = self.cli("activate")
        self.assertTrue(activated["activated"])
        self.assertEqual(self.git(self.repo, "symbolic-ref", "--short", "HEAD"), "human")
        self.assertEqual(self.git(self.repo, "rev-parse", "HEAD"), baseline)

        alice, alice_path, alice_sha = self.author("alice", "first=base", "first=alice")
        bob, bob_path, bob_sha = self.author("bob", "second=base", "second=bob")
        self.assertFalse((alice_path / ".board").exists())
        first_try, _ = self.cli("try", alice_sha)
        self.assertTrue(first_try["ok"])
        first, _ = self.cli("submit", alice_sha, "--request-id", "alice-1",
                            "--workspace-id", alice["workspace_id"])
        duplicate, _ = self.cli("submit", alice_sha, "--request-id", "alice-1",
                                "--workspace-id", alice["workspace_id"])
        self.assertEqual(first["id"], duplicate["id"])
        self.assertEqual(self.cli("status", first["id"])[0]["status"], "queued")
        first_run, first_elapsed = self.cli("run", "--once")
        self.assertEqual(first_run["job"]["status"], "landed", first_run)
        first_receipt, _ = self.cli("receipt", first["id"])
        self.assertTrue(first_receipt["verified"])
        first_main, _ = self.cli("main-status")
        self.assertEqual(first_main["installed_sha"], first_receipt["published_sha"])

        previous = subprocess.Popen(
            [sys.executable, str(CLI), "--repo", str(self.repo), "--state-root",
             str(self.state), "main-run", "--", "--hold"], cwd=self.root, env=self.env,
            text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        self.addCleanup(self._stop, previous)
        old_output = json.loads(previous.stdout.readline())
        self.assertEqual(old_output["sha"], first_main["installed_sha"])

        second, _ = self.cli("submit", bob_sha, "--request-id", "bob-1",
                             "--workspace-id", bob["workspace_id"])
        (bob_path / "data.txt").write_text((bob_path / "data.txt").read_text().replace(
            "second=bob", "second=bob-local-after-submit"))
        second_run, second_elapsed = self.cli("run", "--once")
        self.assertEqual(second_run["job"]["status"], "landed", second_run)
        second_receipt, _ = self.cli("receipt", second["id"])
        self.assertEqual(second_receipt["submitted_sha"], bob_sha)
        self.assertEqual(self.git(bob_path, "rev-parse", "HEAD"), bob_sha)
        self.assertIn("bob-local-after-submit", (bob_path / "data.txt").read_text())
        self.assertNotIn("bob-local-after-submit", self.git(self.repo, "show", "main:data.txt"))
        self.assertIsNone(previous.poll(), "the previous installed run stopped during update")
        current, _ = self.cli("main-status")
        self.assertEqual(current["installed_sha"], second_receipt["published_sha"])
        self.assertEqual(current["lag_commits"], 0)
        proc = subprocess.run([sys.executable, str(CLI), "--repo", str(self.repo),
                               "--state-root", str(self.state), "main-run"],
                              cwd=self.root, env=self.env, text=True, capture_output=True,
                              timeout=20)
        self.assertEqual(proc.returncode, 0, proc.stderr)
        live = json.loads(proc.stdout)
        self.assertEqual(live["sha"], current["installed_sha"])
        self.assertEqual((live["data"][0], live["data"][-1]),
                         ("first=alice", "second=bob"))
        self.assertEqual((old_output["data"][0], old_output["data"][-1]),
                         ("first=alice", "second=base"))
        self.assertEqual(self.git(alice_path, "rev-parse", "HEAD"), alice_sha)

        bad, bad_path, bad_sha = self.author("bad", "first=alice", "first=BAD")
        rejected, _ = self.cli("submit", bad_sha, "--request-id", "bad-1",
                               "--workspace-id", bad["workspace_id"])
        bad_run, _ = self.cli("run", "--once", expected=5)
        self.assertEqual(bad_run["job"]["status"], "failed", bad_run)
        self.assertEqual(self.cli("status", rejected["id"])[0]["status"], "failed")
        self.assertEqual(self.git(self.repo, "rev-parse", "main"), current["installed_sha"])
        self.assertEqual(self.git(bad_path, "rev-parse", "HEAD"), bad_sha)

        self.cli("workspace", "release", bad["workspace_id"], "--owner", bad["owner"])
        unlanded, _ = self.cli("workspace", "remove", bad["workspace_id"], expected=2)
        self.assertIn("receipt", unlanded["error"].lower())
        self.cli("workspace", "release", bob["workspace_id"], "--owner", bob["owner"])
        dirty, _ = self.cli("workspace", "remove", bob["workspace_id"], expected=2)
        self.assertIn("dirty", dirty["error"].lower())
        (alice_path / "local-build").mkdir()
        (alice_path / "local-build/cache.dat").write_text("keep ignored data\n")
        self.cli("workspace", "release", alice["workspace_id"], "--owner", alice["owner"])
        ignored, _ = self.cli("workspace", "remove", alice["workspace_id"], expected=2)
        self.assertIn("ignored", ignored["error"].lower())

        (self.repo / ".board/BOARD.md").write_text("# Independent project Board\n\nUpdated canonically.\n")
        board_job, _ = self.cli("board-submit", ".board/BOARD.md", "--session", "board-owner")
        self.assertEqual(board_job["kind"], "metadata")
        board_run, _ = self.cli("run", "--once", "--no-main")
        self.assertEqual(board_run["job"]["status"], "landed", board_run)
        self.assertIn("Updated canonically", self.git(self.repo, "show", "main:.board/BOARD.md"))
        self.assertFalse((alice_path / ".board").exists())
        lagged, _ = self.cli("main-status")
        self.assertEqual(lagged["installed_sha"], current["installed_sha"])
        self.assertGreater(lagged["lag_commits"], 0)

        (self.repo / "local-note.txt").write_text("keep me\n")
        rollback, _ = self.cli("rollback", "--keep-head")
        self.assertEqual(rollback["mode"], "legacy")
        self.assertEqual(self.git(self.repo, "symbolic-ref", "--short", "HEAD"), "human")
        self.assertEqual((self.repo / "local-note.txt").read_text(), "keep me\n")
        self.assertEqual(self.cli("status", rejected["id"])[0]["status"], "failed")
        print(json.dumps({"first_run_seconds": first_elapsed,
                          "second_run_seconds": second_elapsed,
                          "source_bytes": self._bytes(self.repo, exclude_git=True),
                          "tree_bytes": self._bytes(alice_path),
                          "build_bytes": self._bytes(self.root / "cache/relay/integration" /
                                                     init["registry"]["id"] / "tip/build"),
                          "installed_release_bytes": self._bytes(Path(current["current"]["path"])),
                          "first_main_build_seconds": first_run["main_release"].get("duration_seconds"),
                          "second_main_build_seconds": second_run["main_release"].get("duration_seconds")},
                         sort_keys=True))

    @staticmethod
    def _stop(proc):
        if proc.poll() is None:
            proc.terminate()
        try:
            proc.communicate(timeout=5)
        except subprocess.TimeoutExpired:
            proc.kill()
            proc.communicate(timeout=5)

    @staticmethod
    def _bytes(root, *, exclude_git=False):
        return sum(path.stat().st_size for path in root.rglob("*") if path.is_file()
                   and not path.is_symlink()
                   and not (exclude_git and ".git" in path.relative_to(root).parts))


if __name__ == "__main__":
    unittest.main()
