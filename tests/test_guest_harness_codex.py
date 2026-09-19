# SPDX-License-Identifier: GPL-3.0-or-later
"""Tier A: the Codex harness adapter, replayed against recorded `codex app-server` transcripts.

Nothing here starts a real codex. The fixtures under `tests/fixtures/guest_harness_codex/` are
real, redacted transcripts (see `docs/qa_evidence/2026-09-19-claude-codex-guest-integration/
harness-codex-README.md` for how they were recorded); `ReplayProcess` below plays the server half
back in order, rewriting the recorded request ids onto the ids the adapter actually used, so the
adapter is exercised against exactly the bytes codex sent.

    PYTHONPATH=backend python3 -m unittest tests.test_guest_harness_codex
"""
from __future__ import annotations

import json
import os
import stat
import sys
import tempfile
import threading
import unittest
from collections import deque

sys.path.insert(0, os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
                                "backend"))

from relay_core.guest_harness import (HarnessError, HarnessEvent, HarnessNotAvailable,
                                      map_tool_name)
from relay_core import guest_harness_codex as gh

FIXTURES = os.path.join(os.path.dirname(os.path.abspath(__file__)), "fixtures",
                        "guest_harness_codex")


# ----- the fake process --------------------------------------------------------------------------


def load(name: str) -> list[dict]:
    """The recorded transcript as a list of {"dir": "->"|"<-"|"<-raw", "line": ...}."""
    with open(os.path.join(FIXTURES, name), encoding="utf-8") as handle:
        return [json.loads(line) for line in handle if line.strip()]


def index_of(entries: list[dict], direction: str, method: str, occurrence: int = 0) -> int:
    seen = 0
    for i, entry in enumerate(entries):
        line = entry["line"]
        if entry["dir"] != direction or not isinstance(line, dict):
            continue
        if line.get("method") == method:
            if seen == occurrence:
                return i
            seen += 1
    raise AssertionError(f"no {direction} {method} in the transcript")


def server(method: str, params: dict) -> dict:
    return {"dir": "<-", "line": {"method": method, "params": params}}


class _Stdin:
    def __init__(self, proc):
        self._proc = proc
        self._buffer = ""
        self.closed = False

    def write(self, text):
        if self.closed:
            raise ValueError("stdin is closed")
        self._buffer += text
        while "\n" in self._buffer:
            line, self._buffer = self._buffer.split("\n", 1)
            if line.strip():
                self._proc.on_write(json.loads(line))

    def flush(self):
        pass

    def close(self):
        self.closed = True


class _Stdout:
    def __init__(self, proc):
        self._proc = proc

    def readline(self):
        return self._proc.read_line()


class ReplayProcess:
    """Plays a recorded transcript back, in order, as if it were `codex app-server`.

    Each line the adapter writes consumes the next recorded `->` line (whose method is checked),
    and then every recorded `<-` line up to the following `->` is released. Recorded response ids
    are rewritten onto the ids the adapter used, so the adapter's own numbering is free.
    """

    def __init__(self, entries, *, stderr: str = "", die_when_exhausted: bool = False):
        self.entries = list(entries)
        self.position = 0
        self.writes: list[dict] = []
        self.methods: list[tuple[str, str]] = []
        self.die_when_exhausted = die_when_exhausted
        self._out: deque = deque()
        self._cond = threading.Condition()
        self._eof = False
        self.terminated = 0
        self.killed = 0
        self.stdin = _Stdin(self)
        self.stdout = _Stdout(self)
        self.stderr = _StderrStream(stderr)
        self._ids: dict = {}
        self._release()

    # -- what the adapter sees ------------------------------------------------------------------

    def read_line(self):
        with self._cond:
            while not self._out and not self._eof:
                self._cond.wait(5.0)
                if not self._out and not self._eof:
                    raise AssertionError("the adapter is waiting for a line the transcript "
                                         "never sends")
            if self._out:
                return self._out.popleft()
            return ""

    def terminate(self):
        self.terminated += 1
        self.die()

    def kill(self):
        self.killed += 1
        self.die()

    def wait(self, timeout=None):
        return 0

    def poll(self):
        return 0 if self._eof else None

    def die(self):
        with self._cond:
            self._eof = True
            self._cond.notify_all()

    # -- the transcript -------------------------------------------------------------------------

    def on_write(self, message: dict):
        self.writes.append(message)
        while self.position < len(self.entries) and self.entries[self.position]["dir"] != "->":
            self._emit(self.entries[self.position])
            self.position += 1
        if self.position >= len(self.entries):
            if self.die_when_exhausted:
                self.die()
            return
        recorded = self.entries[self.position]["line"]
        self.position += 1
        self.methods.append((recorded.get("method"), message.get("method")))
        if "id" in recorded and "id" in message:
            self._ids[recorded["id"]] = message["id"]
        self._release()

    def _release(self):
        while self.position < len(self.entries) and self.entries[self.position]["dir"] != "->":
            self._emit(self.entries[self.position])
            self.position += 1
        if self.position >= len(self.entries) and self.die_when_exhausted:
            self.die()

    def _emit(self, entry):
        if entry["dir"] == "<-raw":
            text = entry["line"]
        else:
            line = dict(entry["line"])
            if "id" in line and ("result" in line or "error" in line):
                line["id"] = self._ids.get(line["id"], line["id"])
            text = json.dumps(line)
        with self._cond:
            self._out.append(text + "\n")
            self._cond.notify_all()

    # -- assertions helpers ---------------------------------------------------------------------

    def sent(self, method: str) -> list[dict]:
        return [m for m in self.writes if m.get("method") == method]

    def responses(self) -> list[dict]:
        return [m for m in self.writes if "method" not in m and "id" in m]


