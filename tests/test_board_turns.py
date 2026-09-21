# SPDX-License-Identifier: AGPL-3.0-or-later
"""`relay_core.board_turns`: one console, conversation and queue per card (19.16, #CTRN).

The pool itself, with neither a model nor a board behind it: `board_protocol`'s use of it is
covered by `test_board_protocol.AskTests`.
"""
import threading
import time
import unittest

from relay_core import board_turns


class FakeScope:
    def __init__(self, mode, card_id):
        self.mode, self.card_id = mode, card_id


class FakeTools:
    """Stands in for one card's BoardTools: all the pool touches is the scope."""

    def __init__(self):
        self.card_scope = None
        self.opened: list[tuple[str, str]] = []

    def begin_card_turn(self, mode, card_id):
        self.card_scope = FakeScope(mode, card_id)
        self.opened.append((mode, card_id))
        return self.card_scope

    def end_card_turn(self):
        self.card_scope = None


class FakeAgent:
    def __init__(self, card_id, emit):
        self.card_id, self.emit = card_id, emit
        self.cancel_event = threading.Event()
        self.prompts: list[str] = []
        self.gate: threading.Event | None = None
        self.answer = ["Answered."]
        self.outcome = "done"
        self.scope_during = None
        self.tools = None
        self.raises = None
        #: `TurnSupervisor.set_agent` hands the agent its steering source.
        self.steer_source = None

    def ask(self, prompt, reset_cancellation=True, turn_id=None, **kw):
        self.prompts.append(prompt)
        self.scope_during = self.tools.card_scope
        if self.raises is not None:
            raise self.raises
        if self.gate is not None:
            self.gate.wait(10)
        if self.cancel_event.is_set():
            self.emit({"event": "cancelled", "turn_id": turn_id})
            return
        for chunk in self.answer:
            self.emit({"event": "delta", "text": chunk, "turn_id": turn_id})
        self.emit({"event": self.outcome, "turn_id": turn_id})

    def set_card_turn(self, mode, card_id):
        """What `Agent.set_card_turn` does: the supervisor's two lines, and the card's scope."""
        if mode and card_id:
            self.tools.begin_card_turn(mode, card_id)
        else:
            self.tools.end_card_turn()

    def stop(self):
        self.cancel_event.set()
        if self.gate is not None:
            self.gate.set()


class PoolTest(unittest.TestCase):
    def setUp(self):
        self.events: list[dict] = []
        self.agents: dict[str, FakeAgent] = {}
        self.builds: list[str] = []
        self.answers: list[tuple[str, str, str]] = []
        self.gates: dict[str, threading.Event] = {}
        self.pool = board_turns.CardTurns(self.events.append, self.build, on_answer=self.answered)
        self.addCleanup(self.pool.drop)

    def build(self, card_id, emit):
        agent, tools = FakeAgent(card_id, emit), FakeTools()
        agent.tools = tools
        agent.gate = self.gates.get(card_id)
        self.agents[card_id] = agent
        self.builds.append(card_id)
        return agent, tools

    def answered(self, session, turn_id, text):
        self.answers.append((session.card_id, session.mode, text))

    # ---- helpers
    def hold(self, card_id) -> threading.Event:
        gate = self.gates.setdefault(card_id, threading.Event())
        if card_id in self.agents:
            self.agents[card_id].gate = gate
        return gate

    def wait_idle(self, timeout=5.0):
        """Nothing running *and* nothing queued: a card has a queue of its own now (#CTRN)."""
        deadline = time.time() + timeout
        while time.time() < deadline and not self.pool.idle():
            time.sleep(0.005)
        self.assertTrue(self.pool.idle())
        self.assertEqual(self.pool.count(), 0)

    def wait_running(self, count=1, timeout=5.0):
        deadline = time.time() + timeout
        while time.time() < deadline and self.pool.count() < count:
            time.sleep(0.005)
        self.assertGreaterEqual(self.pool.count(), count)


