# SPDX-License-Identifier: AGPL-3.0-or-later
"""`stream_tool_output`: counts instead of text on the wire (protocol 23.10, card #PPR4).

The unit half holds `relay_core.tool_stream` against every shape it has to leave alone. The worker
half drives a real turn — a real `run_command` whose output is thousands of lines, through a
loopback OpenAI-compatible provider — twice, once with the option and once without, and compares
the two transcripts: the same line count, the same label, the same `exit_code`, and the full text
still there for the fold to fetch either way.
"""
import json
import subprocess
import sys
import tempfile
import threading
import unittest
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "backend"))

from relay_core import tool_stream   # noqa: E402

LINES = 2000          # what the command prints; enough that the text dwarfs its counts
COMMAND = f"seq 1 {LINES}"


class CountedTests(unittest.TestCase):
    def test_a_streamed_chunk_becomes_its_counts(self):
        out = tool_stream.counted({"event": "tool_output", "call_id": "c1", "text": "one\ntwo\n"})
        self.assertEqual(out, {"event": "tool_output", "call_id": "c1", "lines": 2, "bytes": 8,
                               "partial": False, "counted": True})

    def test_a_chunk_that_leaves_a_line_open_says_so(self):
        out = tool_stream.counted({"event": "tool_output", "text": "one\nhalf"})
        self.assertEqual((out["lines"], out["partial"], out["bytes"]), (1, True, 8))

    def test_bytes_are_utf8_not_characters(self):
        out = tool_stream.counted({"event": "tool_output", "text": "☃\n"})
        self.assertEqual((out["lines"], out["bytes"]), (1, 4))

    def test_an_empty_chunk_closes_nothing(self):
        out = tool_stream.counted({"event": "tool_output", "text": ""})
        self.assertEqual((out["lines"], out["partial"]), (0, False))

    def test_a_result_keeps_its_verdict_and_loses_its_output(self):
        out = tool_stream.counted({"event": "tool_result", "tool": "run_command", "ms": 12,
                                   "label": {"title": "ran seq"}, "diff": "",
                                   "result": {"output": "a\nb\n", "exit_code": 0, "truncated": False,
                                              "omitted_bytes": 0, "job_id": "job-1"}})
        self.assertEqual(out["result"], {"output_lines": 2, "output_bytes": 4, "exit_code": 0,
                                         "truncated": False, "omitted_bytes": 0, "job_id": "job-1"})
        # Everything the concise line and the wrong-mode hint read is untouched.
        self.assertEqual((out["tool"], out["ms"], out["label"]), ("run_command", 12, {"title": "ran seq"}))
        self.assertTrue(out["counted"])

    def test_a_read_loses_its_content_the_same_way(self):
        out = tool_stream.counted({"event": "tool_result", "result": {"content": "x\ny\nz", "path": "a.py"}})
        self.assertEqual(out["result"], {"content_lines": 2, "content_bytes": 5, "path": "a.py"})

    def test_an_error_result_is_sent_whole(self):
        event = {"event": "tool_result", "result": {"error": "old_string was not found in the file"}}
        self.assertEqual(tool_stream.counted(event), event)   # short, and every surface reads it

    def test_the_stored_reply_is_never_trimmed(self):
        # The `tool_output` name collision (section 5): this one is the fold's fetch, and it is the
        # one surface that must come back with the whole of it.
        event = {"event": "tool_output", "stored": True, "turn_id": "t1", "call_id": "c1",
                 "result": {"output": "a\nb\n"}, "ok": True}
        self.assertEqual(tool_stream.counted(event), event)

    def test_other_events_pass_through(self):
        for event in ({"event": "delta", "text": "hello"},
                      {"event": "tool_started", "tool": "run_command", "preview": "RUN COMMAND\n\nseq"},
                      {"event": "subagent_event", "id": "a1",
                       "payload": {"event": "tool_output", "text": "a\n"}}):
            self.assertEqual(tool_stream.counted(dict(event)), event)

    def test_a_bad_value_is_refused(self):
        for bad in ("true", 1, None, [], 0):
            with self.assertRaises(ValueError):
                tool_stream.validate(bad)
        self.assertIs(tool_stream.validate(False), False)


# ----- the worker, end to end --------------------------------------------------------------------

