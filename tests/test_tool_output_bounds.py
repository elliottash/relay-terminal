# SPDX-License-Identifier: AGPL-3.0-or-later
"""Card #0C0V, steps 3 and 4: a tool result enters the model's context bounded (head + tail, exact
counts, a re-read handle) while the user's fold keeps today's output; command_output and read_file
re-read by line range; finished jobs are kept by bytes; stale tool results are cleared in one batch."""
import json
import re
import tempfile
import threading
import unittest
from pathlib import Path
from unittest import mock

from relay_core import context, jobs
from relay_core.agent import Agent, validate_turn_options
from relay_core.provider import ProviderConfig
from relay_core.tools import MAX_OUTPUT, MODEL_RESULT_CHARS, ToolExecutor, model_result

CONFIG = ProviderConfig("http://127.0.0.1:12345/v1", "mock", "")
MARKER = re.compile(r'\[… ([\d,]+) lines? / ([\d,]+) bytes omitted; (\w+)\((.*?)\) reads them\]')
# The model's copy is the bounded output plus a few short fields: well under this.
MODEL_JSON_CAP = MODEL_RESULT_CHARS + 1_000


def call(index, name, arguments):
    return {"role": "assistant", "content": "", "tool_calls": [
        {"id": f"call-{index}", "type": "function",
         "function": {"name": name, "arguments": json.dumps(arguments)}}]}


class Scripted:
    """Answers with each of `steps` in turn, then a final text."""
    def __init__(self, steps):
        self.steps, self.calls, self.seen = list(steps), 0, []

    def complete(self, messages, tools, emit, cancel):
        self.seen.append(json.loads(json.dumps(messages)))
        self.calls += 1
        if self.calls <= len(self.steps):
            return self.steps[self.calls - 1]
        emit({"event": "delta", "text": "Done."})
        return {"role": "assistant", "content": "Done."}

    def cancel(self):
        pass


