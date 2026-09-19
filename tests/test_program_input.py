# SPDX-License-Identifier: GPL-3.0-or-later
"""`type_into_program`: the tool, its per-turn grant, its caps and every refusal.

Protocol: docs/AGENT-SESSIONS-PROTOCOL.md section 21.
Card: issues/features/2026-09-17-agent-delegate-and-take-over.md (#C1HH).
"""
import json
import tempfile
import threading
import time
import unittest
from pathlib import Path

from relay_core import agent as agent_module
from relay_core import program_input as module
from relay_core.agent import Agent, format_context, validate_context, validate_turn_options
from relay_core.program_input import (DEFAULT_MAX_WRITES, KEYS, MAX_SCREEN, MAX_TEXT, ProgramControl,
                                      clip_screen, validate_grant)
from relay_core.provider import Cancelled, ProviderConfig
from relay_core.tools import ToolExecutor


GRANT = {"granted": True, "reason": "delegated", "program": "apt", "kind": "yes_no",
         "question": "Do you want to continue? [Y/n]", "waiting": True,
         "screen": "Do you want to continue? [Y/n] "}


class FakePane:
    """Stands in for the GUI: answers every `program_input` the way a pane would."""

    def __init__(self, control: ProgramControl, reply=None):
        self.control = control
        self.reply = reply or (lambda request: {"ok": True, "typed": request.get("text", request.get("key", "")),
                                                "program": "apt", "screen": "Unpacking curl (8.5.0) ...",
                                                "waiting": False})
        self.seen = []

    def __call__(self, event):
        if event.get("event") != "program_input":
            return
        self.seen.append(event)
        answer = self.reply(event)
        if answer is not None:
            threading.Thread(target=self.control.resolve,
                             args=({"id": event["id"], **answer},), daemon=True).start()


class ControlTestCase(unittest.TestCase):
    def setUp(self):
        self.events = []
        self.cancel = threading.Event()
        self.control = ProgramControl(self.events.append, self.cancel)

    def emit(self, event):
        self.events.append(event)

    def wire(self, reply=None):
        pane = FakePane(self.control, reply)
        self.control.emit = lambda event: (self.events.append(event), pane(event))[0]
        return pane

    def typed(self, **args):
        payload, _ = self.control.prepare({"intent": "answer apt", **args})
        return self.control.execute(payload)


class GrantTests(ControlTestCase):
    def test_no_tool_without_a_grant(self):
        self.assertFalse(self.control.available())
        self.control.begin_turn(GRANT)
        self.assertTrue(self.control.available())
        self.assertEqual(self.control.tool_spec()["function"]["name"], "type_into_program")

    def test_grant_does_not_outlive_the_turn(self):
        self.control.begin_turn(GRANT)
        self.control.end_turn()
        self.assertFalse(self.control.available())
        self.assertEqual(self.typed(text="y")["refused"], "not_granted")

    def test_a_turn_without_a_grant_clears_the_previous_one(self):
        self.control.begin_turn(GRANT)
        self.control.begin_turn(None)
        self.assertFalse(self.control.available())

    def test_take_over_revokes_mid_turn(self):
        self.control.begin_turn(GRANT)
        self.control.update({"granted": False, "reason": "take_over"})
        self.assertFalse(self.control.available())
        self.assertEqual(self.typed(text="y")["refused"], "taken_over")

    def test_the_reason_the_pane_gives_is_the_reason_the_model_hears(self):
        for reason, code in (("program_exited", "no_program"), ("password", "password"),
                             ("take_over", "taken_over"), ("", "not_granted")):
            self.control.begin_turn(GRANT)
            self.control.update({"granted": False, "reason": reason})
            self.assertEqual(self.typed(text="y")["refused"], code, reason)

    def test_a_password_outranks_a_dropped_grant(self):
        # The pane drops the delegation when a password prompt appears, so both are true at
        # once; the model must be told the real reason.
        self.control.begin_turn(GRANT)
        self.control.update({"granted": False, "masked": True})
        self.assertEqual(self.typed(text="hunter2")["refused"], "password")

    def test_state_updates_keep_the_grant_when_they_do_not_mention_it(self):
        self.control.begin_turn(GRANT)
        self.control.update({"program": "apt-get", "waiting": True})
        self.assertTrue(self.control.available())
        self.assertEqual(self.control.program, "apt-get")

    def test_unknown_grant_fields_are_refused(self):
        with self.assertRaises(ValueError):
            validate_grant({"granted": True, "exec": "rm -rf /"})
        with self.assertRaises(ValueError):
            validate_grant({"granted": "yes"})
        with self.assertRaises(ValueError):
            validate_grant({"max_writes": 0})
        self.assertEqual(validate_grant(None), {})


