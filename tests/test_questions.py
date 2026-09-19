# SPDX-License-Identifier: GPL-3.0-or-later
"""`ask_user`: validation, open and multiple-choice questions, the round trip, skipping, Stop.

Protocol: docs/AGENT-SESSIONS-PROTOCOL.md section 27.
Card: issues/features/2026-09-19-the-planner-asks-the-user-questions.md (#MQ9C).
"""
import tempfile
import threading
import unittest
from pathlib import Path

from relay_core import questions as module
from relay_core.agent import Agent
from relay_core.planning import PLAN_MODE_NOTE
from relay_core.provider import Cancelled, ProviderConfig
from relay_core.questions import MAX_ASKS_PER_TURN, Questions
from relay_core.subagents import RestrictedExecutor
from relay_core.tools import ToolExecutor


OPEN = {"questions": [{
    "header": "Wording",
    "question": "What should the error message say when the file is missing?"}]}

ASK = {"questions": [{
    "header": "Scope",
    "question": "How far should the rename go?",
    "options": [{"label": "This file only", "description": "Leave the callers alone.",
                 "recommended": True},
                {"label": "The whole package", "description": "Rename every caller too."}]}]}


class FakePane:
    """Stands in for the GUI: answers every `question` the way a pane would."""

    def __init__(self, questions: Questions, reply=None):
        self.questions = questions
        self.reply = reply if reply is not None else (lambda event: {"answers": [["This file only"]]})
        self.seen = []

    def __call__(self, event):
        if event.get("event") != "question":
            return
        self.seen.append(event)
        answer = self.reply(event)
        if answer is not None:
            threading.Thread(target=self.questions.resolve,
                             args=({"id": event["id"], **answer},), daemon=True).start()


class QuestionsTestCase(unittest.TestCase):
    def setUp(self):
        self.events = []
        self.cancel = threading.Event()
        self.questions = Questions(self.events.append, self.cancel)
        self.questions.begin_turn()

    def wire(self, reply=None):
        pane = FakePane(self.questions, reply)
        self.questions.emit = lambda event: (self.events.append(event), pane(event))[0]
        return pane

    def ask(self, args=None):
        payload, preview = self.questions.prepare(args or ASK)
        self.preview = preview
        return self.questions.execute(payload, turn_id="t1")


class ValidationTests(QuestionsTestCase):
    def test_a_question_becomes_what_the_pane_draws(self):
        payload, preview = self.questions.prepare(ASK)
        question = payload["questions"][0]
        self.assertEqual(question["header"], "Scope")
        self.assertFalse(question["multiple"])
        self.assertTrue(question["options"][0]["recommended"])
        self.assertNotIn("recommended", question["options"][1])
        self.assertIn("ASK THE USER", preview)
        self.assertIn("How far should the rename go?", preview)

    def test_a_question_with_no_options_is_open(self):
        # Owner, 2026-09-19: "dont force multiple choice -- allow open-ended questions".
        payload, preview = self.questions.prepare(OPEN)
        question = payload["questions"][0]
        self.assertEqual(question["options"], [])
        self.assertFalse(question["multiple"])
        self.assertIn("(open)", preview)

    def test_one_option_is_not_a_question(self):
        args = {"questions": [{**ASK["questions"][0], "options": ASK["questions"][0]["options"][:1]}]}
        with self.assertRaises(ValueError):
            self.questions.prepare(args)

    def test_multiple_needs_something_to_choose_between(self):
        with self.assertRaisesRegex(ValueError, "options to choose between"):
            self.questions.prepare({"questions": [{**OPEN["questions"][0], "multiple": True}]})

    def test_a_catch_all_option_is_refused_because_the_pane_offers_one(self):
        args = {"questions": [{**ASK["questions"][0],
                               "options": [*ASK["questions"][0]["options"],
                                           {"label": module.CUSTOM_LABEL, "description": "x"}]}]}
        with self.assertRaisesRegex(ValueError, "offered already"):
            self.questions.prepare(args)

    def test_only_one_recommendation_per_question(self):
        options = [{**o, "recommended": True} for o in ASK["questions"][0]["options"]]
        with self.assertRaisesRegex(ValueError, "recommend at most one"):
            self.questions.prepare({"questions": [{**ASK["questions"][0], "options": options}]})

    def test_the_same_answer_is_not_offered_twice(self):
        options = [ASK["questions"][0]["options"][0], {"label": "this file only", "description": "x"}]
        with self.assertRaisesRegex(ValueError, "twice"):
            self.questions.prepare({"questions": [{**ASK["questions"][0], "options": options}]})

    def test_too_many_questions(self):
        with self.assertRaises(ValueError):
            self.questions.prepare({"questions": ASK["questions"] * (module.MAX_QUESTIONS + 1)})

    def test_an_unknown_field_is_named(self):
        with self.assertRaises(ValueError):
            self.questions.prepare({"questions": [{**ASK["questions"][0], "colour": "amber"}]})


