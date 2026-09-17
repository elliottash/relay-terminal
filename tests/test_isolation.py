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


if __name__ == "__main__":
    unittest.main()
