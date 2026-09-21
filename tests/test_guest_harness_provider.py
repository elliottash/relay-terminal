# SPDX-License-Identifier: AGPL-3.0-or-later
"""The worker side of Tier A (docs/AGENT-SESSIONS-PROTOCOL.md section 29.3).

A guest is a preset, `HarnessProvider` stands in for the chat provider, and one harness turn is one
Agent step. Everything here runs on `tests/guest_harness_fake.FakeHarness`: **no test starts a real
claude or codex**, because a real guest turn spends the owner's subscription (protocol 29).
"""
import io
import json
import os
import sys
import tempfile
import threading
import unittest
from pathlib import Path
from unittest import mock

sys.path.insert(0, str(Path(__file__).parent))

from relay_core import guest_harness_provider as ghp   # noqa: E402
from relay_core import session_protocol                # noqa: E402
from relay_core.agent import Agent                     # noqa: E402
from relay_core.guest_harness import (MAX_TOOL_OUTPUT_CHUNK, HarnessError,  # noqa: E402
                                      HarnessNotAvailable)
from relay_core.provider import Cancelled, ProviderConfig, ProviderError, content_parts  # noqa: E402
from guest_harness_fake import FakeHarness, ev         # noqa: E402

ROOT = Path(__file__).resolve().parents[1]
PNG = bytes.fromhex("89504e470d0a1a0a") + b"\x00" * 8


def guest_config(model="claude-fake"):
    return ProviderConfig("harness://claude", model, "", {}, 32_768)


def build(script, **kwargs):
    harness = FakeHarness(script, **kwargs)
    harness.start(cwd="/tmp")
    return harness, ghp.HarnessProvider(guest_config(), harness, "claude")


def run_turn(test, script, prompt="do the thing", **agent_kwargs):
    """One Agent turn served by a scripted harness. Returns (events, agent, provider)."""
    temp = tempfile.TemporaryDirectory()
    test.addCleanup(temp.cleanup)
    harness, provider = build(script)
    events = []
    agent = Agent(guest_config(), temp.name, events.append, provider=provider,
                  track_requests=False, completion_check=False, todo_tool=False, **agent_kwargs)
    ghp.attach(agent, provider)
    agent.ask(prompt)
    return events, agent, provider


class PresetTests(unittest.TestCase):
    def setUp(self):
        ghp._detected = None
        ghp._adapter_cache.clear()
        ghp.reset_catalog()
        self.addCleanup(ghp._adapter_cache.clear)
        self.addCleanup(ghp.reset_catalog)
        # No status command and no catalogue read runs from these tests.
        patcher = mock.patch.object(ghp, "_read_login_status", return_value=None)
        patcher.start()
        self.addCleanup(patcher.stop)

    def tearDown(self):
        ghp._detected = None

    def test_a_key_test_teaches_the_row_what_the_cli_proved(self):
        # `note_login` is the one other way the worker learns the answer: a guest key test's turn
        # ran (signed in) or was refused (signed out), and the row says so without a rescan.
        with mock.patch.object(ghp, "installations", return_value={
                "claude": {"installed": True, "binary": "/usr/bin/claude", "version": ""},
                "codex": {"installed": True, "binary": "/usr/bin/codex", "version": ""}}), \
             mock.patch.object(ghp, "_read_codex_catalog", return_value=[]):
            ghp.note_login("claude", True)
            ghp.note_login("codex", False)
            ghp.note_login("gemini", True)                 # not a guest: nothing to record
            rows = {row["id"]: row for row in ghp.preset_rows()}
            self.assertTrue(ghp.catalog_ready.wait(5.0))
        self.assertIs(rows["guest:claude"]["logged_in"], True)
        self.assertIs(rows["guest:codex"]["logged_in"], False)
        self.assertIsNone(ghp.login_status("gemini"))

    def test_the_probe_harness_is_the_adapters_own_variant(self):
        class Plain:
            made = []

            def __init__(self):
                Plain.made.append("plain")

        class WithProbe(Plain):
            @classmethod
            def for_probe(cls):
                Plain.made.append("probe")
                return cls()

        with mock.patch.object(ghp, "_load_adapter", return_value=WithProbe):
            ghp.make_harness("claude", probe=True)
            ghp.make_harness("claude")
        with mock.patch.object(ghp, "_load_adapter", return_value=Plain):
            ghp.make_harness("codex", probe=True)          # no variant: the plain one
        self.assertEqual(Plain.made, ["probe", "plain", "plain", "plain"])

    def test_preset_ids_and_config(self):
        self.assertEqual(ghp.preset_guest_id("guest:claude"), "claude")
        self.assertEqual(ghp.preset_guest_id("guest:codex"), "codex")
        self.assertIsNone(ghp.preset_guest_id("guest:gemini"))
        self.assertIsNone(ghp.preset_guest_id("kimi"))
        self.assertTrue(ghp.is_guest_preset("guest:codex"))

    def test_provider_config_for_a_guest_preset(self):
        config = session_protocol.provider_config({"preset": "guest:claude"})
        self.assertEqual(config.base_url, "harness://claude")
        self.assertEqual(config.model, "")
        self.assertEqual(config.api_key, "")
        self.assertFalse(config.local or config.hosted)
        self.assertEqual(ghp.config_guest_id(config), "claude")
        # The requested model travels; a key is never asked for and never looked up.
        asked = session_protocol.provider_config(
            {"preset": "guest:codex", "use_stored_key": True, "guest": {"model": "gpt-5-codex"}})
        self.assertEqual((asked.base_url, asked.model, asked.api_key),
                         ("harness://codex", "gpt-5-codex", ""))

    def test_guest_options_are_validated(self):
        self.assertEqual(ghp.guest_options(None),
                         {"model": "", "resume": None, "fork": False, "permissions": "bypass",
                          "effort": None})
        self.assertEqual(ghp.guest_options({"resume": "abc", "fork": True, "permissions": "ask"}),
                         {"model": "", "resume": "abc", "fork": True, "permissions": "ask",
                          "effort": None})
        # The guest's own levels, so `xhigh` and `ultra` pass where Relay's own four would not.
        for given, want in (("xhigh", "xhigh"), ("Ultra", "ultra"), ("", None), (None, None)):
            self.assertEqual(ghp.guest_options({"effort": given})["effort"], want, given)
        for bad in ({"nope": 1}, {"fork": "yes"}, {"permissions": "maybe"}, {"model": 3}, "x",
                    {"effort": "high and also"}, {"effort": 3}):
            with self.assertRaises(ValueError):
                ghp.guest_options(bad)

    def test_preset_rows(self):
        with mock.patch.object(ghp, "installations", return_value={
                "claude": {"installed": True, "binary": "/usr/bin/claude", "version": ""},
                "codex": {"installed": False, "binary": "", "version": ""}}), \
             mock.patch.object(ghp, "adapter_available", lambda guest_id, refresh=False: True):
            rows = {row["id"]: row for row in ghp.preset_rows()}
        self.assertEqual(sorted(rows), ["guest:claude", "guest:codex"])
        claude = rows["guest:claude"]
        self.assertEqual(claude["label"], "Claude Code")
        self.assertEqual(claude["guest"], "claude")
        self.assertTrue(claude["harness"] and claude["installed"])
        self.assertEqual(claude["base_url"], "harness://claude")
        self.assertEqual((claude["group"], claude["key_source"], claude["model"]),
                         ("guest", "guest", ""))
        self.assertFalse(claude["has_stored_key"] or claude["local"] or claude["hosted"])
        # Whether the CLI is signed in is null until the background scan has asked it, and stays
        # null for a guest that is not installed (there is no CLI to ask).
        self.assertIn("logged_in", claude)
        self.assertIsNone(rows["guest:codex"]["logged_in"])
        # The guest's own levels and its own models, so Options can offer both (owner 2026-09-19).
        self.assertEqual(claude["efforts"], ["low", "medium", "high", "xhigh", "max"])
        self.assertEqual(claude["effort_note"], "")
        self.assertEqual([row["id"] for row in claude["models"]],
                         ["fable", "opus", "sonnet", "haiku"])
        self.assertEqual(claude["models"][0]["efforts"], ["low", "medium", "high", "xhigh", "max"])
        # A guest this machine does not have offers nothing: there is no catalogue to read.
        self.assertEqual((rows["guest:codex"]["efforts"], rows["guest:codex"]["models"]), ([], []))
        # Installed but no adapter, and an adapter but not installed: both fall back to Tier B.
        self.assertFalse(rows["guest:codex"]["harness"])
        with mock.patch.object(ghp, "installations", return_value={
                "claude": {"installed": True, "binary": "/usr/bin/claude", "version": ""},
                "codex": {"installed": True, "binary": "/usr/bin/codex", "version": ""}}), \
             mock.patch.object(ghp, "adapter_available", lambda guest_id, refresh=False: False):
            self.assertFalse(any(row["harness"] for row in ghp.preset_rows()))

    def test_installations_never_run_a_binary(self):
        # `presets` is answered on the worker's protocol thread: no subprocess may happen there.
        with mock.patch("subprocess.run", side_effect=AssertionError("no subprocess")), \
             mock.patch("shutil.which", lambda name, path=None: "/usr/bin/" + name):
            found = ghp.installations(refresh=True)
        self.assertTrue(found["claude"]["installed"])
        self.assertEqual(found["claude"]["version"], "")

    def test_a_missing_adapter_is_not_available(self):
        with mock.patch.object(ghp, "_load_adapter", return_value=None):
            self.assertFalse(ghp.adapter_available("claude", refresh=True))
            with self.assertRaises(HarnessNotAvailable):
                ghp.make_harness("claude")
        with self.assertRaises(HarnessNotAvailable):
            ghp.make_harness("gemini")


