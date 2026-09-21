# SPDX-License-Identifier: AGPL-3.0-or-later
"""The Claude Code harness adapter (Tier A, protocol 29), replayed against recorded transcripts.

Nothing here starts the real `claude`. `tests/fixtures/guest_harness_claude/*.jsonl` are real
transcripts recorded once against Claude Code 2.1.278 (see
`docs/qa_evidence/2026-09-19-claude-codex-guest-integration/harness-claude-README.md`), one JSON
object per line with a `dir` marker: `in` is a line the adapter is expected to write, `out` one
the CLI wrote back, `out_raw` a line that is not JSON at all, `meta` the command line it was
recorded with. `FakeClaude` below replays one: it waits for each `in` line before going on, so a
fixture is a two-sided script and a test that drifts from the protocol hangs on the line it
skipped rather than passing.

Run: PYTHONPATH=backend python3 -m unittest tests.test_guest_harness_claude
"""
from __future__ import annotations

import io
import json
import os
import queue
import tempfile
import threading
import time
import unittest

from relay_core import guest_harness
from relay_core.guest_harness import HarnessError, HarnessNotAvailable
from relay_core import guest_harness_claude as gh

FIXTURES = os.path.join(os.path.dirname(os.path.abspath(__file__)), "fixtures",
                        "guest_harness_claude")
WAIT = 10.0            # how long a replay waits for the adapter to write the line it expects


def load(name: str) -> list[dict]:
    with open(os.path.join(FIXTURES, name + ".jsonl")) as handle:
        return [json.loads(line) for line in handle if line.strip()]


# ----- the fake process ------------------------------------------------------------------------


class _Stdin:
    """The pipe the adapter writes its JSON lines into."""

    def __init__(self, owner):
        self._owner = owner
        self._buffer = ""
        self.closed = False

    def write(self, text):
        if self.closed:
            raise ValueError("write to a closed stdin")
        self._buffer += text
        while "\n" in self._buffer:
            line, self._buffer = self._buffer.split("\n", 1)
            if line.strip():
                self._owner._on_line(line)

    def flush(self):
        pass

    def close(self):
        self.closed = True
        self._owner._on_stdin_closed()


class FakeClaude:
    """A scripted stand-in for one `claude` process."""

    def __init__(self, script, *, stderr: str = "", hang_at_end: bool = True):
        self._script = [r for r in script if r.get("dir") != "meta"]
        self._hang_at_end = hang_at_end
        read_fd, self._write_fd = os.pipe()
        self.stdout = os.fdopen(read_fd, "r")
        self.stderr = io.StringIO(stderr)
        self.stdin = _Stdin(self)
        self.written: list[dict] = []
        self.returncode = None
        self.pid = 4242
        self.finished = threading.Event()
        self._incoming: queue.Queue = queue.Queue()
        self._ids: dict[str, str] = {}      # recorded request_id -> the one the adapter used
        self._out_lock = threading.Lock()
        self._failure = None
        self._thread = threading.Thread(target=self._replay, daemon=True)
        self._thread.start()

    # -- the adapter's side
    def _on_line(self, line: str) -> None:
        message = json.loads(line)
        self.written.append(message)
        self._incoming.put(message)

    def _on_stdin_closed(self) -> None:
        self._close_out()
        self.returncode = 0

    def poll(self):
        return self.returncode

    def wait(self, timeout=None):
        self.returncode = 0 if self.returncode is None else self.returncode
        return self.returncode

    def terminate(self):
        self.returncode = -15
        self._close_out()

    def kill(self):
        self.returncode = -9
        self._close_out()

    def _close_out(self):
        with self._out_lock:
            if self._write_fd is not None:
                try:
                    os.close(self._write_fd)
                except OSError:
                    pass
                self._write_fd = None

    # -- the script
    def _emit(self, text: str) -> None:
        with self._out_lock:
            if self._write_fd is None:
                return
            try:
                os.write(self._write_fd, (text + "\n").encode())
            except OSError:
                pass

    def _await_line(self, expected: dict) -> None:
        want_type = expected.get("type")
        want_sub = (expected.get("request") or {}).get("subtype") if want_type == \
            "control_request" else None
        while True:
            try:
                got = self._incoming.get(timeout=WAIT)
            except queue.Empty:
                self._failure = f"the adapter never wrote a {want_type} ({want_sub})"
                raise RuntimeError(self._failure)
            if got.get("type") != want_type:
                continue                                  # an extra line is not a failure
            if want_sub and (got.get("request") or {}).get("subtype") != want_sub:
                continue
            if want_type == "control_request":
                recorded = str(expected.get("request_id") or "")
                live = str(got.get("request_id") or "")
                if recorded:
                    self._ids[recorded] = live
            return

    def _substitute(self, message: dict) -> dict:
        """A recorded control_response names the recorded request_id; the adapter waits on the
        one it actually sent, so swap it in."""
        if message.get("type") == "control_response":
            body = message.get("response")
            if isinstance(body, dict):
                recorded = str(body.get("request_id") or "")
                if recorded in self._ids:
                    body = dict(body, request_id=self._ids[recorded])
                    message = dict(message, response=body)
        return message

    def _replay(self) -> None:
        try:
            for record in self._script:
                where = record.get("dir")
                if where == "in":
                    self._await_line(record.get("json") or {})
                elif where == "out":
                    self._emit(json.dumps(self._substitute(record.get("json") or {})))
                elif where == "out_raw":
                    self._emit(str(record.get("text") or ""))
                elif where == "err":
                    pass
        except Exception:
            pass
        finally:
            self.finished.set()
            if not self._hang_at_end:
                self._close_out()


class Spawner:
    """Hands out one FakeClaude per `start()`, and remembers every command line."""

    def __init__(self, *procs):
        self._procs = list(procs)
        self.calls: list[tuple] = []

    def __call__(self, cmd, cwd, env):
        self.calls.append((list(cmd), cwd, dict(env)))
        if not self._procs:
            raise AssertionError("the adapter started more processes than the test scripted")
        return self._procs.pop(0)


class Collector:
    def __init__(self):
        self.events: list[guest_harness.HarnessEvent] = []

    def __call__(self, event):
        self.events.append(event)

    @property
    def kinds(self):
        return [e.kind for e in self.events]

    def of(self, kind):
        return [e for e in self.events if e.kind == kind]

    def text(self):
        return "".join(e.data.get("text", "") for e in self.of("delta"))


def harness_on(proc, **kwargs):
    """A started harness talking to `proc`, with `claude` faked onto PATH."""
    spawner = Spawner(proc)
    return spawner, gh.ClaudeHarness(spawn=spawner, binary=_on_path(), **kwargs)


_PATH_BIN = None


def _on_path() -> str:
    """A real executable name on PATH, so `shutil.which` is happy without a real claude."""
    global _PATH_BIN
    if _PATH_BIN is None:
        _PATH_BIN = "sh" if os.path.exists("/bin/sh") else "python3"
    return _PATH_BIN


def run_turn(test, proc, prompt, *, harness=None, **kwargs):
    """Start a harness on `proc`, run one turn, and hand back everything a test looks at."""
    spawner = None
    if harness is None:
        spawner, harness = harness_on(proc, **kwargs)
    start = harness.start(cwd=os.getcwd())
    collector = Collector()
    result = harness.send(prompt, emit=collector, cancel=threading.Event())
    return harness, collector, result, start, spawner


# ----- the recorded turns ----------------------------------------------------------------------


