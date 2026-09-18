# SPDX-License-Identifier: GPL-3.0-or-later
"""`run_in_terminal`: the tool, the pane's ceiling on it, its cap and every refusal.

Protocol: docs/AGENT-SESSIONS-PROTOCOL.md section 22.
"""
import tempfile
import threading
import time
import unittest

from relay_core import agent as agent_module
from relay_core import terminal_handoff as module
from relay_core.agent import format_context, validate_context
from relay_core.provider import Cancelled
from relay_core.terminal_handoff import MAX_COMMAND, MAX_HANDOFFS, TerminalHandoff, validate_ceiling
from relay_core.tools import ToolExecutor

SSH = {"command": "ssh -t filly '~/bin/codex login'", "mode": "run", "intent": "log in on filly"}


class FakePane:
    """Stands in for the GUI: answers every `terminal_command` the way a pane would."""

    def __init__(self, handoff: TerminalHandoff, reply=None):
        self.handoff = handoff
        self.reply = reply or (lambda request: {
            "ok": True, "action": "started" if request["mode"] == "run" else "prefilled"})
        self.seen = []

    def __call__(self, event):
        if event.get("event") != "terminal_command":
            return
        self.seen.append(event)
        answer = self.reply(event)
        if answer is not None:
            threading.Thread(target=self.handoff.resolve,
                             args=({"id": event["id"], **answer},), daemon=True).start()


class HandoffTestCase(unittest.TestCase):
    def setUp(self):
        self.events = []
        self.cancel = threading.Event()
        self.handoff = TerminalHandoff(self.events.append, self.cancel)

    def wire(self, reply=None):
        pane = FakePane(self.handoff, reply)
        self.handoff.emit = lambda event: (self.events.append(event), pane(event))[0]
        return pane

    def call(self, **changes):
        payload, _ = self.handoff.prepare({**SSH, **changes})
        return self.handoff.execute(payload, turn_id="t1")


class OfferTests(HandoffTestCase):
    def test_no_tool_unless_the_pane_offers_it(self):
        self.assertFalse(self.handoff.available())
        self.handoff.begin_turn("agent")
        self.assertTrue(self.handoff.available())

    def test_the_offer_does_not_outlive_the_turn(self):
        self.handoff.begin_turn("agent")
        self.handoff.end_turn()
        self.assertFalse(self.handoff.available())

    def test_a_turn_without_the_field_clears_the_previous_one(self):
        self.handoff.begin_turn("agent")
        self.handoff.begin_turn(None)
        self.assertFalse(self.handoff.available())

    def test_unknown_ceilings_are_refused(self):
        for bad in ("run", "off", True, 1, {"mode": "agent"}):
            with self.assertRaises(ValueError):
                validate_ceiling(bad)

    def test_calling_it_when_not_offered_reaches_no_pane(self):
        pane = self.wire()
        self.assertEqual(self.call()["refused"], "not_offered")
        self.assertEqual(pane.seen, [])


class RoundTripTests(HandoffTestCase):
    def test_run_reaches_the_pane_and_says_the_output_comes_later(self):
        pane = self.wire()
        self.handoff.begin_turn("agent")
        result = self.call()
        self.assertEqual((result["ok"], result["action"]), (True, "started"))
        self.assertIn("follow-up turn", result["note"])
        self.assertNotIn("output", {k for k in result if k != "note"})
        request = pane.seen[0]
        self.assertEqual((request["command"], request["mode"], request["turn_id"], request["report_back"]),
                         (SSH["command"], "run", "t1", True))

    def test_prefill_is_the_models_choice_too(self):
        pane = self.wire()
        self.handoff.begin_turn("agent")
        result = self.call(mode="prefill")
        self.assertEqual(result["action"], "prefilled")
        self.assertNotIn("downgraded", result)
        self.assertEqual(pane.seen[0]["mode"], "prefill")

    def test_the_users_ceiling_downgrades_a_run(self):
        pane = self.wire()
        self.handoff.begin_turn("prefill")
        result = self.call(mode="run")
        self.assertEqual(pane.seen[0]["mode"], "prefill")
        self.assertEqual((result["action"], result["downgraded"]), ("prefilled", True))

    def test_the_pane_can_downgrade_a_run_itself(self):
        self.wire(lambda request: {"ok": True, "action": "prefilled"})
        self.handoff.begin_turn("agent")
        result = self.call()
        self.assertTrue(result["downgraded"])
        self.assertIn("not yet run", result["note"])

    def test_report_back_false_promises_no_follow_up(self):
        self.wire()
        self.handoff.begin_turn("agent")
        self.assertNotIn("follow-up", self.call(report_back=False)["note"])

    def test_late_replies_are_dropped(self):
        self.handoff.resolve({"id": "th-nope", "ok": True, "action": "started"})
        with self.assertRaises(ValueError):
            self.handoff.resolve({"ok": True})


