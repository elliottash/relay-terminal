# SPDX-License-Identifier: GPL-3.0-or-later
"""run_command jobs: a command outlives its timeout, is read and stopped by id, and ends with its
conversation (relay_core/jobs.py)."""
import os
import tempfile
import threading
import time
import unittest
from pathlib import Path

from relay_core.jobs import JobTable, MAX_RUNNING
from relay_core.provider import Cancelled
from relay_core.subagents import RestrictedExecutor
from relay_core.tools import MAX_OUTPUT, MAX_WAIT, ToolExecutor, clamp_seconds


def alive(pid: int) -> bool:
    try:
        os.kill(pid, 0)
    except ProcessLookupError:
        return False
    return True


class JobToolTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name)
        self.cancel = threading.Event()
        self.events = []
        self.tools = ToolExecutor(self.tmp.name, self.events.append, self.cancel)

    def tearDown(self):
        self.tools.shutdown()
        self.tmp.cleanup()

    def run_tool(self, name, **args):
        return self.tools.execute(self.tools.prepare(name, args))

    def test_the_tools_are_offered(self):
        names = {tool["function"]["name"] for tool in self.tools.tools()}
        self.assertLessEqual({"run_command", "command_output", "stop_command"}, names)
        spec = next(t for t in self.tools.tools() if t["function"]["name"] == "run_command")
        self.assertEqual(spec["function"]["parameters"]["properties"]["timeout_seconds"]["maximum"], MAX_WAIT)

    def test_a_quick_command_finishes_in_the_call(self):
        result = self.run_tool("run_command", command="echo hi; exit 3")
        self.assertEqual(result["exit_code"], 3)
        self.assertEqual(result["output"], "hi\n")
        self.assertNotIn("still_running", result)

    def test_a_slow_command_continues_as_a_job_and_is_read_later(self):
        result = self.run_tool("run_command", command="echo start; sleep 1.5; echo end; exit 4", timeout_seconds=1)
        self.assertTrue(result["still_running"])
        self.assertEqual(result["output"], "start\n")
        self.assertIn(result["job_id"], result["note"])
        later = self.run_tool("command_output", job_id=result["job_id"], wait_seconds=10)
        self.assertEqual(later["exit_code"], 4)
        self.assertEqual(later["output"], "end\n")        # only what was not read before
        again = self.run_tool("command_output", job_id=result["job_id"])
        self.assertEqual(again["output"], "")

    def test_a_wait_returns_early_when_the_job_ends(self):
        result = self.run_tool("run_command", command="sleep 2", timeout_seconds=1)
        self.assertTrue(result["still_running"])
        started = time.monotonic()
        done = self.run_tool("command_output", job_id=result["job_id"], wait_seconds=60)
        self.assertEqual(done["exit_code"], 0)
        self.assertLess(time.monotonic() - started, 4)

    def test_background_returns_at_once_and_stop_ends_the_group(self):
        started = time.monotonic()
        result = self.run_tool("run_command", command="sleep 60 & echo $! > child.pid; echo up; wait", background=True)
        self.assertLess(time.monotonic() - started, 5)
        self.assertTrue(result["still_running"])
        self.assertEqual(result["output"], "up\n")
        child = int((self.root / "child.pid").read_text())
        self.assertTrue(alive(child))
        stopped = self.run_tool("stop_command", job_id=result["job_id"])
        self.assertTrue(stopped["stopped"])
        self.assertIn("exit_code", stopped)
        deadline = time.monotonic() + 3
        while alive(child) and time.monotonic() < deadline:
            time.sleep(0.05)
        self.assertFalse(alive(child))

    def test_a_finished_shell_takes_its_leftovers_with_it(self):
        result = self.run_tool("run_command", command="sleep 60 & echo $! > child.pid; echo done")
        self.assertEqual(result["exit_code"], 0)
        child = int((self.root / "child.pid").read_text())
        deadline = time.monotonic() + 3
        while alive(child) and time.monotonic() < deadline:
            time.sleep(0.05)
        self.assertFalse(alive(child))

    def test_stop_during_the_wait_ends_that_command(self):
        timer = threading.Timer(0.5, lambda: (self.cancel.set(), self.tools.stop_process()))
        timer.start()
        started = time.monotonic()
        with self.assertRaises(Cancelled):
            self.run_tool("run_command", command="sleep 30", timeout_seconds=60)
        self.assertLess(time.monotonic() - started, 5)
        self.assertEqual(self.tools.jobs.running(), [])

    def test_stop_leaves_jobs_handed_back_earlier(self):
        server = self.run_tool("run_command", command="sleep 30", background=True)
        self.tools.stop_process()   # nothing is being waited on
        self.assertEqual([job.id for job in self.tools.jobs.running()], [server["job_id"]])

    def test_shutdown_stops_every_job(self):
        for _ in range(2):
            self.run_tool("run_command", command="sleep 30", background=True)
        self.tools.shutdown()
        self.assertEqual(self.tools.jobs.running(), [])

    def test_timeouts_are_clamped_not_refused(self):
        self.assertEqual(self.tools.prepare("run_command", {"command": "true", "timeout_seconds": 5000}).arguments["timeout_seconds"], MAX_WAIT)
        self.assertEqual(self.tools.prepare("run_command", {"command": "true", "timeout_seconds": 0}).arguments["timeout_seconds"], 1)
        self.assertEqual(self.tools.prepare("run_command", {"command": "true", "timeout_seconds": "180"}).arguments["timeout_seconds"], 180)
        self.assertEqual(self.tools.prepare("run_command", {"command": "true", "timeout_seconds": 90.4}).arguments["timeout_seconds"], 90)
        self.assertEqual(self.tools.prepare("run_command", {"command": "true", "timeout_seconds": "soon"}).arguments["timeout_seconds"], 30)
        self.assertEqual(self.tools.prepare("run_command", {"command": "true"}).arguments["timeout_seconds"], 30)
        with self.assertRaises(ValueError):
            self.tools.prepare("run_command", {"command": "true", "background": "yes"})
        self.assertEqual(clamp_seconds(float("nan"), 7, 0, 10), 7)

    def test_unknown_jobs_and_arguments_are_refused(self):
        with self.assertRaises(ValueError):
            self.tools.prepare("command_output", {"job_id": "job-99"})
        with self.assertRaises(ValueError):
            self.tools.prepare("stop_command", {"job_id": "job-1", "force": True})

    def test_long_output_keeps_the_newest(self):
        result = self.run_tool("run_command", command="head -c 100000 /dev/zero | tr '\\0' x; echo; echo LAST")
        self.assertTrue(result["truncated"])
        self.assertEqual(len(result["output"]), MAX_OUTPUT)
        self.assertTrue(result["output"].endswith("LAST\n"))
        self.assertEqual(result["omitted_bytes"], 100000 + 6 - MAX_OUTPUT)

    def test_the_live_stream_runs_only_while_a_call_waits(self):
        result = self.run_tool("run_command", command="echo a; sleep 1.2; echo b", timeout_seconds=1)
        streamed = "".join(e["text"] for e in self.events if e.get("event") == "tool_output")
        self.assertEqual(streamed, "a\n")
        time.sleep(0.6)   # "b" is printed with no call waiting: it is kept, not streamed
        self.assertEqual("".join(e["text"] for e in self.events if e.get("event") == "tool_output"), "a\n")
        self.assertEqual(self.run_tool("command_output", job_id=result["job_id"], wait_seconds=5)["output"], "b\n")


