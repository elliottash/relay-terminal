"""The private worker's NDJSON is UTF-8 regardless of the Windows ANSI code page.

Run directly with an installed embedded Python too:
  runtime/python/python.exe -S tests/test_worker_encoding.py --worker share/relay/backend/worker.py
"""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

WORKER = Path(__file__).resolve().parents[1] / "backend" / "worker.py"
if "--worker" in sys.argv:
    index = sys.argv.index("--worker")
    WORKER = Path(sys.argv[index + 1]).resolve()
    del sys.argv[index:index + 2]


class WorkerEncodingTests(unittest.TestCase):
    def test_redirected_legacy_encoding_still_emits_utf8(self):
        # Force the pre-entry stream into a real legacy codec, even on UTF-8 Linux
        # or Windows hosts. The embedded interpreter ignores Python environment
        # overrides, so the test does not rely on those to simulate the failure.
        bootstrap = (
            "import runpy,sys; "
            "sys.stdout.reconfigure(encoding='cp1252'); "
            "sys.stderr.reconfigure(encoding='cp1252'); "
            "sys.path.insert(0,sys.argv[1]); "
            "runpy.run_path(sys.argv[2],run_name='__main__')"
        )
        request = {"type": "route", "id": "日本語-λ-🦊", "text": "Explain this workspace", "mode": "agent"}
        with tempfile.TemporaryDirectory() as directory:
            env = dict(os.environ, XDG_DATA_HOME=directory, XDG_CONFIG_HOME=directory,
                       HOME=directory, USERPROFILE=directory, RELAY_KEYRING="off", RELAY_INDEX="off")
            process = subprocess.run([sys.executable, "-S", "-u", "-c", bootstrap,
                                      str(WORKER.parent), str(WORKER)],
                                     input=(json.dumps(request) + "\n").encode("utf-8"),
                                     capture_output=True, env=env, timeout=30)
            self.assertEqual(process.returncode, 0, process.stderr.decode("utf-8", "replace"))
            events = [json.loads(line) for line in process.stdout.decode("utf-8").splitlines()]
            self.assertTrue(any(event.get("event") == "ready" for event in events))
            response = next(event for event in events if event.get("event") == "route")
            self.assertEqual(response["id"], request["id"])
            self.assertIn(request["id"].encode("utf-8"), process.stdout)


if __name__ == "__main__":
    unittest.main()