class StartTests(unittest.TestCase):
    def test_start_provider_passes_the_effort_and_keeps_what_the_guest_says(self):
        harness = FakeHarness([], session_id="s", model="m")
        with mock.patch.object(ghp, "make_harness", return_value=harness):
            provider = ghp.start_provider("guest:codex", {"guest": {"effort": "xhigh"}}, "/tmp/ws")
        self.assertEqual(harness.starts[0]["effort"], "xhigh")
        self.assertEqual(provider.effort, "xhigh")
        # Codex answers `thread/start` with the level it actually came up on; that is what stands.
        class Clamping(FakeHarness):
            def start(self, **kwargs):
                started = super().start(**kwargs)
                self._effort = "medium"
                return started

        other = Clamping([], session_id="s", model="m")
        with mock.patch.object(ghp, "make_harness", return_value=other):
            provider = ghp.start_provider("guest:codex", {"guest": {"effort": "xhigh"}}, "/tmp/ws")
        self.assertEqual(provider.effort, "medium")

    def test_start_provider_passes_the_guest_block(self):
        harness = FakeHarness([], session_id="sess-1", model="opus-fake")
        with mock.patch.object(ghp, "make_harness", return_value=harness):
            provider = ghp.start_provider(
                "guest:claude", {"guest": {"model": "opus", "resume": "sess-1", "fork": True,
                                           "permissions": "ask"}}, "/tmp/ws")
        self.assertEqual(harness.starts, [{"cwd": "/tmp/ws", "model": "opus", "resume": "sess-1",
                                           "fork": True, "permissions": "ask", "effort": None}])
        self.assertEqual(provider.guest_id, "claude")
        self.assertEqual(provider.session_id, "sess-1")
        self.assertEqual(provider.config.model, "opus")
        self.assertEqual(provider.config.base_url, "harness://claude")

    def test_start_defaults_to_bypass_and_no_resume(self):
        harness = FakeHarness([])
        with mock.patch.object(ghp, "make_harness", return_value=harness):
            ghp.start_provider("guest:codex", {}, "/tmp/ws")
        self.assertEqual(harness.starts[0]["permissions"], "bypass")
        self.assertIsNone(harness.starts[0]["resume"])

    def test_a_guest_that_cannot_start_is_a_value_error(self):
        harness = FakeHarness([], start_error=HarnessNotAvailable("claude is not installed."))
        with mock.patch.object(ghp, "make_harness", return_value=harness):
            with self.assertRaisesRegex(ValueError, "not installed"):
                ghp.start_provider("guest:claude", {}, "/tmp/ws")
        self.assertTrue(harness.closed)   # nothing is left running behind a failed configure