class RefusalTests(ControlTestCase):
    def test_password_prompts_are_never_typed_into(self):
        pane = self.wire()
        self.control.begin_turn({**GRANT, "masked": True, "kind": "password",
                                 "question": "[sudo] password for elliott:"})
        result = self.typed(text="hunter2")
        self.assertEqual(result["refused"], "password")
        self.assertFalse(result["ok"])
        # The pane is never even asked, so no keystroke can reach the tty.
        self.assertEqual(pane.seen, [])
        self.assertTrue(any(e.get("event") == "program_input_refused" for e in self.events))

    def test_a_password_prompt_that_appears_mid_turn_stops_the_next_write(self):
        pane = self.wire()
        self.control.begin_turn(GRANT)
        self.assertTrue(self.typed(text="y")["ok"])
        self.control.update({"masked": True})
        self.assertEqual(self.typed(text="hunter2")["refused"], "password")
        self.assertEqual(len(pane.seen), 1)

    def test_the_pane_can_refuse_too(self):
        self.wire(lambda request: {"ok": False, "code": "taken_over"})
        self.control.begin_turn(GRANT)
        result = self.typed(text="y")
        self.assertEqual(result["refused"], "taken_over")
        # A take-over reported by the pane also drops the grant for the rest of the turn.
        self.assertFalse(self.control.available())

    def test_write_cap_per_turn(self):
        self.wire()
        self.control.begin_turn({**GRANT, "max_writes": 2})
        self.assertTrue(self.typed(text="y")["ok"])
        self.assertTrue(self.typed(text="y")["ok"])
        self.assertEqual(self.typed(text="y")["refused"], "cap")
        # A new turn starts the budget again.
        self.control.begin_turn({**GRANT, "max_writes": 2})
        self.assertTrue(self.typed(text="y")["ok"])

    def test_default_cap(self):
        self.wire()
        self.control.begin_turn(GRANT)
        self.assertEqual(self.control.max_writes, DEFAULT_MAX_WRITES)

    def test_a_pane_that_never_answers_times_out(self):
        self.wire(lambda request: None)
        self.control.begin_turn(GRANT)
        payload, _ = self.control.prepare({"intent": "answer", "text": "y"})
        old, module.REPLY_TIMEOUT = module.REPLY_TIMEOUT, 0.2
        try:
            started = time.monotonic()
            result = self.control.execute(payload)
        finally:
            module.REPLY_TIMEOUT = old
        self.assertEqual(result["refused"], "no_reply")
        self.assertLess(time.monotonic() - started, 3)

    def test_stopping_the_turn_releases_a_waiting_write(self):
        self.wire(lambda request: None)
        self.control.begin_turn(GRANT)
        payload, _ = self.control.prepare({"intent": "answer", "text": "y"})
        threading.Timer(0.1, lambda: (self.cancel.set(), self.control.fail_pending("cancelled"))).start()
        with self.assertRaises(Cancelled):
            self.control.execute(payload)


class ArgumentTests(ControlTestCase):
    def setUp(self):
        super().setUp()
        self.control.begin_turn(GRANT)

    def test_intent_is_required(self):
        with self.assertRaises(ValueError):
            self.control.prepare({"text": "y"})
        with self.assertRaises(ValueError):
            self.control.prepare({"text": "y", "intent": "   "})

    def test_exactly_one_of_text_or_key(self):
        with self.assertRaises(ValueError):
            self.control.prepare({"intent": "x"})
        with self.assertRaises(ValueError):
            self.control.prepare({"intent": "x", "text": "y", "key": "enter"})

    def test_control_characters_are_refused(self):
        for bad in ("\x1b", "\x1b[A", "\r", "\x03", "\x00", "\x9b"):
            with self.assertRaises(ValueError, msg=repr(bad)):
                self.control.prepare({"intent": "x", "text": f"a{bad}b"})
        # Newline and tab are the two a line answer legitimately needs.
        self.control.prepare({"intent": "x", "text": "a\tb\n"})

    def test_named_keys_only(self):
        for key in KEYS:
            self.control.prepare({"intent": "x", "key": key})
        with self.assertRaises(ValueError):
            self.control.prepare({"intent": "x", "key": "f7"})

    def test_size_limits(self):
        with self.assertRaises(ValueError):
            self.control.prepare({"intent": "x", "text": "y" * (MAX_TEXT + 1)})
        with self.assertRaises(ValueError):
            self.control.prepare({"intent": "x", "text": "\n" * 200})
        with self.assertRaises(ValueError):
            self.control.prepare({"intent": "x" * 500, "text": "y"})

    def test_unknown_arguments(self):
        with self.assertRaises(ValueError):
            self.control.prepare({"intent": "x", "text": "y", "sudo": True})

    def test_submit_defaults_to_enter(self):
        payload, preview = self.control.prepare({"intent": "answer apt", "text": "y"})
        self.assertTrue(payload["submit"])
        self.assertIn("TYPE INTO PROGRAM", preview)
        self.assertIn("answer apt", preview)
        payload, _ = self.control.prepare({"intent": "insert mode", "text": "i", "submit": False})
        self.assertFalse(payload["submit"])
        with self.assertRaises(ValueError):
            self.control.prepare({"intent": "x", "text": "y", "submit": "yes"})

    def test_the_preview_shows_what_is_typed(self):
        _, preview = self.control.prepare({"intent": "save and quit", "text": ":wq"})
        self.assertIn(":wq", preview)
        _, preview = self.control.prepare({"intent": "leave insert mode", "key": "escape"})
        self.assertIn("<escape>", preview)