class JobListTests(unittest.TestCase):
    """The jobs the pane lists: only handed-back ones, announced as they change (src/JobsPanel.h)."""

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.cancel = threading.Event()
        self.events = []
        self.tools = ToolExecutor(self.tmp.name, self.events.append, self.cancel)

    def tearDown(self):
        self.tools.shutdown()
        self.tmp.cleanup()

    def lists(self):
        return [e["jobs"] for e in self.events if e.get("event") == "jobs"]

    def run_tool(self, name, **args):
        return self.tools.execute(self.tools.prepare(name, args))

    def test_a_quick_command_is_never_listed(self):
        self.run_tool("run_command", command="true")
        self.assertEqual(self.lists(), [])
        self.assertEqual(self.tools.jobs_event()["jobs"], [])

    def test_a_handed_back_job_is_listed_and_its_end_announced(self):
        result = self.run_tool("run_command", command="sleep 1; exit 2", timeout_seconds=0)
        first = self.lists()[-1]
        self.assertEqual([(j["job_id"], j["running"]) for j in first], [(result["job_id"], True)])
        self.assertIn("sleep 1", first[0]["command"])
        deadline = time.monotonic() + 5
        while self.lists()[-1][0]["running"] and time.monotonic() < deadline:
            time.sleep(0.05)
        last = self.lists()[-1][0]
        self.assertFalse(last["running"])
        self.assertEqual(last["exit_code"], 2)

    def test_the_users_peek_leaves_the_models_output_unread(self):
        result = self.run_tool("run_command", command="echo one; sleep 30", background=True)
        job = self.tools.jobs.get(result["job_id"])
        self.run_tool("command_output", job_id=job.id)          # the model has read "one"
        self.assertEqual(self.tools.jobs.peek(job)["output"], "one\n")
        self.assertEqual(self.run_tool("command_output", job_id=job.id)["output"], "")

    def test_a_new_conversation_empties_the_list(self):
        self.run_tool("run_command", command="sleep 30", background=True)
        self.tools.shutdown()
        self.assertEqual(self.lists()[-1], [])
        self.assertEqual(self.tools.jobs.running(), [])

    def test_finished_jobs_are_forgotten_beyond_the_limit(self):
        from relay_core.jobs import KEEP_FINISHED
        for _ in range(KEEP_FINISHED + 5):
            self.run_tool("run_command", command="true")
        self.assertLessEqual(len(self.tools.jobs._jobs), KEEP_FINISHED)
        with self.assertRaises(ValueError):
            self.tools.jobs.get("job-1")

    def test_a_subagents_jobs_are_not_announced(self):
        events = []
        with tempfile.TemporaryDirectory() as tmp:
            sub = RestrictedExecutor(tmp, events.append, threading.Event(), None, ["run_command"])
            sub.execute(sub.prepare("run_command", {"command": "sleep 30", "background": True}))
            sub.shutdown()
        self.assertEqual([e for e in events if e.get("event") == "jobs"], [])