class _StderrStream:
    def __init__(self, text: str):
        self._lines = deque(text.splitlines(keepends=True))
        self._cond = threading.Condition()

    def readline(self):
        if self._lines:
            return self._lines.popleft()
        return ""


# ----- the harness under test ---------------------------------------------------------------------


class HarnessCase(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory(prefix="hcodex-")
        self.addCleanup(self._tmp.cleanup)
        self.fake_codex = os.path.join(self._tmp.name, "codex")
        with open(self.fake_codex, "w", encoding="utf-8") as handle:
            handle.write("#!/bin/sh\nexit 0\n")
        os.chmod(self.fake_codex, os.stat(self.fake_codex).st_mode | stat.S_IEXEC)
        self.events: list[HarnessEvent] = []

    def emit(self, event: HarnessEvent) -> None:
        self.events.append(event)

    def kinds(self) -> list[str]:
        return [e.kind for e in self.events]

    def only(self, kind: str) -> list[dict]:
        return [e.data for e in self.events if e.kind == kind]

    def harness(self, entries, **kwargs) -> tuple[gh.CodexHarness, ReplayProcess]:
        proc = ReplayProcess(entries, **kwargs)
        harness = gh.CodexHarness(codex_path=self.fake_codex, spawn=lambda argv, cwd: proc,
                                  request_timeout=5.0, close_timeout=0.1, error_grace=0.5)
        self.addCleanup(harness.close)
        return harness, proc

    def started(self, name="ok-turn.jsonl", *, permissions="bypass", entries=None, **kwargs):
        harness, proc = self.harness(entries if entries is not None else load(name), **kwargs)
        start = harness.start(cwd="/tmp/relay-harness-codex", permissions=permissions)
        return harness, proc, start


# ----- availability and posture -------------------------------------------------------------------


class AvailabilityTest(HarnessCase):
    def test_start_raises_not_available_when_codex_is_not_on_path(self):
        harness = gh.CodexHarness(codex_path="codex")
        with tempfile.TemporaryDirectory(prefix="hcodex-path-") as empty:
            old = os.environ.get("PATH")
            os.environ["PATH"] = empty
            try:
                with self.assertRaises(HarnessNotAvailable) as caught:
                    harness.start(cwd=empty)
            finally:
                os.environ["PATH"] = old if old is not None else ""
        self.assertIn("not installed", str(caught.exception))

    def test_start_raises_not_available_when_the_process_dies_at_once(self):
        harness, _ = self.harness([], die_when_exhausted=True)
        with self.assertRaises(HarnessNotAvailable):
            harness.start(cwd="/tmp/relay-harness-codex")

    def test_permission_modes_are_codexs_approval_policy_and_sandbox(self):
        self.assertEqual(gh.PERMISSION_MODES["bypass"], ("never", "danger-full-access"))
        self.assertEqual(gh.PERMISSION_MODES["ask"], ("on-request", "workspace-write"))
        self.assertEqual(gh.PERMISSION_MODES["deny"], ("on-request", "read-only"))
        for posture, (policy, sandbox) in gh.PERMISSION_MODES.items():
            harness, proc, _ = self.started(permissions=posture)
            params = proc.sent("thread/start")[0]["params"]
            self.assertEqual(params["approvalPolicy"], policy, posture)
            self.assertEqual(params["sandbox"], sandbox, posture)
            self.assertEqual(params["cwd"], "/tmp/relay-harness-codex")
            harness.close()

    def test_start_says_hello_as_relay_and_reports_the_thread_id(self):
        harness, proc, start = self.started()
        self.assertEqual(proc.sent("initialize")[0]["params"]["clientInfo"]["name"], "relay")
        self.assertEqual(proc.sent("initialized")[0].get("params"), {})
        self.assertEqual(start.session_id, "01a0ba5c-89bb-7fb2-9e5b-e96f7f6f07e4")
        self.assertEqual(start.model, "gpt-5.6-sol")
        self.assertEqual(harness.session_id, start.session_id)
        self.assertEqual(harness.model, "gpt-5.6-sol")

    def test_resume_and_fork_pick_the_right_method(self):
        for fork, method in ((False, "thread/resume"), (True, "thread/fork")):
            entries = load("ok-turn.jsonl")
            entries[index_of(entries, "->", "thread/start")]["line"]["method"] = method
            harness, proc = self.harness(entries)
            harness.start(cwd="/tmp/relay-harness-codex", resume="prior-thread", fork=fork)
            self.assertEqual(proc.sent(method)[0]["params"]["threadId"], "prior-thread")
            harness.close()


# ----- a plain turn -------------------------------------------------------------------------------


class PlainTurnTest(HarnessCase):
    def test_one_turn_emits_started_delta_and_usage_and_returns_the_answer(self):
        harness, proc, _ = self.started("ok-turn.jsonl")
        result = harness.send("Reply with the single word ok.", emit=self.emit,
                              cancel=threading.Event())
        self.assertEqual(self.kinds(), ["started", "delta", "usage"])
        self.assertEqual(self.only("started")[0],
                         {"session_id": "01a0ba5c-89bb-7fb2-9e5b-e96f7f6f07e4",
                          "model": "gpt-5.6-sol"})
        self.assertEqual(self.only("delta")[0], {"text": "ok"})
        self.assertEqual(result.text, "ok")
        self.assertEqual(result.stop_reason, "end")
        self.assertEqual(proc.sent("turn/start")[0]["params"]["input"],
                         [{"type": "text", "text": "Reply with the single word ok."}])

    def test_usage_carries_the_turns_own_tokens_and_the_context_share(self):
        harness, _, _ = self.started("ok-turn.jsonl")
        result = harness.send("Reply with the single word ok.", emit=self.emit,
                              cancel=threading.Event())
        usage = self.only("usage")[0]
        self.assertEqual(usage["input_tokens"], 13312)
        self.assertEqual(usage["output_tokens"], 5)
        self.assertEqual(usage["model"], "gpt-5.6-sol")
        self.assertEqual(usage["context_pct"], round(100.0 * 13317 / 258400, 1))
        self.assertNotIn("cost_usd", usage)
        self.assertEqual(result.usage, usage)

    def test_started_is_announced_once_and_again_after_a_model_switch(self):
        entries = load("ok-turn.jsonl")
        models = load("models.jsonl")
        ask = models[index_of(models, "->", "model/list")]
        answer = next(e for e in models
                      if e["dir"] == "<-" and e["line"].get("id") == ask["line"]["id"]
                      and "result" in e["line"])
        at = index_of(entries, "->", "turn/start")
        entries[at:at] = [ask, answer]
        harness, proc, _ = self.started(entries=entries)
        self.assertEqual(harness.set_model("GPT-5.6-Terra"), "gpt-5.6-terra")
        harness.send("Reply with the single word ok.", emit=self.emit, cancel=threading.Event())
        self.assertEqual(self.only("started")[0]["model"], "gpt-5.6-terra")
        self.assertEqual(proc.sent("turn/start")[0]["params"]["model"], "gpt-5.6-terra")
        self.assertEqual(harness.model, "gpt-5.6-terra")

    def test_the_thread_is_started_on_the_effort_that_was_asked_for(self):
        entries = load("ok-turn.jsonl")
        harness, proc = self.harness(entries)
        start = harness.start(cwd="/tmp/relay-harness-codex", effort="xhigh")
        params = proc.sent("thread/start")[0]["params"]
        # `ThreadStartParams` has no `effort` field; `config` is the overrides map and
        # `model_reasoning_effort` is the key the TUI's `-c` sets (0.155.1 schema).
        self.assertEqual(params["config"], {"model_reasoning_effort": "xhigh"})
        self.assertNotIn("effort", params)
        # The fixture's thread came up on "low", and codex's answer is what the adapter keeps.
        self.assertEqual(harness.effort, "low")
        self.assertTrue(start.session_id)

    def test_no_config_is_sent_when_no_effort_is_asked_for(self):
        harness, proc, _ = self.started("ok-turn.jsonl")
        self.assertNotIn("config", proc.sent("thread/start")[0]["params"])

    def test_set_effort_rides_on_the_next_turn_start(self):
        harness, proc, _ = self.started("ok-turn.jsonl")
        self.assertEqual(harness.set_effort("Ultra"), "ultra")
        self.assertEqual(harness.effort, "ultra")
        harness.send("Reply with the single word ok.", emit=self.emit, cancel=threading.Event())
        self.assertEqual(proc.sent("turn/start")[0]["params"]["effort"], "ultra")

    def test_the_effort_is_only_sent_once_per_change(self):
        entries = load("ok-turn.jsonl")
        turn = entries[index_of(entries, "->", "turn/start"):]
        harness, proc, _ = self.started(entries=entries + turn)
        harness.set_effort("high")
        harness.send("one", emit=self.emit, cancel=threading.Event())
        harness.send("two", emit=self.emit, cancel=threading.Event())
        sent = proc.sent("turn/start")
        self.assertEqual(sent[0]["params"]["effort"], "high")
        self.assertNotIn("effort", sent[1]["params"])      # codex keeps it for later turns

    def test_a_shapeless_effort_is_refused_and_an_empty_one_is_an_error(self):
        harness, proc, _ = self.started("ok-turn.jsonl")
        with self.assertRaises(ValueError):
            harness.set_effort("high, and also this")
        with self.assertRaises(HarnessError):
            harness.set_effort("")

    def test_attachments_go_as_a_data_url_image_item(self):
        harness, proc, _ = self.started("ok-turn.jsonl")
        harness.send("look", attachments=[{"kind": "image", "media_type": "image/png",
                                           "data": "AAAA"}],
                     emit=self.emit, cancel=threading.Event())
        self.assertEqual(proc.sent("turn/start")[0]["params"]["input"],
                         [{"type": "text", "text": "look"},
                          {"type": "image", "url": "data:image/png;base64,AAAA"}])

    def test_compact_asks_codex_to_compact_and_does_not_block(self):
        harness, proc, _ = self.started("ok-turn.jsonl")
        harness.compact()
        self.assertEqual(proc.sent("thread/compact/start")[0]["params"]["threadId"],
                         harness.session_id)

    def test_close_terminates_once_and_is_idempotent(self):
        harness, proc, _ = self.started("ok-turn.jsonl")
        harness.close()
        harness.close()
        self.assertEqual(proc.terminated, 1)


class ModelsTest(HarnessCase):
    """`models()` is `model/list`, in the contract's shape (29.3)."""

    def _models(self):
        entries = load("ok-turn.jsonl")
        models = load("models.jsonl")
        ask = models[index_of(models, "->", "model/list")]
        answer = next(e for e in models
                      if e["dir"] == "<-" and e["line"].get("id") == ask["line"]["id"]
                      and "result" in e["line"])
        at = index_of(entries, "->", "turn/start")
        entries[at:at] = [ask, answer]
        harness, proc, _ = self.started(entries=entries)
        return harness, harness.models()

    def test_each_row_is_a_slug_a_name_and_the_levels_that_model_has(self):
        harness, rows = self._models()
        by_id = {row["id"]: row for row in rows}
        self.assertIn("gpt-6-astra", by_id)
        self.assertEqual(by_id["gpt-6-astra"]["label"], "GPT-6-Astra")
        self.assertEqual(by_id["gpt-6-astra"]["efforts"],
                         ["low", "medium", "high", "xhigh", "max", "ultra"])
        self.assertEqual(by_id["gpt-6-astra"]["default_effort"], "medium")
        # gpt-5.5 has no `ultra`: the levels are per model, not a table of codex's.
        self.assertEqual(by_id["gpt-5.5"]["efforts"], ["low", "medium", "high", "xhigh"])

    def test_the_running_model_is_marked_current(self):
        harness, rows = self._models()
        self.assertEqual([row["id"] for row in rows if row.get("current")], ["gpt-5.6-sol"])
        self.assertEqual(harness.model, "gpt-5.6-sol")

    def test_models_is_empty_when_the_harness_is_not_up(self):
        harness = gh.CodexHarness(codex_path=self.fake_codex)
        self.assertEqual(harness.models(), [])

    def test_the_snake_case_catalogue_reads_the_same(self):
        """`codex debug models` spells the same facts differently; one reader serves both."""
        rows = gh.catalog_rows([
            {"slug": "gpt-6-astra", "display_name": "GPT-6-Astra", "visibility": "list",
             "default_reasoning_level": "medium",
             "supported_reasoning_levels": [{"effort": "low", "description": "x"},
                                            {"effort": "ultra", "description": "y"}]},
            {"slug": "gpt-reserve", "display_name": "GPT-Reserve", "visibility": "hide",
             "default_reasoning_level": "medium", "supported_reasoning_levels": []}],
            current="gpt-6-astra")
        self.assertEqual(rows, [{"id": "gpt-6-astra", "label": "GPT-6-Astra",
                                 "efforts": ["low", "ultra"], "default_effort": "medium",
                                 "current": True}])


# ----- tools --------------------------------------------------------------------------------------


class ToolTurnTest(HarnessCase):
    def test_a_shell_command_becomes_run_command_started_and_a_result(self):
        harness, _, _ = self.started("shell-turn.jsonl")
        result = harness.send("Run `echo relay-harness-ok` and report its output.",
                              emit=self.emit, cancel=threading.Event())
        self.assertEqual(self.kinds().count("tool_started"), 1)
        started = self.only("tool_started")[0]
        self.assertEqual(started["tool"], "run_command")
        self.assertEqual(started["call_id"], "exec-fd850614-3420-4b80-ab94-b5111876744e")
        self.assertEqual(started["input"]["command"], "/bin/bash -lc 'echo relay-harness-ok'")
        self.assertEqual(started["input"]["_guest_tool"], "commandExecution")
        self.assertEqual(started["label"], "/bin/bash -lc 'echo relay-harness-ok'")

        done = self.only("tool_result")[0]
        self.assertEqual(done["call_id"], started["call_id"])
        self.assertEqual(done["tool"], "run_command")
        self.assertEqual(done["output"], "relay-harness-ok\n")
        self.assertTrue(done["ok"])
        self.assertEqual(done["ms"], 0)
        self.assertNotIn("diff", done)
        self.assertEqual(result.text, "`relay-harness-ok`")

    def test_the_answer_is_the_final_phase_message_not_the_commentary(self):
        harness, _, _ = self.started("shell-turn.jsonl")
        result = harness.send("Run `echo relay-harness-ok` and report its output.",
                              emit=self.emit, cancel=threading.Event())
        joined = "".join(d["text"] for d in self.only("delta"))
        self.assertIn("I’ll run that command", joined)     # the commentary is still streamed
        self.assertEqual(result.text, "`relay-harness-ok`")     # but is not the answer

    def test_the_event_order_of_a_tool_turn(self):
        harness, _, _ = self.started("shell-turn.jsonl")
        harness.send("Run `echo relay-harness-ok` and report its output.", emit=self.emit,
                     cancel=threading.Event())
        order = [k for i, k in enumerate(self.kinds())
                 if k != "delta" or self.kinds()[i - 1] != "delta"]
        self.assertEqual(order, ["started", "delta", "tool_started", "tool_result", "delta",
                                 "usage"])

    def test_a_file_change_becomes_edit_file_with_a_unified_diff(self):
        harness, _, _ = self.started("approval-turn.jsonl")
        harness.send("Create a file named ok.txt containing the word ok.", emit=self.emit,
                     cancel=threading.Event())
        started = self.only("tool_started")[0]
        self.assertEqual(started["tool"], "edit_file")
        self.assertEqual(started["input"]["_guest_tool"], "fileChange")
        self.assertEqual(started["input"]["files"], ["/tmp/relay-harness-codex/ok.txt"])
        self.assertEqual(started["label"], "/tmp/relay-harness-codex/ok.txt")

        done = self.only("tool_result")[0]
        self.assertTrue(done["ok"])
        self.assertEqual(done["diff"],
                         "--- /dev/null\n+++ /tmp/relay-harness-codex/ok.txt\n"
                         "@@ -0,0 +1,1 @@\n+ok\n")
        self.assertIn("+1", done["output"])

    def test_codex_item_types_map_onto_relays_tool_names(self):
        self.assertEqual(map_tool_name("codex", "commandExecution"), "run_command")
        self.assertEqual(map_tool_name("codex", "fileChange"), "edit_file")
        self.assertEqual(map_tool_name("codex", "webSearch"), "web")
        self.assertEqual(map_tool_name("codex", "mcpToolCall"), "other")
        self.assertEqual(map_tool_name("codex", "collabAgentToolCall"), "agent")
        self.assertEqual(map_tool_name("codex", "imageView"), "read_file")
        self.assertEqual(map_tool_name("codex", "somethingNewIn2027"), "other")
        for kind in gh.TOOL_ITEM_TYPES:
            self.assertIn(map_tool_name("codex", kind),
                          ("run_command", "edit_file", "read_file", "search", "web", "agent",
                           "other", "write_file", "list_directory"))


# ----- interrupting -------------------------------------------------------------------------------


class InterruptTest(HarnessCase):
    def test_interrupt_ends_the_turn_and_says_so(self):
        harness, proc, _ = self.started("interrupt-turn.jsonl")
        done = threading.Event()

        # The transcript stalls at the point where the interrupt was typed, so ask for it from
        # another thread once the turn has begun.
        def ask():
            while not proc.sent("turn/start"):
                pass
            harness.interrupt()
            done.set()

        thread = threading.Thread(target=ask, daemon=True)
        thread.start()
        result = harness.send("Count from 1 to 200, one number per line.", emit=self.emit,
                              cancel=threading.Event())
        thread.join(5)
        self.assertTrue(done.is_set())
        self.assertEqual(result.stop_reason, "interrupted")
        self.assertEqual(proc.sent("turn/interrupt")[0]["params"]["threadId"],
                         harness.session_id)
        self.assertEqual(result.text, "")

    def test_a_set_cancel_event_interrupts_the_turn_too(self):
        harness, proc, _ = self.started("interrupt-turn.jsonl")
        cancel = threading.Event()

        def ask():
            while not proc.sent("turn/start"):
                pass
            cancel.set()

        threading.Thread(target=ask, daemon=True).start()
        result = harness.send("Count from 1 to 200, one number per line.", emit=self.emit,
                              cancel=cancel)
        self.assertEqual(result.stop_reason, "interrupted")
        self.assertEqual(len(proc.sent("turn/interrupt")), 1)

    def test_interrupt_outside_a_turn_is_a_no_op(self):
        harness, proc, _ = self.started("ok-turn.jsonl")
        harness.interrupt()
        harness.interrupt()
        self.assertEqual(proc.sent("turn/interrupt"), [])


# ----- approvals and questions --------------------------------------------------------------------


class ApprovalTest(HarnessCase):
    def test_ask_surfaces_the_approval_and_answer_allows_it(self):
        harness, proc, _ = self.started("approval-turn.jsonl", permissions="ask")
        answered = []

        def emit(event):
            self.events.append(event)
            if event.kind == "approval":
                answered.append(event.data)
                harness.answer(event.data["id"], {"behavior": "allow"})

        harness.send("Create a file named ok.txt containing the word ok.", emit=emit,
                     cancel=threading.Event())
        self.assertEqual(len(answered), 1)
        self.assertEqual(answered[0]["kind"], "patch")
        self.assertEqual(answered[0]["id"], "0")
        self.assertEqual(answered[0]["detail"], "apply its file changes")
        self.assertEqual(proc.responses()[0]["result"], {"decision": "accept"})
        self.assertIn("tool_result", self.kinds())

    def test_answering_an_unknown_request_is_an_error(self):
        harness, _, _ = self.started("ok-turn.jsonl")
        with self.assertRaises(HarnessError):
            harness.answer("nope", {"behavior": "allow"})

    def test_bypass_answers_the_approval_itself_and_leaves_a_notice(self):
        harness, proc, _ = self.started("approval-turn.jsonl", permissions="bypass")
        harness.send("Create a file named ok.txt containing the word ok.", emit=self.emit,
                     cancel=threading.Event())
        self.assertNotIn("approval", self.kinds())
        self.assertEqual(proc.responses()[0]["result"], {"decision": "accept"})
        self.assertTrue(any("allowed" in n["text"] for n in self.only("notice")))

    def test_deny_refuses_the_approval_itself(self):
        entries = load("approval-turn.jsonl")
        harness, proc, _ = self.started(entries=entries, permissions="deny")
        harness.send("Create a file named ok.txt containing the word ok.", emit=self.emit,
                     cancel=threading.Event())
        self.assertNotIn("approval", self.kinds())
        self.assertEqual(proc.responses()[0]["result"], {"decision": "decline"})
        self.assertTrue(any("refused" in n["text"] for n in self.only("notice")))

    def test_a_command_approval_names_the_command(self):
        entries = load("approval-turn.jsonl")
        at = index_of(entries, "<-", "item/fileChange/requestApproval")
        entries[at] = {"dir": "<-", "line": {
            "id": 0, "method": "item/commandExecution/requestApproval",
            "params": {"threadId": "t", "turnId": "u", "itemId": "i", "startedAtMs": 1,
                       "command": "rm -rf build", "cwd": "/tmp/relay-harness-codex"}}}
        harness, proc, _ = self.started(entries=entries, permissions="ask")
        seen = []

        def emit(event):
            self.events.append(event)
            if event.kind == "approval":
                seen.append(event.data)
                harness.answer(event.data["id"], {"behavior": "deny", "message": "no"})

        harness.send("go", emit=emit, cancel=threading.Event())
        self.assertEqual(seen[0]["kind"], "command")
        self.assertEqual(seen[0]["detail"], "rm -rf build")
        self.assertEqual(proc.responses()[0]["result"], {"decision": "decline"})

    def test_a_tool_question_becomes_a_question_event_answered_by_id(self):
        entries = load("approval-turn.jsonl")
        at = index_of(entries, "<-", "item/fileChange/requestApproval")
        entries[at] = {"dir": "<-", "line": {
            "id": 0, "method": "item/tool/requestUserInput",
            "params": {"threadId": "t", "turnId": "u", "itemId": "i", "isBlocking": True,
                       "questions": [{"id": "q1", "header": "Branch",
                                      "question": "Which branch?",
                                      "options": [{"label": "main"}, {"label": "dev"}]}]}}}
        harness, proc, _ = self.started(entries=entries, permissions="ask")
        seen = []

        def emit(event):
            self.events.append(event)
            if event.kind == "question":
                seen.append(event.data)
                harness.answer(event.data["id"], {"answers": [["main"]]})

        harness.send("go", emit=emit, cancel=threading.Event())
        self.assertEqual(seen[0]["questions"],
                         [{"header": "Branch", "question": "Which branch?",
                           "options": ["main", "dev"]}])
        self.assertEqual(proc.responses()[0]["result"],
                         {"answers": {"q1": {"answers": ["main"]}}})

    def test_a_server_request_relay_does_not_know_is_refused(self):
        entries = load("approval-turn.jsonl")
        at = index_of(entries, "<-", "item/fileChange/requestApproval")
        entries.insert(at, {"dir": "<-", "line": {"id": 99, "method": "item/tool/call",
                                                  "params": {"tool": "whatever"}}})
        harness, proc, _ = self.started(entries=entries, permissions="ask")

        def emit(event):
            self.events.append(event)
            if event.kind == "approval":
                harness.answer(event.data["id"], {"behavior": "allow"})

        harness.send("go", emit=emit, cancel=threading.Event())
        refusals = [r for r in proc.responses() if "error" in r]
        self.assertEqual(refusals[0]["id"], 99)
        self.assertEqual(refusals[0]["error"]["code"], -32601)


# ----- when things go wrong -----------------------------------------------------------------------


class RobustnessTest(HarnessCase):
    def test_an_unknown_notification_is_ignored(self):
        entries = load("ok-turn.jsonl")
        at = index_of(entries, "<-", "item/agentMessage/delta")
        entries.insert(at, server("thread/somethingNobodyHasWrittenYet", {"whatever": 1}))
        harness, _, _ = self.started(entries=entries)
        result = harness.send("Reply with the single word ok.", emit=self.emit,
                              cancel=threading.Event())
        self.assertEqual(self.kinds(), ["started", "delta", "usage"])
        self.assertEqual(result.text, "ok")

    def test_a_non_json_line_on_stdout_is_skipped(self):
        entries = load("ok-turn.jsonl")
        at = index_of(entries, "<-", "item/agentMessage/delta")
        entries.insert(at, {"dir": "<-raw", "line": "warning: bubblewrap is unhappy"})
        entries.insert(at, {"dir": "<-raw", "line": "[]"})
        harness, _, _ = self.started(entries=entries)
        result = harness.send("Reply with the single word ok.", emit=self.emit,
                              cancel=threading.Event())
        self.assertEqual(result.text, "ok")
        self.assertEqual(self.kinds(), ["started", "delta", "usage"])

    def test_the_process_dying_mid_turn_raises_with_the_last_stderr_lines(self):
        entries = load("ok-turn.jsonl")
        keep = index_of(entries, "<-", "turn/started") + 1
        harness, proc, _ = self.started(entries=entries[:keep], die_when_exhausted=True,
                                        stderr="one\nthe model provider fell over\n")
        with self.assertRaises(HarnessError) as caught:
            harness.send("Reply with the single word ok.", emit=self.emit,
                         cancel=threading.Event())
        self.assertIn("stopped", str(caught.exception))
        self.assertIn("the model provider fell over", str(caught.exception))

    def test_an_error_notification_becomes_an_error_event_and_raises(self):
        entries = load("ok-turn.jsonl")
        at = index_of(entries, "<-", "item/agentMessage/delta")
        entries[at:] = [
            server("error", {"threadId": "t", "turnId": "u", "willRetry": False,
                             "error": {"message": "stream disconnected before completion"}}),
            server("turn/completed", {"threadId": "t",
                                      "turn": {"id": "01a0ba5c-89f0-7a00-8ceb-bec59db02a3b",
                                               "items": [], "status": "failed",
                                               "error": {"message": "stream disconnected"}}}),
        ]
        harness, _, _ = self.started(entries=entries)
        with self.assertRaises(HarnessError) as caught:
            harness.send("Reply with the single word ok.", emit=self.emit,
                         cancel=threading.Event())
        self.assertIn("stream disconnected before completion", str(caught.exception))
        self.assertEqual(self.only("error")[0]["text"], "stream disconnected before completion")

    def test_a_retryable_error_is_only_a_notice(self):
        entries = load("ok-turn.jsonl")
        at = index_of(entries, "<-", "item/agentMessage/delta")
        entries.insert(at, server("error", {"threadId": "t", "turnId": "u", "willRetry": True,
                                            "error": {"message": "rate limited"}}))
        harness, _, _ = self.started(entries=entries)
        result = harness.send("Reply with the single word ok.", emit=self.emit,
                              cancel=threading.Event())
        self.assertNotIn("error", self.kinds())
        self.assertIn("rate limited", self.only("notice")[0]["text"])
        self.assertEqual(result.text, "ok")

    def test_two_turns_at_once_are_refused(self):
        harness, _, _ = self.started("interrupt-turn.jsonl")
        running = threading.Event()
        finished = threading.Event()

        def first(event):
            self.events.append(event)
            running.set()

        def run():
            try:
                harness.send("a", emit=first, cancel=threading.Event())
            except HarnessError:                                      # pragma: no cover
                pass
            finally:
                finished.set()

        thread = threading.Thread(target=run, daemon=True)
        thread.start()
        self.assertTrue(running.wait(5))
        with self.assertRaises(HarnessError) as caught:
            harness.send("b", emit=self.emit, cancel=threading.Event())
        self.assertIn("already running", str(caught.exception))
        harness.interrupt()
        thread.join(5)
        self.assertTrue(finished.is_set())

    def test_sending_before_start_is_an_error(self):
        harness = gh.CodexHarness(codex_path=self.fake_codex)
        with self.assertRaises(HarnessError):
            harness.send("hi", emit=self.emit, cancel=threading.Event())

    def test_a_diff_that_is_already_unified_keeps_its_hunks(self):
        diff = gh._unified_diff({"path": "/a/b.py", "kind": {"type": "update"},
                                 "diff": "@@ -1,2 +1,2 @@\n-old\n+new\n"})
        self.assertEqual(diff, "--- /a/b.py\n+++ /a/b.py\n@@ -1,2 +1,2 @@\n-old\n+new\n")
        self.assertEqual(gh._diff_counts(diff), (1, 1))

    def test_a_deleted_file_becomes_a_removal_diff(self):
        diff = gh._unified_diff({"path": "x.txt", "kind": {"type": "delete"}, "diff": "a\nb\n"})
        self.assertEqual(diff, "--- a/x.txt\n+++ /dev/null\n@@ -1,2 +0,0 @@\n-a\n-b\n")


if __name__ == "__main__":
    unittest.main()