class ExecutorBounds(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name)
        self.events = []
        self.tools = ToolExecutor(self.tmp.name, self.events.append, threading.Event())

    def tearDown(self):
        self.tools.shutdown()
        self.tmp.cleanup()

    def run_tool(self, name, **args):
        return self.tools.execute(self.tools.prepare(name, args))

    def test_big_output_is_bounded_for_the_model_with_counts_and_a_handle(self):
        result = self.run_tool("run_command", command="seq 1 20000")
        total = len("".join(f"{n}\n" for n in range(1, 20001)))
        # The user's copy is what it always was: the newest MAX_OUTPUT bytes.
        self.assertEqual(len(result["output"]), MAX_OUTPUT)
        self.assertTrue(result["output"].endswith("19999\n20000\n"))
        model = model_result("run_command", result)
        self.assertLessEqual(len(json.dumps(model, ensure_ascii=False)), MODEL_JSON_CAP)
        self.assertEqual((model["total_lines"], model["total_bytes"], model["exit_code"]), (20000, total, 0))
        self.assertNotIn("omitted_bytes", model)
        head, found, tail = MARKER.split(model["output"], maxsplit=1)[0], MARKER.search(model["output"]), \
            MARKER.split(model["output"])[-1]
        self.assertEqual(found.group(3), "command_output")
        args = dict(re.findall(r'(\w+)=("[^"]*"|\d+)', found.group(4)))
        first, last = int(args["from_line"]), int(args["to_line"])
        self.assertEqual(json.loads(args["job_id"]), result["job_id"])
        # Head and tail are the true start and end, cut at whole lines around the omitted range.
        self.assertTrue(head.startswith("1\n2\n"))
        self.assertEqual(head.rstrip("\n").split("\n")[-1], str(first - 1))
        self.assertEqual(tail.lstrip("\n").split("\n")[0], str(last + 1))
        self.assertTrue(tail.endswith("20000\n"))
        self.assertEqual(int(found.group(1).replace(",", "")), last - first + 1)
        omitted = len("".join(f"{n}\n" for n in range(first, last + 1)))
        self.assertEqual(int(found.group(2).replace(",", "")), omitted)
        # The handle is real: the finished foreground command's omitted lines read back exactly.
        back = self.run_tool("command_output", job_id=result["job_id"], from_line=first, to_line=last)
        self.assertEqual(back["from_line"], first)
        self.assertLessEqual(len(back["output"]), MODEL_RESULT_CHARS)
        self.assertEqual(back["output"], "".join(f"{n}\n" for n in range(first, back["to_line"] + 1)))
        self.assertEqual(back["next_from_line"], back["to_line"] + 1)
        self.assertEqual((back["exit_code"], back["total_lines"]), (0, 20000))
        self.assertEqual(model_result("command_output", back), back)
        # A re-read leaves the unread position alone: nothing new since the run.
        self.assertEqual(self.run_tool("command_output", job_id=result["job_id"])["output"], "")

    def test_one_enormous_line_is_cut_inside_it(self):
        result = self.run_tool("run_command", command="head -c 100000 /dev/zero | tr '\\0' x")
        model = model_result("run_command", result)
        self.assertLessEqual(len(json.dumps(model)), MODEL_JSON_CAP)
        self.assertEqual(model["total_lines"], 1)
        self.assertIn(f'command_output(job_id="{result["job_id"]}", from_line=1, to_line=1)', model["output"])
        back = self.run_tool("command_output", job_id=result["job_id"], from_line=1, to_line=1)
        self.assertIn("cut -c", back["cut"])
        self.assertLessEqual(len(back["output"]), MODEL_RESULT_CHARS)

    def test_small_output_is_the_same_for_model_and_user(self):
        result = self.run_tool("run_command", command="seq 1 5")
        self.assertIs(model_result("run_command", result), result)
        self.assertEqual(result["output"], "1\n2\n3\n4\n5\n")

    def test_range_reread_of_a_running_background_job(self):
        result = self.run_tool("run_command", command="seq 1 3; sleep 30", background=True)
        self.assertTrue(result["still_running"])
        back = self.run_tool("command_output", job_id=result["job_id"], from_line=2, to_line=3)
        self.assertEqual((back["output"], back["still_running"]), ("2\n3\n", True))
        self.assertNotIn("exit_code", back)

    def test_range_before_the_kept_buffer_says_so(self):
        with mock.patch.object(jobs, "KEEP_BYTES", 1000):
            result = self.run_tool("run_command", command="seq 1 2000")
            back = self.run_tool("command_output", job_id=result["job_id"], from_line=1, to_line=5000)
        self.assertIn("were dropped", back["note"])
        self.assertEqual(back["total_lines"], 2000)
        self.assertTrue(back["output"].endswith("2000\n"))

    def test_bad_ranges_are_refused(self):
        job = self.run_tool("run_command", command="true")["job_id"]
        for args in ({"from_line": 0}, {"from_line": 5, "to_line": 2}, {"to_line": "3"}):
            with self.subTest(args=args), self.assertRaises(ValueError):
                self.tools.prepare("command_output", {"job_id": job, **args})

    def test_stop_command_result_is_bounded_too(self):
        result = self.run_tool("run_command", command="seq 1 20000; sleep 30", timeout_seconds=2)
        stopped = self.run_tool("stop_command", job_id=result["job_id"])
        self.assertLessEqual(len(json.dumps(model_result("stop_command", stopped))), MODEL_JSON_CAP)

    def test_read_file_without_a_range_is_bounded_and_says_how_to_read_more(self):
        text = "".join(f"line {n}\n" for n in range(1, 4001))
        (self.root / "big.txt").write_text(text)
        result = self.run_tool("read_file", path="big.txt")
        self.assertEqual(result["content"], text)                  # the user's fold: all of it
        model = model_result("read_file", result)
        self.assertLessEqual(len(json.dumps(model)), MODEL_JSON_CAP)
        self.assertEqual((model["total_lines"], model["bytes"]), (4000, len(text)))
        found = MARKER.search(model["content"])
        self.assertEqual(found.group(3), "read_file")
        args = dict(re.findall(r'(\w+)=("[^"]*"|\d+)', found.group(4)))
        self.assertEqual(json.loads(args["path"]), "big.txt")
        first, last = int(args["from_line"]), int(args["to_line"])
        ranged = self.run_tool("read_file", path="big.txt", from_line=first, to_line=first + 9)
        self.assertEqual(ranged["content"], "".join(f"line {n}\n" for n in range(first, first + 10)))
        self.assertEqual((ranged["total_lines"], ranged["to_line"]), (4000, first + 9))
        self.assertIs(model_result("read_file", ranged), ranged)
        self.assertLess(first, last)
        small = self.run_tool("read_file", path="big.txt", to_line=2)
        self.assertEqual(small["content"], "line 1\nline 2\n")

    def test_search_matches_are_cut_for_the_model(self):
        result = {"matches": [f"src/a.py:{n}: " + "x" * 190 for n in range(80)], "count": 80,
                  "truncated": False, "files_scanned": 3}
        model = model_result("search_files", result)
        self.assertLess(len(model["matches"]), 80)
        self.assertLessEqual(len(json.dumps(model)), MODEL_JSON_CAP)
        self.assertTrue(model["truncated"])
        self.assertEqual(len(result["matches"]), 80)               # the user's copy is untouched


