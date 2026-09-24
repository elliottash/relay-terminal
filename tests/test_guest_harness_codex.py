# SPDX-License-Identifier: AGPL-3.0-or-later
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
import time
import unittest
from collections import deque

sys.path.insert(0, os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
                                "backend"))

from relay_core.guest_harness import (MAX_TOOL_OUTPUT_CHUNK, HarnessError, HarnessEvent,
                                      HarnessNotAvailable, map_tool_name)
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


# `account/rateLimits/read`'s real answer, recorded on 2026-09-20 from codex-cli 0.155.1 on a Pro
# plan (account id redacted): the *primary* window is the weekly one (10080 minutes), there is no
# secondary, so the kind has to come from `windowDurationMins`. The transcripts under `FIXTURES`
# predate the adapter asking for it; `ReplayProcess` answers the request from here instead of
# consuming a recorded line, so every recorded turn still replays byte for byte.
RATE_LIMITS_READ = {
    "ordinaryUsageAllowed": True,
    "rateLimits": {"limitId": "codex", "limitName": None, "normalModelSlug": None,
                   "primary": {"usedPercent": 53, "windowDurationMins": 10080,
                               "resetsAt": 1790065926},
                   "secondary": None,
                   "credits": {"hasCredits": False, "unlimited": False, "balance": "0"},
                   "individualLimit": None, "spendControlReached": False, "planType": "pro",
                   "rateLimitReachedType": None},
    "rateLimitsByLimitId": {"codex": {"limitId": "codex", "primary": {
        "usedPercent": 53, "windowDurationMins": 10080, "resetsAt": 1790065926},
        "secondary": None, "planType": "pro", "rateLimitReachedType": None}},
    "rateLimitResetCredits": {"availableCount": 0, "credits": []},
    "accountId": "00000000-0000-0000-0000-000000000000", "rateLimitUpsell": None,
}
# What an older server, or one with nothing to report, says instead.
RATE_LIMITS_UNAVAILABLE = {"code": -32601, "message": "Method not found"}


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

    def __init__(self, entries, *, stderr: str = "", die_when_exhausted: bool = False,
                 rate_limits=None):
        self.entries = list(entries)
        # The answer to `account/rateLimits/read` when the transcript has none recorded: a result
        # dict, or an error dict (the default, RATE_LIMITS_UNAVAILABLE, is what a server without
        # the method says, so every recorded turn also proves the adapter shrugs it off).
        self.rate_limits = rate_limits
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
        if message.get("method") == "account/rateLimits/read" and (
                self.position >= len(self.entries)
                or self.entries[self.position]["line"].get("method") != message["method"]):
            answer = self.rate_limits if self.rate_limits is not None else RATE_LIMITS_UNAVAILABLE
            key = "error" if "code" in answer and "message" in answer else "result"
            with self._cond:
                self._out.append(json.dumps({"id": message.get("id"), key: answer}) + "\n")
                self._cond.notify_all()
            return
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
    def test_instructions_use_native_field_on_start_resume_and_fork(self):
        text = "Relay guest context\nUse only the tools actually offered."
        for resume, fork, method in ((None, False, "thread/start"),
                                    ("prior-thread", False, "thread/resume"),
                                    ("prior-thread", True, "thread/fork")):
            entries = load("ok-turn.jsonl")
            entries[index_of(entries, "->", "thread/start")]["line"]["method"] = method
            harness, proc = self.harness(entries)
            harness.start(cwd="/tmp/relay-harness-codex", resume=resume, fork=fork,
                          instructions=text)
            params = proc.sent(method)[0]["params"]
            self.assertEqual(params["developerInstructions"], text)
            self.assertNotIn("baseInstructions", params)
            harness.send("Reply with the single word ok.", emit=self.emit,
                         cancel=threading.Event())
            self.assertEqual(proc.sent("turn/start")[0]["params"]["input"],
                             [{"type": "text", "text": "Reply with the single word ok."}])
            harness.close()

    def test_board_bridge_start_resume_fork_overrides(self):
        descriptor = {"command": "/python", "args": ["/proxy", "/capability"]}
        h = gh.CodexHarness()
        h._permissions = "bypass"
        h._board_bridge = descriptor
        from unittest.mock import Mock
        h._request = Mock(return_value={})
        for resume, fork, method in [(None, False, "thread/start"),
                                     ("saved", False, "thread/resume"),
                                     ("saved", True, "thread/fork")]:
            h._start_thread(cwd="/tmp", model=None, resume=resume, fork=fork, effort="high")
            name, params = h._request.call_args.args
            self.assertEqual(name, method)
            self.assertEqual(params["config"]["mcp_servers.relay_board.args"], descriptor["args"])
            self.assertEqual(params["config"]["mcp_servers.relay_board.tool_timeout_sec"], 86400)
            self.assertEqual(params["config"][gh.EFFORT_CONFIG_KEY], "high")

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
        self.assertEqual(start.model, "gpt-6-sol")
        self.assertEqual(harness.session_id, start.session_id)
        self.assertEqual(harness.model, "gpt-6-sol")

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
        # `limits` is the recorded `account/rateLimits/updated` codex sends after each response
        # (redacted to zeros in the fixture). It is not at the front: the replay answered the
        # adapter's own `account/rateLimits/read` with "method not found", which is ignored.
        self.assertEqual(self.kinds(), ["started", "delta", "limits", "usage"])
        self.assertEqual(self.only("started")[0],
                         {"session_id": "01a0ba5c-89bb-7fb2-9e5b-e96f7f6f07e4",
                          "model": "gpt-6-sol"})
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
        self.assertEqual(usage["model"], "gpt-6-sol")
        self.assertEqual(usage["context_pct"], round(100.0 * 13317 / 258400, 1))
        self.assertNotIn("cost_usd", usage)
        self.assertEqual(result.usage, usage)

    def test_usage_carries_the_window_and_what_is_in_it(self):
        """GT7X t:a3: a chip that can say "13k of 258k", not only the share of it."""
        harness, _, _ = self.started("ok-turn.jsonl")
        harness.send("Reply with the single word ok.", emit=self.emit, cancel=threading.Event())
        usage = self.only("usage")[0]
        # `thread/tokenUsage/updated`'s `modelContextWindow` and its `last.totalTokens`.
        self.assertEqual(usage["context_window"], 258400)
        self.assertEqual(usage["context_tokens"], 13317)
        self.assertEqual(usage["context_pct"], round(100.0 * 13317 / 258400, 1))

    def test_usage_carries_what_the_prefix_cache_saved(self):
        """#GMCF decision 5: codex's own `cachedInputTokens`, the turn's share of it."""
        harness, _, _ = self.started("ok-turn.jsonl")
        harness.send("Reply with the single word ok.", emit=self.emit, cancel=threading.Event())
        usage = self.only("usage")[0]
        # `tokenUsage.total` for the first turn of the thread, so the baseline is nothing.
        self.assertEqual(usage["cached_input_tokens"], 11136)
        self.assertEqual(usage["cache_write_input_tokens"], 0)
        # It is part of `inputTokens`, not extra to it (13312 input, of which 11136 were cached).
        self.assertLess(usage["cached_input_tokens"], usage["input_tokens"])

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
        # Not codex's "GPT-6-Astra": the owner's picker says gpt-6-astra (card #MDL1).
        self.assertEqual(by_id["gpt-6-astra"]["name"], "gpt-6-astra")
        self.assertEqual(by_id["gpt-6-astra"]["label"], "gpt-6-astra")
        self.assertEqual(by_id["gpt-6-astra"]["efforts"],
                         ["low", "medium", "high", "xhigh", "max", "ultra"])
        self.assertEqual(by_id["gpt-6-astra"]["default_effort"], "medium")
        # gpt-5.5 has no `ultra`: the levels are per model, not a table of codex's.
        self.assertEqual(by_id["gpt-5.5"]["efforts"], ["low", "medium", "high", "xhigh"])

    def test_the_running_model_is_marked_current(self):
        harness, rows = self._models()
        self.assertEqual([row["id"] for row in rows if row.get("current")], ["gpt-6-sol"])
        self.assertEqual(harness.model, "gpt-6-sol")

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
        self.assertEqual(rows, [{"id": "gpt-6-astra", "name": "gpt-6-astra", "label": "gpt-6-astra",
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
        self.assertEqual(order, ["started", "delta", "tool_started", "tool_result", "limits",
                                 "delta", "usage"])

    def test_output_deltas_stream_as_tool_output_while_the_command_runs(self):
        """GT7X t:a3. `CommandExecutionOutputDeltaNotification` is `{threadId, turnId, itemId,
        delta}` with a plain-text delta (0.155.1's `generate-json-schema`); the recorded turn's
        command is an `echo` short enough that codex sent none, so these are written from that
        schema rather than recorded."""
        entries = load("shell-turn.jsonl")
        item = "exec-fd850614-3420-4b80-ab94-b5111876744e"
        at = index_of(entries, "<-", "item/started", occurrence=2)      # the commandExecution
        entries[at + 1:at + 1] = [
            server("item/commandExecution/outputDelta",
                   {"threadId": "t", "turnId": "u", "itemId": item, "delta": "relay-"}),
            server("item/commandExecution/outputDelta",
                   {"threadId": "t", "turnId": "u", "itemId": item, "delta": "harness-ok\n"}),
        ]
        harness, _, _ = self.started(entries=entries)
        harness.send("Run `echo relay-harness-ok` and report its output.", emit=self.emit,
                     cancel=threading.Event())
        streamed = self.only("tool_output")
        self.assertEqual([d["text"] for d in streamed], ["relay-", "harness-ok\n"])
        self.assertEqual({d["call_id"] for d in streamed}, {item})
        kinds = self.kinds()
        self.assertLess(kinds.index("tool_started"), kinds.index("tool_output"))
        self.assertLess(kinds.index("tool_output"), kinds.index("tool_result"))
        # Still buffered: the buffer is the fallback when `item/completed` has no aggregatedOutput.
        self.assertEqual(self.only("tool_result")[0]["output"], "relay-harness-ok\n")

    def test_one_huge_delta_is_split_into_events_the_channel_can_carry(self):
        chunk = MAX_TOOL_OUTPUT_CHUNK
        entries = load("shell-turn.jsonl")
        item = "exec-fd850614-3420-4b80-ab94-b5111876744e"
        at = index_of(entries, "<-", "item/started", occurrence=2)
        entries[at + 1:at + 1] = [server("item/commandExecution/outputDelta",
                                         {"threadId": "t", "turnId": "u", "itemId": item,
                                          "delta": "x" * (chunk * 2 + 7)})]
        harness, _, _ = self.started(entries=entries)
        harness.send("go", emit=self.emit, cancel=threading.Event())
        texts = [d["text"] for d in self.only("tool_output")]
        self.assertEqual([len(t) for t in texts], [chunk, chunk, 7])
        self.assertEqual("".join(texts), "x" * (chunk * 2 + 7))       # split, never truncated

    def test_an_empty_delta_says_nothing(self):
        entries = load("shell-turn.jsonl")
        at = index_of(entries, "<-", "item/started", occurrence=2)
        entries[at + 1:at + 1] = [server("item/commandExecution/outputDelta",
                                         {"threadId": "t", "turnId": "u", "itemId": "exec-1",
                                          "delta": ""})]
        harness, _, _ = self.started(entries=entries)
        harness.send("go", emit=self.emit, cancel=threading.Event())
        self.assertNotIn("tool_output", self.kinds())

    def test_the_deprecated_file_change_delta_still_streams(self):
        """0.155.1's schema: "the server no longer emits this notification". Older ones do."""
        entries = load("approval-turn.jsonl")
        at = index_of(entries, "<-", "item/started", occurrence=2)
        item = (entries[at]["line"]["params"]["item"] or {}).get("id")
        entries[at + 1:at + 1] = [server("item/fileChange/outputDelta",
                                         {"threadId": "t", "turnId": "u", "itemId": item,
                                          "delta": "patching ok.txt\n"})]
        harness, _, _ = self.started(entries=entries, permissions="bypass")
        harness.send("Create a file named ok.txt containing the word ok.", emit=self.emit,
                     cancel=threading.Event())
        self.assertEqual([d["text"] for d in self.only("tool_output")], ["patching ok.txt\n"])

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

    def _answer_with(self, decision, *, method=None):
        """One `ask` approval on the recorded turn, answered with `decision`. Returns the
        adapter's JSON-RPC response to it."""
        entries = load("approval-turn.jsonl")
        if method is not None:
            at = index_of(entries, "<-", "item/fileChange/requestApproval")
            line = dict(entries[at]["line"])
            line["method"] = method
            entries[at] = {"dir": "<-", "line": line}
        harness, proc, _ = self.started(entries=entries, permissions="ask")

        def emit(event):
            self.events.append(event)
            if event.kind == "approval":
                harness.answer(event.data["id"], decision)

        harness.send("Create a file named ok.txt containing the word ok.", emit=emit,
                     cancel=threading.Event())
        return proc.responses()[0], harness, proc

    def test_an_allow_for_the_session_is_codexs_acceptForSession(self):
        """GT7X t:a3. `FileChangeApprovalDecision`: accept | acceptForSession | decline | cancel."""
        response, _, _ = self._answer_with({"behavior": "allow", "scope": "session"})
        self.assertEqual(response["result"], {"decision": "acceptForSession"})

    def test_a_deny_that_stops_the_turn_is_codexs_cancel(self):
        response, harness, _ = self._answer_with({"behavior": "deny", "scope": "stop"})
        self.assertEqual(response["result"], {"decision": "cancel"})

    def test_the_default_scope_is_still_this_one_action(self):
        for decision in ({"behavior": "allow"}, {"behavior": "allow", "scope": "once"},
                         {"behavior": "allow", "scope": "stop"},      # meaningless on an allow
                         {"behavior": "allow", "scope": "forever"}):  # not a scope this build has
            response, _, _ = self._answer_with(decision)
            self.assertEqual(response["result"], {"decision": "accept"}, decision)
        response, _, _ = self._answer_with({"behavior": "deny", "scope": "once"})
        self.assertEqual(response["result"], {"decision": "decline"})

    def test_the_v1_spelling_takes_the_same_scopes(self):
        """The older `ReviewDecision` words, for a server that still asks the v1 way."""
        response, _, _ = self._answer_with({"behavior": "allow", "scope": "session"},
                                           method="applyPatchApproval")
        self.assertEqual(response["result"], {"decision": "approved_for_session"})
        response, _, _ = self._answer_with({"behavior": "deny", "scope": "stop"},
                                           method="applyPatchApproval")
        self.assertEqual(response["result"], {"decision": "abort"})
        response, _, _ = self._answer_with({"behavior": "allow"}, method="applyPatchApproval")
        self.assertEqual(response["result"], {"decision": "approved"})

    def test_a_permissions_request_grants_for_the_session_or_the_turn(self):
        """`PermissionsRequestApprovalResponse.scope` is its own `PermissionGrantScope`."""
        params = {"threadId": "t", "turnId": "u", "reason": "read outside the workspace",
                  "permissions": {"fileSystem": {"read": ["/etc"]}}}
        for scope, expected in (("session", "session"), ("once", "turn"), (None, "turn")):
            decision = {"behavior": "allow"} if scope is None else {"behavior": "allow",
                                                                    "scope": scope}
            entries = load("approval-turn.jsonl")
            at = index_of(entries, "<-", "item/fileChange/requestApproval")
            entries[at] = {"dir": "<-", "line": {
                "id": 0, "method": "item/permissions/requestApproval", "params": params}}
            harness, proc, _ = self.started(entries=entries, permissions="ask")

            def emit(event, harness=harness, decision=decision):
                self.events.append(event)
                if event.kind == "approval":
                    harness.answer(event.data["id"], decision)

            harness.send("go", emit=emit, cancel=threading.Event())
            self.assertEqual(proc.responses()[0]["result"],
                             {"permissions": params["permissions"], "scope": expected}, scope)

    def test_a_refused_permissions_request_that_stops_also_interrupts(self):
        """The one method with no "and stop" of its own: the adapter ends the turn itself."""
        params = {"threadId": "t", "turnId": "u", "reason": "widen the sandbox",
                  "permissions": {}}
        entries = load("approval-turn.jsonl")
        at = index_of(entries, "<-", "item/fileChange/requestApproval")
        entries[at] = {"dir": "<-", "line": {
            "id": 0, "method": "item/permissions/requestApproval", "params": params}}
        harness, proc, _ = self.started(entries=entries, permissions="ask")

        def emit(event):
            self.events.append(event)
            if event.kind == "approval":
                harness.answer(event.data["id"], {"behavior": "deny", "scope": "stop",
                                                  "message": "no"})

        result = harness.send("go", emit=emit, cancel=threading.Event())
        self.assertEqual(proc.responses()[0]["error"]["message"], "no")
        self.assertTrue(proc.sent("turn/interrupt"))
        self.assertEqual(result.stop_reason, "interrupted")

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


# ----- usage limits ---------------------------------------------------------------------------------


def _window(used, minutes, resets):
    return {"usedPercent": used, "windowDurationMins": minutes, "resetsAt": resets}


class LimitsTest(HarnessCase):
    """The subscription's rolling windows (29.1 `limits`): read once at start, merged from every
    `account/rateLimits/updated`, and the kind decided by `windowDurationMins`."""

    def wait_for_limits(self, harness):
        deadline = time.monotonic() + 5.0
        while time.monotonic() < deadline:
            with harness._lock:
                if harness._limits_fresh:
                    return
            time.sleep(0.01)
        self.fail("the rate-limit read was never answered")

    def test_start_asks_once_and_the_first_turn_reports_the_answer(self):
        harness, proc, _ = self.started("ok-turn.jsonl", rate_limits=RATE_LIMITS_READ)
        reads = proc.sent("account/rateLimits/read")
        self.assertEqual(len(reads), 1)
        self.assertEqual(reads[0]["params"], {})
        self.wait_for_limits(harness)
        harness.send("Reply with the single word ok.", emit=self.emit, cancel=threading.Event())
        self.assertEqual(self.kinds()[:2], ["started", "limits"])
        # A Pro plan with only a weekly allowance: primary is the 7-day window, no secondary.
        # The read answer's top-level rateLimitResetCredits (none banked here) ride along.
        self.assertEqual(self.only("limits")[0],
                         {"windows": [{"kind": "weekly", "used_percent": 53.0,
                                       "resets_at": 1790065926}],
                          "resets_available": 0})
        self.assertEqual(len(proc.sent("account/rateLimits/read")), 1)   # once, not per turn

    def test_a_read_the_server_does_not_have_is_shrugged_off(self):
        harness, proc, _ = self.started("ok-turn.jsonl")      # answered "method not found"
        time.sleep(0.05)
        self.assertEqual(harness._limits, {})
        harness.send("Reply with the single word ok.", emit=self.emit, cancel=threading.Event())
        self.assertNotEqual(self.kinds()[1], "limits")          # nothing invented at the start

    def test_an_update_outside_a_turn_is_reported_on_the_next(self):
        entries = load("ok-turn.jsonl")
        at = index_of(entries, "->", "turn/start")
        entries.insert(at, server("account/rateLimits/updated", {"rateLimits": {
            "primary": _window(62, 300, 1789926600), "secondary": _window(40, 10080, 1790499600),
            "planType": "plus", "rateLimitReachedType": None}}))
        harness, proc, _ = self.started(entries=entries)
        self.wait_for_limits(harness)
        harness.send("Reply with the single word ok.", emit=self.emit, cancel=threading.Event())
        self.assertEqual(self.kinds()[:2], ["started", "limits"])
        self.assertEqual(self.only("limits")[0]["windows"],
                         [{"kind": "5h", "used_percent": 62.0, "resets_at": 1789926600},
                          {"kind": "weekly", "used_percent": 40.0, "resets_at": 1790499600}])

    def test_updates_inside_a_turn_are_merged_sparsely_and_reported_at_once(self):
        entries = load("ok-turn.jsonl")
        del entries[index_of(entries, "<-", "account/rateLimits/updated")]   # the recorded one
        at = index_of(entries, "<-", "item/agentMessage/delta")
        entries.insert(at, server("account/rateLimits/updated", {"rateLimits": {
            "primary": _window(62, 300, 1789926600), "secondary": None}}))
        entries.insert(at + 1, server("account/rateLimits/updated", {"rateLimits": {
            "primary": None, "secondary": _window(40, 10080, 1790499600)}}))
        harness, _, _ = self.started(entries=entries)
        harness.send("Reply with the single word ok.", emit=self.emit, cancel=threading.Event())
        limits = self.only("limits")
        # Merged on the reader thread, reported on the turn thread: two updates that land before
        # the turn thread gets to the first are one event with the newest state, never a stale
        # one — so this is one or two events, and the last always shows the merge. The second
        # update named no primary: the one already known is kept, not cleared.
        self.assertIn(len(limits), (1, 2))
        self.assertEqual(limits[-1]["windows"],
                         [{"kind": "5h", "used_percent": 62.0, "resets_at": 1789926600},
                          {"kind": "weekly", "used_percent": 40.0, "resets_at": 1790499600}])
        self.assertLess(self.kinds().index("limits"), self.kinds().index("delta"))
        self.assertEqual(self.kinds()[-1], "usage")

    def test_the_kind_comes_from_the_duration_and_a_reached_limit_is_rejected(self):
        harness = gh.CodexHarness(codex_path=self.fake_codex, spawn=lambda argv, cwd: None)
        harness._merge_limits({"primary": _window(53, 10080, 1790065926), "secondary": None})
        self.assertEqual([w["kind"] for w in harness._limits_event()["windows"]], ["weekly"])
        harness._merge_limits({"primary": _window(7, 300, 9), "secondary": _window(53, 10080, 9)})
        event = harness._limits_event()
        self.assertEqual([w["kind"] for w in event["windows"]], ["5h", "weekly"])
        self.assertNotIn("status", event)
        harness._merge_limits({"rateLimitReachedType": "rate_limit_reached"})
        self.assertEqual(harness._limits_event()["status"], "rejected")
        # No duration at all: the guest's order decides. A zero reset is no reset.
        harness._limits = {}
        harness._merge_limits({"primary": {"usedPercent": 0, "windowDurationMins": 0,
                                           "resetsAt": 0}})
        self.assertEqual(harness._limits_event()["windows"],
                         [{"kind": "5h", "used_percent": 0.0, "resets_at": None}])
        # Nothing known, nothing said.
        harness._limits = {}
        self.assertEqual(harness._limits_event(), {})

    def test_banked_usage_resets_ride_the_limits_event(self):
        # The read answer carries `rateLimitResetCredits` beside the snapshot (the figure the
        # CLI's /usage panel counts down); an update notification does not, so what was read
        # survives one (29.3's sparse rule) and an event without the figure keeps the last count.
        harness = gh.CodexHarness(codex_path=self.fake_codex, spawn=lambda argv, cwd: None)
        harness._merge_limits({"primary": _window(53, 10080, 1790065926), "secondary": None},
                              credits={"availableCount": 1, "credits": []})
        event = harness._limits_event()
        self.assertEqual(event["resets_available"], 1)
        harness._merge_limits({"primary": _window(9, 10080, 1790065926), "secondary": None})
        self.assertEqual(harness._limits_event()["resets_available"], 1)
        # A fresh read with none banked says so; a read that reports no count at all (an older
        # server) leaves the last one, the same sparse rule the windows follow.
        harness._merge_limits({"primary": _window(9, 10080, 1790065926), "secondary": None},
                              credits={"availableCount": 0})
        self.assertEqual(harness._limits_event()["resets_available"], 0)
        harness._merge_limits({"primary": _window(9, 10080, 1790065926), "secondary": None},
                              credits={"availableCount": None, "credits": None})
        self.assertEqual(harness._limits_event()["resets_available"], 0)
        harness._merge_limits({"primary": _window(9, 10080, 1790065926), "secondary": None},
                              credits={"credits": []})
        self.assertEqual(harness._limits_event()["resets_available"], 0)


# ----- when things go wrong -----------------------------------------------------------------------


class RobustnessTest(HarnessCase):
    def test_an_unknown_notification_is_ignored(self):
        entries = load("ok-turn.jsonl")
        at = index_of(entries, "<-", "item/agentMessage/delta")
        entries.insert(at, server("thread/somethingNobodyHasWrittenYet", {"whatever": 1}))
        harness, _, _ = self.started(entries=entries)
        result = harness.send("Reply with the single word ok.", emit=self.emit,
                              cancel=threading.Event())
        self.assertEqual(self.kinds(), ["started", "delta", "limits", "usage"])
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
        self.assertEqual(self.kinds(), ["started", "delta", "limits", "usage"])

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


class ApprovalDetail(unittest.TestCase):
    """What the ask says a file change is about (protocol 29.3).

    v1's `applyPatchApproval` carried the paths; v2's `item/fileChange/requestApproval` carries
    only `itemId`, `reason` and `grantRoot`, so an ask built from the request alone could only
    quote codex's reason — "command failed; retry without sandbox?" — and never name the file the
    guest was about to write. Seen live on 2026-09-19 (`approval-drive.py`): the ask read "Apply
    edit · command failed; retry without sandbox?". The paths come from the item instead, which
    the adapter already records from `item/started` and `patchUpdated` for the diff.
    """

    METHOD = "item/fileChange/requestApproval"

    def test_the_item_names_the_file_when_the_request_cannot(self):
        detail = gh._approval_detail(
            self.METHOD, {"itemId": "fc-1", "reason": "command failed; retry without sandbox?"},
            {"changes": [{"path": "notes.txt", "kind": {"type": "add"}}]})
        self.assertEqual("edit notes.txt (command failed; retry without sandbox?)", detail)

    def test_several_files_are_named_up_to_three(self):
        item = {"changes": [{"path": f"f{n}.txt"} for n in range(5)]}
        detail = gh._approval_detail(self.METHOD, {"itemId": "fc-1"}, item)
        self.assertEqual("edit f0.txt, f1.txt, f2.txt…", detail)

    def test_the_v1_shape_still_carries_its_own_paths(self):
        detail = gh._approval_detail(
            "applyPatchApproval", {"changes": {"b.txt": {}, "a.txt": {}}}, None)
        self.assertEqual("edit a.txt, b.txt", detail)

    def test_with_neither_it_says_what_codex_said(self):
        self.assertEqual("command failed; retry without sandbox?", gh._approval_detail(
            self.METHOD, {"itemId": "fc-1", "reason": "command failed; retry without sandbox?"}, {}))
        self.assertEqual("apply its file changes",
                         gh._approval_detail(self.METHOD, {"itemId": "fc-1"}, {}))

    def test_a_command_approval_is_unaffected(self):
        self.assertEqual("rm -rf build", gh._approval_detail(
            "item/commandExecution/requestApproval", {"command": ["rm", "-rf", "build"]}, {}))


class LoginStatusTest(unittest.TestCase):
    """`codex login status` (codex-cli 0.155.1), as the worker's background scan reads it (29.3)."""

    def test_the_logged_in_line_and_exit_0(self):
        self.assertIs(gh.parse_login_status(0, "Logged in using ChatGPT\n"), True)
        self.assertIs(gh.parse_login_status(0, "Logged in using an API key - sk-***abcd\n"), True)

    def test_not_logged_in_is_stderr_and_exit_1(self):
        self.assertIs(gh.parse_login_status(1, "", "Not logged in\n"), False)
        # A status that cannot be read is not one to launch a turn on.
        self.assertIs(gh.parse_login_status(0, ""), False)
        self.assertIs(gh.parse_login_status(127, "", "codex: command not found\n"), False)

    def test_the_command_and_the_probe(self):
        self.assertEqual(gh.LOGIN_STATUS_ARGS, ("login", "status"))
        self.assertIsInstance(gh.CodexHarness.for_probe(codex_path="codex"), gh.CodexHarness)


if __name__ == "__main__":
    unittest.main()
