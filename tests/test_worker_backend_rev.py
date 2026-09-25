"""The worker's ready message names the backend it runs from (card #FYEY).

A pinned worker is spawned with RELAY_BUILD_ID = the pin's content hash so
relay_core.logs.source_changed() stops firing for a tree frozen by design; the
ready message echoes it as `backend_rev` so the pane can show what runs and say
when the checkout has moved on. An unpinned worker reports "live".

Run directly with an installed embedded Python too:
  runtime/python/python.exe -S tests/test_worker_backend_rev.py --worker share/relay/backend/worker.py
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


def ready_event(extra_env):
    # An empty stdin closes at once: the worker greets, then exits.
    with tempfile.TemporaryDirectory() as directory:
        env = dict(os.environ, XDG_DATA_HOME=directory, XDG_CONFIG_HOME=directory,
                   HOME=directory, USERPROFILE=directory, RELAY_KEYRING="off",
                   RELAY_INDEX="off")
        env.pop("RELAY_BUILD_ID", None)   # a host running this from a pin must not leak in
        env.update(extra_env)
        process = subprocess.run([sys.executable, "-S", "-u", str(WORKER)],
                                 input=b"", capture_output=True, env=env, timeout=30)
        assert process.returncode == 0, process.stderr.decode("utf-8", "replace")
        lines = process.stdout.decode("utf-8").splitlines()
        assert lines, "no ready message"
        return json.loads(lines[0])


class WorkerBackendRevTests(unittest.TestCase):
    def test_ready_carries_the_build_id_when_pinned(self):
        ready = ready_event({"RELAY_BUILD_ID": "0123456789ab"})
        self.assertEqual(ready.get("event"), "ready")
        self.assertEqual(ready.get("backend_rev"), "0123456789ab")

    def test_ready_reports_live_without_a_build_id(self):
        ready = ready_event({})
        self.assertEqual(ready.get("event"), "ready")
        self.assertEqual(ready.get("backend_rev"), "live")


if __name__ == "__main__":
    unittest.main()