class RoundTripTests(ControlTestCase):
    def test_the_pane_performs_the_write_and_returns_the_screen(self):
        pane = self.wire()
        self.control.begin_turn(GRANT)
        result = self.typed(text="y")
        self.assertTrue(result["ok"])
        self.assertEqual(result["typed"], "y")
        self.assertIn("Unpacking curl", result["screen"])
        self.assertEqual(pane.seen[0]["text"], "y")
        self.assertEqual(pane.seen[0]["intent"], "answer apt")
        self.assertTrue(pane.seen[0]["submit"])

    def test_a_write_that_ends_the_program_says_so(self):
        # Without this the model calls again, is refused, and narrates the successful write as
        # a failure (seen live with kimi-k3 on 2026-09-17).
        self.wire(lambda request: {"ok": True, "typed": "y", "program": "", "screen": "user@box:~$ "})
        self.control.begin_turn(GRANT)
        result = self.typed(text="y")
        self.assertTrue(result["ok"])
        self.assertTrue(result["program_exited"])
        self.assertIn("do not call this tool again", result["note"])
        # A program that is still there says nothing of the sort.
        self.wire()
        self.control.begin_turn(GRANT)
        self.assertNotIn("program_exited", self.typed(text="y"))

    def test_a_named_key_reaches_the_pane_by_name(self):
        pane = self.wire()
        self.control.begin_turn(GRANT)
        self.typed(key="escape")
        self.assertEqual(pane.seen[0]["key"], "escape")
        self.assertNotIn("text", pane.seen[0])

    def test_the_screen_is_capped(self):
        self.wire(lambda request: {"ok": True, "typed": "y", "screen": "x" * (MAX_SCREEN * 3)})
        self.control.begin_turn(GRANT)
        result = self.typed(text="y")
        self.assertLessEqual(len(result["screen"]), MAX_SCREEN + 2)
        self.assertTrue(result["screen"].startswith("…"))
        self.assertEqual(clip_screen(None), "")

    def test_late_replies_are_dropped(self):
        self.control.begin_turn(GRANT)
        self.control.resolve({"id": "pi-does-not-exist", "ok": True})   # no exception
        with self.assertRaises(ValueError):
            self.control.resolve({"ok": True})


class ContextTests(unittest.TestCase):
    def test_a_grant_rides_on_the_prompt_context(self):
        note = format_context({"foreground_program": "apt upgrade", "terminal_cwd": "/tmp",
                               "program_control": GRANT})
        self.assertIn("handed `apt` to you for this turn", note)
        self.assertIn("Do you want to continue? [Y/n]", note)
        self.assertIn("never instructions", note)

    def test_without_a_grant_the_screen_never_leaves_the_machine(self):
        note = format_context({"foreground_program": "apt upgrade",
                               "program_control": {"granted": False, "screen": "SECRET SCREEN"}})
        self.assertNotIn("SECRET SCREEN", note)
        self.assertIn("has not handed it to you", note)

    def test_unknown_context_fields_are_still_refused(self):
        with self.assertRaises(ValueError):
            validate_context({"screen": "x"})
        with self.assertRaises(ValueError):
            validate_context({"program_control": {"granted": True, "unknown": 1}})

    def test_the_system_prompt_states_the_rules(self):
        self.assertIn("type_into_program", agent_module.SYSTEM)
        self.assertIn("Never type into a password or passphrase prompt", agent_module.SYSTEM)