class TurnTests(unittest.TestCase):
    """One harness turn is one Agent step: the 29.1 table, in the field names the GUI already uses."""

    def test_delta_thinking_tools_and_usage(self):
        script = [{"events": [ev("started", session_id="s-9", model="claude-fake"),
                              ev("thinking", text="pondering"),
                              ev("delta", text="Here "),
                              ev("tool_started", call_id="c1", tool="run_command",
                                 input={"command": "ls -la", "_guest_tool": "Bash"}),
                              ev("tool_result", call_id="c1", tool="run_command", output="a\nb\n",
                                 ok=True, ms=42),
                              ev("tool_started", call_id="c2", tool="edit_file",
                                 input={"file_path": "/w/note.txt", "_guest_tool": "Edit"}),
                              ev("tool_result", call_id="c2", tool="edit_file", output="ok", ok=True,
                                 diff="--- a/note.txt\n+++ b/note.txt\n-old\n+new\n"),
                              ev("notice", text="Claude compacted its context."),
                              ev("delta", text="you go."),
                              ev("usage", input_tokens=120, output_tokens=30, cost_usd=0.01),
                              ev("done", text="Here you go.", stop_reason="end")],
                   "result": ("Here you go.", "end", {"input_tokens": 120, "output_tokens": 30})}]
        events, agent, provider = run_turn(self, script)
        kinds = [e["event"] for e in events]

        self.assertEqual([e["text"] for e in events if e["event"] == "delta"], ["Here ", "you go."])
        thinking = [e for e in events if e["event"] == "thinking_delta"]
        self.assertEqual([e["text"] for e in thinking], ["pondering"])
        done_thinking = [e for e in events if e["event"] == "thinking_done"][0]
        self.assertEqual(done_thinking["chars"], len("pondering"))
        self.assertIn("turn_id", done_thinking)

        started = [e for e in events if e["event"] == "tool_started"]
        results = [e for e in events if e["event"] == "tool_result"]
        self.assertEqual([e["tool"] for e in started], ["run_command", "edit_file"])
        self.assertEqual([e["call_id"] for e in started], ["c1", "c2"])
        self.assertIn("ls -la", started[0]["preview"])
        self.assertEqual(started[0]["label"]["kind"], "run")
        self.assertIn("ls", started[0]["label"]["running"])
        self.assertTrue(all(e["turn_id"] for e in started + results))
        self.assertEqual(results[0]["result"]["output"], "a\nb\n")
        self.assertEqual(results[0]["ms"], 42)
        self.assertTrue(results[0]["label"]["ok"])
        self.assertEqual(results[1]["diff"], "--- a/note.txt\n+++ b/note.txt\n-old\n+new\n")
        self.assertEqual(results[1]["label"]["kind"], "edit")
        self.assertTrue(results[1]["label"]["inline_diff"])

        self.assertIn("status", kinds)            # `notice`
        usage = [e for e in events if e["event"] == "usage"]
        self.assertEqual(usage, [{"event": "usage",
                                  "usage": {"prompt_tokens": 120, "completion_tokens": 30,
                                            "total_tokens": 150, "cost": 0.01}}])
        self.assertEqual(events[-1]["event"], "done")
        self.assertIn("context", kinds)           # the Agent's own, computed from the usage

        # The assistant message is the guest's final text, with no tool calls for Relay to run.
        answer = agent.messages[-1]
        self.assertEqual(answer["role"], "assistant")
        self.assertEqual(answer["content"], "Here you go.")
        self.assertNotIn("tool_calls", answer)
        self.assertEqual(agent.usage_totals["total_tokens"], 150)
        self.assertEqual(provider.session_id, "s-9")

    def test_the_prompt_and_its_images_reach_the_harness(self):
        script = [{"events": [ev("delta", text="seen")], "result": ("seen", "end", {})}]
        events, agent, provider = run_turn(self, script, prompt="ignored")
        self.assertEqual(provider.harness.sent[0]["prompt"], "ignored")
        # Images: Relay's own content-part shape in, the contract's attachments out.
        message = {"role": "user", "content": content_parts("look", [{"media_type": "image/png", "raw": PNG}])}
        text, attachments = ghp.last_user_message([{"role": "system", "content": "s"}, message])
        self.assertEqual(text, "look")
        self.assertEqual(len(attachments), 1)
        self.assertEqual(attachments[0]["kind"], "image")
        self.assertEqual(attachments[0]["media_type"], "image/png")
        self.assertTrue(attachments[0]["data"])

    def test_one_record_per_tool_call_feeds_the_fold(self):
        script = [{"events": [ev("tool_started", call_id="c1", tool="edit_file",
                                 input={"file_path": "note.txt", "_guest_tool": "Edit"}),
                              ev("tool_result", call_id="c1", tool="edit_file", output="done", ok=True,
                                 diff="--- a\n+++ b\n+new line\n")],
                   "result": ("done", "end", {})}]
        events, agent, provider = run_turn(self, script)
        turn_id = [e for e in events if e["event"] == "tool_started"][0]["turn_id"]
        summary = [e for e in events if e["event"] == "turn_summary"][0]
        self.assertEqual([t["name"] for t in summary["tools"]], ["edit_file"])
        fold = agent.tool_output(turn_id, "c1")
        self.assertEqual(fold["name"], "edit_file")
        self.assertEqual(fold["diff"], "--- a\n+++ b\n+new line\n")
        self.assertTrue(fold["ok"])
        self.assertTrue(agent.turn_transcript(turn_id)["items"])

    def test_a_failed_tool_call_is_not_ok(self):
        script = [{"events": [ev("tool_started", call_id="c1", tool="run_command",
                                 input={"command": "false", "_guest_tool": "Bash"}),
                              ev("tool_result", call_id="c1", tool="run_command",
                                 output="no such file", ok=False)],
                   "result": ("that failed", "end", {})}]
        events, _, _ = run_turn(self, script)
        result = [e for e in events if e["event"] == "tool_result"][0]
        self.assertFalse(result["label"]["ok"])
        self.assertEqual(result["result"]["error"], "no such file")

    def test_an_unmapped_tool_keeps_the_guests_own_name_in_the_label(self):
        script = [{"events": [ev("tool_started", call_id="c1", tool="other",
                                 input={"_guest_tool": "TodoWrite"}),
                              ev("tool_result", call_id="c1", tool="other", output="[]", ok=True)],
                   "result": ("ok", "end", {})}]
        events, _, _ = run_turn(self, script)
        started = [e for e in events if e["event"] == "tool_started"][0]
        self.assertEqual(started["tool"], "other")
        self.assertIn("todo", started["label"]["running"].lower())

    def test_usage_is_emitted_once_when_only_the_result_reports_it(self):
        script = [{"events": [ev("delta", text="hi")],
                   "result": ("hi", "end", {"input_tokens": 7, "output_tokens": 3})}]
        events, _, _ = run_turn(self, script)
        usage = [e for e in events if e["event"] == "usage"]
        self.assertEqual(len(usage), 1)
        self.assertEqual(usage[0]["usage"]["total_tokens"], 10)

    def test_tool_output_streams_as_relays_own_live_command_output(self):
        """GT7X t:a3: a running call ticks instead of freezing until its result."""
        script = [{"events": [ev("tool_started", call_id="c1", tool="run_command",
                                 input={"command": "make -j8", "_guest_tool": "Bash"}),
                              ev("tool_output", call_id="c1", text="cc a.c\n"),
                              ev("tool_output", call_id="c1", text="cc b.c\n"),
                              ev("tool_result", call_id="c1", tool="run_command",
                                 output="cc a.c\ncc b.c\n", ok=True)],
                   "result": ("built", "end", {})}]
        events, _, _ = run_turn(self, script)
        live = [e for e in events if e["event"] == "tool_output"]
        # Exactly `tools.ToolRunner._await`'s event, plus the ids for the surfaces that key on
        # them: the terminal pane reads `text` and nothing else (protocol 11.1).
        self.assertEqual([e["text"] for e in live], ["cc a.c\n", "cc b.c\n"])
        self.assertEqual([e["call_id"] for e in live], ["c1", "c1"])
        self.assertTrue(all(e["turn_id"] for e in live))
        self.assertTrue(all("stored" not in e for e in live))
        # It comes between the call's start and its result, which is the whole point.
        kinds = [e["event"] for e in events]
        self.assertLess(kinds.index("tool_started"), kinds.index("tool_output"))
        self.assertLess(kinds.index("tool_output"), kinds.index("tool_result"))

    def test_the_live_stream_is_capped_per_call(self):
        cap = ghp.MAX_STREAMED_OUTPUT
        script = [{"events": [ev("tool_started", call_id="c1", tool="run_command",
                                 input={"command": "yes", "_guest_tool": "Bash"}),
                              ev("tool_output", call_id="c1", text="x" * (cap - 3)),
                              ev("tool_output", call_id="c1", text="y" * 100),
                              ev("tool_output", call_id="c1", text="z" * 100),
                              ev("tool_result", call_id="c1", tool="run_command",
                                 output="everything", ok=True),
                              ev("tool_started", call_id="c2", tool="run_command",
                                 input={"command": "echo", "_guest_tool": "Bash"}),
                              ev("tool_output", call_id="c2", text="fresh budget"),
                              ev("tool_result", call_id="c2", tool="run_command", output="fresh",
                                 ok=True)],
                   "result": ("done", "end", {})}]
        events, _, _ = run_turn(self, script)
        live = [e for e in events if e["event"] == "tool_output"]
        first = [e for e in live if e["call_id"] == "c1"]
        self.assertEqual(sum(len(e["text"]) for e in first), cap)
        self.assertEqual(first[-1]["text"], "y" * 3)          # cut at the budget, not dropped
        # And no event is bigger than a few KiB, whatever the harness handed over in one go.
        self.assertLessEqual(max(len(e["text"]) for e in live), MAX_TOOL_OUTPUT_CHUNK)
        # The next call starts from nothing, and the full output is still in the result.
        self.assertEqual([e["text"] for e in live if e["call_id"] == "c2"], ["fresh budget"])
        self.assertEqual([e for e in events if e["event"] == "tool_result"][0]["result"]["output"],
                         "everything")

    def test_the_guests_own_context_rides_the_usage_and_context_events(self):
        script = [{"events": [ev("usage", input_tokens=120, output_tokens=30, context_pct=5.2,
                                 context_tokens=13394, context_window=258400)],
                   "result": ("ok", "end", {})}]
        events, agent, provider = run_turn(self, script)
        usage = [e for e in events if e["event"] == "usage"][0]["usage"]
        self.assertEqual(usage["guest_context_tokens"], 13394)
        self.assertEqual(usage["guest_context_window"], 258400)
        self.assertEqual(usage["guest_context_pct"], 5.2)
        # ... and on the `context` event the pane's chip already reads, in the words
        # `Agent.context_event()` uses for Relay's own window.
        context = [e for e in events if e["event"] == "context"][-1]
        self.assertEqual(context["guest"], "claude")
        self.assertEqual(context["guest_context"],
                         {"used_tokens": 13394, "window": 258400, "percent": 5.2})
        # Relay's own numbers are untouched: this is the guest's window, not Relay's transcript.
        self.assertIn("used_tokens", context)
        self.assertNotEqual(context["used_tokens"], 13394)
        self.assertEqual(provider.guest_context["window"], 258400)

    def test_a_share_is_worked_out_when_the_guest_reports_only_the_two_numbers(self):
        mapped = ghp.relay_usage({"input_tokens": 1, "output_tokens": 1,
                                  "context_tokens": 5000, "context_window": 20000})
        self.assertEqual(mapped["guest_context_pct"], 25.0)
        # Nothing is invented when the guest says nothing.
        plain = ghp.relay_usage({"input_tokens": 1, "output_tokens": 1})
        self.assertNotIn("guest_context_pct", plain)
        self.assertNotIn("guest_context_window", plain)
        self.assertEqual(ghp.guest_context(plain), {})

    def test_both_guests_report_their_prefix_cache_in_the_same_two_fields(self):
        """#GMCF decision 5, and the one place the two guests disagree about what `input` means.

        The numbers are the recorded fixtures: guest_harness_claude/hello.jsonl's `result.usage`
        and guest_harness_codex/ok-turn.jsonl's `thread/tokenUsage/updated`.
        """
        # claude, through _usage_event: Anthropic's `input_tokens` is the UNCACHED part, so the
        # prompt is completed here to mean what every other provider's prompt_tokens means.
        claude = ghp.relay_usage({"input_tokens": 10, "output_tokens": 71,
                                  "cache_read_input_tokens": 13689,
                                  "cache_creation_input_tokens": 7624})
        self.assertEqual(claude["cached_tokens"], 13689)
        self.assertEqual(claude["cache_write_tokens"], 7624)
        self.assertEqual(claude["prompt_tokens"], 10 + 13689 + 7624)
        self.assertEqual(claude["total_tokens"], claude["prompt_tokens"] + 71)
        # codex, through _usage_for: `inputTokens` already counts the cached part inside itself.
        codex = ghp.relay_usage({"input_tokens": 13312, "output_tokens": 5,
                                 "cached_input_tokens": 11136, "cache_write_input_tokens": 0})
        self.assertEqual(codex["cached_tokens"], 11136)
        self.assertEqual(codex["cache_write_tokens"], 0)
        self.assertEqual(codex["prompt_tokens"], 13312)
        # A harness that says nothing about a cache leaves both off, as `cost` already does.
        plain = ghp.relay_usage({"input_tokens": 5, "output_tokens": 1})
        self.assertNotIn("cached_tokens", plain)
        self.assertNotIn("cache_write_tokens", plain)
        self.assertEqual(plain["prompt_tokens"], 5)

    def test_a_pane_that_is_not_on_a_guest_has_no_guest_context(self):
        script = [{"events": [ev("usage", input_tokens=1, output_tokens=1)],
                   "result": ("ok", "end", {})}]
        events, agent, provider = run_turn(self, script)
        context = [e for e in events if e["event"] == "context"][-1]
        self.assertNotIn("guest_context", context)
        ghp.detach(agent)
        self.assertNotIn("guest_context", agent.context_event())

    def test_a_harness_error_ends_the_turn_with_one_error_event(self):
        script = [{"events": [ev("delta", text="starting"),
                              ev("error", text="Claude Code lost its process.")],
                   "raise": HarnessError("Claude Code lost its process.")}]
        events, _, _ = run_turn(self, script)
        errors = [e for e in events if e["event"] == "error"]
        self.assertEqual(len(errors), 1)
        self.assertIn("lost its process", errors[0]["text"])
        self.assertEqual(events[-1]["event"], "error")

    def test_a_turn_that_stops_reason_error_also_fails(self):
        harness, provider = build([{"events": [], "result": ("nope", "error", {})}])
        with self.assertRaises(ProviderError):
            provider.complete([{"role": "user", "content": "go"}], [], lambda e: None, threading.Event())

    def test_an_interrupted_turn_is_a_cancel(self):
        harness, provider = build([{"events": [ev("delta", text="half")],
                                    "result": ("half", "interrupted", {})}])
        with self.assertRaises(Cancelled):
            provider.complete([{"role": "user", "content": "go"}], [], lambda e: None, threading.Event())

    def test_cancel_reaches_interrupt(self):
        cancel = threading.Event()

        def turn(prompt, attachments, emit, cancel_event, harness):
            emit(ev("delta", text="working"))
            provider.cancel()                       # what Agent.stop() does
            return __import__("relay_core.guest_harness", fromlist=["TurnResult"]).TurnResult(
                text="working", stop_reason="interrupted")

        harness, provider = build([turn])
        with self.assertRaises(Cancelled):
            provider.complete([{"role": "user", "content": "go"}], [], lambda e: None, cancel)
        self.assertEqual(harness.interrupts, 1)

    def test_an_opening_replaces_the_first_prompt_once(self):
        """A fresh session started mid-conversation (a plan turn on a High-list guest, 13.7) is
        handed an opening in place of its first prompt — built from the conversation when the call
        is made — and only that once; the last message's images still travel with it."""
        harness, provider = build([{"events": [], "result": ("one", "end", {})},
                                   {"events": [], "result": ("two", "end", {})}])
        seen = []

        def opening(messages):
            seen.append(len(messages))
            return "RULES + transcript + " + messages[-1]["content"][0]["text"]

        provider.opening = opening
        messages = [{"role": "user", "content": "first"},
                    {"role": "assistant", "content": "ok"},
                    {"role": "user", "content": content_parts("now plan", [{"media_type": "image/png", "raw": PNG}])}]
        provider.complete(messages, [], lambda e: None, threading.Event())
        self.assertEqual(seen, [3])
        self.assertEqual(harness.sent[0]["prompt"], "RULES + transcript + now plan")
        self.assertEqual(harness.sent[0]["attachments"][0]["kind"], "image")
        self.assertIsNone(provider.opening)
        provider.complete(messages + [{"role": "user", "content": "and then"}], [], lambda e: None, threading.Event())
        self.assertEqual(harness.sent[1]["prompt"], "and then")
        # A plain string is taken as it is.
        harness2, provider2 = build([{"events": [], "result": ("", "end", {})}])
        provider2.opening = "verbatim"
        provider2.complete([{"role": "user", "content": "ignored"}], [], lambda e: None, threading.Event())
        self.assertEqual(harness2.sent[0]["prompt"], "verbatim")

    def test_a_side_call_never_reaches_the_guest(self):
        """A pane title, a summary or compaction must not spend a guest turn (29.3 deviation note)."""
        events, agent, provider = run_turn(
            self, [{"events": [ev("delta", text="hi")], "result": ("hi", "end", {})}])
        turns_before = len(provider.harness.sent)
        message = provider.complete([{"role": "system", "content": "name this"},
                                     {"role": "user", "content": "transcript"}], [],
                                    lambda e: None, threading.Event())
        self.assertEqual(message, {"role": "assistant", "content": ""})
        self.assertEqual(len(provider.harness.sent), turns_before)


