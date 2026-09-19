# SPDX-License-Identifier: GPL-3.0-or-later
"""`ask_user`: validation, open and multiple-choice questions, the round trip, skipping, Stop.

Protocol: docs/AGENT-SESSIONS-PROTOCOL.md section 27.
Card: issues/features/2026-09-19-the-planner-asks-the-user-questions.md (#MQ9C).
"""
import re
import tempfile
import threading
import time
import unittest
from pathlib import Path
from unittest import mock

from relay_core import questions as module
from relay_core.agent import Agent
from relay_core.planning import PLAN_MODE_NOTE
from relay_core.provider import Cancelled, ProviderConfig
from relay_core.questions import MAX_ASKS_PER_TURN, Questions
from relay_core.subagents import RestrictedExecutor
from relay_core.tools import ToolExecutor

ROOT = Path(__file__).resolve().parents[1]


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

    def test_recommended_must_be_a_boolean_and_not_merely_truthy(self):
        # The type check used to sit *inside* `if option.get("recommended")`, so `0` and `""` —
        # a model getting the schema wrong — were read as "not recommended" and never reported.
        for value in (0, "", "yes", 1, None):
            options = [{**ASK["questions"][0]["options"][0], "recommended": value},
                       ASK["questions"][0]["options"][1]]
            with self.subTest(recommended=value), \
                    self.assertRaisesRegex(ValueError, "recommended must be true or false"):
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

    def test_a_long_typed_answer_is_kept_whole_up_to_a_paragraph(self):
        # An open question ("what should the error message say?") can fairly be answered at length.
        typed = "word " * 700                                     # 3500 characters
        self.wire(lambda event: {"answers": [[typed]]})
        answer = self.ask(OPEN)["answers"][0]["answer"]
        self.assertEqual(answer, typed.strip())
        self.assertNotIn(module.ANSWER_CUT, answer)

    def test_an_answer_that_is_cut_says_so_where_the_model_can_see_it(self):
        self.wire(lambda event: {"answers": [["x" * (module.MAX_ANSWER + 500)]]})
        answer = self.ask(OPEN)["answers"][0]["answer"]
        self.assertEqual(len(answer), module.MAX_ANSWER)
        # A cut that says nothing reads as the user stopping mid-sentence, and the model acts on
        # half of one.
        self.assertTrue(answer.endswith(module.ANSWER_CUT))

    def test_a_card_id_is_never_reused_by_the_next_worker(self):
        # `q-1` again in every process: a pane that outlived a worker could answer a new card with
        # an old card's id and be believed.
        ids = {module._call_id() for _ in range(100)}
        self.assertEqual(len(ids), 100)
        self.assertNotIn("q-1", ids)
        pane = self.wire()
        self.ask()
        self.assertTrue(pane.seen[0]["id"].startswith("q-"))
        self.assertGreater(len(pane.seen[0]["id"]), len("q-") + 16)

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

    def test_stop_wakes_the_wait_rather_than_being_polled_for(self):
        """A card has no deadline, so the wait under it can last hours; it must not be a timer.

        The wait used to come round twenty times a second to ask an event that had not changed.
        Here every wait the waiting thread does is untimed, and Stop still ends it at once.
        """
        self.wire(lambda event: None)
        waiter = threading.current_thread()
        timeouts, real_wait = [], threading.Event.wait

        def record(event, timeout=None):
            if threading.current_thread() is waiter:
                timeouts.append(timeout)
            return real_wait(event, timeout)

        def stop_soon():
            time.sleep(0.05)
            self.cancel.set()

        threading.Thread(target=stop_soon, daemon=True).start()
        started = time.monotonic()
        with mock.patch.object(threading.Event, "wait", record):
            with self.assertRaises(Cancelled):
                self.ask()
        self.assertLess(time.monotonic() - started, 2.0)
        self.assertEqual(timeouts, [None])
        self.assertEqual([e["event"] for e in self.events if e["event"].startswith("question")],
                         ["question", "question_closed"])
        self.assertEqual(self.questions._pending, {})

    def test_stop_in_the_gap_before_the_card_is_registered_still_ends_the_wait(self):
        # Stop landing after `execute`'s own check and before the card is in `_pending` is the one
        # moment the hook has nothing to fail, and an untimed wait would then never be woken. The
        # second look, after the card has gone up, is what covers it.
        self.wire(lambda event: None)
        real = module._call_id

        def stop_first():
            self.cancel.set()
            return real()

        with mock.patch.object(module, "_call_id", stop_first):
            with self.assertRaises(Cancelled):
                self.ask()

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


