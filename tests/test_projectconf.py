# SPDX-License-Identifier: AGPL-3.0-or-later
"""projectconf: config v1 load/normalize/reject, policy_hash, detect, and the real gate runner.

The gate tests run real subprocesses: genuine pytest for the passing/selected-tests paths and
fake `pytest`/`ctest` executables for the zero-tests-on-exit-0 hole (ctest really exits 0 when it
found no tests; the gate must still fail closed).  Temp repos use a local test identity only.
"""
import os
import stat
import subprocess
import sys
import tempfile
import time
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "backend"))

from relay_core import projectconf as PC  # noqa: E402

CONTRACT_EXAMPLE = """
version = 1
[project]
target = "main"
[workspace]
exclude = [".board", "docs/qa_evidence"]
max_workspaces = 50
init = []
[verification]
commands = [["python3", "-m", "pytest", "-q"]]
timeout_seconds = 900
ungated = false
[resources]
memory_bytes = 8589934592
disk_bytes = 4294967296
cpus = 2
[main]
build = []
install = []
executable = "bin/relay"
keep = 2
[reconcile]
enabled = true
max_attempts = 2
tokens_per_case = 200000
tokens_per_day = 10000000
"""


def write(path: Path, text: str) -> Path:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8")
    return path


def fake_exe(directory: Path, name: str, body: str) -> Path:
    script = write(directory / name, "#!/bin/sh\n" + body)
    script.chmod(script.stat().st_mode | stat.S_IXUSR | stat.S_IXGRP | stat.S_IXOTH)
    return script


class LoadTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.repo = Path(self.tmp.name)

    def tearDown(self):
        self.tmp.cleanup()

    def load_text(self, text: str):
        write(self.repo / ".relay" / "project.toml", text)
        return PC.load(self.repo)

    def test_contract_example_normalizes(self):
        cfg = self.load_text(CONTRACT_EXAMPLE)
        self.assertEqual(cfg["project"]["target"], "main")
        self.assertEqual(cfg["workspace"]["exclude"], [".board", "docs/qa_evidence"])
        self.assertEqual(cfg["workspace"]["max_workspaces"], 50)
        self.assertEqual(cfg["verification"]["commands"], [["python3", "-m", "pytest", "-q"]])
        self.assertEqual(cfg["verification"]["timeout_seconds"], 900)
        self.assertFalse(cfg["verification"]["ungated"])
        self.assertEqual(cfg["resources"]["cpus"], 2)
        self.assertEqual(cfg["main"]["executable"], "bin/relay")
        self.assertEqual(cfg["main"]["keep"], 2)
        self.assertTrue(cfg["reconcile"]["enabled"])
        self.assertEqual(cfg["reconcile"]["tokens_per_day"], 10000000)
        # defaults filled
        self.assertEqual(cfg["main"]["smoke"], [])

    def test_missing_file_defaults_and_require_file(self):
        cfg = PC.load(self.repo)
        self.assertEqual(cfg["version"], 1)
        self.assertEqual(cfg["verification"]["commands"], [])
        with self.assertRaises(PC.ProjectConfigError):
            PC.load(self.repo, require_file=True)

    def test_load_at_revision(self):
        subprocess.run(["git", "init", "-q", "-b", "main"], cwd=self.repo, check=True)
        subprocess.run(["git", "config", "user.email", "t@example.com"], cwd=self.repo, check=True)
        subprocess.run(["git", "config", "user.name", "T"], cwd=self.repo, check=True)
        write(self.repo / ".relay" / "project.toml", CONTRACT_EXAMPLE)
        subprocess.run(["git", "add", ".relay/project.toml"], cwd=self.repo, check=True)
        subprocess.run(["git", "commit", "-qm", "config"], cwd=self.repo, check=True)
        cfg = PC.load(self.repo, revision="HEAD")
        self.assertEqual(cfg["reconcile"]["max_attempts"], 2)
        (self.repo / ".relay" / "project.toml").unlink()
        cfg = PC.load(self.repo, revision="HEAD")  # still there at the revision
        self.assertEqual(cfg["project"]["target"], "main")
        with self.assertRaises(PC.ProjectConfigError):
            PC.load(self.repo, revision="does-not-exist")

    def test_rejections(self):
        bad = {
            "bad TOML": "version = [",
            "unknown section": "[bogus]\nx = 1\n",
            "unknown key": "[verification]\ncomands = []\n",
            "bad version": "version = 2\n",
            "shell string command": '[verification]\ncommands = ["pytest -q"]\n',
            "placeholder in verification": '[verification]\ncommands = [["t", "{dest}"]]\n',
            "unknown placeholder": '[main]\ninstall = [["cp", "{build}", "{destdir}"]]\n',
            "keep below two": "[main]\nkeep = 1\n",
            "absolute executable": '[main]\nexecutable = "/bin/relay"\n',
            "escaping executable": '[main]\nexecutable = "../relay"\n',
            "escaping exclude": '[workspace]\nexclude = ["../secret"]\n',
            "bad target": '[project]\ntarget = "main..evil"\n',
            "bad timeout": "[verification]\ntimeout_seconds = 0\n",
            "bad cpus": "[resources]\ncpus = -1\n",
            "bool as int": "[resources]\ncpus = true\n",
            "GIT_ env rejected": '[verification]\ncommands = [["true"]]\n'
                                 '[verification.environment]\nGIT_DIR = "/tmp/x"\n',
            "reconcile attempts capped": "[reconcile]\nmax_attempts = 99\n",
        }
        for name, text in bad.items():
            with self.assertRaises(PC.ProjectConfigError, msg=name):
                self.load_text(text), name

    def test_policy_hash_stable_and_sensitive(self):
        a = self.load_text(CONTRACT_EXAMPLE)
        b = self.load_text(CONTRACT_EXAMPLE.replace('target = "main"', 'target = "main"'))
        self.assertEqual(PC.policy_hash(a), PC.policy_hash(b))
        c = self.load_text(CONTRACT_EXAMPLE.replace("timeout_seconds = 900",
                                                    "timeout_seconds = 901"))
        self.assertNotEqual(PC.policy_hash(a), PC.policy_hash(c))
        self.assertEqual(PC.policy_hash(a), PC.policy_hash(PC.normalize_config(a)))