class RetentionByBytes(unittest.TestCase):
    def test_finished_jobs_are_kept_until_their_bytes_pass_the_limit(self):
        table = JobTableHarness()
        try:
            with mock.patch.object(jobs, "KEEP_FINISHED_BYTES", 300_000):
                first = table.run("head -c 200000 /dev/zero | tr '\\0' a")
                for _ in range(20):
                    table.run("true")                          # 20 small jobs: count is no limit
                self.assertIs(table.jobs.get(first.id), first)
                table.run("head -c 200000 /dev/zero | tr '\\0' b")
                table.run("true")                              # the next start evicts by bytes
            with self.assertRaises(ValueError):
                table.jobs.get(first.id)
        finally:
            table.jobs.stop_all(forget=True)


class JobTableHarness:
    def __init__(self):
        self.jobs = jobs.JobTable()
        self.tmp = tempfile.gettempdir()

    def run(self, command):
        from relay_core.tools import command_env
        job = self.jobs.start(command, self.tmp, command_env())
        job.done.wait(20)
        return job


def conversation(groups, size=15_000):
    messages = [{"role": "system", "content": "sys"}, {"role": "user", "content": "go", "relay_kind": "prompt"}]
    for n in range(groups):
        messages.append(call(n, "run_command", {"command": f"make step{n}"}))
        messages.append({"role": "tool", "tool_call_id": f"call-{n}",
                         "content": json.dumps({"output": "y" * size, "job_id": f"job-{n}", "exit_code": 0})})
    return messages


def pairing_valid(messages):
    ids = [c["id"] for m in messages if m.get("role") == "assistant" for c in m.get("tool_calls") or ()]
    answered = [m["tool_call_id"] for m in messages if m.get("role") == "tool"]
    return ids == answered


class ClearingUnit(unittest.TestCase):
    def test_under_the_threshold_nothing_changes(self):
        messages = conversation(5)          # 2 old groups x 15k = 30k < 40k
        out, count, chars = context.clear_stale_tool_results(messages)
        self.assertIs(out, messages)
        self.assertEqual((count, chars), (0, 0))

    def test_over_the_threshold_all_old_results_go_in_one_batch(self):
        messages = conversation(6)          # 3 old groups x 15k = 45k
        out, count, chars = context.clear_stale_tool_results(messages)
        self.assertEqual(count, 3)
        self.assertGreater(chars, 44_000)
        self.assertEqual(len(out), len(messages))
        self.assertTrue(pairing_valid(out))
        tools = [m for m in out if m["role"] == "tool"]
        for stub in tools[:3]:
            data = json.loads(stub["content"])
            self.assertTrue(data["cleared"])
            self.assertEqual(data["tool"], "run_command")
            self.assertIn("make step", data["args"])
            self.assertEqual(data["chars"], len(messages[3]["content"]))
            self.assertIn('command_output(job_id="job-', data["note"])
            self.assertNotIn("\n", stub["content"])
        # The last three groups are untouched, and the original list is not modified.
        self.assertEqual(tools[3:], [m for m in messages if m["role"] == "tool"][3:])
        self.assertIn("yyyy", messages[3]["content"])
        # Stubs are not counted again: the next group alone does not fire.
        again = out + conversation(1)[2:]
        again[-2]["tool_calls"][0]["id"] = again[-1]["tool_call_id"] = "call-new"
        self.assertEqual(context.clear_stale_tool_results(again)[1], 0)

    def test_read_file_stub_names_the_reread(self):
        messages = [{"role": "system", "content": "s"}]
        for n in range(6):
            messages.append(call(n, "read_file", {"path": f"src/f{n}.py"}))
            messages.append({"role": "tool", "tool_call_id": f"call-{n}",
                             "content": json.dumps({"path": f"src/f{n}.py", "content": "z" * 20_000})})
        out, count, _ = context.clear_stale_tool_results(messages)
        self.assertEqual(count, 3)
        self.assertIn('read_file(path="src/f0.py")', json.loads(out[2]["content"])["note"])