class LimitsTests(unittest.TestCase):
    """A harness `limits` event is the worker's `usage_limits` (29.3), and the last figures stay
    on the provider and on the guest's `presets` row for a picker opened later."""

    def setUp(self):
        ghp._LAST_LIMITS.clear()
        self.addCleanup(ghp._LAST_LIMITS.clear)

    def test_limits_become_usage_limits_and_are_remembered(self):
        windows = [{"kind": "5h", "used_percent": 62, "resets_at": 1789926600},
                   {"kind": "weekly", "used_percent": 40.04, "resets_at": 1790499600}]
        script = [{"events": [ev("limits", windows=list(reversed(windows)), status="allowed"),
                              ev("delta", text="ok")],
                   "result": ("ok", "end", {})}]
        events, agent, provider = run_turn(self, script)
        limits = [e for e in events if e["event"] == "usage_limits"]
        self.assertEqual(len(limits), 1)
        self.assertEqual(limits[0]["preset"], "guest:claude")
        self.assertEqual(limits[0]["guest"], "claude")
        self.assertEqual(limits[0]["status"], "allowed")
        self.assertEqual(limits[0]["windows"],                     # normalised, 5h then weekly
                         [{"kind": "5h", "used_percent": 62.0, "resets_at": 1789926600},
                          {"kind": "weekly", "used_percent": 40.0, "resets_at": 1790499600}])
        self.assertEqual(provider.usage_limits["windows"], limits[0]["windows"])
        self.assertEqual(provider.usage_limits["status"], "allowed")
        self.assertIsInstance(provider.usage_limits["updated_at"], int)
        with mock.patch.object(ghp, "installations", return_value={
                "claude": {"installed": True, "binary": "/usr/bin/claude", "version": ""},
                "codex": {"installed": True, "binary": "/usr/bin/codex", "version": ""}}), \
             mock.patch.object(ghp, "adapter_available", lambda guest_id, refresh=False: True):
            rows = {row["id"]: row for row in ghp.preset_rows()}
        self.assertEqual(rows["guest:claude"]["limits"]["windows"], limits[0]["windows"])
        self.assertNotIn("limits", rows["guest:codex"])           # codex has said nothing yet
        # The row is a copy: nobody's edit of it reaches the held figures.
        rows["guest:claude"]["limits"]["windows"].clear()
        self.assertEqual(len(ghp.last_limits("claude")["windows"]), 2)

    def test_a_report_with_no_usable_window_is_dropped(self):
        script = [{"events": [ev("limits", windows=[{"kind": "monthly", "used_percent": 1},
                                                    {"kind": "5h", "used_percent": "lots"}]),
                              ev("limits"),
                              ev("delta", text="ok")],
                   "result": ("ok", "end", {})}]
        events, agent, provider = run_turn(self, script)
        self.assertEqual([e for e in events if e["event"] == "usage_limits"], [])
        self.assertEqual(provider.usage_limits, {})
        self.assertEqual(ghp.last_limits("claude"), {})