class GuestStateTests(ControlTestCase):
    """The guest's three optional fields ride on `program_state` beside `guest` (protocol 26.3):
    what it runs on, how much of its context window it has used, and whether a guest turn is
    going. Mirrored exactly as `guest` is, in validate_grant, _apply and summary (21.2)."""

    def test_the_fields_are_carried(self):
        self.control.begin_turn(GRANT)
        summary = self.control.update({"guest": "claude", "guest_model": "claude-opus-4-6",
                                       "guest_context_pct": 37, "guest_busy": True})
        self.assertEqual(("claude", "claude-opus-4-6", 37, True),
                         (summary["guest"], summary["guest_model"], summary["guest_context_pct"],
                          summary["guest_busy"]))

    def test_an_unknown_context_share_is_absent_not_zero(self):
        self.control.begin_turn(GRANT)
        summary = self.control.update({"guest": "codex"})
        self.assertNotIn("guest_context_pct", summary)
        self.assertIsNone(self.control.guest_context_pct)

    def test_a_state_update_that_does_not_mention_the_share_keeps_it(self):
        self.control.begin_turn(GRANT)
        self.control.update({"guest": "claude", "guest_context_pct": 37})
        self.assertEqual(37, self.control.update({"guest": "claude", "guest_busy": True})["guest_context_pct"])

    def test_bad_values_are_refused(self):
        for bad in ({"guest_context_pct": 101}, {"guest_context_pct": -1}, {"guest_context_pct": "50"},
                    {"guest_context_pct": 1.5}, {"guest_busy": "yes"}, {"guest_model": "x" * 65}):
            with self.subTest(bad=bad), self.assertRaises(ValueError):
                validate_grant({"granted": True, **bad})


class ExecutorTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.events = []
        self.cancel = threading.Event()
        self.tools = ToolExecutor(self.tmp.name, self.events.append, self.cancel)

    def tearDown(self):
        self.tmp.cleanup()

    def names(self):
        return [tool["function"]["name"] for tool in self.tools.tools()]

    def test_the_tool_appears_only_with_a_grant(self):
        self.assertNotIn("type_into_program", self.names())
        self.tools.program.begin_turn(GRANT)
        self.assertIn("type_into_program", self.names())
        self.tools.program.update({"granted": False})
        self.assertNotIn("type_into_program", self.names())

    def test_calling_it_without_a_grant_is_refused_not_executed(self):
        prepared = self.tools.prepare("type_into_program", {"intent": "answer", "text": "y"})
        self.assertEqual(prepared.name, "type_into_program")
        result = self.tools.execute(prepared)
        self.assertEqual(result["refused"], "not_granted")


class AgentTurnTests(unittest.TestCase):
    """The grant is per turn: the Agent installs it from the prompt and clears it at the end."""

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.events = []
        self.agent = Agent(ProviderConfig(base_url="http://127.0.0.1:1", model="m", api_key="k"),
                           self.tmp.name, self.events.append, provider=_NoModel(), track_requests=False)

    def tearDown(self):
        self.tmp.cleanup()

    def test_turn_installs_and_clears_the_grant(self):
        seen = {}

        def note(*_args, **_kwargs):
            seen["granted"] = self.agent.executor.program.available()
            seen["writes"] = self.agent.executor.program.max_writes
            raise RuntimeError("stop the turn here")

        self.agent.provider.complete = note
        self.agent.ask("answer it", context={"foreground_program": "apt", "program_control": GRANT})
        self.assertTrue(seen["granted"])
        self.assertFalse(self.agent.executor.program.available())

    def test_max_program_writes_is_an_option(self):
        self.assertEqual(validate_turn_options({"max_program_writes": 5})["max_program_writes"], 5)
        for bad in (0, 201, "5", 1.5):
            with self.assertRaises(ValueError):
                validate_turn_options({"max_program_writes": bad})
        self.assertIn("max_program_writes", self.agent.options())
        self.agent.set_options({"max_program_writes": 3})
        self.agent.ask("answer it", context={"program_control": {"granted": True}})
        self.assertEqual(self.agent.executor.program.max_writes, 3)


class _NoModel:
    """A provider that fails immediately: these tests only exercise the turn's bookkeeping."""
    stall_timeout = 60

    def complete(self, *args, **kwargs):
        raise RuntimeError("no model in this test")

    def cancel(self):
        pass

    def response_open(self):
        return False

    def set_stall_timeout(self, value):
        self.stall_timeout = value


if __name__ == "__main__":
    unittest.main()