class HelloTurnTest(unittest.TestCase):
    """(a) init + "Reply with the single word ok." — the smallest whole turn there is."""

    def test_event_sequence_and_result(self):
        proc = FakeClaude(load("hello"))
        harness, events, result, start, _ = run_turn(self, proc, "Reply with the single word ok.")
        self.addCleanup(harness.close)

        self.assertEqual(events.kinds[0], "started")
        self.assertEqual(events.kinds[-1], "done")
        self.assertIn("delta", events.kinds)
        self.assertIn("usage", events.kinds)
        self.assertEqual(events.kinds.count("started"), 1)
        self.assertEqual(events.text(), "ok")
        self.assertEqual(result.text, "ok")
        self.assertEqual(result.stop_reason, "end")
        for event in events.events:
            self.assertIn(event.kind, guest_harness.EVENT_KINDS)

    def test_started_carries_the_session_and_model(self):
        proc = FakeClaude(load("hello"))
        harness, events, _, start, _ = run_turn(self, proc, "Reply with the single word ok.")
        self.addCleanup(harness.close)
        started = events.of("started")[0].data
        self.assertTrue(started["session_id"])
        self.assertEqual(started["model"], "claude-haiku-4-5-20251001")
        self.assertEqual(harness.session_id, started["session_id"])
        self.assertEqual(harness.model, "claude-haiku-4-5-20251001")
        # `start()` returns before the CLI has said anything: the id is the one we named it with.
        self.assertTrue(start.session_id)

    def test_usage_and_context_pct(self):
        proc = FakeClaude(load("hello"))
        harness, events, result, _, _ = run_turn(self, proc, "Reply with the single word ok.")
        self.addCleanup(harness.close)
        usage = events.of("usage")[0].data
        self.assertEqual(usage["input_tokens"], 10)
        self.assertEqual(usage["output_tokens"], 71)
        self.assertEqual(usage["cache_read_input_tokens"], 13689)
        self.assertEqual(usage["cache_creation_input_tokens"], 7624)
        self.assertEqual(usage["model"], "claude-haiku-4-5-20251001")
        self.assertAlmostEqual(usage["context_pct"], round(100 * 21323 / 200000, 1))
        self.assertEqual(usage["cost_usd"], 0.0)          # redacted in the fixture
        self.assertEqual(result.usage, usage)

    def test_the_handshake_and_the_prompt_are_what_we_write(self):
        proc = FakeClaude(load("hello"))
        harness, _, _, _, _ = run_turn(self, proc, "Reply with the single word ok.")
        self.addCleanup(harness.close)
        first, second = proc.written[0], proc.written[1]
        self.assertEqual(first["type"], "control_request")
        self.assertEqual(first["request"]["subtype"], "initialize")
        self.assertEqual(second["type"], "user")
        self.assertEqual(second["message"]["role"], "user")
        self.assertEqual(second["message"]["content"],
                         [{"type": "text", "text": "Reply with the single word ok."}])


class BashTurnTest(unittest.TestCase):
    """(b) one turn that runs one shell command."""

    def setUp(self):
        self.proc = FakeClaude(load("bash-tool"))
        self.harness, self.events, self.result, _, _ = run_turn(
            self, self.proc, "Run `echo relay-harness-ok` with Bash and report its output")
        self.addCleanup(self.harness.close)

    def test_the_call_line(self):
        started = self.events.of("tool_started")[0].data
        self.assertEqual(started["tool"], "run_command")
        self.assertTrue(started["call_id"].startswith("toolu_"))
        self.assertEqual(started["input"]["command"], "echo relay-harness-ok")
        self.assertEqual(started["input"]["_guest_tool"], "Bash")
        self.assertEqual(started["label"], "Output a test string")

    def test_the_result_line(self):
        result = self.events.of("tool_result")[0].data
        self.assertEqual(result["tool"], "run_command")
        self.assertEqual(result["call_id"], self.events.of("tool_started")[0].data["call_id"])
        self.assertEqual(result["output"], "relay-harness-ok")
        self.assertTrue(result["ok"])
        self.assertIsInstance(result["ms"], int)
        self.assertNotIn("diff", result)                  # a command makes no edit

    def test_the_order_of_a_tool_turn(self):
        kinds = [k for k in self.events.kinds if k in
                 ("started", "tool_started", "tool_result", "usage", "done")]
        self.assertEqual(kinds, ["started", "tool_started", "tool_result", "usage", "done"])

    def test_the_answer(self):
        self.assertIn("relay-harness-ok", self.result.text)
        self.assertEqual(self.result.stop_reason, "end")