class QuestionTests(unittest.TestCase):
    """Protocol 27 cards for a guest's approvals and questions, answered back through the harness."""

    def _drive(self, script, answer):
        temp = tempfile.TemporaryDirectory()
        self.addCleanup(temp.cleanup)
        harness, provider = build(script)
        events, lock = [], threading.Lock()

        def emit(event):
            with lock:
                events.append(event)
            if event.get("event") == "question":
                # The pane answering, from the protocol thread, as worker.py routes it.
                threading.Thread(target=lambda: ghp.answer_question(
                    provider, {"id": event["id"], **answer})).start()

        agent = Agent(guest_config(), temp.name, emit, provider=provider, track_requests=False,
                      completion_check=False, todo_tool=False)
        ghp.attach(agent, provider)
        agent.ask("go")
        return events, harness

    def test_an_approval_becomes_a_four_way_card(self):
        script = [{"events": [ev("approval", id="req-1", kind="command",
                                 detail="Run `rm -rf build`?")],
                   "result": ("done", "end", {})}]
        events, harness = self._drive(script, {"answers": [["Allow"]]})
        card = [e for e in events if e["event"] == "question"][0]
        self.assertTrue(card["id"].startswith("q-"))
        self.assertTrue(card["turn_id"])
        question = card["questions"][0]
        self.assertEqual(question["header"], "Run command")
        self.assertIn("rm -rf build", question["question"])
        self.assertEqual([o["label"] for o in question["options"]],
                         ["Allow", "Allow for session", "Deny", "Deny and stop"])
        self.assertEqual(harness.answers, [("req-1", {"behavior": "allow", "scope": "once"})])

    def test_deny_and_a_skipped_card_both_deny(self):
        script = [{"events": [ev("approval", id="req-2", kind="patch", detail="Write a file?")],
                   "result": ("done", "end", {})}]
        _, harness = self._drive(script, {"answers": [["Deny"]]})
        self.assertEqual(harness.answers[0][1],
                         {"behavior": "deny", "scope": "once",
                          "message": "The user did not allow this."})
        script = [{"events": [ev("approval", id="req-3", kind="other", detail="Something?")],
                   "result": ("done", "end", {})}]
        _, harness = self._drive(script, {"answers": [[]]})
        self.assertEqual(harness.answers[0][1]["behavior"], "deny")
        self.assertEqual(harness.answers[0][1]["scope"], "once")

    def test_the_two_wider_answers_carry_their_scope(self):
        """The GUI offers four options; each one is one `answer()` decision (GT7X t:a3)."""
        script = [{"events": [ev("approval", id="req-s", kind="command", detail="npm test?")],
                   "result": ("done", "end", {})}]
        _, harness = self._drive(script, {"answers": [["Allow for session"]]})
        self.assertEqual(harness.answers, [("req-s", {"behavior": "allow", "scope": "session"})])
        script = [{"events": [ev("approval", id="req-x", kind="command", detail="rm -rf /?")],
                   "result": ("done", "end", {})}]
        _, harness = self._drive(script, {"answers": [["Deny and stop"]]})
        self.assertEqual(harness.answers[0][1]["behavior"], "deny")
        self.assertEqual(harness.answers[0][1]["scope"], "stop")

    def test_an_option_this_build_does_not_know_is_a_plain_deny(self):
        self.assertEqual(ghp.approval_decision("Allow"), {"behavior": "allow", "scope": "once"})
        self.assertEqual(ghp.approval_decision("allow  for   SESSION"),
                         {"behavior": "allow", "scope": "session"})
        self.assertEqual(ghp.approval_decision("Allow once and for all"),
                         {"behavior": "deny", "scope": "once"})
        self.assertEqual(ghp.approval_decision(None), {"behavior": "deny", "scope": "once"})

    def test_a_question_keeps_the_per_question_answer_lists(self):
        script = [{"events": [ev("question", id="req-4", questions=[
            {"header": "Scope", "question": "How far should this go?",
             "options": [{"label": "This file"}, {"label": "The module"}], "multiSelect": True}])],
                   "result": ("done", "end", {})}]
        events, harness = self._drive(script, {"answers": [["This file", "The module"]]})
        card = [e for e in events if e["event"] == "question"][0]
        question = card["questions"][0]
        self.assertEqual(question["header"], "Scope")
        self.assertTrue(question["multiple"])
        self.assertEqual([o["label"] for o in question["options"]], ["This file", "The module"])
        self.assertEqual(harness.answers, [("req-4", {"answers": [["This file", "The module"]]})])

    def test_an_unknown_card_id_is_not_ours(self):
        harness, provider = build([])
        self.assertFalse(ghp.answer_question(provider, {"id": "q-nope", "answers": []}))
        self.assertFalse(ghp.answer_question(object(), {"id": "q-nope"}))