class Stub(BaseHTTPRequestHandler):
    """One `run_command` call, then a one-line reply. Nothing is read from the network."""

    def log_message(self, *args):
        pass

    def _chunk(self, delta, finish=None):
        body = {"id": "stub", "object": "chat.completion.chunk", "created": 0, "model": "stub",
                "choices": [{"index": 0, "delta": delta, "finish_reason": finish}]}
        self.wfile.write(b"data: " + json.dumps(body).encode() + b"\n\n")
        self.wfile.flush()

    def do_GET(self):
        raw = json.dumps({"data": [{"id": "stub"}]}).encode()
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(raw)))
        self.end_headers()
        self.wfile.write(raw)

    def do_POST(self):
        request = json.loads(self.rfile.read(int(self.headers.get("Content-Length") or 0)) or b"{}")
        messages = request.get("messages") or []
        # The step is how many tool calls the model has already made this turn: the worker appends
        # messages of its own, so the *last* user message cannot be the key (QA stub note).
        ran = any(m.get("role") == "tool" for m in messages)
        self.send_response(200)
        self.send_header("Content-Type", "text/event-stream")
        self.end_headers()
        try:
            if ran:
                self._chunk({"role": "assistant", "content": "done."})
                self._chunk({}, "stop")
            else:
                self._chunk({"role": "assistant", "content": None,
                             "tool_calls": [{"index": 0, "id": "call_1", "type": "function",
                                             "function": {"name": "run_command",
                                                          "arguments": json.dumps({"command": COMMAND})}}]})
                self._chunk({}, "tool_calls")
            self.wfile.write(b"data: [DONE]\n\n")
            self.wfile.flush()
        except (BrokenPipeError, ConnectionResetError):
            return


