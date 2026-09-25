import json
import os
import subprocess
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "backend"))
import worker  # noqa: E402


class OomPriorityTests(unittest.TestCase):
    def test_worker_raises_its_own_oom_score(self):
        code = ("import sys; sys.path.insert(0, %r); import worker; ok = worker.prefer_as_oom_victim(); "
                "print(ok, open('/proc/self/oom_score_adj').read().strip())") % str(ROOT / "backend")
        out = subprocess.run([sys.executable, "-S", "-c", code], capture_output=True, text=True, timeout=10).stdout.split()
        self.assertEqual(out[0], "True")
        self.assertGreaterEqual(int(out[1]), worker.OOM_SCORE_ADJ)

    def test_never_lowers_an_existing_higher_score(self):
        code = ("import sys; sys.path.insert(0, %r); import worker; open('/proc/self/oom_score_adj','w').write('800'); "
                "worker.prefer_as_oom_victim(); print(open('/proc/self/oom_score_adj').read().strip())") % str(ROOT / "backend")
        out = subprocess.run([sys.executable, "-S", "-c", code], capture_output=True, text=True, timeout=10).stdout.strip()
        self.assertEqual(out, "800")

    def test_worker_process_starts_with_raised_score(self):
        proc = subprocess.Popen([sys.executable, "-S", "-u", str(ROOT / "backend/worker.py")], stdin=subprocess.PIPE,
                                stdout=subprocess.PIPE, text=True, cwd=ROOT)
        try:
            self.assertEqual(json.loads(proc.stdout.readline())["event"], "ready")
            with open(f"/proc/{proc.pid}/oom_score_adj") as handle:
                self.assertGreaterEqual(int(handle.read()), worker.OOM_SCORE_ADJ)
        finally:
            proc.stdin.write(json.dumps({"type": "shutdown"}) + "\n"); proc.stdin.flush()
            proc.wait(timeout=10)


class ShellIntegrationTests(unittest.TestCase):
    def test_integration_raises_shell_oom_score(self):
        text = (ROOT / "shell/integration.bash").read_text()
        self.assertIn("/proc/$$/oom_score_adj", text)


class ChildOomPriorityTests(unittest.TestCase):
    """run_command children run at oom_score_adj 1000 (#ZPWT), raise-only like the worker: the
    kernel's memory kill lands on the command, not on the worker that owns the conversation."""

    def test_child_raises_to_1000(self):
        code = ("import sys; sys.path.insert(0, %r); from relay_core import jobs; jobs.raise_oom_score_adj(1000); "
                "print(open('/proc/self/oom_score_adj').read().strip())") % str(ROOT / "backend")
        out = subprocess.run([sys.executable, "-S", "-c", code], capture_output=True, text=True,
                             timeout=10).stdout.strip()
        self.assertEqual(out, "1000")

    def test_child_never_lowers_an_existing_higher_score(self):
        code = ("import sys; sys.path.insert(0, %r); from relay_core import jobs; "
                "open('/proc/self/oom_score_adj','w').write('1000'); jobs.raise_oom_score_adj(200); "
                "print(open('/proc/self/oom_score_adj').read().strip())") % str(ROOT / "backend")
        out = subprocess.run([sys.executable, "-S", "-c", code], capture_output=True, text=True,
                             timeout=10).stdout.strip()
        self.assertEqual(out, "1000")


class ScopedArgvTests(unittest.TestCase):
    """memory_max runs one command in a scope of its own under app-relay.slice (#WBDX)."""

    def setUp(self):
        sys.path.insert(0, str(ROOT / "backend"))
        from relay_core import jobs
        self.jobs = jobs

    def test_wraps_with_a_slice_and_a_bound(self):
        from unittest import mock
        with mock.patch.object(self.jobs.shutil, "which", return_value="/usr/bin/systemd-run"):
            argv = self.jobs.scoped_argv(["bash", "-c", "true"], "64G")
        self.assertEqual(argv, ["/usr/bin/systemd-run", "--user", "--scope", "--quiet", "--collect",
                                "--slice=app-relay.slice", "-p", "MemoryMax=64G",
                                "-p", "MemorySwapMax=0", "-p", "OOMPolicy=continue",
                                "--", "bash", "-c", "true"])

    def test_rejects_sizes_that_are_not_sizes(self):
        for bad in ("banana", "-1G", "8GB", "1 GiB", ""):
            with self.assertRaises(ValueError):
                self.jobs.scoped_argv(["true"], bad)

    def test_accepts_the_documented_shapes(self):
        from unittest import mock
        with mock.patch.object(self.jobs.shutil, "which", return_value="/usr/bin/systemd-run"):
            for good in ("8G", "512M", "4T", "1024K", "infinity"):
                self.assertTrue(self.jobs.is_memory_size(good))
        self.assertFalse(self.jobs.is_memory_size("8GB"))


if __name__ == "__main__":
    unittest.main()