class RunningTests(PoolTest):
    def test_a_turn_runs_its_prompt_and_tags_every_event_with_the_card(self):
        self.pool.submit("AAAA", "plan", "plan this")
        self.wait_idle()
        self.assertEqual(self.agents["AAAA"].prompts, ["plan this"])
        tagged = [e for e in self.events if e["event"] in ("delta", "done")]
        self.assertTrue(tagged)
        self.assertEqual({e["card_id"] for e in tagged}, {"AAAA"})
        self.assertEqual({e["mode"] for e in tagged}, {"plan"})
        self.assertEqual(self.answers, [("AAAA", "plan", "Answered.")])

    def test_turns_on_different_cards_run_at_the_same_time(self):
        self.hold("AAAA")
        self.hold("BBBB")
        self.pool.submit("AAAA", "plan", "one")
        self.pool.submit("BBBB", "discuss", "two")
        self.wait_running(2)
        self.assertEqual(sorted(self.pool.running_cards()), ["AAAA", "BBBB"])
        self.assertEqual(self.agents["AAAA"].scope_during.mode, "plan")
        self.assertEqual(self.agents["BBBB"].scope_during.mode, "discuss")
        self.gates["AAAA"].set()
        self.gates["BBBB"].set()
        self.wait_idle()
        self.assertEqual(sorted(c for c, _, _ in self.answers), ["AAAA", "BBBB"])

    def test_a_second_turn_on_the_same_card_queues_behind_the_first(self):
        """Card #CTRN: the refusal (`board_busy`, "a turn is already running on #AAAA") is gone.

        A card has the pane's queue now, so the second prompt waits in the §12 strip with its
        own mode on the row, and the turn that runs it is bracketed as the mode it is.
        """
        self.hold("AAAA")
        first = self.pool.submit("AAAA", "discuss", "one")
        self.wait_running()
        second = self.pool.submit("AAAA", "plan", "two")       # must not raise
        rows = [e for e in self.events if e["event"] == "queue_changed"][-1]
        self.assertEqual([(r["id"], r["mode"], r["card_id"], r["surface"]) for r in rows["items"]],
                         [(second, "plan", "AAAA", "card:AAAA")])
        self.assertEqual(rows["surface"], "card:AAAA")
        self.gates["AAAA"].set()
        self.wait_idle()
        self.assertEqual(self.agents["AAAA"].prompts, ["one", "two"])
        # Each turn is bracketed as its own mode, and the second one's brief is the Plan's.
        brackets = [(e["id"], e["mode"]) for e in self.events if e["event"] == "agent_started"]
        self.assertEqual(brackets, [(first, "discuss"), (second, "plan")])
        self.assertEqual([(c, m) for c, m, _ in self.answers], [("AAAA", "discuss"), ("AAAA", "plan")])

    def test_a_cards_queue_is_its_own_and_is_reached_by_its_surface(self):
        """`queue_remove` and friends name the card with `surface: "card:<ID>"` (33, #CTRN)."""
        self.hold("AAAA")
        self.pool.submit("AAAA", "discuss", "one")
        self.wait_running()
        waiting = self.pool.submit("AAAA", "discuss", "two")
        self.assertEqual(board_turns.card_of_surface("card:AAAA"), "AAAA")
        self.assertEqual(board_turns.card_of_surface("options"), "")
        self.assertIsNone(self.pool.queue_of("BBBB"))           # no session, no queue
        queue = self.pool.queue_of("AAAA")
        queue.remove(waiting, "r1")
        ack = [e for e in self.events if e["event"] == "queue_ack"][-1]
        self.assertEqual((ack["id"], ack["op"], ack["card_id"], ack["surface"]),
                         ("r1", "remove", "AAAA", "card:AAAA"))
        self.gates["AAAA"].set()
        self.wait_idle()
        self.assertEqual(self.agents["AAAA"].prompts, ["one"])

    def test_as_many_cards_run_at_once_as_are_started(self):
        """No concurrent cap (owner, 2026-09-19: 'remove the cap on number of agents in the
        switchboard').  Five is past both the old default of 3 and the old ceiling's reach in
        one test; the point is that nothing counts them."""
        for card in ("AAAA", "BBBB", "CCCC", "DDDD", "EEEE"):
            self.hold(card)
            self.pool.submit(card, "plan", "go")
        self.wait_running(5)
        self.assertEqual(sorted(self.pool.running_cards()), ["AAAA", "BBBB", "CCCC", "DDDD", "EEEE"])
        for card in ("AAAA", "BBBB", "CCCC", "DDDD", "EEEE"):
            self.gates[card].set()
        self.wait_idle()
        self.assertEqual(len(self.answers), 5)

    def test_a_leaked_tool_call_never_reaches_the_card_thread(self):
        """Card #VN69: a provider that broke a `board_update_card` call streamed part of it as
        content; the deltas ride `session.text`, and what `_card_answer` wrote to the thread was
        the whole join. The join is stripped before it reaches the thread now."""
        self.hold("AAAA")
        self.pool.submit("AAAA", "discuss", "record this")
        self.wait_running()
        agent = self.agents["AAAA"]
        agent.answer = [
            "Recording the decisions on the card:\n\n",
            '{"type": "tool_use", "id": "toolu_bdrk_01FnV1wBKtgZ6ggKyUqNuqbB",\n',
            ' "name": "board_update_card", "input": {"id": "AAAA"}}\n\n',
            "The decisions are on the card.",
        ]
        self.gates["AAAA"].set()
        self.wait_idle()
        self.assertEqual(len(self.answers), 1)
        text = self.answers[0][2]
        self.assertNotIn("toolu_bdrk", text)
        self.assertNotIn('"tool_use"', text)
        self.assertIn("Recording the decisions on the card:", text)
        self.assertIn("The decisions are on the card.", text)

    def test_the_scope_is_opened_for_the_turn_and_closed_after_it(self):
        self.pool.submit("AAAA", "plan", "one")
        self.wait_idle()
        tools = self.agents["AAAA"].tools
        self.assertEqual(tools.opened, [("plan", "AAAA")])
        self.assertIsNone(tools.card_scope)
        self.assertEqual(self.agents["AAAA"].scope_during.card_id, "AAAA")

    def test_an_error_or_a_stop_ends_the_turn_and_writes_no_answer(self):
        self.agents_outcome = None
        self.hold("AAAA")
        self.pool.submit("AAAA", "plan", "one")
        self.wait_running()
        self.agents["AAAA"].outcome = "error"
        self.gates["AAAA"].set()
        self.wait_idle()
        self.assertEqual(self.answers, [])
        self.assertIsNone(self.agents["AAAA"].tools.card_scope)

    def test_stop_ends_one_card_and_leaves_the_other_running(self):
        self.hold("AAAA")
        self.hold("BBBB")
        self.pool.submit("AAAA", "plan", "one")
        self.pool.submit("BBBB", "plan", "two")
        self.wait_running(2)
        self.assertTrue(self.pool.stop("AAAA"))
        deadline = time.time() + 5
        while time.time() < deadline and self.pool.running_cards() != ["BBBB"]:
            time.sleep(0.005)
        self.assertEqual(self.pool.running_cards(), ["BBBB"])
        self.assertFalse(self.pool.stop("CCCC"))        # not running: nothing to stop
        self.gates["BBBB"].set()
        self.wait_idle()
        self.assertTrue(any(e["event"] == "cancelled" and e["card_id"] == "AAAA"
                            for e in self.events))

    def test_a_turn_that_raises_is_reported_and_leaves_nothing_running(self):
        self.pool.submit("AAAA", "plan", "one")
        self.wait_idle()
        self.agents["AAAA"].raises = RuntimeError("boom")
        self.pool.submit("AAAA", "plan", "two")
        self.wait_idle()
        failed = [e for e in self.events if e["event"] == "error" and e.get("card_id") == "AAAA"]
        self.assertTrue(failed)
        self.assertIn("RuntimeError", failed[-1]["text"])
        self.assertIsNone(self.agents["AAAA"].tools.card_scope)