class RefusalTests(HandoffTestCase):
    def test_the_users_draft_is_never_overwritten(self):
        self.wire(lambda request: {"ok": False, "code": "draft"})
        self.handoff.begin_turn("agent")
        result = self.call(mode="prefill")
        self.assertEqual(result["refused"], "draft")
        self.assertIn("fenced", result["error"])

    def test_a_refusal_does_not_count_against_the_cap(self):
        self.wire(lambda request: {"ok": False, "code": "busy"})
        self.handoff.begin_turn("agent")
        for _ in range(MAX_HANDOFFS + 2):
            self.assertEqual(self.call()["refused"], "busy")

    def test_cap_per_turn(self):
        pane = self.wire()
        self.handoff.begin_turn("agent")
        for _ in range(MAX_HANDOFFS):
            self.assertTrue(self.call()["ok"])
        self.assertEqual(self.call()["refused"], "cap")
        self.assertEqual(len(pane.seen), MAX_HANDOFFS)
        self.handoff.begin_turn("agent")
        self.assertTrue(self.call()["ok"])

    def test_bash_syntax_errors_never_reach_the_pane(self):
        pane = self.wire()
        self.handoff.begin_turn("agent")
        result = self.call(command="ssh filly 'unterminated")
        self.assertEqual(result["refused"], "syntax")
        self.assertEqual(pane.seen, [])

    def test_an_unknown_refusal_code_is_a_plain_failure(self):
        self.wire(lambda request: {"ok": False, "code": "rm -rf", "error": "no input file"})
        self.handoff.begin_turn("agent")
        result = self.call()
        self.assertEqual(result["refused"], "failed")
        self.assertIn("no input file", result["error"])

    def test_a_pane_that_never_answers_times_out(self):
        self.wire(lambda request: None)
        self.handoff.begin_turn("agent")
        old, module.REPLY_TIMEOUT = module.REPLY_TIMEOUT, 0.2
        try:
            started = time.monotonic()
            result = self.call()
        finally:
            module.REPLY_TIMEOUT = old
        self.assertEqual(result["refused"], "no_reply")
        self.assertLess(time.monotonic() - started, 3)

    def test_stopping_the_turn_releases_a_waiting_call(self):
        self.wire(lambda request: None)
        self.handoff.begin_turn("agent")
        threading.Timer(0.1, lambda: (self.cancel.set(), self.handoff.fail_pending("cancelled"))).start()
        with self.assertRaises(Cancelled):
            self.call()


class ArgumentTests(HandoffTestCase):
    def bad(self, **changes):
        args = {**SSH, **changes}
        with self.assertRaises(ValueError):
            self.handoff.prepare({k: v for k, v in args.items() if v is not None})

    def test_command_mode_and_intent_are_required(self):
        for key in ("command", "mode", "intent"):
            self.bad(**{key: None})
        self.bad(command="   ")
        self.bad(intent="")

    def test_control_characters_are_refused(self):
        for text in ("ls\x1b[2J", "ls\rrm -rf ~", "ls\x03", "ls\x7f", "ls\x9b"):
            self.bad(command=text)
        payload, _ = self.handoff.prepare({**SSH, "command": "for h in a b; do\n\tssh $h uptime\ndone"})
        self.assertIn("\n\t", payload["command"])

    def test_size_limits(self):
        self.bad(command="x" * (MAX_COMMAND + 1))
        self.bad(command="\n".join(["true"] * 60))
        self.bad(intent="x" * 201)

    def test_modes_and_flags_are_checked(self):
        self.bad(mode="auto")
        self.bad(report_back="yes")
        self.bad(cwd="/tmp")

    def test_the_preview_says_which_of_the_two_it_is(self):
        _, run = self.handoff.prepare(SSH)
        _, fill = self.handoff.prepare({**SSH, "mode": "prefill"})
        self.assertTrue(run.startswith("RUN IN TERMINAL"))
        self.assertTrue(fill.startswith("PREFILL PROMPT BOX"))
        self.assertIn(SSH["command"], run)


class ContextTests(unittest.TestCase):
    def test_the_ceiling_rides_on_the_prompt_context(self):
        context = validate_context({"terminal_cwd": "/home/u", "terminal_handoff": "agent"})
        self.assertEqual(context["terminal_handoff"], "agent")
        with self.assertRaises(ValueError):
            validate_context({"terminal_handoff": "always"})

    def test_the_field_alone_survives_validation_and_adds_no_note(self):
        self.assertEqual(validate_context({"terminal_handoff": "prefill"}), {"terminal_handoff": "prefill"})
        self.assertEqual(format_context({"terminal_handoff": "prefill"}), "")

    def test_the_system_prompt_states_the_rules(self):
        self.assertIn("run_in_terminal", agent_module.SYSTEM)
        self.assertIn("relay-run", agent_module.SYSTEM)


class ExecutorTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.events = []
        self.tools = ToolExecutor(self.tmp.name, self.events.append, threading.Event())

    def tearDown(self):
        self.tmp.cleanup()

    def names(self):
        return [tool["function"]["name"] for tool in self.tools.tools()]

    def test_the_tool_appears_only_when_offered(self):
        self.assertNotIn("run_in_terminal", self.names())
        self.tools.terminal.begin_turn("agent")
        self.assertIn("run_in_terminal", self.names())
        self.tools.terminal.end_turn()
        self.assertNotIn("run_in_terminal", self.names())

    def test_calling_it_when_not_offered_is_refused_not_executed(self):
        prepared = self.tools.prepare("run_in_terminal", SSH)
        self.assertEqual(prepared.name, "run_in_terminal")
        self.assertEqual(self.tools.execute(prepared)["refused"], "not_offered")
        self.assertEqual(self.events, [])


if __name__ == "__main__":
    unittest.main()