class ApprovalTest(unittest.TestCase):
    """(c) permissions="ask": the CLI's `can_use_tool` becomes an `approval`, and `answer()`
    is the `control_response` that lets the tool run."""

    def test_approval_answered_allow(self):
        proc = FakeClaude(load("approval-write"))
        spawner, harness = harness_on(proc)
        self.addCleanup(harness.close)
        harness.start(cwd=os.getcwd(), permissions="ask")
        events = Collector()
        answered = []

        def emit(event):
            events(event)
            if event.kind == "approval":
                answered.append(event.data["id"])
                harness.answer(event.data["id"], {"behavior": "allow"})

        result = harness.send("Create a file named harness-ok.txt in the current directory "
                              "containing exactly the word ok, using the Write tool.",
                              emit=emit, cancel=threading.Event())

        approval = events.of("approval")[0].data
        self.assertEqual(approval["kind"], "patch")
        self.assertIn("harness-ok.txt", approval["detail"])
        self.assertEqual(answered, [approval["id"]])

        replies = [m for m in proc.written if m.get("type") == "control_response"]
        self.assertEqual(len(replies), 1)
        body = replies[0]["response"]
        self.assertEqual(body["request_id"], approval["id"])
        self.assertEqual(body["subtype"], "success")
        self.assertEqual(body["response"]["behavior"], "allow")
        self.assertEqual(body["response"]["updatedInput"]["content"], "ok")
        self.assertEqual(result.stop_reason, "end")

    def test_the_write_result_carries_a_diff(self):
        proc = FakeClaude(load("approval-write"))
        spawner, harness = harness_on(proc)
        self.addCleanup(harness.close)
        harness.start(cwd=os.getcwd(), permissions="ask")
        events = Collector()

        def emit(event):
            events(event)
            if event.kind == "approval":
                harness.answer(event.data["id"], {"behavior": "allow"})

        harness.send("write it", emit=emit, cancel=threading.Event())
        result = events.of("tool_result")[0].data
        self.assertEqual(result["tool"], "write_file")
        self.assertIn("diff", result)
        self.assertIn("+ok", result["diff"])
        self.assertIn("harness-ok.txt", result["diff"])

    def _answer_with(self, decision):
        """The recorded approval turn, answered with `decision`. Returns the body of the one
        `control_response` the adapter wrote."""
        proc = FakeClaude(load("approval-write"))
        _, harness = harness_on(proc)
        self.addCleanup(harness.close)
        harness.start(cwd=os.getcwd(), permissions="ask")
        events = Collector()

        def emit(event):
            events(event)
            if event.kind == "approval":
                harness.answer(event.data["id"], decision)

        harness.send("write it", emit=emit, cancel=threading.Event())
        replies = [m for m in proc.written if m.get("type") == "control_response"]
        self.assertEqual(len(replies), 1)
        return replies[0]["response"]["response"]

    def test_an_allow_for_the_session_carries_a_session_permission_rule(self):
        """GT7X t:a3. Claude can express both wider scopes, on this same response: an allow may
        carry `updatedPermissions`, and `destination: "session"` is exactly "until this session
        ends" (2.1.278's validator takes userSettings | projectSettings | localSettings |
        session | cliArg)."""
        body = self._answer_with({"behavior": "allow", "scope": "session"})
        self.assertEqual(body["behavior"], "allow")
        rule = body["updatedPermissions"][0]
        self.assertEqual(len(body["updatedPermissions"]), 1)
        self.assertEqual(rule["type"], "addRules")
        self.assertEqual(rule["behavior"], "allow")
        self.assertEqual(rule["destination"], "session")
        # As narrow as the thing that was asked about: this tool, this file.
        self.assertEqual(rule["rules"], [{"toolName": "Write",
                                          "ruleContent": body["updatedInput"]["file_path"]}])

    def test_a_plain_allow_grants_nothing_beyond_this_call(self):
        for decision in ({"behavior": "allow"}, {"behavior": "allow", "scope": "once"},
                         {"behavior": "allow", "scope": "stop"},       # meaningless on an allow
                         {"behavior": "allow", "scope": "whenever"}):  # not a scope
            body = self._answer_with(decision)
            self.assertEqual(body["behavior"], "allow", decision)
            self.assertNotIn("updatedPermissions", body)

    def test_a_deny_that_stops_the_turn_asks_the_cli_to_interrupt(self):
        """`{"behavior": "deny", "interrupt": true}` is what the CLI logs as "SDK permission
        prompt deny+interrupt" and acts on by aborting the turn."""
        body = self._answer_with({"behavior": "deny", "scope": "stop", "message": "no"})
        self.assertEqual(body["behavior"], "deny")
        self.assertEqual(body["message"], "no")
        self.assertTrue(body["interrupt"])
        body = self._answer_with({"behavior": "deny", "message": "no"})
        self.assertNotIn("interrupt", body)

    def test_a_session_rule_is_as_narrow_as_what_was_asked_about(self):
        command = gh._session_rule("Bash", {"command": "npm test"})
        self.assertEqual(command["rules"], [{"toolName": "Bash", "ruleContent": "npm test"}])
        self.assertEqual(command["destination"], "session")
        edit = gh._session_rule("Edit", {"file_path": "/w/note.txt", "old_string": "a"})
        self.assertEqual(edit["rules"], [{"toolName": "Edit", "ruleContent": "/w/note.txt"}])
        # Nothing specific to name: the tool alone, which is all claude can be told.
        bare = gh._session_rule("TodoWrite", {"todos": []})
        self.assertEqual(bare["rules"], [{"toolName": "TodoWrite"}])
        self.assertIsNone(gh._session_rule("", {}))

    def test_the_ask_command_line(self):
        proc = FakeClaude(load("approval-write"))
        spawner, harness = harness_on(proc)
        self.addCleanup(harness.close)
        harness.start(cwd=os.getcwd(), permissions="ask")
        argv = spawner.calls[0][0]
        self.assertIn("--permission-prompt-tool", argv)
        self.assertEqual(argv[argv.index("--permission-prompt-tool") + 1], "stdio")


class InterruptTest(unittest.TestCase):
    """(c') the interrupt: a `control_request` mid-turn, and a `result` that says `is_error`
    without the turn having failed."""

    def test_interrupt_ends_the_turn_without_an_error(self):
        proc = FakeClaude(load("interrupt"))
        spawner, harness = harness_on(proc)
        self.addCleanup(harness.close)
        harness.start(cwd=os.getcwd())
        events = Collector()

        def emit(event):
            events(event)
            if event.kind == "tool_started":
                harness.interrupt()

        result = harness.send("Run `sleep 20` with Bash, then tell me it finished.",
                              emit=emit, cancel=threading.Event())
        self.assertEqual(result.stop_reason, "interrupted")
        self.assertEqual(events.of("done")[0].data["stop_reason"], "interrupted")
        self.assertEqual(events.of("error"), [])
        interrupts = [m for m in proc.written
                      if m.get("type") == "control_request"
                      and m["request"].get("subtype") == "interrupt"]
        self.assertEqual(len(interrupts), 1)

    def test_cancel_interrupts_too(self):
        proc = FakeClaude(load("interrupt"))
        spawner, harness = harness_on(proc)
        self.addCleanup(harness.close)
        harness.start(cwd=os.getcwd())
        cancel = threading.Event()
        events = Collector()

        def emit(event):
            events(event)
            if event.kind == "tool_started":
                cancel.set()

        result = harness.send("Run `sleep 20` with Bash.", emit=emit, cancel=cancel)
        self.assertEqual(result.stop_reason, "interrupted")
        self.assertTrue(any(m.get("type") == "control_request"
                            and m["request"].get("subtype") == "interrupt"
                            for m in proc.written))

    def test_the_session_takes_the_next_turn(self):
        proc = FakeClaude(load("interrupt-then-turn"))
        spawner, harness = harness_on(proc)
        self.addCleanup(harness.close)
        harness.start(cwd=os.getcwd())
        events = Collector()

        def emit(event):
            events(event)
            if event.kind == "tool_started":
                harness.interrupt()

        first = harness.send("Run `sleep 20` with Bash, then tell me it finished.",
                             emit=emit, cancel=threading.Event())
        self.assertEqual(first.stop_reason, "interrupted")
        second = harness.send("Reply with the single word ok.", emit=Collector(),
                              cancel=threading.Event())
        self.assertEqual(second.stop_reason, "end")
        self.assertEqual(second.text, "ok")


class CompactTest(unittest.TestCase):
    """`/compact` is a user message on this stream, and its own `result` is swallowed."""

    def test_compact_writes_the_slash_command_and_drains_its_result(self):
        proc = FakeClaude(load("compact"))
        spawner, harness = harness_on(proc)
        self.addCleanup(harness.close)
        harness.start(cwd=os.getcwd())
        harness.compact()
        self.assertTrue(proc.finished.wait(WAIT), "the fixture never ran out")
        compacts = [m for m in proc.written if m.get("type") == "user"]
        self.assertEqual(len(compacts), 1)
        self.assertEqual(compacts[0]["message"]["content"][0]["text"], "/compact")
        for _ in range(100):                     # the drain thread has its own pace
            if harness._inbox.empty():
                break
            threading.Event().wait(0.05)
        self.assertTrue(harness._inbox.empty(), "compaction's result was left for the next turn")


# ----- the shapes the adapter has to survive ----------------------------------------------------


def script(*messages, ins=(), request_id="h1"):
    """A hand-written transcript: the initialize handshake, then `messages`."""
    out = [*_handshake(request_id), {"dir": "in", "json": {"type": "user"}}]
    out.extend(messages)
    return out


def _handshake(request_id="h1"):
    """Just the initialize round trip: a process that is started and then left alone."""
    return [{"dir": "in", "json": {"type": "control_request", "request_id": request_id,
                                   "request": {"subtype": "initialize"}}},
            {"dir": "out", "json": {"type": "control_response", "response": {
                "subtype": "success", "request_id": request_id, "response": {}}}}]


def _unsupported(subtype, request_id="c1"):
    """A control request this CLI does not know, answered the way 2.1.278 answers one."""
    return [{"dir": "in", "json": {"type": "control_request", "request_id": request_id,
                                   "request": {"subtype": subtype}}},
            {"dir": "out", "json": {"type": "control_response", "response": {
                "subtype": "error", "request_id": request_id,
                "error": f"Unsupported control request subtype: {subtype}"}}}]