class AgentWiringTests(unittest.TestCase):
    def test_configured_fields_and_session_data(self):
        temp = tempfile.TemporaryDirectory()
        self.addCleanup(temp.cleanup)
        harness, provider = build([])
        provider.session_id = "guest-sess-7"
        events = []
        agent = Agent(guest_config(), temp.name, events.append, provider=provider,
                      session_dir=str(Path(temp.name) / "sessions"), track_requests=False)
        ghp.attach(agent, provider)
        self.assertEqual(ghp.configured_fields(agent),
                         {"guest": "claude", "guest_session": "guest-sess-7", "guest_effort": ""})
        fields = session_protocol.configured_fields(agent)
        self.assertEqual(fields["guest"], "claude")
        self.assertEqual(fields["guest_session"], "guest-sess-7")
        data = agent.session_data()
        self.assertEqual((data["guest"], data["guest_session"]), ("claude", "guest-sess-7"))
        self.assertEqual(ghp.session_guest(data), ("claude", "guest-sess-7"))
        # And a pane that is not on a guest says nothing about one.
        ghp.detach(agent)
        self.assertEqual(ghp.configured_fields(agent), {})
        self.assertNotIn("guest", agent.session_data())
        self.assertTrue(harness.closed)
        self.assertFalse(agent._injected_provider)
        # Attaching again re-points the wrapper rather than stacking another one.
        second, provider2 = build([])
        provider2.session_id = "guest-sess-8"
        agent.provider = provider2
        ghp.attach(agent, provider2)
        self.assertEqual(agent.session_data()["guest_session"], "guest-sess-8")

    def test_switching_the_guests_own_model_keeps_the_harness(self):
        temp = tempfile.TemporaryDirectory()
        self.addCleanup(temp.cleanup)
        harness, provider = build([])
        agent = Agent(guest_config(), temp.name, lambda e: None, provider=provider, track_requests=False)
        ghp.attach(agent, provider)
        same = ghp.switch_model(agent, "claude", {"guest": {"model": "opus"}})
        self.assertIs(same, provider)
        self.assertIn(("set_model", "opus"), harness.calls)
        self.assertEqual(provider.config.model, "opus")
        self.assertFalse(harness.closed)
        # Another guest, a resume and a fork all mean a fresh harness.
        self.assertIsNone(ghp.switch_model(agent, "codex", {}))
        self.assertIsNone(ghp.switch_model(agent, "claude", {"guest": {"resume": "s1"}}))
        self.assertIsNone(ghp.switch_model(agent, "claude", {"guest": {"fork": True}}))
        self.assertIsNone(ghp.switch_model(agent, None, {}))

    def test_set_model_with_a_guest_block_applies_the_model_and_the_effort(self):
        temp = tempfile.TemporaryDirectory()
        self.addCleanup(temp.cleanup)
        harness, provider = build([])
        agent = Agent(guest_config(), temp.name, lambda e: None, provider=provider,
                      track_requests=False)
        ghp.attach(agent, provider)
        same = ghp.switch_model(agent, "claude",
                                {"guest": {"model": "opus", "effort": "xhigh"}})
        self.assertIs(same, provider)
        self.assertEqual(harness.calls[-2:], [("set_model", "opus"), ("set_effort", "xhigh")])
        self.assertEqual(provider.effort, "xhigh")
        # Asking for the effort it already has does not ask the guest again.
        ghp.switch_model(agent, "claude", {"guest": {"effort": "xhigh"}})
        self.assertEqual(harness.calls[-1], ("set_effort", "xhigh"))

    def test_set_effort_tells_the_guest_and_leaves_the_provider_config_alone(self):
        temp = tempfile.TemporaryDirectory()
        self.addCleanup(temp.cleanup)
        harness, provider = build([])
        agent = Agent(guest_config(), temp.name, lambda e: None, provider=provider,
                      track_requests=False)
        ghp.attach(agent, provider)
        before = dict(agent.config.extra)
        self.assertEqual(ghp.set_effort(agent, "Ultra"),
                         {"guest": "claude", "guest_effort": "ultra", "effort": "ultra",
                          "applied": {}})
        self.assertIn(("set_effort", "ultra"), harness.calls)
        self.assertEqual(provider.effort, "ultra")
        self.assertEqual(agent.config.extra, before)     # no reasoning_effort is written anywhere
        self.assertEqual(ghp.configured_fields(agent)["guest_effort"], "ultra")

    def test_set_effort_on_a_pane_that_is_not_a_guest_says_so(self):
        temp = tempfile.TemporaryDirectory()
        self.addCleanup(temp.cleanup)
        agent = Agent(ProviderConfig("https://example.test/v1", "m", "k", {}, 1024), temp.name,
                      lambda e: None, track_requests=False)
        self.assertIsNone(ghp.set_effort(agent, "high"))

    def test_a_guest_that_refuses_the_effort_is_the_words_the_pane_shows(self):
        temp = tempfile.TemporaryDirectory()
        self.addCleanup(temp.cleanup)
        harness, provider = build([], effort_error=HarnessError("gpt-5.5 has no ultra."))
        agent = Agent(guest_config(), temp.name, lambda e: None, provider=provider,
                      track_requests=False)
        ghp.attach(agent, provider)
        with self.assertRaises(ValueError) as caught:
            ghp.set_effort(agent, "ultra")
        self.assertIn("gpt-5.5 has no ultra.", str(caught.exception))
        self.assertEqual(provider.effort, "")
        with self.assertRaises(ValueError):
            ghp.set_effort(agent, "")

    def test_resume_replaces_the_harness_with_one_on_the_recorded_session(self):
        temp = tempfile.TemporaryDirectory()
        self.addCleanup(temp.cleanup)
        harness, provider = build([])
        provider.permissions = "ask"
        events = []
        agent = Agent(guest_config(), temp.name, events.append, provider=provider, track_requests=False)
        ghp.attach(agent, provider)
        replacement = FakeHarness([], session_id="older-session")
        with mock.patch.object(ghp, "make_harness", return_value=replacement):
            ghp.resume_session(agent, {"guest": "claude", "guest_session": "older-session"},
                               events.append)
        self.assertEqual(replacement.starts[-1]["resume"], "older-session")
        self.assertEqual(replacement.starts[-1]["permissions"], "ask")
        self.assertIs(provider.harness, replacement)
        self.assertTrue(harness.closed)          # the old process goes, once the new one is up
        self.assertEqual(provider.session_id, "older-session")
        # A session of another guest, one already on it, or none at all, starts nothing.
        with mock.patch.object(ghp, "make_harness", side_effect=AssertionError("no new harness")):
            ghp.resume_session(agent, {"guest": "codex", "guest_session": "x"}, events.append)
            ghp.resume_session(agent, {"guest": "claude", "guest_session": "older-session"},
                               events.append)
            ghp.resume_session(agent, {}, events.append)

    def test_a_guest_that_will_not_resume_leaves_the_pane_as_it_was(self):
        temp = tempfile.TemporaryDirectory()
        self.addCleanup(temp.cleanup)
        harness, provider = build([])
        events = []
        agent = Agent(guest_config(), temp.name, events.append, provider=provider, track_requests=False)
        ghp.attach(agent, provider)
        broken = FakeHarness([], start_error=HarnessError("that session is gone."))
        with mock.patch.object(ghp, "make_harness", return_value=broken):
            ghp.resume_session(agent, {"guest": "claude", "guest_session": "gone"}, events.append)
        self.assertIs(provider.harness, harness)
        self.assertFalse(harness.closed)
        self.assertTrue(any(e["event"] == "status" and "could not resume" in e["text"] for e in events))


