# SPDX-License-Identifier: GPL-3.0-or-later
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
from relay_core.guest_harness import HarnessError, HarnessNotAvailable  # noqa: E402
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
        self.addCleanup(ghp._adapter_cache.clear)

    def tearDown(self):
        ghp._detected = None

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
                         {"model": "", "resume": None, "fork": False, "permissions": "bypass"})
        self.assertEqual(ghp.guest_options({"resume": "abc", "fork": True, "permissions": "ask"}),
                         {"model": "", "resume": "abc", "fork": True, "permissions": "ask"})
        for bad in ({"nope": 1}, {"fork": "yes"}, {"permissions": "maybe"}, {"model": 3}, "x"):
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
        self.assertEqual(claude["efforts"], [])
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
    def test_start_provider_passes_the_guest_block(self):
        harness = FakeHarness([], session_id="sess-1", model="opus-fake")
        with mock.patch.object(ghp, "make_harness", return_value=harness):
            provider = ghp.start_provider(
                "guest:claude", {"guest": {"model": "opus", "resume": "sess-1", "fork": True,
                                           "permissions": "ask"}}, "/tmp/ws")
        self.assertEqual(harness.starts, [{"cwd": "/tmp/ws", "model": "opus", "resume": "sess-1",
                                           "fork": True, "permissions": "ask"}])
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

    def test_an_approval_becomes_an_allow_deny_card(self):
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
        self.assertEqual([o["label"] for o in question["options"]], ["Allow", "Deny"])
        self.assertEqual(harness.answers, [("req-1", {"behavior": "allow"})])

    def test_deny_and_a_skipped_card_both_deny(self):
        script = [{"events": [ev("approval", id="req-2", kind="patch", detail="Write a file?")],
                   "result": ("done", "end", {})}]
        _, harness = self._drive(script, {"answers": [["Deny"]]})
        self.assertEqual(harness.answers[0][1]["behavior"], "deny")
        script = [{"events": [ev("approval", id="req-3", kind="other", detail="Something?")],
                   "result": ("done", "end", {})}]
        _, harness = self._drive(script, {"answers": [[]]})
        self.assertEqual(harness.answers[0][1]["behavior"], "deny")

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
                         {"guest": "claude", "guest_session": "guest-sess-7"})
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


class WorkerProtocolTests(unittest.TestCase):
    """The real worker loop, in process, with `make_harness` replaced. Nothing spawns."""

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
                 mock.patch.object(ghp, "adapter_available", lambda guest_id, refresh=False: True):
                import worker
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