class DetectTests(unittest.TestCase):
    def test_detect_suggests_not_trusts(self):
        with tempfile.TemporaryDirectory() as td:
            repo = Path(td)
            write(repo / "tests" / "test_x.py", "def test_x(): pass\n")
            write(repo / "CMakeLists.txt", "cmake_minimum_required(VERSION 3.20)\n")
            out = PC.detect(repo)
            cmds = out["suggestion"]["verification"]["commands"]
            self.assertIn(["python3", "-m", "pytest", "-q"], cmds)
            self.assertTrue(any("ctest" in c for c in cmds))
            self.assertIn("{dest}", out["suggestion"]["main"]["install"][0][-1]
                          if out["suggestion"]["main"]["install"] else "")
            self.assertTrue(any("not trust" in n for n in out["notes"]))

    def test_detect_empty(self):
        with tempfile.TemporaryDirectory() as td:
            out = PC.detect(td)
            self.assertEqual(out["suggestion"], {})


class GateTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.tree = Path(self.tmp.name)

    def tearDown(self):
        self.tmp.cleanup()

    def config(self, commands, **kw):
        raw = {"verification": {"commands": commands,
                                "timeout_seconds": kw.pop("timeout_seconds", 30),
                                "ungated": kw.pop("ungated", False)}}
        if kw:
            raw["verification"].update(kw)
        return PC.normalize_config(raw)

    def test_no_commands_fail_closed_unless_ungated(self):
        res = PC.run_gate(self.config([]), self.tree)
        self.assertFalse(res["ok"])
        self.assertFalse(res["verified"])
        self.assertIn("no verification commands", res["reason"])
        res = PC.run_gate(self.config([], ungated=True), self.tree)
        self.assertTrue(res["ok"])
        self.assertFalse(res["verified"])  # approved, but nothing was verified

    def test_a_live_log_shows_output_while_the_command_runs(self):
        # A running gate used to be silent until it ended (#VK6J): its output now streams to
        # live_log, so progress can be read mid-run.
        live = self.tree / "logs" / "live.log"
        script = ("import time, pathlib, sys\n"
                  "print('== gate phase one', flush=True)\n"
                  "p = pathlib.Path(sys.argv[1])\n"
                  "for _ in range(100):\n"
                  "    if p.is_file() and b'phase one' in p.read_bytes(): break\n"
                  "    time.sleep(0.05)\n"
                  "else: sys.exit(3)\n"
                  "print('== gate phase two', flush=True)\n")
        res = PC.run_gate(self.config([[sys.executable, "-c", script, str(live)]], ungated=False),
                          self.tree, live_log=str(live))
        # exit 3 would mean the command never saw its own first line in the live log
        self.assertNotIn("exited 3", str(res.get("reason")))
        self.assertIn("phase two", live.read_text())

    def test_real_pytest_pass(self):
        write(self.tree / "tests" / "test_ok.py", "def test_ok():\n    assert True\n")
        res = PC.run_gate(self.config([[sys.executable, "-m", "pytest", "-q"]]), self.tree)
        self.assertTrue(res["ok"], res["log"][-2000:])
        self.assertTrue(res["verified"])
        self.assertEqual(res["policy_hash"], PC.policy_hash(self.config(
            [[sys.executable, "-m", "pytest", "-q"]])))
        self.assertEqual(res["reason"], None)

    def test_real_pytest_zero_tests_fails(self):
        (self.tree / "tests").mkdir()  # nothing to collect
        res = PC.run_gate(self.config([[sys.executable, "-m", "pytest", "-q"]]), self.tree)
        self.assertFalse(res["ok"])
        self.assertFalse(res["verified"])

    def test_fake_pytest_zero_tests_exit0_fails(self):
        fake_exe(self.tree / "bin", "pytest", "echo 'collected 0 items'; exit 0\n")
        env = {"PATH": str(self.tree / "bin") + os.pathsep + os.environ["PATH"]}
        res = PC.run_gate(self.config([["pytest"]]), self.tree, env=env)
        self.assertFalse(res["ok"])
        self.assertIn(PC.GATE_ZERO_REASON, res["reason"])

    def test_fake_ctest_zero_tests_exit0_fails(self):
        # the real ctest prints this and exits 0 when there is nothing to run
        fake_exe(self.tree / "bin", "ctest",
                 "echo 'No tests were found!!!'; echo '0% tests passed, 0 tests failed out of 0';"
                 " exit 0\n")
        env = {"PATH": str(self.tree / "bin") + os.pathsep + os.environ["PATH"]}
        res = PC.run_gate(self.config([["ctest", "--test-dir", "build"]]), self.tree, env=env)
        self.assertFalse(res["ok"])
        self.assertIn(PC.GATE_ZERO_REASON, res["reason"])

    def test_failing_command(self):
        res = PC.run_gate(self.config([["sh", "-c", "echo boom >&2; exit 3"]]), self.tree)
        self.assertFalse(res["ok"])
        self.assertIn("exited 3", res["reason"])
        self.assertIn("boom", res["log"])

    def test_timeout_kills_process_group(self):
        marker = self.tree / "survived"
        cmd = ["sh", "-c", f"sleep 30; touch {marker}"]
        started = time.monotonic()
        res = PC.run_gate(self.config([cmd], timeout_seconds=1), self.tree)
        self.assertFalse(res["ok"])
        self.assertIn("timed out", res["reason"])
        self.assertLess(time.monotonic() - started, 10)
        self.assertFalse(marker.exists())

    def test_selected_tests_add_focused_pass_without_removing_required(self):
        write(self.tree / "tests" / "test_a.py", "def test_a():\n    assert True\n")
        write(self.tree / "tests" / "test_b.py", "def test_b():\n    assert True\n")
        cfg = self.config([[sys.executable, "-m", "pytest", "-q"]])
        res = PC.run_gate(cfg, self.tree, selected_tests=["tests/test_b.py"])
        self.assertTrue(res["ok"], res["log"][-2000:])
        self.assertEqual(len(res["commands"]), 2)  # full gate + focused pass
        self.assertIn("focused pass", res["log"])
        # a selection matching nothing fails instead of approving
        res = PC.run_gate(cfg, self.tree, selected_tests=["tests/test_nope.py"])
        self.assertFalse(res["ok"])

    def test_git_env_scrubbed(self):
        subprocess.run(["git", "init", "-q", "-b", "main"], cwd=self.tree, check=True)
        os.environ["GIT_DIR"] = "/nonexistent-bogus-git-dir"
        try:
            res = PC.run_gate(self.config([["git", "status", "--short"]]), self.tree)
        finally:
            del os.environ["GIT_DIR"]
        self.assertTrue(res["ok"], res["log"][-1000:])

    def test_noisy_command_log_is_bounded(self):
        cmd = [sys.executable, "-c", "print('x' * 3_000_000)"]
        res = PC.run_gate(self.config([cmd]), self.tree)
        self.assertTrue(res["ok"])
        self.assertLess(len(res["log"]), PC.MAX_LOG_BYTES + PC.CMD_LOG_BYTES)
        self.assertIn("elided", res["log"])


if __name__ == "__main__":
    unittest.main()