class WorkerTests(unittest.TestCase):
    """A real turn, both ways round."""

    @classmethod
    def setUpClass(cls):
        cls.server = ThreadingHTTPServer(("127.0.0.1", 0), Stub)
        threading.Thread(target=cls.server.serve_forever, daemon=True).start()
        cls.base = f"http://127.0.0.1:{cls.server.server_address[1]}/v1"

    @classmethod
    def tearDownClass(cls):
        cls.server.shutdown()
        cls.server.server_close()

    def worker(self, workspace, **extra):
        """A worker on the stub, driven line by line: `shutdown` written before the turn has
        finished cancels it, so nothing here sends the whole script up front."""
        proc = subprocess.Popen([sys.executable, "-S", str(ROOT / "backend/worker.py")],
                                stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True, cwd=ROOT,
                                env={"RELAY_KEYRING": "off", "PATH": "/usr/bin:/bin", "HOME": workspace})
        self.addCleanup(proc.kill)
        # A readline() on a worker waiting on stdin never returns on its own: a broken run fails
        # this test instead of hanging the suite.
        watchdog = threading.Timer(120, proc.kill)
        watchdog.start()
        self.addCleanup(watchdog.cancel)
        self.send(proc, {"type": "configure", "base_url": self.base, "model": "stub", "api_key": "",
                         "workspace": workspace,
                         # The command must run without an ask: this is about the wire, not approvals.
                         "approvals_ask": [], "approvals_chosen": True, **extra})
        return proc

    @staticmethod
    def send(proc, message):
        proc.stdin.write(json.dumps(message) + "\n")
        proc.stdin.flush()

    def finish(self, proc):
        """`shutdown` has been sent: let it go, and leave no pipe or child behind."""
        proc.stdin.close()
        proc.wait(timeout=60)
        proc.stdout.close()

    def read_until(self, proc, kind, events=None):
        """Events up to and including the first one of `kind`."""
        events = events if events is not None else []
        while True:
            line = proc.stdout.readline()
            self.assertTrue(line, f"the worker stopped before {kind}: " + json.dumps(events)[-1500:])
            events.append(json.loads(line))
            self.assertNotEqual(events[-1]["event"], "error", events[-1])
            if events[-1]["event"] == kind:
                return events

    def run_turn(self, stream_tool_output=None):
        """One turn in a throwaway workspace; returns its events."""
        workspace = tempfile.mkdtemp(prefix="relay-toolstream-")
        extra = {} if stream_tool_output is None else {"stream_tool_output": stream_tool_output}
        proc = self.worker(workspace, **extra)
        events = self.read_until(proc, "configured")
        self.send(proc, {"type": "ask", "id": "a1", "text": "please run it", "when": "now"})
        self.read_until(proc, "agent_finished", events)
        self.send(proc, {"type": "shutdown"})
        self.finish(proc)
        return events

    @staticmethod
    def of(events, kind):
        return [e for e in events if e["event"] == kind]

    def counted_lines(self, events):
        """What the pane's row would count, exactly as src/CallLines.cpp does it."""
        total, partial = 0, False
        for event in self.of(events, "tool_output"):
            if event.get("stored"):
                continue
            if event.get("counted"):
                total += event["lines"]
                partial = event["partial"]
            else:
                total += event["text"].count("\n")
                partial = bool(event["text"]) and not event["text"].endswith("\n")
        return total + (1 if partial else 0)

    def test_the_default_is_the_text_and_the_option_is_the_counts(self):
        streamed = self.run_turn()                       # no option at all: an older GUI
        counted = self.run_turn(stream_tool_output=False)

        # 1. The default is unchanged, down to the field.
        chunks = [e for e in self.of(streamed, "tool_output") if not e.get("stored")]
        self.assertTrue(chunks)
        self.assertTrue(all("text" in e and "counted" not in e for e in chunks))
        self.assertEqual(self.of(streamed, "tool_result")[0]["result"]["output"].count("\n"), LINES)

        # 2. With the option, not one chunk carries text, and the result has no output.
        trimmed = [e for e in self.of(counted, "tool_output") if not e.get("stored")]
        self.assertTrue(trimmed)
        self.assertTrue(all("text" not in e and e["counted"] for e in trimmed))
        result = self.of(counted, "tool_result")[0]["result"]
        self.assertNotIn("output", result)
        self.assertEqual(result["output_lines"], LINES)
        self.assertEqual(result["exit_code"], 0)

        # 3. The row counts the same number either way — the claim the option rests on.
        self.assertEqual(self.counted_lines(counted), self.counted_lines(streamed))
        self.assertEqual(self.counted_lines(counted), LINES)

        # 4. And the concise line (§ 23) is byte for byte the same.
        self.assertEqual(self.of(counted, "tool_result")[0]["label"],
                         self.of(streamed, "tool_result")[0]["label"])

        # 5. The point of it: the bytes on the wire.
        def size(events):
            return sum(len(json.dumps(e)) for e in events
                       if e["event"] in ("tool_output", "tool_result") and not e.get("stored"))
        self.assertLess(size(counted) * 20, size(streamed))   # well past the 80 % the card asks for

        # 6. `configured` says which shape this worker is sending.
        self.assertIs(self.of(streamed, "configured")[0]["stream_tool_output"], True)
        self.assertIs(self.of(counted, "configured")[0]["stream_tool_output"], False)

    def test_set_agent_options_answers_with_the_shape_in_force(self):
        workspace = tempfile.mkdtemp(prefix="relay-toolstream-")
        proc = subprocess.Popen([sys.executable, "-S", str(ROOT / "backend/worker.py")],
                                stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True, cwd=ROOT,
                                env={"RELAY_KEYRING": "off", "PATH": "/usr/bin:/bin", "HOME": workspace})
        self.addCleanup(proc.kill)
        script = [{"type": "configure", "base_url": self.base, "model": "stub", "api_key": "",
                   "workspace": workspace},
                  {"type": "set_agent_options", "id": "o1", "stream_tool_output": False},
                  {"type": "set_agent_options", "id": "o2", "max_auto_turns": 3},
                  {"type": "set_agent_options", "id": "o3", "stream_tool_output": "no"},
                  {"type": "shutdown"}]
        out, _ = proc.communicate("".join(json.dumps(m) + "\n" for m in script), timeout=60)
        events = [json.loads(line) for line in out.splitlines()]
        answers = {e["id"]: e for e in self.of(events, "agent_options")}
        self.assertIs(answers["o1"]["stream_tool_output"], False)
        # A set_agent_options that carries one key leaves the other alone (protocol 12.1).
        self.assertIs(answers["o2"]["stream_tool_output"], False)
        # A bad value is an error and changes nothing.
        self.assertNotIn("o3", answers)
        self.assertIn("boolean", self.of(events, "error")[0]["text"])

    def test_a_fold_fetch_still_returns_the_whole_output(self):
        workspace = tempfile.mkdtemp(prefix="relay-toolstream-")
        proc = self.worker(workspace, stream_tool_output=False)
        events = self.read_until(proc, "configured")
        self.send(proc, {"type": "ask", "id": "a1", "text": "please run it", "when": "now"})
        result = self.read_until(proc, "tool_result", events)[-1]
        self.assertNotIn("output", result["result"])   # the option is on
        self.read_until(proc, "agent_finished", events)
        # The same round trip a fold makes when it is clicked (`foldRequested`, src/Pane.h).
        self.send(proc, {"type": "tool_output_get", "id": "fold-1",
                         "turn_id": result["turn_id"], "call_id": result["call_id"]})
        stored = self.read_until(proc, "tool_output", [])[-1]
        self.assertTrue(stored["stored"])
        # The whole of it is still there, which is why the option may keep it off the stream.
        self.assertEqual(stored["result"]["output"].count("\n"), LINES)
        self.assertEqual(stored["detail"][-1]["text"].count("\n"), LINES)
        self.send(proc, {"type": "shutdown"})
        self.finish(proc)


if __name__ == "__main__":
    unittest.main()