def init_message(session="s-1", model="claude-test-1"):
    return {"dir": "out", "json": {"type": "system", "subtype": "init", "session_id": session,
                                   "model": model, "tools": ["Bash", "Read"],
                                   "permissionMode": "bypassPermissions"}}


def result_message(**fields):
    body = {"type": "result", "subtype": "success", "is_error": False, "result": "done",
            "session_id": "s-1", "usage": {"input_tokens": 1, "output_tokens": 2},
            "total_cost_usd": 0.5}
    body.update(fields)
    return {"dir": "out", "json": body}


class RobustnessTest(unittest.TestCase):

    def test_a_line_that_is_not_json_is_skipped(self):
        proc = FakeClaude(script(
            init_message(),
            {"dir": "out_raw", "text": "npm notice: a new version is available"},
            {"dir": "out_raw", "text": "[1,2,3]"},                    # JSON, but not an object
            {"dir": "out", "json": {"type": "stream_event", "event": {
                "type": "content_block_delta", "index": 0,
                "delta": {"type": "text_delta", "text": "hi"}}}},
            result_message(result="hi")))
        harness, events, result, _, _ = run_turn(self, proc, "hello")
        self.addCleanup(harness.close)
        self.assertEqual(events.text(), "hi")
        self.assertEqual(result.text, "hi")

    def test_an_unknown_message_type_is_ignored(self):
        proc = FakeClaude(script(
            init_message(),
            {"dir": "out", "json": {"type": "something_new", "payload": {"a": 1}}},
            {"dir": "out", "json": {"type": "rate_limit_event", "rate_limit_info": {}}},
            result_message()))
        harness, events, result, _, _ = run_turn(self, proc, "hello")
        self.addCleanup(harness.close)
        self.assertEqual(result.stop_reason, "end")
        self.assertEqual(events.kinds, ["started", "usage", "done"])

    def test_a_running_tool_streams_nothing_and_tool_progress_is_not_faked(self):
        """GT7X t:a3. The contract has `tool_output` for what a call prints while it runs, and
        this adapter never emits it: 2.1.278 sends a host no such text.

        The one per-tool frame there is, `tool_progress`, carries `elapsed_time_seconds` and no
        output at all — the CLI's internal `bash_progress` has `output`/`fullOutput`, and its
        stream-json serialiser drops both. Elapsed seconds are not output, so they are ignored
        here rather than dressed up as some. See the module docstring for what else was checked
        (`--include-partial-messages`, `--include-hook-events`).
        """
        proc = FakeClaude(script(
            init_message(),
            {"dir": "out", "json": {"type": "assistant", "message": {
                "model": "claude-test-1", "content": [
                    {"type": "tool_use", "id": "t1", "name": "Bash",
                     "input": {"command": "make -j8"}}]}}},
            {"dir": "out", "json": {"type": "tool_progress", "tool_use_id": "t1",
                                    "tool_name": "Bash", "parent_tool_use_id": None,
                                    "elapsed_time_seconds": 5, "session_id": "s-1",
                                    "uuid": "u-1"}},
            {"dir": "out", "json": {"type": "tool_progress", "tool_use_id": "t1",
                                    "tool_name": "Bash", "parent_tool_use_id": None,
                                    "elapsed_time_seconds": 10, "heartbeat": True,
                                    "session_id": "s-1", "uuid": "u-2"}},
            {"dir": "out", "json": {"type": "user", "message": {"content": [
                {"type": "tool_result", "tool_use_id": "t1", "content": "built\n"}]}}},
            result_message(result="built")))
        harness, events, result, _, _ = run_turn(self, proc, "build it")
        self.addCleanup(harness.close)
        self.assertEqual(result.stop_reason, "end")
        self.assertNotIn("tool_output", events.kinds)
        self.assertEqual(events.kinds,
                         ["started", "tool_started", "tool_result", "usage", "done"])
        self.assertEqual(events.of("tool_result")[0].data["output"], "built\n")

    def test_the_process_dying_mid_turn_is_an_error_with_its_stderr(self):
        proc = FakeClaude(script(init_message()), stderr="claude: out of memory\n",
                          hang_at_end=False)
        spawner, harness = harness_on(proc)
        self.addCleanup(harness.close)
        harness.start(cwd=os.getcwd())
        with self.assertRaises(HarnessError) as caught:
            harness.send("hello", emit=Collector(), cancel=threading.Event())
        self.assertIn("out of memory", str(caught.exception))

    def test_a_result_with_is_error_raises_and_says_so(self):
        proc = FakeClaude(script(
            init_message(),
            result_message(subtype="error_during_execution", is_error=True, result=None,
                           terminal_reason="completed",
                           errors=["the API refused the request"])))
        spawner, harness = harness_on(proc)
        self.addCleanup(harness.close)
        harness.start(cwd=os.getcwd())
        events = Collector()
        with self.assertRaises(HarnessError) as caught:
            harness.send("hello", emit=events, cancel=threading.Event())
        self.assertIn("the API refused the request", str(caught.exception))
        error = events.of("error")[0].data
        self.assertIn("the API refused the request", error["text"])
        self.assertEqual(error["code"], "error_during_execution")

    def test_a_thinking_delta_is_its_own_event(self):
        proc = FakeClaude(script(
            init_message(),
            {"dir": "out", "json": {"type": "stream_event", "event": {
                "type": "content_block_delta", "index": 0,
                "delta": {"type": "thinking_delta", "thinking": "weighing it up"}}}},
            {"dir": "out", "json": {"type": "stream_event", "event": {
                "type": "content_block_delta", "index": 0,
                "delta": {"type": "signature_delta", "signature": "abc"}}}},
            result_message(result="fine")))
        harness, events, _, _, _ = run_turn(self, proc, "hello")
        self.addCleanup(harness.close)
        self.assertEqual(events.of("thinking")[0].data["text"], "weighing it up")
        self.assertEqual(events.kinds, ["started", "thinking", "usage", "done"])

    def test_an_assistant_text_block_is_printed_when_no_delta_came(self):
        proc = FakeClaude(script(
            init_message(),
            {"dir": "out", "json": {"type": "assistant", "message": {
                "role": "assistant", "model": "claude-test-1",
                "content": [{"type": "text", "text": "whole answer"}]}}},
            result_message(result="whole answer")))
        harness, events, result, _, _ = run_turn(self, proc, "hello")
        self.addCleanup(harness.close)
        self.assertEqual(events.text(), "whole answer")
        self.assertEqual(result.text, "whole answer")

    def test_a_question_tool_becomes_a_question_event(self):
        questions = [{"header": "Colour", "question": "Which one?",
                      "options": [{"label": "red"}, {"label": "blue"}]}]
        proc = FakeClaude(script(
            init_message(),
            {"dir": "out", "json": {"type": "control_request", "request_id": "q-1", "request": {
                "subtype": "can_use_tool", "tool_name": "AskUserQuestion",
                "input": {"questions": questions}, "tool_use_id": "toolu_q"}}},
            {"dir": "in", "json": {"type": "control_response"}},
            result_message()))
        spawner, harness = harness_on(proc)
        self.addCleanup(harness.close)
        harness.start(cwd=os.getcwd(), permissions="ask")
        events = Collector()

        def emit(event):
            events(event)
            if event.kind == "question":
                harness.answer(event.data["id"], {"answers": [["red"]]})

        harness.send("pick one", emit=emit, cancel=threading.Event())
        question = events.of("question")[0].data
        self.assertEqual(question["id"], "q-1")
        self.assertEqual(question["questions"], questions)
        reply = [m for m in proc.written if m.get("type") == "control_response"][0]
        self.assertEqual(reply["response"]["request_id"], "q-1")
        self.assertIn("red", reply["response"]["response"]["message"])

    def test_an_unanswered_approval_is_denied_when_the_turn_ends(self):
        proc = FakeClaude(script(
            init_message(),
            {"dir": "out", "json": {"type": "control_request", "request_id": "a-1", "request": {
                "subtype": "can_use_tool", "tool_name": "Bash",
                "input": {"command": "rm -rf /"}, "tool_use_id": "toolu_a"}}},
            result_message(),
            {"dir": "in", "json": {"type": "control_response"}}))
        spawner, harness = harness_on(proc)
        self.addCleanup(harness.close)
        harness.start(cwd=os.getcwd(), permissions="ask")
        events = Collector()
        harness.send("go", emit=events, cancel=threading.Event())
        self.assertEqual(events.of("approval")[0].data["kind"], "command")
        self.assertIn("rm -rf /", events.of("approval")[0].data["detail"])
        reply = [m for m in proc.written if m.get("type") == "control_response"][0]
        self.assertEqual(reply["response"]["response"]["behavior"], "deny")

    def test_a_control_request_we_do_not_handle_is_refused_not_ignored(self):
        proc = FakeClaude(script(
            init_message(),
            {"dir": "out", "json": {"type": "control_request", "request_id": "z-1",
                                    "request": {"subtype": "something_new"}}},
            {"dir": "in", "json": {"type": "control_response"}},
            result_message()))
        harness, events, _, _, _ = run_turn(self, proc, "hello")
        self.addCleanup(harness.close)
        reply = [m for m in proc.written if m.get("type") == "control_response"][0]
        self.assertEqual(reply["response"]["subtype"], "error")
        self.assertEqual(reply["response"]["request_id"], "z-1")