class SessionTests(PoolTest):
    def test_a_card_keeps_its_conversation_between_turns(self):
        self.pool.submit("AAAA", "discuss", "one")
        self.wait_idle()
        self.pool.submit("AAAA", "discuss", "two")
        self.wait_idle()
        self.assertEqual(self.builds, ["AAAA"])
        self.assertEqual(self.agents["AAAA"].prompts, ["one", "two"])

    def test_forget_drops_an_idle_conversation_and_keeps_a_running_one(self):
        self.pool.submit("AAAA", "discuss", "one")
        self.wait_idle()
        self.pool.forget("AAAA")
        self.assertIsNone(self.pool.session("AAAA"))
        self.hold("BBBB")
        self.pool.submit("BBBB", "plan", "two")
        self.wait_running()
        self.pool.forget("BBBB")
        self.assertIsNotNone(self.pool.session("BBBB"))
        self.gates["BBBB"].set()
        self.wait_idle()

    def test_the_least_recently_used_conversation_is_dropped_past_the_cap(self):
        pool = board_turns.CardTurns(self.events.append, self.build, max_sessions=2)
        self.addCleanup(pool.drop)
        for card in ("AAAA", "BBBB", "CCCC"):
            pool.submit(card, "discuss", "hello")
            deadline = time.time() + 5
            # `idle`, not `count`: a prompt is on its card's queue for a heartbeat before the
            # dispatcher takes it, and that card is not idle yet (#CTRN).
            while time.time() < deadline and not pool.idle():
                time.sleep(0.005)
        self.assertIsNone(pool.session("AAAA"))
        self.assertIsNotNone(pool.session("BBBB"))
        self.assertIsNotNone(pool.session("CCCC"))

    def test_the_seed_hash_and_brief_mode_travel_with_the_session(self):
        self.pool.submit("AAAA", "plan", "one", seed_hash="h1")
        self.wait_idle()
        session = self.pool.session("AAAA")
        self.assertEqual((session.seed_hash, session.brief_mode, session.mode), ("h1", "plan", "plan"))

    def test_drop_stops_everything_and_forgets_it(self):
        self.hold("AAAA")
        self.pool.submit("AAAA", "plan", "one")
        self.wait_running()
        self.pool.drop()
        self.assertEqual(self.pool.running_cards(), [])
        self.assertIsNone(self.pool.session("AAAA"))

    def test_the_next_prompt_never_races_the_last_turns_unwind(self):
        # The pane sends the next ask the moment it sees `done`, which arrives from inside
        # `ask()` — a hair before the supervisor lets go of the agent. The pool used to wait for
        # that thread by hand (`_await_unwinding`); the queue is what makes it a non-question.
        self.pool.submit("AAAA", "discuss", "one")
        session = self.pool.session("AAAA")
        deadline = time.time() + 5
        while time.time() < deadline and not session.ended:
            time.sleep(0.001)
        self.pool.submit("AAAA", "discuss", "two")     # must not raise
        self.wait_idle()
        self.assertEqual(self.agents["AAAA"].prompts, ["one", "two"])

    def test_a_card_with_a_queue_is_never_dropped_under_the_cap(self):
        """The LRU takes idle conversations only: a queued prompt would be lost with its card."""
        pool = board_turns.CardTurns(self.events.append, self.build, max_sessions=1)
        self.addCleanup(pool.drop)
        self.hold("AAAA")
        pool.submit("AAAA", "discuss", "one")
        deadline = time.time() + 5
        while time.time() < deadline and not pool.is_running("AAAA"):
            time.sleep(0.005)
        pool.submit("AAAA", "discuss", "two")           # waiting on #AAAA's own queue
        pool.submit("BBBB", "discuss", "hello")         # would evict #AAAA if it counted as idle
        self.assertIsNotNone(pool.session("AAAA"))
        self.gates["AAAA"].set()
        deadline = time.time() + 5
        while time.time() < deadline and not pool.idle():
            time.sleep(0.005)
        self.assertEqual(self.agents["AAAA"].prompts, ["one", "two"])


