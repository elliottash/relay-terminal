"""Real worker protocol: diagnose configure defects and recover in a fresh process (#40SN)."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
BOOT = r'''
import builtins, os
from pathlib import Path
import worker
original = worker.KeybindingCatalog.from_request
marker = Path(os.environ["FAULT_MARKER"])
broken_import = not marker.exists()
def catalogue(request):
    if broken_import:
        marker.touch()
        raise getattr(builtins, os.environ["FAULT_KIND"])("configuration fixture defect")
    return original(request)
worker.KeybindingCatalog.from_request = catalogue
worker.main()
'''


class ConfigureRecoveryTests(unittest.TestCase):
    def run_worker(self, home, kind, requests):
        env = {**os.environ, "HOME": home, "XDG_CONFIG_HOME": home + "/config",
               "XDG_DATA_HOME": home + "/data", "RELAY_KEYRING": "off",
               "PYTHONPATH": str(ROOT / "backend"), "FAULT_KIND": kind,
               "FAULT_MARKER": home + "/repaired"}
        proc = subprocess.run([sys.executable, "-S", "-c", BOOT], env=env, cwd=home,
                              input="".join(json.dumps(r) + "\n" for r in requests),
                              capture_output=True, text=True, timeout=20)
        self.assertEqual(proc.returncode, 0, proc.stderr)
        return [json.loads(line) for line in proc.stdout.splitlines()]

    def configure(self, home):
        return {"type": "configure", "id": "startup", "workspace": home,
                "base_url": "http://127.0.0.1:1/v1", "model": "fixture",
                "api_key": "", "board": {"attach": False}}

    def test_configure_defects_report_diagnostics_and_restart_hint(self):
        for kind in ("NameError", "AttributeError", "TypeError", "ImportError"):
            with self.subTest(kind=kind), tempfile.TemporaryDirectory() as home:
                events = self.run_worker(home, kind, [self.configure(home), {"type": "shutdown"}])
                error, = [e for e in events if e["event"] == "error"]
                self.assertEqual(error["code"], "configure_failed")
                self.assertEqual(error["exception"], kind)
                self.assertEqual(error["text"], "configuration fixture defect")
                self.assertEqual(error["id"], "startup")
                self.assertTrue(error["restart_worker"])

    def test_cached_failure_needs_a_fresh_process_then_configures(self):
        with tempfile.TemporaryDirectory() as home:
            request = self.configure(home)
            events = self.run_worker(home, "NameError", [request, request, {"type": "shutdown"}])
            self.assertEqual(len([e for e in events if e["event"] == "error"]), 2)
            self.assertFalse(any(e["event"] == "configured" for e in events))
            events = self.run_worker(home, "NameError", [request, {"type": "shutdown"}])
            configured, = [e for e in events if e["event"] == "configured"]
            self.assertEqual(configured["model"], "fixture")
            self.assertFalse(any(e["event"] == "error" for e in events))

    def test_validation_failures_do_not_request_restart(self):
        with tempfile.TemporaryDirectory() as home:
            events = self.run_worker(home, "ValueError", [self.configure(home), {"type": "shutdown"}])
            error, = [e for e in events if e["event"] == "error"]
            self.assertEqual(error["text"], "configuration fixture defect")
            self.assertNotIn("restart_worker", error)

    def test_unclassified_exceptions_remain_redacted(self):
        with tempfile.TemporaryDirectory() as home:
            events = self.run_worker(home, "RuntimeError", [self.configure(home), {"type": "shutdown"}])
            error, = [e for e in events if e["event"] == "error"]
            self.assertEqual(error["text"], "Protocol error (RuntimeError).")
            self.assertNotIn("restart_worker", error)


if __name__ == "__main__":
    unittest.main()