# ----- starting, the command line, the environment ----------------------------------------------


class StartTest(unittest.TestCase):

    def test_no_claude_on_path_is_not_available(self):
        with tempfile.TemporaryDirectory(dir="/tmp") as empty:
            old = os.environ.get("PATH")
            os.environ["PATH"] = empty
            try:
                harness = gh.ClaudeHarness(spawn=Spawner(), binary="claude")
                with self.assertRaises(HarnessNotAvailable) as caught:
                    harness.start(cwd=os.getcwd())
            finally:
                if old is None:
                    os.environ.pop("PATH", None)
                else:
                    os.environ["PATH"] = old
        self.assertIn("not installed", str(caught.exception))

    def test_a_missing_directory_is_not_available(self):
        harness = gh.ClaudeHarness(spawn=Spawner(), binary=_on_path())
        with self.assertRaises(HarnessNotAvailable):
            harness.start(cwd="/tmp/claude-1000/hclaude/does-not-exist")

    def test_the_child_environment_drops_this_session(self):
        env = gh.child_environment({
            "PATH": "/usr/bin", "CLAUDECODE": "1", "CLAUDE_EFFORT": "high",
            "CLAUDE_CODE_CHILD_SESSION": "yes", "CLAUDE_CODE_ENTRYPOINT": "cli",
            "ANTHROPIC_API_KEY": "keep-me", "HOME": "/home/user"})
        self.assertEqual(sorted(env), ["ANTHROPIC_API_KEY", "HOME", "PATH"])

    def test_the_spawned_process_gets_that_environment(self):
        proc = FakeClaude(script(init_message(), result_message()))
        spawner, harness = harness_on(proc)
        self.addCleanup(harness.close)
        os.environ["CLAUDE_CODE_CHILD_SESSION"] = "yes"
        os.environ["CLAUDECODE"] = "1"
        try:
            harness.start(cwd=os.getcwd())
        finally:
            os.environ.pop("CLAUDE_CODE_CHILD_SESSION", None)
            os.environ.pop("CLAUDECODE", None)
        env = spawner.calls[0][2]
        self.assertNotIn("CLAUDE_CODE_CHILD_SESSION", env)
        self.assertNotIn("CLAUDECODE", env)
        self.assertIn("PATH", env)

    def _argv_for(self, **kwargs):
        proc = FakeClaude(script(init_message(), result_message()))
        spawner, harness = harness_on(proc)
        self.addCleanup(harness.close)
        harness.start(cwd=os.getcwd(), **kwargs)
        return spawner.calls[0][0]

    def test_board_bridge_is_launch_local_and_survives_resume(self):
        descriptor = {"command": "/python", "args": ["/proxy", "/capability"]}
        argv = self._argv_for(board_bridge=descriptor, resume="saved-session")
        self.assertEqual(json.loads(argv[argv.index("--mcp-config") + 1]),
                         {"mcpServers": {"relay_board": descriptor}})
        self.assertNotIn("--strict-mcp-config", argv)

    def test_guest_instructions_append_to_defaults_on_start_resume_and_fork(self):
        text = "Relay guest context\nUse only the tools actually offered."
        for options in ({}, {"resume": "saved"}, {"resume": "saved", "fork": True}):
            argv = self._argv_for(instructions=text, **options)
            self.assertEqual(argv[argv.index("--append-system-prompt") + 1], text)
            self.assertNotIn("--system-prompt", argv)
        self.assertNotIn("--append-system-prompt", self._argv_for())

    def test_the_bypass_command_line(self):
        argv = self._argv_for()
        for flag in ("-p", "--input-format", "stream-json", "--output-format", "--verbose",
                     "--include-partial-messages", "--permission-mode", "bypassPermissions",
                     "--dangerously-skip-permissions", "--session-id"):
            self.assertIn(flag, argv)
        self.assertNotIn("--resume", argv)

    def test_the_deny_command_line(self):
        argv = self._argv_for(permissions="deny")
        self.assertEqual(argv[argv.index("--permission-prompts") + 1], "none")

    def test_the_effort_is_a_flag_when_one_is_asked_for(self):
        argv = self._argv_for(effort="high")
        self.assertEqual(argv[argv.index("--effort") + 1], "high")

    def test_no_effort_flag_when_none_is_asked_for(self):
        self.assertNotIn("--effort", self._argv_for())

    def test_an_effort_claude_does_not_have_is_refused_before_the_process_starts(self):
        harness = gh.ClaudeHarness(spawn=Spawner(), binary=_on_path())
        with self.assertRaises(HarnessError) as caught:
            harness.start(cwd=os.getcwd(), effort="ultra")
        self.assertIn("low, medium, high, xhigh, max", str(caught.exception))

    def test_resume_and_fork(self):
        argv = self._argv_for(resume="abc-123", fork=True, model="sonnet")
        self.assertEqual(argv[argv.index("--resume") + 1], "abc-123")
        self.assertIn("--fork-session", argv)
        self.assertEqual(argv[argv.index("--model") + 1], "sonnet")
        self.assertNotIn("--session-id", argv)

    def test_a_settings_file_is_handed_over(self):
        proc = FakeClaude(script(init_message(), result_message()))
        spawner = Spawner(proc)
        harness = gh.ClaudeHarness(spawn=spawner, binary=_on_path(), settings="/tmp/s.json")
        self.addCleanup(harness.close)
        harness.start(cwd=os.getcwd())
        argv = spawner.calls[0][0]
        self.assertEqual(argv[argv.index("--settings") + 1], "/tmp/s.json")

    def test_a_bad_permission_posture_is_refused(self):
        harness = gh.ClaudeHarness(spawn=Spawner(), binary=_on_path())
        with self.assertRaises(ValueError):
            harness.start(cwd=os.getcwd(), permissions="whatever")

    def test_resuming_names_the_session_before_the_first_turn(self):
        proc = FakeClaude(script(init_message(), result_message()))
        spawner, harness = harness_on(proc)
        self.addCleanup(harness.close)
        start = harness.start(cwd=os.getcwd(), resume="abc-123")
        self.assertEqual(start.session_id, "abc-123")