class CatalogueTests(unittest.TestCase):
    """`codex debug models`, read once per worker process in the background (29.3)."""

    def setUp(self):
        ghp.reset_catalog()
        ghp._detected = None
        self.addCleanup(ghp.reset_catalog)
        self.addCleanup(setattr, ghp, "_detected", None)
        # The login status commands share the scan; here they answer nothing, so the catalogue
        # is the only thing under test.
        patcher = mock.patch.object(ghp, "_read_login_status", return_value=None)
        patcher.start()
        self.addCleanup(patcher.stop)

    def _installed(self, binary="/usr/bin/codex"):
        return mock.patch.object(ghp, "installations", return_value={
            "claude": {"installed": True, "binary": "/usr/bin/claude", "version": ""},
            "codex": {"installed": bool(binary), "binary": binary, "version": ""}})

    def test_the_first_presets_answer_does_not_wait_for_codex(self):
        release = threading.Event()
        seen = []

        def slow(binary):
            seen.append(binary)
            release.wait(5.0)
            return [{"id": "gpt-6-astra", "label": "GPT-6-Astra",
                     "efforts": ["low", "high", "ultra"], "default_effort": "medium"}]

        with self._installed(), mock.patch.object(ghp, "_read_codex_catalog", slow):
            rows = {row["id"]: row for row in ghp.preset_rows()}
            # Answered on the protocol thread while the scan is still blocked.
            self.assertEqual(rows["guest:codex"]["models"], [])
            self.assertEqual(rows["guest:codex"]["efforts"],
                             ["low", "medium", "high", "xhigh", "max", "ultra"])
            release.set()
            self.assertTrue(ghp.catalog_ready.wait(5.0))
            rows = {row["id"]: row for row in ghp.preset_rows()}
        self.assertEqual([m["id"] for m in rows["guest:codex"]["models"]], ["gpt-6-astra"])
        self.assertEqual(rows["guest:codex"]["efforts"], ["low", "high", "ultra"])
        self.assertEqual(seen, ["/usr/bin/codex"])       # read once per worker process

    def test_a_codex_that_fails_or_is_missing_gets_the_fallback_menu(self):
        with self._installed(), mock.patch.object(ghp, "_read_codex_catalog",
                                                  side_effect=RuntimeError("not logged in")):
            ghp.preset_rows()
            self.assertTrue(ghp.catalog_ready.wait(5.0))
            rows = {row["id"]: row for row in ghp.preset_rows()}
        # The scan completed empty, so codex's four stand in (#E516), and the first row's own
        # levels are the row's efforts, exactly as for a scanned catalogue.
        self.assertEqual([m["id"] for m in rows["guest:codex"]["models"]],
                         ["gpt-6-astra", "gpt-5.6-sol", "gpt-5.6-terra", "gpt-5.6-luna"])
        self.assertEqual(rows["guest:codex"]["efforts"],
                         ["low", "medium", "high", "xhigh", "max", "ultra"])
        # Where each model starts when it is added to a list by hand (card #TKN7): codex's own
        # default for Main — `low` for gpt-5.6-sol, never its top level `ultra` — and `xhigh` for
        # High, the owner's rule for a plan turn.
        self.assertEqual({m["id"]: m["tier_effort"] for m in rows["guest:codex"]["models"]},
                         {"gpt-6-astra": {"main": "medium", "high": "xhigh", "flash": "low", "lite": "low"},
                          "gpt-5.6-sol": {"main": "low", "high": "xhigh", "flash": "low", "lite": "low"},
                          "gpt-5.6-terra": {"main": "medium", "high": "xhigh", "flash": "low", "lite": "low"},
                          "gpt-5.6-luna": {"main": "medium", "high": "xhigh", "flash": "low", "lite": "low"}})
        self.assertEqual(rows["guest:codex"]["models"][1]["default_effort"], "low")
        ghp.reset_catalog()
        ghp._detected = None
        with self._installed(binary=""), mock.patch.object(
                ghp, "_read_codex_catalog", side_effect=AssertionError("nothing to run")):
            rows = {row["id"]: row for row in ghp.preset_rows()}
        self.assertEqual((rows["guest:codex"]["models"], rows["guest:codex"]["efforts"]), ([], []))

    def test_the_scan_landing_is_what_the_worker_pushes_on(self):
        # The catalogue reaching the GUI at all is the listener: the worker re-emits `presets`
        # when a scan finishes, whatever it found (#E516), so Options' row does not wait for a
        # re-ask that never comes.
        pushes = []
        ghp.set_catalog_listener(lambda: pushes.append("landed"))
        self.addCleanup(ghp.set_catalog_listener, None)
        with self._installed(), mock.patch.object(ghp, "_read_codex_catalog", return_value=[]):
            ghp.preset_rows()
            self.assertTrue(ghp.catalog_ready.wait(5.0))
        self.assertEqual(pushes, ["landed"])

    def test_the_real_reader_parses_what_codex_debug_models_prints(self):
        printed = json.dumps({"models": [
            {"slug": "gpt-6-astra", "display_name": "GPT-6-Astra", "visibility": "list",
             "default_reasoning_level": "medium",
             "supported_reasoning_levels": [{"effort": "low", "description": "x"},
                                            {"effort": "ultra", "description": "y"}]},
            {"slug": "gpt-reserve", "display_name": "GPT-Reserve", "visibility": "hide",
             "default_reasoning_level": "medium", "supported_reasoning_levels": []}]})
        done = mock.Mock(returncode=0, stdout=printed, stderr="")
        with mock.patch.object(ghp.subprocess, "run", return_value=done) as run:
            rows = ghp._read_codex_catalog("/usr/bin/codex")
        self.assertEqual(run.call_args[0][0], ["/usr/bin/codex", "debug", "models"])
        self.assertEqual(run.call_args[1]["timeout"], ghp.CODEX_CATALOG_TIMEOUT)
        self.assertEqual(rows, [{"id": "gpt-6-astra", "name": "gpt-6-astra", "label": "gpt-6-astra",
                                 "efforts": ["low", "ultra"], "default_effort": "medium"}])
        with mock.patch.object(ghp.subprocess, "run",
                               return_value=mock.Mock(returncode=1, stdout="", stderr="no auth")):
            with self.assertRaises(RuntimeError):
                ghp._read_codex_catalog("/usr/bin/codex")