class ClearingInTheAgent(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()

    def tearDown(self):
        self.tmp.cleanup()

    def agent(self, steps, **options):
        events = []
        provider = Scripted(steps)
        agent = Agent(CONFIG, self.tmp.name, events.append, provider=provider, **options)
        return agent, provider, events

    def test_model_gets_the_bounded_copy_and_the_event_the_full_one(self):
        agent, provider, events = self.agent([call(0, "run_command", {"command": "seq 1 20000"})])
        agent.ask("run it")
        sent = [m for m in provider.seen[-1] if m.get("role") == "tool"][0]["content"]
        self.assertLessEqual(len(sent), MODEL_JSON_CAP)
        self.assertIn("total_lines", json.loads(sent))
        result = [e for e in events if e["event"] == "tool_result"][0]["result"]
        self.assertEqual(len(result["output"]), MAX_OUTPUT)
        self.assertNotIn("total_lines", result)

    # 7 groups of ~12k-character results: after the 7th, the 4 older than the last three pass 40k.
    SEQ = "seq 1 20000"

    def test_clearing_fires_once_and_keeps_the_last_groups(self):
        steps = [call(n, "run_command", {"command": self.SEQ}) for n in range(9)]
        agent, provider, events = self.agent(steps)
        agent.ask("go")
        cleared = [e for e in events if e["event"] == "tool_results_cleared"]
        self.assertEqual(len(cleared), 1)
        self.assertEqual((cleared[0]["count"], cleared[0]["keep_groups"]), (4, 3))
        self.assertGreater(cleared[0]["chars"], 40_000)
        final = provider.seen[-1]
        self.assertTrue(pairing_valid(final))
        results = [m["content"] for m in final if m.get("role") == "tool"]
        self.assertEqual(len(results), 9)
        self.assertTrue(all(r.startswith('{"cleared": true') for r in results[:4]))
        self.assertFalse(any(r.startswith('{"cleared": true') for r in results[4:]))
        self.assertIn('command_output(job_id="job-1", from_line=1)', json.loads(results[0])["note"])

    def test_clear_tool_results_false_keeps_everything(self):
        steps = [call(n, "run_command", {"command": self.SEQ}) for n in range(8)]
        agent, provider, events = self.agent(steps, clear_tool_results=False)
        agent.ask("go")
        self.assertFalse([e for e in events if e["event"] == "tool_results_cleared"])
        self.assertFalse(any(m["content"].startswith('{"cleared"') for m in provider.seen[-1]
                             if m.get("role") == "tool"))

    def test_the_option_is_validated_reported_and_settable(self):
        with self.assertRaises(ValueError):
            validate_turn_options({"clear_tool_results": "no"})
        self.assertEqual(validate_turn_options({"clear_tool_results": False}), {"clear_tool_results": False})
        agent, _, _ = self.agent([])
        self.assertTrue(agent.options()["clear_tool_results"])
        self.assertFalse(agent.set_options({"clear_tool_results": False})["clear_tool_results"])
        from relay_core.session_protocol import agent_options
        self.assertFalse(agent_options({"clear_tool_results": False}, self.tmp.name)["clear_tool_results"])


if __name__ == "__main__":
    unittest.main()