class SetModelTest(unittest.TestCase):

    def _harness(self, *procs):
        spawner = Spawner(*procs)
        harness = gh.ClaudeHarness(spawn=spawner, binary=_on_path())
        self.addCleanup(harness.close)
        return spawner, harness

    def test_set_model_is_a_control_request(self):
        proc = FakeClaude([
            {"dir": "in", "json": {"type": "control_request", "request_id": "h1",
                                   "request": {"subtype": "initialize"}}},
            {"dir": "out", "json": {"type": "control_response", "response": {
                "subtype": "success", "request_id": "h1", "response": {}}}},
            {"dir": "in", "json": {"type": "control_request", "request_id": "m1",
                                   "request": {"subtype": "set_model"}}},
            {"dir": "out", "json": {"type": "control_response", "response": {
                "subtype": "success", "request_id": "m1", "response": None}}}])
        spawner, harness = self._harness(proc)
        harness.start(cwd=os.getcwd())
        self.assertEqual(harness.set_model("sonnet"), "sonnet")
        self.assertEqual(harness.model, "sonnet")
        asked = [m for m in proc.written if m.get("type") == "control_request"][-1]
        self.assertEqual(asked["request"], {"subtype": "set_model", "model": "sonnet"})

    def test_an_old_cli_without_set_model_is_restarted_on_the_session(self):
        """After a turn there is a transcript to resume, and the restart resumes it."""
        first = FakeClaude(script(init_message(), result_message()) + _unsupported("set_model"))
        second = FakeClaude(_handshake("h2"))
        spawner, harness = self._harness(first, second)
        harness.start(cwd=os.getcwd(), instructions="Relay guest context")
        harness.send("hello", emit=Collector(), cancel=threading.Event())
        self.assertEqual(harness.set_model("opus"), "opus")
        self.assertEqual(len(spawner.calls), 2)
        argv = spawner.calls[1][0]
        self.assertEqual(argv[argv.index("--model") + 1], "opus")
        # The id the CLI reported on `init`, which is the one its transcript is filed under.
        self.assertEqual(argv[argv.index("--resume") + 1], harness.session_id)
        self.assertNotIn("--session-id", argv)
        self.assertEqual(argv[argv.index("--append-system-prompt") + 1], "Relay guest context")

    def test_a_restart_before_the_first_turn_keeps_the_id_and_does_not_resume(self):
        """`--resume <id>` on a session claude has never written exits 1 ("No conversation found
        with session ID", measured against 2.1.278), and there is nothing to carry over, so the
        replacement is a fresh process under the same id."""
        first = FakeClaude(_handshake("h1") + _unsupported("set_model"))
        second = FakeClaude(_handshake("h2"))
        spawner, harness = self._harness(first, second)
        start = harness.start(cwd=os.getcwd(), instructions="Relay guest context")
        self.assertEqual(harness.set_model("opus"), "opus")
        argv = spawner.calls[1][0]
        self.assertNotIn("--resume", argv)
        self.assertEqual(argv[argv.index("--session-id") + 1], start.session_id)
        self.assertEqual(argv[argv.index("--append-system-prompt") + 1], "Relay guest context")


class SetEffortTest(unittest.TestCase):
    """The effort is a command-line flag and 2.1.278 has no control request for it (probed
    2026-09-19), so changing it restarts the process on the same session."""

    def _harness(self, *procs):
        spawner = Spawner(*procs)
        harness = gh.ClaudeHarness(spawn=spawner, binary=_on_path())
        self.addCleanup(harness.close)
        return spawner, harness

    def test_set_effort_restarts_with_the_new_flag_and_resumes(self):
        first = FakeClaude(script(init_message(), result_message()))
        second = FakeClaude(_handshake("h2"))
        spawner, harness = self._harness(first, second)
        harness.start(cwd=os.getcwd(), effort="low", instructions="Relay guest context")
        harness.send("hello", emit=Collector(), cancel=threading.Event())
        self.assertEqual(harness.set_effort("xhigh"), "xhigh")
        self.assertEqual(harness.effort, "xhigh")
        self.assertEqual(len(spawner.calls), 2)
        argv = spawner.calls[1][0]
        self.assertEqual(argv[argv.index("--effort") + 1], "xhigh")
        self.assertEqual(argv[argv.index("--resume") + 1], harness.session_id)
        self.assertEqual(argv[argv.index("--append-system-prompt") + 1], "Relay guest context")
        # No control request was tried: the CLI has none, and asking would only log an error.
        self.assertEqual([m["request"]["subtype"] for m in first.written
                          if m.get("type") == "control_request"], ["initialize"])

    def test_the_same_effort_again_changes_nothing(self):
        proc = FakeClaude(_handshake("h1"))
        spawner, harness = self._harness(proc)
        harness.start(cwd=os.getcwd(), effort="high")
        self.assertEqual(harness.set_effort("HIGH"), "high")
        self.assertEqual(len(spawner.calls), 1)

    def test_an_effort_claude_does_not_have_is_refused(self):
        proc = FakeClaude(_handshake("h1"))
        _, harness = self._harness(proc)
        harness.start(cwd=os.getcwd())
        with self.assertRaises(HarnessError) as caught:
            harness.set_effort("ultra")
        self.assertIn("xhigh", str(caught.exception))
        self.assertEqual(harness.effort, "")

    def test_a_shapeless_effort_is_refused_by_the_contract(self):
        proc = FakeClaude(_handshake("h1"))
        _, harness = self._harness(proc)
        harness.start(cwd=os.getcwd())
        with self.assertRaises(ValueError):
            harness.set_effort("HIGH; rm -rf /")

    def test_an_effort_asked_for_during_a_turn_lands_on_the_next_one(self):
        """The running turn keeps the effort it started with — a restart would kill it — and the
        replacement process is started at the top of the next `send()`."""
        first = FakeClaude(script(init_message(), result_message()))
        second = FakeClaude(script(init_message(), result_message(), request_id="h2"))
        spawner, harness = self._harness(first, second)
        harness.start(cwd=os.getcwd(), effort="low")

        asked = threading.Event()

        class Ask(Collector):
            def __call__(self, event):
                super().__call__(event)
                if event.kind == "started" and not asked.is_set():
                    asked.set()
                    threading.Thread(target=harness.set_effort, args=("max",)).start()

        events = Ask()
        harness.send("first", emit=events, cancel=threading.Event())
        self.assertEqual(len(spawner.calls), 1)          # the turn ran on --effort low
        self.assertEqual(spawner.calls[0][0][spawner.calls[0][0].index("--effort") + 1], "low")
        for _ in range(100):                             # the thread may not have run yet
            if harness.effort == "max":
                break
            time.sleep(0.01)
        harness.send("second", emit=Collector(), cancel=threading.Event())
        self.assertEqual(len(spawner.calls), 2)
        argv = spawner.calls[1][0]
        self.assertEqual(argv[argv.index("--effort") + 1], "max")