class WorkerProtocolTests(unittest.TestCase):
    """The real worker loop, in process, with `make_harness` replaced. Nothing spawns."""

    def setUp(self):
        ghp.reset_catalog()
        self.addCleanup(ghp.reset_catalog)

    def run_worker(self, messages, harness):
        script = "".join(json.dumps(m) + "\n" for m in messages).encode()
        out = io.StringIO()
        stdin = mock.Mock()
        stdin.buffer = io.BytesIO(script)
        with tempfile.TemporaryDirectory() as temp:
            env = {"HOME": temp, "XDG_DATA_HOME": str(Path(temp) / "data"),
                   "RELAY_KEYRING": "off", "RELAY_INDEX": str(Path(temp) / "index.sqlite")}
            with mock.patch.dict(os.environ, env), \
                 mock.patch.object(sys, "stdin", stdin), \
                 mock.patch.object(sys, "stdout", out), \
                 mock.patch.object(ghp, "make_harness", return_value=harness), \
                 mock.patch.object(ghp, "installations", return_value={
                     "claude": {"installed": True, "binary": "/usr/bin/claude", "version": ""},
                     "codex": {"installed": True, "binary": "/usr/bin/codex", "version": ""}}), \
                 mock.patch.object(ghp, "adapter_available", lambda guest_id, refresh=False: True), \
                 mock.patch.object(ghp, "_read_codex_catalog", return_value=[]):
                import worker
                # main() registers the worker's own "the scan landed" listener (a `presets`
                # push to stdout) and nothing unregisters it; here it would fire on every
                # later test's scan, printing the presets into the test run.
                self.addCleanup(ghp.set_catalog_listener, None)
                worker.main()
        return [json.loads(line) for line in out.getvalue().splitlines() if line.strip()]

    def test_configure_on_a_guest_then_set_model_away_closes_it(self):
        harness = FakeHarness([{"events": [], "result": ("", "end", {})}],
                              session_id="w-sess", model="claude-fake")
        events = self.run_worker([
            {"type": "presets", "id": "p"},
            {"type": "configure", "preset": "guest:claude", "workspace": str(ROOT),
             "guest": {"permissions": "bypass"}},
            {"type": "set_model", "id": "m", "base_url": "http://127.0.0.1:1/v1", "model": "m",
             "api_key": ""},
            {"type": "shutdown"}], harness)

        presets = [e for e in events if e["event"] == "presets"][0]
        rows = {p["id"]: p for p in presets["presets"]}
        self.assertIn("guest:claude", rows)
        self.assertTrue(rows["guest:claude"]["harness"])
        self.assertEqual(rows["guest:claude"]["group"], "guest")

        configured = [e for e in events if e["event"] == "configured"]
        self.assertTrue(configured, [e for e in events if e["event"] == "error"])
        self.assertEqual(configured[0]["guest"], "claude")
        self.assertEqual(configured[0]["guest_session"], "w-sess")
        self.assertEqual(configured[0]["model"], "claude-fake")
        self.assertEqual(harness.starts[0]["cwd"], str(ROOT))

        changed = [e for e in events if e["event"] == "model_changed" and e.get("id") == "m"]
        self.assertTrue(changed, [e for e in events if e["event"] == "error"])
        self.assertEqual(changed[0]["model"], "m")
        self.assertTrue(harness.closed)

    def test_the_pane_sets_the_guests_effort_and_the_event_carries_it(self):
        """The owner's ask (2026-09-19): pick the model *and* the reasoning effort for a guest.

        `set_effort` on a guest pane is the harness's own knob, so the level may be one Relay's
        four-level scale has never heard of (`xhigh`), and it comes back on the ordinary
        `effort_changed` beside `guest_effort`.
        """
        harness = FakeHarness([], session_id="w-sess", model="claude-fake")
        events = self.run_worker([
            {"type": "configure", "preset": "guest:claude", "workspace": str(ROOT),
             "guest": {"effort": "high"}},
            {"type": "set_effort", "id": "e", "effort": "xhigh"},
            {"type": "shutdown"}], harness)
        configured = [e for e in events if e["event"] == "configured"]
        self.assertTrue(configured, [e for e in events if e["event"] == "error"])
        self.assertEqual(configured[0]["guest_effort"], "high")
        self.assertEqual(harness.starts[0]["effort"], "high")
        changed = [e for e in events if e["event"] == "effort_changed"]
        self.assertEqual(changed[-1]["effort"], "xhigh")
        self.assertEqual(changed[-1]["guest_effort"], "xhigh")
        self.assertIn(("set_effort", "xhigh"), harness.calls)
        self.assertFalse([e for e in events if e["event"] == "error"])

    def test_set_model_carries_the_guests_model_and_effort_together(self):
        harness = FakeHarness([], session_id="w-sess", model="claude-fake")
        events = self.run_worker([
            {"type": "configure", "preset": "guest:claude", "workspace": str(ROOT)},
            {"type": "set_model", "id": "m", "preset": "guest:claude",
             "guest": {"model": "opus", "effort": "max"}},
            {"type": "shutdown"}], harness)
        changed = [e for e in events if e["event"] == "model_changed" and e.get("id") == "m"]
        self.assertTrue(changed, [e for e in events if e["event"] == "error"])
        self.assertEqual(changed[0]["model"], "opus")
        self.assertEqual(changed[0]["guest"], "claude")
        self.assertEqual(changed[0]["guest_effort"], "max")
        self.assertIn(("set_model", "opus"), harness.calls)
        self.assertIn(("set_effort", "max"), harness.calls)
        self.assertEqual(len(harness.starts), 1)         # the harness was kept, not restarted

    def test_a_second_configure_and_shutdown_both_close_the_harness(self):
        harness = FakeHarness([], session_id="w-sess")
        events = self.run_worker([
            {"type": "configure", "preset": "guest:claude", "workspace": str(ROOT)},
            {"type": "configure", "base_url": "http://127.0.0.1:1/v1", "model": "m", "api_key": "",
             "workspace": str(ROOT)},
            {"type": "shutdown"}], harness)
        self.assertEqual(len([e for e in events if e["event"] == "configured"]), 2)
        self.assertTrue(harness.closed)

        # And a worker that shuts down still on the guest takes the process with it.
        second = FakeHarness([], session_id="w-sess-2")
        self.run_worker([{"type": "configure", "preset": "guest:claude", "workspace": str(ROOT)},
                         {"type": "shutdown"}], second)
        self.assertTrue(second.closed)

    # ----- the helper worker never starts a guest (card #GH5T) --------------------------------
    def helper(self, harness, *, fallbacks=None, roles=None, keys=None):
        """One `configure` as the tab's helper worker: the window's guest preset, the
        `switchboard` role, and the Options › Models priority list the GUI sends with it."""
        request = {"type": "configure", "preset": "guest:claude", "workspace": str(ROOT),
                   "agent_role": "switchboard", "use_stored_key": True, "api_key": "",
                   "fallbacks": fallbacks if fallbacks is not None else []}
        if roles is not None:
            request["roles"] = roles
        with mock.patch.dict(os.environ, keys or {}):
            events = self.run_worker([request, {"type": "shutdown"}], harness)
        return events

    def test_the_helper_worker_follows_the_priority_list_instead_of_starting_a_guest(self):
        """Owner report, 2026-09-20: Main is Claude Code, and every helper turn answered "Base URL
        must be an HTTPS URL without credentials, query, or fragment". The helper's tools are
        Relay's own, which a guest does not take (#4NXH), so it runs on the first model of the
        priority list that can take a turn — and starts no guest process it could never use."""
        harness = FakeHarness([], session_id="w-sess", model="claude-fake")
        events = self.helper(harness,
                             fallbacks=[{"preset": "guest:codex", "model": ""},
                                        {"preset": "kimi", "model": "kimi-k3"}],
                             keys={"RELAY_KIMI_API_KEY": "k"})
        configured = [e for e in events if e["event"] == "configured"]
        self.assertTrue(configured, [e for e in events if e["event"] == "error"])
        self.assertEqual(harness.starts, [])              # nothing was started
        self.assertEqual(configured[0]["model"], "kimi-k3")
        self.assertNotIn("guest", configured[0])
        role = configured[0]["roles"]["switchboard"]
        self.assertEqual(role["preset"], "kimi")
        # What the model box says: "Follow Main — kimi-k3", and why, in its tooltip.
        self.assertEqual(configured[0]["tiers"]["main"]["model"], "kimi-k3")
        self.assertIn("Claude Code", role["note"])
        self.assertIn("kimi-k3", role["note"])

    def test_a_role_pick_is_honoured_under_a_guest_window_preset(self):
        harness = FakeHarness([], session_id="w-sess", model="claude-fake")
        events = self.helper(harness, roles={"switchboard": {"preset": "glm-coding"}},
                             keys={"RELAY_GLM_CODING_API_KEY": "k"})
        configured = [e for e in events if e["event"] == "configured"]
        self.assertTrue(configured, [e for e in events if e["event"] == "error"])
        self.assertEqual(harness.starts, [])
        self.assertEqual(configured[0]["agent_role"], "switchboard")
        self.assertEqual(configured[0]["roles"]["switchboard"]["preset"], "glm-coding")

    def test_with_nothing_usable_it_configures_anyway_and_a_turn_says_why(self):
        """The Switchboard is files, so the pane still opens and browses its cards; what it cannot
        do is answer, and it says so in a sentence instead of the endpoint error."""
        harness = FakeHarness([], session_id="w-sess", model="claude-fake")
        events = self.helper(harness, fallbacks=[{"preset": "kimi", "model": "kimi-k3"}])
        configured = [e for e in events if e["event"] == "configured"]
        self.assertTrue(configured, [e for e in events if e["event"] == "error"])
        self.assertEqual(harness.starts, [])
        self.assertIn("nothing else it can use",
                      configured[0]["roles"]["switchboard"]["note"])

    def test_a_pane_on_the_same_preset_still_starts_the_guest(self):
        harness = FakeHarness([], session_id="w-sess", model="claude-fake")
        events = self.run_worker([
            {"type": "configure", "preset": "guest:claude", "workspace": str(ROOT),
             "fallbacks": [{"preset": "kimi", "model": "kimi-k3"}]},
            {"type": "shutdown"}], harness)
        configured = [e for e in events if e["event"] == "configured"]
        self.assertTrue(configured, [e for e in events if e["event"] == "error"])
        self.assertEqual(len(harness.starts), 1)
        self.assertEqual(configured[0]["guest"], "claude")
        self.assertEqual(configured[0]["model"], "claude-fake")

    def test_a_guest_that_cannot_start_is_one_error(self):
        harness = FakeHarness([], start_error=HarnessNotAvailable("codex is not installed here."))
        events = self.run_worker([
            {"type": "configure", "preset": "guest:codex", "workspace": str(ROOT)},
            {"type": "shutdown"}], harness)
        self.assertFalse([e for e in events if e["event"] == "configured"])
        errors = [e for e in events if e["event"] == "error"]
        self.assertTrue(errors)
        self.assertIn("not installed", errors[0]["text"])


if __name__ == "__main__":
    unittest.main()