class TurnBoundaryTests(PoolTest):
    """A card turn has a beginning and an end on the wire, like a pane's (#AGNT).

    The helper panel used to *infer* where a turn started and stopped from the events it saw —
    one line of the Issue's table — so a console could not draw a busy strip it was sure of.
    """

    def of(self, name):
        return [e for e in self.events if e.get("event") == name]

    def test_a_turn_is_bracketed_and_every_event_of_it_names_its_surface(self):
        turn_id = self.pool.submit("AAAA", "discuss", "one")
        self.wait_idle()
        started, finished = self.of("agent_started"), self.of("agent_finished")
        self.assertEqual([e["id"] for e in started], [turn_id])
        self.assertEqual([e["id"] for e in finished], [turn_id])
        self.assertEqual(started[0]["card_id"], "AAAA")
        self.assertEqual(finished[0]["outcome"], "done")
        for event in started + finished + self.of("delta") + self.of("done"):
            self.assertEqual(event["surface"], "card:AAAA", event)

    def test_a_stopped_turn_says_how_it_ended(self):
        self.hold("AAAA")
        self.pool.submit("AAAA", "plan", "one")
        self.wait_running()
        self.pool.stop("AAAA")
        self.wait_idle()
        self.assertEqual(self.of("agent_finished")[0]["outcome"], "cancelled")


if __name__ == "__main__":                              # pragma: no cover
    unittest.main()