class RoundTripTests(QuestionsTestCase):
    def test_the_turn_waits_for_the_pane_and_gets_the_answer(self):
        pane = self.wire()
        result = self.ask()
        self.assertTrue(result["ok"])
        self.assertEqual(result["answers"][0]["answer"], "This file only")
        self.assertEqual(result["answers"][0]["header"], "Scope")
        self.assertIn("This file only", result["summary"])
        self.assertNotIn("note", result)
        self.assertEqual(pane.seen[0]["turn_id"], "t1")
        self.assertEqual(pane.seen[0]["questions"][0]["header"], "Scope")

    def test_several_chosen_answers_come_back_as_one_line(self):
        self.wire(lambda event: {"answers": [["This file only", "The whole package"]]})
        self.assertEqual(self.ask()["answers"][0]["answer"], "This file only, The whole package")

    def test_an_open_question_comes_back_as_the_words_they_typed(self):
        self.wire(lambda event: {"answers": [["Say which file, and what to do about it."]]})
        result = self.ask(OPEN)
        self.assertEqual(result["answers"][0]["answer"], "Say which file, and what to do about it.")
        self.assertEqual(self.questions._pending, {})

    def test_the_users_own_words_are_the_answer(self):
        self.wire(lambda event: {"answers": [["neither: delete the function"]]})
        self.assertEqual(self.ask()["answers"][0]["answer"], "neither: delete the function")

    def test_a_skipped_question_is_answered_by_the_model_itself(self):
        self.wire(lambda event: {"answers": [[]]})
        result = self.ask()
        self.assertEqual(result["answers"][0]["answer"], module.UNANSWERED)
        self.assertIn("Do not ask again", result["note"])

    def test_the_cap_ends_the_interview(self):
        self.wire()
        for _ in range(MAX_ASKS_PER_TURN):
            self.assertTrue(self.ask()["ok"])
        refused = self.ask()
        self.assertFalse(refused["ok"])
        self.assertEqual(refused["refused"], "cap")
        self.assertIn("limit", refused["error"])

    def test_a_new_turn_asks_again(self):
        self.wire()
        for _ in range(MAX_ASKS_PER_TURN):
            self.ask()
        self.questions.end_turn()
        self.questions.begin_turn()
        self.assertTrue(self.ask()["ok"])

    def test_stop_unwinds_the_wait_and_closes_the_card(self):
        # The pane never answers; Stop is what ends it.
        self.wire(lambda event: None)
        threading.Timer(0.05, self.cancel.set).start()
        with self.assertRaises(Cancelled):
            self.ask()
        self.assertEqual([e["event"] for e in self.events if e["event"].startswith("question")],
                         ["question", "question_closed"])

    def test_a_turn_that_ends_takes_its_card_with_it(self):
        self.wire(lambda event: None)
        threading.Timer(0.05, self.questions.end_turn).start()
        with self.assertRaises(Cancelled):
            self.ask()
        # The pane is told, so a card is never left up for a turn that no longer exists.
        self.assertEqual([e["event"] for e in self.events if e["event"].startswith("question")],
                         ["question", "question_closed"])

    def test_an_answer_to_a_card_nobody_is_waiting_on_is_ignored(self):
        self.questions.resolve({"id": "q-does-not-exist", "answers": [["x"]]})


class ToolListTests(unittest.TestCase):
    def executor(self, cls=ToolExecutor, **kw):
        return cls("/tmp", lambda event: None, threading.Event(), **kw)

    def names(self, executor):
        return [tool["function"]["name"] for tool in executor.tools()]

    def test_the_pane_agent_can_ask_in_either_mode(self):
        # Owner, 2026-09-19: "let the non-plan agent use the questions as well (like warp / claude)".
        self.assertIn("ask_user", self.names(self.executor()))

    def test_a_subagent_cannot_reach_the_user(self):
        subagent = RestrictedExecutor("/tmp", lambda event: None, threading.Event(), None,
                                      {"read_file", "ask_user"})
        self.assertFalse(subagent.can_ask)
        self.assertNotIn("ask_user", self.names(subagent))
        with self.assertRaises(ValueError):
            subagent.prepare("ask_user", ASK)

    def test_plan_mode_tells_the_planner_to_ask_before_it_writes_the_plan(self):
        self.assertIn("ask_user", PLAN_MODE_NOTE)
        self.assertLess(PLAN_MODE_NOTE.index("ask_user"), PLAN_MODE_NOTE.index("write_plan"))

    def test_both_modes_hand_the_agent_the_tool(self):
        with tempfile.TemporaryDirectory() as root:
            agent = Agent(ProviderConfig("http://127.0.0.1:12345/v1", "mock", ""), str(Path(root)),
                          lambda event: None, provider=object())
            for mode in ("build", "plan"):
                agent.set_mode(mode)
                self.assertIn("ask_user", [t["function"]["name"] for t in agent.tools()], mode)
                self.assertEqual(agent._prepare("ask_user", ASK).name, "ask_user", mode)
                self.assertEqual(agent._prepare("ask_user", OPEN).name, "ask_user", mode)


if __name__ == "__main__":
    unittest.main()