class ModelsTest(unittest.TestCase):

    def test_the_aliases_and_their_levels(self):
        harness = gh.ClaudeHarness(spawn=Spawner(), binary=_on_path())
        rows = harness.models()
        self.assertEqual([row["id"] for row in rows], list(gh.MODEL_ALIASES))
        # The name is the model the alias points at (card #MDL1), so the picker shows one row for
        # it whether it comes from here, the Anthropic API or OpenRouter. `label` is the same.
        self.assertEqual([row["name"] for row in rows],
                         ["claude-fable-5.1", "claude-opus-5", "claude-sonnet-5", "claude-haiku-4.5"])
        self.assertEqual([row["label"] for row in rows], [row["name"] for row in rows])
        for row in rows:
            self.assertEqual(row["efforts"], ["low", "medium", "high", "xhigh", "max"])
            self.assertIsNone(row["default_effort"])
            self.assertNotIn("current", row)

    def test_the_running_full_name_is_added_and_marked_current(self):
        proc = FakeClaude(script(init_message(model="claude-haiku-4-5-20251001"),
                                 result_message()))
        harness, _, _, _, _ = run_turn(self, proc, "hello")
        self.addCleanup(harness.close)
        rows = harness.models()
        self.assertEqual(rows[-1]["id"], "claude-haiku-4-5-20251001")
        self.assertTrue(rows[-1]["current"])
        self.assertEqual(rows[-1]["efforts"], list(gh.EFFORTS))

    def test_an_alias_in_force_is_the_one_marked_current(self):
        proc = FakeClaude(script(init_message(model="sonnet"), result_message()))
        harness, _, _, _, _ = run_turn(self, proc, "hello")
        self.addCleanup(harness.close)
        current = [row for row in harness.models() if row.get("current")]
        self.assertEqual([row["id"] for row in current], ["sonnet"])


# ----- the pieces on their own ------------------------------------------------------------------


class ToolNameTest(unittest.TestCase):

    def test_claude_tools_land_on_relays_names(self):
        for name, want in (("Bash", "run_command"), ("Read", "read_file"),
                           ("Write", "write_file"), ("Edit", "edit_file"),
                           ("MultiEdit", "edit_file"), ("Glob", "list_directory"),
                           ("Grep", "search"), ("WebFetch", "web"), ("Task", "agent"),
                           ("mcp__thing__do", "other"), ("", "other")):
            self.assertEqual(guest_harness.map_tool_name("claude", name), want, name)
            self.assertIn(guest_harness.map_tool_name("claude", name), guest_harness.TOOL_NAMES)


class DiffTest(unittest.TestCase):

    def test_a_structured_patch_is_rendered(self):
        diff = gh.tool_diff("Edit", {"file_path": "/w/a.py"}, {
            "filePath": "/w/a.py",
            "structuredPatch": [{"oldStart": 1, "oldLines": 2, "newStart": 1, "newLines": 2,
                                 "lines": [" import os", "-x = 1", "+x = 2"]}]})
        self.assertIn("--- a//w/a.py", diff)
        self.assertIn("@@ -1,2 +1,2 @@", diff)
        self.assertIn("-x = 1", diff)
        self.assertIn("+x = 2", diff)

    def test_a_created_file_is_a_diff_against_nothing(self):
        diff = gh.tool_diff("Write", {"file_path": "/w/new.txt", "content": "ok\n"},
                            {"type": "create", "filePath": "/w/new.txt", "content": "ok\n",
                             "structuredPatch": [], "originalFile": None})
        self.assertIn("+ok", diff)

    def test_an_edit_falls_back_to_its_own_input(self):
        diff = gh.tool_diff("Edit", {"file_path": "/w/a.py", "old_string": "a\n",
                                     "new_string": "b\n"}, None)
        self.assertIn("-a", diff)
        self.assertIn("+b", diff)

    def test_a_multiedit_diffs_every_edit(self):
        diff = gh.tool_diff("MultiEdit", {"file_path": "/w/a.py", "edits": [
            {"old_string": "a\n", "new_string": "b\n"},
            {"old_string": "c\n", "new_string": "d\n"}]}, None)
        self.assertIn("+b", diff)
        self.assertIn("+d", diff)

    def test_a_command_has_no_diff(self):
        self.assertEqual(gh.tool_diff("Bash", {"command": "ls"}, {"stdout": "a"}), "")


class UsageTest(unittest.TestCase):

    def test_context_pct_comes_from_the_models_window(self):
        data = gh._usage_event({"usage": {"input_tokens": 10, "output_tokens": 5,
                                          "cache_read_input_tokens": 90,
                                          "cache_creation_input_tokens": 100},
                                "total_cost_usd": 1.5,
                                "modelUsage": {"claude-x": {"contextWindow": 1000}}},
                               {"input_tokens": 10, "cache_read_input_tokens": 90,
                                "cache_creation_input_tokens": 100})
        self.assertEqual(data["context_pct"], 20.0)
        self.assertEqual(data["model"], "claude-x")
        self.assertEqual(data["cost_usd"], 1.5)

    def test_no_window_means_no_percentage(self):
        data = gh._usage_event({"usage": {"input_tokens": 10, "output_tokens": 5}})
        self.assertNotIn("context_pct", data)
        self.assertNotIn("context_window", data)
        self.assertNotIn("context_tokens", data)
        self.assertEqual(data["input_tokens"], 10)

    def test_the_window_and_what_is_in_it_travel_beside_the_share(self):
        """GT7X t:a3: a chip that can say "13k of 258k", not only the share of it."""
        data = gh._usage_event({"usage": {"input_tokens": 10, "output_tokens": 5,
                                          "cache_read_input_tokens": 90,
                                          "cache_creation_input_tokens": 100},
                                "modelUsage": {"claude-x": {"contextWindow": 1000}}},
                               {"input_tokens": 10, "cache_read_input_tokens": 90,
                                "cache_creation_input_tokens": 100})
        self.assertEqual(data["context_window"], 1000)
        self.assertEqual(data["context_tokens"], 200)      # 10 + 90 + 100, the last request
        self.assertEqual(data["context_pct"], 20.0)

    def test_a_window_with_nothing_in_it_yet_is_still_reported(self):
        data = gh._usage_event({"usage": {"output_tokens": 5},
                                "modelUsage": {"claude-x": {"contextWindow": 1000}}})
        self.assertEqual(data["context_window"], 1000)
        self.assertNotIn("context_tokens", data)
        self.assertNotIn("context_pct", data)


# One `rate_limit_event` exactly as 2.1.278 wrote it on 2026-09-20 (`claude -p "Reply with the
# single word ok." --output-format stream-json --verbose --include-partial-messages`), ids
# redacted. `utilization` is a 0-1 fraction; `resetsAt` is unix seconds; the top-level
# `rateLimitType`/`resetsAt` name the window that governs and carry no figure of their own.
RATE_LIMIT_EVENT = {
    "type": "rate_limit_event",
    "rate_limit_info": {"status": "allowed", "resetsAt": 1789926600, "rateLimitType": "five_hour",
                        "overageStatus": "rejected", "overageDisabledReason": "org_level_disabled",
                        "isUsingOverage": False,
                        "unifiedWindows": {"five_hour": {"utilization": 0.05,
                                                         "resetsAt": 1789926600},
                                           "seven_day": {"utilization": 0.01,
                                                         "resetsAt": 1790499600}}},
    "uuid": "00000000-0000-0000-0000-000000000000",
    "session_id": "00000000-0000-0000-0000-000000000001",
}