class PaneCardTests(unittest.TestCase):
    """The card itself is C++ (`src/Pane.h`), and nothing can include that header but the one
    translation unit it belongs to, so the two decisions this card's review turned on are checked
    where they are written. `tests/test_presets.py` reads the same file the same way.
    """

    @classmethod
    def setUpClass(cls):
        cls.source = (ROOT / "src" / "Pane.h").read_text(encoding="utf-8")

    def block(self, start: str, end: str) -> str:
        self.assertIn(start, self.source)
        rest = self.source.split(start, 1)[1]
        self.assertIn(end, rest)
        return rest.split(end, 1)[0]

    def test_a_number_the_card_has_no_option_for_is_not_sent_as_the_answer(self):
        # "4" with three options used to be forwarded to the model as the prose answer "4".
        reading = self.block("inline Reading read(", "}  // namespace relay::ask")
        self.assertIn("if (!ok || index > labels.size()) return {Reading::NoSuchOption, {}, part};",
                      reading)
        self.assertIn("if (ok && index == 0) return {Reading::Skip, {}, {}};", reading)
        answer = self.block("bool answerQuestion(", "void sendAnswers()")
        branch = answer.split("Reading::NoSuchOption) {", 1)[1].split("return true;", 1)[0]
        self.assertIn("There is no option", branch)      # named, in the card's own ink
        self.assertIn("printQuestion();", branch)        # the card goes up again
        self.assertNotIn("m_ask.answers", branch)        # nothing is recorded
        self.assertNotIn("sendAnswers", branch)          # and nothing is sent: the wait goes on

    def test_a_superseded_card_is_answered_before_the_new_one_goes_up(self):
        # Protocol 27 has one question open at a time, so a second `question` arriving while a
        # card is up can only mean the first is stale. Dropping it used to be the whole of it,
        # which would have left whoever asked it blocked on an answer nobody could still type.
        show = self.block("void showQuestion(const QJsonObject &event) {",
                          "m_ask.questions = event.value")
        self.assertIn("closeQuestion(QString());", show)
        superseded = show.split("closeQuestion(QString());", 1)[1]
        self.assertIn('{"type", "question_answer"}', superseded)   # the old turn is released
        self.assertIn("QJsonArray()", superseded)                  # with nothing answered
        self.assertIn("superseded != incoming", superseded)        # a redraw of the same id is not

    def test_esc_skips_the_question_rather_than_stopping_the_turn(self):
        # Owner, 2026-09-19: while a card is up Esc is "skip this one". It used to stop the turn,
        # which made the key nearest the reader's hand end the work they were being asked about.
        key = self.block("if (mods == Qt::NoModifier && k == Qt::Key_Escape && m_agentBusy) {",
                         'toast(QStringLiteral("Agent interrupted"));')
        self.assertIn("if (skipQuestion()) return true;", key)   # the card first; the turn only after
        self.assertIn("stopAgent();", key)                       # ... and with no card, Esc still stops
        skip = self.block("bool skipQuestion() {", "void recordAnswer(")
        self.assertIn("if (!m_ask.open()) return false;", skip)
        # The same thing typing `0` does: an empty answer recorded through the one shared path.
        self.assertIn("recordAnswer(questionAt(m_ask.current), QStringList());", skip)
        record = self.block("void recordAnswer(", "public:")
        self.assertIn("m_ask.answers[m_ask.current] = answer;", record)
        self.assertIn("++m_ask.current;", record)
        self.assertIn("sendAnswers();", record)                  # the last question sends them all

    def test_the_card_says_esc_skips_and_where_the_turns_stop_went(self):
        # Relay ships `agent.stop` unbound, so Esc was the only keyboard Stop: a card that takes it
        # over has to say where Stop is instead, in the user's own keys.
        card = self.block("void printQuestion() {", "// The turn's Stop, named the way")
        self.assertIn("0 or Esc skips", card)
        self.assertIn("/skip or Esc passes", card)               # an open question has no numbers
        self.assertIn("stopTurnHint()", card)
        hint = self.block("static QString stopTurnHint() {", "\n    }")
        self.assertIn('Keymap::instance().shortcutText(QStringLiteral("agent.stop"))', hint)
        self.assertIn("Stop agent stops the turn", hint)         # nothing bound: name the action
        self.assertIn('QStringLiteral("%1 stops the turn").arg(stop)', hint)
        # And the busy caption stops promising a Stop that Esc no longer does.
        clock = self.block("void tickTurnClock() {", "// The \"Relaying…\" line when no turn")
        self.assertIn('asked ? QStringLiteral("Esc skips it")', clock)
        self.assertIn("Esc skips this question. %2.", clock)

    def test_a_line_from_a_paired_device_is_routed_before_the_card_sees_it(self):
        # Owner, 2026-09-19: a phone's line used to be taken as the answer before anything was
        # routed, so an unanswered card left the shell unreachable from that phone. The rule itself
        # is `relay::input::cardTakesRemoteLine` (tests/inputpolicy_test.cpp); this is the wiring.
        submit = self.block("void submitRemote(const QString &text, bool route, const QString &origin,",
                            "// The router's verdict for a remote prompt")
        self.assertIn("relay::input::cardTakesRemoteLine({m_ask.open(), false, false})", submit)
        # ... and only for the two doors that are agent-bound with no router in them.
        self.assertIn("(steering || !route || !m_workerReady)", submit)
        route = self.block("bool takeRemoteRoute(const QString &id, const QJsonObject &event) {",
                           "void submitTerminal(")
        self.assertIn("relay::input::cardTakesRemoteLine({m_ask.open(), true, toShell})", route)
        self.assertIn("submitTerminal(prompt.text, false);", route)   # a command still runs
        # Whoever typed it keeps their name on it: on the card's echo and on the queue row.
        self.assertIn("answerQuestion(prompt.text, prompt.author)", route)
        self.assertIn("m_remoteAuthor = prompt.author;", route)
        answer = self.block("bool answerQuestion(", "bool skipQuestion()")
        self.assertIn("recordAnswer(question, answer, author);", answer)
        self.assertIn("QStringLiteral(\" · from %1\").arg(author.trimmed())",
                      self.block("void recordAnswer(", "public:"))

    def test_a_card_that_cannot_be_drawn_is_answered_rather_than_dropped(self):
        # An empty id or no questions used to `return` and leave the worker blocked for good.
        show = self.block("void showQuestion(const QJsonObject &event) {",
                          "// The worker took the card away")
        malformed = show.split("if (m_ask.id.isEmpty() || m_ask.questions.isEmpty()) {", 1)[1]
        malformed = malformed.split("return;", 1)[0]
        self.assertIn("Ink::Error", malformed)                       # the pane says so
        self.assertIn('{"type", "question_answer"}', malformed)      # and the turn carries on
        self.assertIn("QJsonArray()", malformed)                     # with nothing answered


if __name__ == "__main__":
    unittest.main()
