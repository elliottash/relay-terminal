"""HG26: equivalent native/guest results, content-free logs and inherited test isolation."""
import json
import logging
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import threading
from types import SimpleNamespace
import unittest
from unittest.mock import patch

from relay_core import logs
from relay_core.agent import Agent
from relay_core.guest_harness_provider import _Turn
from relay_core.guest_harness_codex import _tool_result
from relay_core.tool_outcomes import classify


class OutcomeTests(unittest.TestCase):
    def test_bounded_categories_and_no_error_prose_inference(self):
        cases = [({}, "success"), ({"timed_out": True}, "timed_out"),
                 ({"still_running": True}, "pending"), ({"refused": True}, "refused"), ({"exit_code": 1}, "command_nonzero"),
                 ({"error_code": "connection_error"}, "transport_error"),
                 ({"error_code": "internal_error"}, "internal_error"),
                 ({"error": "secret output timeout internal_error"}, "unknown"),
                 ({"ok": False}, "unknown"), (None, "unknown")]
        for result, expected in cases:
            with self.subTest(result=result):
                self.assertEqual(classify("run_command", result)[0], expected)
        self.assertEqual(classify("agent_wait", {"timed_out": True})[0], "pending")
        self.assertEqual(classify("agent_wait", {"timed_out": True, "error": "oops"})[0], "timed_out")
        self.assertEqual(classify("run_command", {"error": "x", "error_code": "secret"}),
                         ("unknown", "unclassified"))

    def agent(self):
        agent = Agent.__new__(Agent)
        agent.session_id = "s1"
        agent.provider = SimpleNamespace()
        return agent

    def test_guest_command_and_wait_match_native_records(self):
        agent = self.agent()
        for name, native, guest in [
            ("run_command", {"exit_code": 7}, _tool_result("commandExecution", {
                "status": "completed", "exitCode": 7, "aggregatedOutput": "private"}, {})),
            ("agent_wait", {"agents": [], "timed_out": True}, {
                "ok": True, "output": json.dumps({"content": [{"type": "text", "text":
                    json.dumps({"agents": [], "timed_out": True})}]})}),
            ("run_command", {"timed_out": True}, {"ok": False, "timed_out": True}),
        ]:
            record = {"turn_id": "t1", "tools": {}}
            agent._record_tool(record, "native", name, "private", native, 12)
            turn = _Turn(SimpleNamespace(context_generation=0, guest_id="codex"), agent,
                         record, lambda e: None, threading.Event())
            turn.calls["guest"] = {"name": name, "guest_tool": name, "args": {},
                                    "preview": "private", "started": None}
            turn._on_tool_result({"call_id": "guest", **guest})
            for field in ("outcome", "error_code", "ok"):
                self.assertEqual(record["tools"]["native"][field], record["tools"]["guest"][field])

    def test_log_contains_identity_and_category_without_result_content(self):
        with tempfile.TemporaryDirectory() as tmp, patch.dict(os.environ, {
                "XDG_DATA_HOME": tmp, "RELAY_LOG_ORIGIN": "qa", "RELAY_LOG_RUN_ID": "run-1",
                "RELAY_BUILD_ID": "build-1"}):
            logger = logs.configure(level="info")
            self.addCleanup(lambda: [h.close() for h in logger.handlers])
            self.agent()._record_tool({"turn_id": "t1", "tools": {}}, "c1", "run_command",
                "private-preview", {"error": "private-output", "error_code": "private-code"}, 13)
            text = (Path(tmp) / "relay/logs/worker.log").read_text()
            for value in ("origin=qa", "run_id=run-1", "build_id=build-1", "outcome=unknown",
                          "error_code=unclassified", "ms=13"):
                self.assertIn(value, text)
            self.assertNotIn("private-", text)
            for h in list(logger.handlers):
                logger.removeHandler(h)
                h.close()
            logger.addHandler(logging.NullHandler())

    def test_unittest_worker_inheritance_cannot_write_to_live_logs(self):
        with tempfile.TemporaryDirectory() as live:
            env = dict(os.environ, XDG_DATA_HOME=live, RELAY_PANE_ID="live-pane")
            env.pop("RELAY_LOG_ORIGIN", None)
            script = """
import unittest, os, subprocess, sys
before = dict(os.environ)
from relay_core import logs
from relay_core.test_logging import runner_environment
assert dict(os.environ) == before
with runner_environment():
    assert os.environ['XDG_DATA_HOME'] != sys.argv[1]
    subprocess.run([sys.executable, '-S', '-c', 'from relay_core import logs; logs.configure(); logs.event(logs.get(), "negative_test")'], check=True)
    print(logs.log_dir().joinpath('worker.log').read_text())
assert dict(os.environ) == before
"""
            result = subprocess.run([sys.executable, "-c", script, live], env=env,
                                    text=True, capture_output=True, timeout=20)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn("origin=test", result.stdout)
            self.assertIn("pane=live-pane", result.stdout)
            self.assertFalse((Path(live) / "relay/logs").exists())

    def test_explicit_test_directory_is_retained(self):
        from relay_core.test_logging import runner_environment
        with tempfile.TemporaryDirectory() as directory, patch.dict(os.environ, {
                "XDG_DATA_HOME": directory, "RELAY_LOG_ORIGIN": "test"}):
            with runner_environment():
                self.assertEqual(os.environ["XDG_DATA_HOME"], directory)
            self.assertTrue(Path(directory).exists())

    def test_native_exception_producers(self):
        from test_agent import FakeProvider, CONFIG, tool
        for exception, outcome in ((ConnectionResetError("private"), "transport_error"),
                                   (TimeoutError("private"), "timed_out"),
                                   (FileNotFoundError("private"), "unknown")):
            with tempfile.TemporaryDirectory() as directory:
                agent = Agent(CONFIG, directory, lambda e: None,
                              provider=FakeProvider(tool("run_command", {"command": "true"})))
                with patch.object(agent, "_execute", side_effect=exception):
                    agent.ask("exercise typed failure")
                record = list(agent.turn_log.values())[-1]
                self.assertEqual(record["tools"]["call-1"]["outcome"], outcome)

    def test_guest_dispatch_exception_producers(self):
        from test_guest_board_bridge import BridgeTests
        fixture = BridgeTests()
        fixture.setUp()
        self.addCleanup(fixture.doCleanups)
        fixture.active()
        for index, (exception, outcome) in enumerate((
                (AttributeError("private"), "internal_error"),
                (ConnectionResetError("private"), "transport_error"),
                (FileNotFoundError("private"), "unknown"))):
            with patch.object(fixture.agent, "_execute", side_effect=exception):
                result = fixture.call(key="typed-" + str(index))
            self.assertEqual(classify("board_read", result)[0], outcome)

    def test_direct_role_unittest_preserves_inherited_logs(self):
        with tempfile.TemporaryDirectory() as directory:
            live = Path(directory) / "relay/logs"
            live.mkdir(parents=True)
            sentinel = live / "worker.log"
            sentinel.write_text("interactive sentinel\n")
            env = dict(os.environ, XDG_DATA_HOME=directory, RELAY_PANE_ID="live-pane",
                       RELAY_LOG_ORIGIN="interactive", RELAY_LOG_LEVEL="info")
            result = subprocess.run([sys.executable, "-m", "unittest", "tests.test_roles", "-q"],
                                    env=env, capture_output=True, text=True, timeout=30)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(sentinel.read_text(), "interactive sentinel\n")
            self.assertEqual(sorted(p.name for p in live.iterdir()), ["worker.log"])

    def test_role_worker_honors_explicit_xdg_fixture(self):
        from test_roles import run_worker
        with tempfile.TemporaryDirectory() as directory:
            _, process = run_worker([{"type": "shutdown"}], {
                "XDG_DATA_HOME": directory, "RELAY_LOG_LEVEL": "info"})
            self.assertEqual(process.returncode, 0, process.stderr)
            body = (Path(directory) / "relay/logs/worker.log").read_text()
            self.assertIn("worker_start", body)
            self.assertIn("origin=test", body)