class JobProtocolTests(unittest.TestCase):
    """jobs_list, job_output_get and job_stop (relay_core/observe_protocol.py)."""

    def setUp(self):
        from relay_core.observe_protocol import ObserveCommands
        self.tmp = tempfile.TemporaryDirectory()
        self.events = []
        self.executor = ToolExecutor(self.tmp.name, self.events.append, threading.Event())
        agent = type("Agent", (), {"executor": self.executor})()
        turns = type("Turns", (), {"agent": agent, "busy": False})()
        self.observe = ObserveCommands(turns, self.events.append)

    def tearDown(self):
        self.executor.shutdown()
        self.tmp.cleanup()

    def test_list_output_and_stop(self):
        result = self.executor.execute(self.executor.prepare("run_command", {"command": "echo hi; sleep 30", "background": True}))
        job_id = result["job_id"]
        self.observe.handle("jobs_list", {"type": "jobs_list", "id": 1})
        listed = [e for e in self.events if e.get("event") == "jobs" and e.get("id") == 1][0]
        self.assertEqual(listed["jobs"][0]["job_id"], job_id)
        self.observe.handle("job_output_get", {"type": "job_output_get", "id": 2, "job_id": job_id})
        output = [e for e in self.events if e.get("event") == "job_output"][0]
        self.assertEqual((output["output"], output["running"], output["id"]), ("hi\n", True, 2))
        self.observe.handle("job_stop", {"type": "job_stop", "id": 3, "job_id": job_id})
        deadline = time.monotonic() + 5
        while self.executor.jobs.running() and time.monotonic() < deadline:
            time.sleep(0.05)
        self.assertEqual(self.executor.jobs.running(), [])
        final = [e for e in self.events if e.get("event") == "jobs"][-1]["jobs"][0]
        self.assertTrue(final["stopped"])
        self.assertFalse(final["running"])

    def test_an_unknown_job_is_an_error(self):
        with self.assertRaises(ValueError):
            self.observe.handle("job_stop", {"type": "job_stop", "job_id": "job-9"})


class JobTableTests(unittest.TestCase):
    def test_at_most_max_running(self):
        table = JobTable()
        try:
            for _ in range(MAX_RUNNING):
                table.start("sleep 30", ".", dict(os.environ))
            with self.assertRaises(ValueError):
                table.start("sleep 30", ".", dict(os.environ))
        finally:
            table.stop_all()


class SubagentJobTests(unittest.TestCase):
    def test_job_tools_come_with_run_command(self):
        with tempfile.TemporaryDirectory() as tmp:
            shell = RestrictedExecutor(tmp, lambda e: None, threading.Event(), None, ["run_command"])
            reader = RestrictedExecutor(tmp, lambda e: None, threading.Event(), None, ["read_file"])
            self.assertIn("command_output", {t["function"]["name"] for t in shell.tools()})
            self.assertNotIn("stop_command", {t["function"]["name"] for t in reader.tools()})


if __name__ == "__main__":
    unittest.main()