class LimitsTest(unittest.TestCase):
    """`rate_limit_event` is the subscription's rolling windows and becomes `limits` (29.1)."""

    def test_the_real_event_becomes_two_windows_in_percent(self):
        data = gh._limits_event(RATE_LIMIT_EVENT)
        self.assertEqual(data, {"status": "allowed",
                                "windows": [{"kind": "5h", "used_percent": 5.0,
                                             "resets_at": 1789926600},
                                            {"kind": "weekly", "used_percent": 1.0,
                                             "resets_at": 1790499600}]})

    def test_an_event_naming_no_window_is_nothing(self):
        self.assertEqual(gh._limits_event({"type": "rate_limit_event",
                                           "rate_limit_info": {"status": "allowed",
                                                               "rateLimitType": "five_hour"}}),
                         {})
        self.assertEqual(gh._limits_event({"type": "rate_limit_event"}), {})
        # A window with no figure is skipped; the other still counts. Utilization is clamped.
        data = gh._limits_event({"rate_limit_info": {"unifiedWindows": {
            "five_hour": {"resetsAt": 5}, "seven_day": {"utilization": 1.7, "resetsAt": 0}}}})
        self.assertEqual(data["windows"], [{"kind": "weekly", "used_percent": 100.0,
                                            "resets_at": None}])

    def test_dispatch_emits_it_as_a_limits_event(self):
        harness = gh.ClaudeHarness(spawn=Spawner(), binary=_on_path())
        events = Collector()
        harness._dispatch(dict(RATE_LIMIT_EVENT), {"text": [], "saw_delta": False,
                                                   "asked_stop": False}, events)
        self.assertEqual(events.kinds, ["limits"])
        self.assertEqual(events.of("limits")[0].data["windows"][0]["kind"], "5h")
        self.assertEqual(events.of("limits")[0].data["windows"][0]["used_percent"], 5.0)

    def test_every_recorded_turn_reports_its_limits(self):
        """The CLI sends one per turn, after the model's `message_stop`; each recorded turn has
        it, and it lands before `done`."""
        proc = FakeClaude(load("hello"))
        harness, events, _, _, _ = run_turn(self, proc, "Reply with the single word ok.")
        self.addCleanup(harness.close)
        self.assertEqual(events.kinds.count("limits"), 1)
        self.assertLess(events.kinds.index("limits"), events.kinds.index("done"))
        windows = events.of("limits")[0].data["windows"]
        self.assertEqual([w["kind"] for w in windows], ["5h", "weekly"])

    def test_a_figure_that_arrived_between_turns_is_reported_with_the_next(self):
        """Only the newest one: an older figure is not a figure any more."""
        proc = FakeClaude(load("hello"))
        spawner, harness = harness_on(proc)
        self.addCleanup(harness.close)
        harness.start(cwd=os.getcwd())
        older = json.loads(json.dumps(RATE_LIMIT_EVENT))
        older["rate_limit_info"]["unifiedWindows"]["five_hour"]["utilization"] = 0.4
        newer = json.loads(json.dumps(RATE_LIMIT_EVENT))
        newer["rate_limit_info"]["unifiedWindows"]["five_hour"]["utilization"] = 0.62
        harness._inbox.put(older)
        harness._inbox.put({"type": "prompt_suggestion", "text": "stale, dropped"})
        harness._inbox.put(newer)
        events = Collector()
        harness.send("Reply with the single word ok.", emit=events, cancel=threading.Event())
        limits = events.of("limits")
        self.assertEqual(limits[0].data["windows"][0]["used_percent"], 62.0)
        self.assertLess(events.kinds.index("limits"), events.kinds.index("delta"))
        # The held one, then the recorded turn's own; never the older one.
        self.assertEqual(len(limits), 2)
        self.assertNotIn(40.0, [w["used_percent"] for e in limits for w in e.data["windows"]])


class AttachmentTest(unittest.TestCase):

    def test_an_image_travels_as_a_base64_block(self):
        proc = FakeClaude(script(init_message(), result_message()))
        spawner, harness = harness_on(proc)
        self.addCleanup(harness.close)
        harness.start(cwd=os.getcwd())
        harness.send("what is this?", attachments=[
            {"kind": "image", "media_type": "image/png", "data": "QUJD"},
            {"kind": "file", "path": "/tmp/x"}], emit=Collector(),
            cancel=threading.Event())
        content = [m for m in proc.written if m.get("type") == "user"][0]["message"]["content"]
        self.assertEqual(content[0], {"type": "text", "text": "what is this?"})
        self.assertEqual(content[1], {"type": "image", "source": {
            "type": "base64", "media_type": "image/png", "data": "QUJD"}})
        self.assertEqual(len(content), 2)


class ContractTest(unittest.TestCase):

    def test_the_adapter_has_every_member_the_contract_names(self):
        for name in ("start", "send", "interrupt", "set_model", "set_effort", "models",
                     "compact", "answer", "close", "session_id", "model", "guest"):
            self.assertTrue(hasattr(gh.ClaudeHarness, name), name)
        self.assertEqual(gh.ClaudeHarness.guest, "claude")
        self.assertIn(gh.GUEST, guest_harness.HARNESS_GUESTS)

    def test_every_permission_posture_has_a_command_line(self):
        self.assertEqual(sorted(gh.PERMISSION_FLAGS), sorted(guest_harness.PERMISSIONS))

    def test_close_is_idempotent(self):
        proc = FakeClaude(script(init_message(), result_message()))
        spawner, harness = harness_on(proc)
        harness.start(cwd=os.getcwd())
        harness.close()
        harness.close()
        self.assertTrue(proc.stdin.closed)

    def test_interrupt_before_a_turn_is_a_no_op(self):
        harness = gh.ClaudeHarness(spawn=Spawner(), binary=_on_path())
        harness.interrupt()                      # nothing started: must not raise
        harness.compact()
        harness.close()


class LoginStatusTest(unittest.TestCase):
    """`claude auth status --json` (2.1.278), as the worker's background scan reads it (29.3)."""

    SIGNED_IN = json.dumps({"loggedIn": True, "authMethod": "claude.ai", "apiProvider": "firstParty",
                            "analyticsDisabled": False, "email": "u@example.com",
                            "subscriptionType": "max"}, indent=2)

    def test_the_json_field_is_the_answer(self):
        self.assertIs(gh.parse_login_status(0, self.SIGNED_IN), True)
        self.assertIs(gh.parse_login_status(1, self.SIGNED_IN.replace("true", "false", 1)), False)
        # The field wins over the exit status either way.
        self.assertIs(gh.parse_login_status(1, self.SIGNED_IN), True)
        self.assertIs(gh.parse_login_status(0, '{"loggedIn": false}'), False)

    def test_text_output_falls_back_to_the_exit_status(self):
        self.assertIs(gh.parse_login_status(0, "Logged in as u@example.com\n"), True)
        self.assertIs(gh.parse_login_status(1, "", "Not logged in\n"), False)
        self.assertIs(gh.parse_login_status(0, "Not logged in. Run claude auth login.\n"), False)
        self.assertIs(gh.parse_login_status(127, ""), False)

    def test_the_command_and_the_probe_flags(self):
        self.assertEqual(gh.LOGIN_STATUS_ARGS, ("auth", "status", "--json"))
        harness = gh.ClaudeHarness.for_probe(spawn=Spawner(), binary="claude")
        argv = harness._argv(model=None, session_id="s", resume=None, fork=False,
                             permissions="deny")
        self.assertEqual(argv[-2:], ["--tools", ""])       # no built-in tool for the key test


if __name__ == "__main__":
    unittest.main()
