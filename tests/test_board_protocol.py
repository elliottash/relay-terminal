# SPDX-License-Identifier: AGPL-3.0-or-later
"""Switchboard worker protocol (docs/AGENT-SESSIONS-PROTOCOL.md section 17) and the
`board_*` tools' wiring into the agent.

No model, no network, no keyring: `board_ask` runs against a stub supervisor.
"""
import json
import os
import subprocess
import sys
import tempfile
import threading
import time
import unittest
from pathlib import Path

import unittest.mock

import fake_cards
import fake_github as FG
from relay_core import board as B
from relay_core import board_protocol as P
from relay_core import board_tools as T
from relay_core import forge_github as GH
from relay_core import forge_sync as F
from relay_core import project_probe as PP
from relay_core import qa_verifiers as QA
from relay_core.agent import Agent
from relay_core.provider import ProviderConfig

ROOT = Path(__file__).resolve().parents[1]

CONFIG = """\
version: 1
tabs: [{id: features, folder: features}, {id: bugs, folder: changes}]
columns: [inbox, discussing, ready, in-progress, waiting, needs-qa, done]
agent: {autonomy: auto, max_creates_per_turn: 5}
"""


class StubTurns:
    """Stands in for TurnSupervisor: records submissions, never calls a provider."""

    def __init__(self):
        self.submitted = []
        self.resets = 0
        self.agent = None
        self.busy = False

    def reset(self):
        self.resets += 1

    def submit(self, prompt, when="now", request_id=None, context=None, attachments=None, **kw):
        self.submitted.append({"prompt": prompt, "when": when, "id": request_id})
        return "q1"

    def now_or_later(self, now, later):
        """`TurnSupervisor.now_or_later`: the deferral `set_agent_role` and `set_board` share."""
        return later() if self.busy else now()


def boardless_dir(case) -> Path:
    """A directory with no `issues/board.yaml` in it or above it.

    A subdirectory of a project *does* have a board since 2026-09-18 (the backend walks up to
    the nearest ancestor holding one, as the GUI always has), so "no board" has to be a tree of
    its own rather than `<repo>/nowhere`.
    """
    tmp = tempfile.TemporaryDirectory()
    case.addCleanup(tmp.cleanup)
    elsewhere = Path(tmp.name).resolve() / "elsewhere"
    elsewhere.mkdir()
    if T.find_board_root(elsewhere) is not None:    # pragma: no cover - a board above the temp dir
        case.skipTest(f"{elsewhere} has a Switchboard above it")
    return elsewhere


class ProtocolTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.repo = Path(self.tmp.name).resolve()
        self.root = self.repo / "issues"
        self.root.mkdir()
        (self.root / B.BOARD_CONFIG).write_text(CONFIG, encoding="utf-8")
        self.board = B.Board(self.root, self.repo)
        self.events = []
        self.turns = StubTurns()
        self.commands = P.BoardCommands(self.turns, self.events.append)
        self.commands.configure(str(self.repo), {})
        # A card turn runs on that card's own agent, on its own thread (19.16): these are those
        # agents, with no model behind them.
        self.cards = fake_cards.CardAgents(self.commands, str(self.repo), self.events)

    def tearDown(self):
        self.commands.cards.drop()
        self.tmp.cleanup()

    def send(self, **request):
        self.events.clear()
        self.commands.dispatch(request)
        if self.cards.autowait:
            self.cards.wait()
        return self.events

    def of(self, name):
        return [e for e in self.events if e["event"] == name]

    def asked(self, card_id):
        """The owner's last question on a card: a finished turn's answer sits behind it."""
        return [e for e in self.board.thread(card_id)
                if e.author == "owner" and e.kind == "comment"][-1]

    def make_card(self, title="Voice mode", text="add voice transcribe mode", **kw):
        events = self.send(type="board_create", id="r1", tab="features", status="inbox",
                           title=title, text=text, **kw)
        written = [e for e in events if e["event"] == "board_written"]
        self.assertTrue(written, events)
        self.assertEqual(written[0]["id"], "r1")
        return written[0]["card_id"]


# -------------------------------------------------------------------- open, refresh

class OpenTests(ProtocolTest):
    def test_board_open_answers_with_the_config_the_cards_and_the_problems(self):
        card_id = self.make_card()
        events = self.send(type="board_open", id="o1")
        board = [e for e in events if e["event"] == "board"][0]
        self.assertEqual(board["id"], "o1")
        self.assertEqual([c["id"] for c in board["cards"]], [card_id])
        self.assertEqual(board["config"]["columns"],
                         ["inbox", "discussing", "ready", "in-progress", "waiting", "needs-qa", "done"])
        self.assertEqual([t["id"] for t in board["config"]["tabs"]], ["features", "bugs"])
        self.assertIn("column_statuses", board["config"])
        self.assertEqual(board["problems"], [])
        self.assertGreaterEqual(board["rev"], 1)

    def test_a_card_row_carries_what_the_pane_draws(self):
        self.make_card()
        board = [e for e in self.send(type="board_open") if e["event"] == "board"][0]
        row = board["cards"][0]
        for key in ("id", "title", "status", "tab", "labels", "assignee", "waiting_on", "rank",
                    "thread_entries", "tasks_done", "tasks_total", "path", "created", "updated",
                    "text"):
            self.assertIn(key, row)

    def test_a_row_says_when_the_card_last_changed(self):
        card_id = self.make_card()
        board = [e for e in self.send(type="board_open") if e["event"] == "board"][0]
        first = next(r for r in board["cards"] if r["id"] == card_id)["updated"]
        self.assertRegex(first, r"^\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}Z$")
        # A comment touches the thread file, and the row's `updated` — the later of the card's
        # and the thread's mtime — follows it, so the pane's Recently updated sort moves too.
        time.sleep(1.05)
        events = self.send(type="board_comment", id="c1", card=card_id, text="a nudge")
        changed = [e for e in events if e["event"] == "board_changed"][-1]
        after = next(r for r in changed["upserts"] if r["id"] == card_id)["updated"]
        self.assertRegex(after, r"^\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}Z$")
        self.assertGreater(after, first)

    def test_a_row_carries_the_whole_card_for_the_panes_full_text_filter(self):
        card_id = self.make_card(text="the composer eats the third bullet point")
        board = [e for e in self.send(type="board_open") if e["event"] == "board"][0]
        row = next(r for r in board["cards"] if r["id"] == card_id)
        # The body rides on the row…
        self.assertIn("## Issue", row["text"])
        self.assertIn("the composer eats the third bullet point", row["text"])
        # …and so does the thread: its words, its author, its kind. The comment's own
        # `board_changed` carries the upsert, so a live filter matches it without a refresh.
        events = self.send(type="board_comment", id="c1", card=card_id, author="dana",
                           kind="question", text="does it work offline?")
        changed = [e for e in events if e["event"] == "board_changed"][-1]
        row = next(r for r in changed["upserts"] if r["id"] == card_id)
        self.assertIn("does it work offline?", row["text"])
        self.assertIn("dana", row["text"])
        self.assertIn("question", row["text"])
        # But not the entries' header metadata: `pane=switchboard` sits in every header, so the
        # word "switchboard" would otherwise match every card that has a thread.
        self.assertNotIn("relay:entry", row["text"])
        self.assertNotIn("pane=", row["text"])

    def test_the_rows_text_is_capped_so_the_board_message_stays_bounded(self):
        card_id = self.make_card()
        # Quick add refuses a text over 8000 characters, so a card long enough to test the cap
        # is written through the Board itself.
        card = self.board.card_by_id(card_id)
        card.body = "# Voice mode\n\n## Issue\n" + "a very long ask " * 100_000
        self.board.save(card)
        board = [e for e in self.send(type="board_open") if e["event"] == "board"][0]
        text = next(r for r in board["cards"] if r["id"] == card_id)["text"]
        self.assertLessEqual(len(text), P.MAX_ROW_TEXT)
        self.assertTrue(text.startswith("# Voice mode"))

    def test_board_refresh_reports_only_what_changed(self):
        card_id = self.make_card()
        self.send(type="board_open")
        self.assertEqual(self.send(type="board_refresh")[0]["upserts"], [])
        card = self.board.card_by_id(card_id)
        card.set("assignee", "agent")
        self.board.save(card)
        changed = self.send(type="board_refresh")[0]
        self.assertEqual([c["id"] for c in changed["upserts"]], [card_id])
        self.assertEqual(changed["removed"], [])

    def test_a_card_that_disappears_is_reported_as_removed(self):
        card_id = self.make_card()
        self.send(type="board_open")
        self.board.card_by_id(card_id).path.unlink()
        changed = self.send(type="board_refresh")[0]
        self.assertEqual(changed["removed"], [card_id])

    def test_the_revision_number_moves_forward_on_every_change(self):
        self.send(type="board_open")
        first = self.send(type="board_refresh")[0]["rev"]
        self.assertGreater(self.send(type="board_refresh")[0]["rev"], first)

    def test_board_check_reports_the_format_problems(self):
        (self.root / "features").mkdir(parents=True, exist_ok=True)
        (self.root / "features" / "broken.md").write_text("no front matter here\n", encoding="utf-8")
        items = self.send(type="board_check", id="c1")[0]["items"]
        self.assertTrue(any(i["severity"] == "error" for i in items))

    def test_a_workspace_without_a_board_refuses_every_command(self):
        commands = P.BoardCommands(self.turns, self.events.append)
        self.assertIsNone(commands.configure(str(boardless_dir(self)), {}))
        with self.assertRaises(ValueError):
            commands.dispatch({"type": "board_open"})


# ------------------------------------------------------------------------- writes

class WriteTests(ProtocolTest):
    def test_quick_add_keeps_the_text_verbatim_and_titles_it_from_the_first_line(self):
        self.send(type="board_create", tab="features", status="inbox",
                  text="parse all folders and filenames and highlight them")
        card = self.board.cards()[0]
        self.assertIn("parse all folders and filenames and highlight them", card.body)
        self.assertEqual(card.title, "parse all folders and filenames and highlight them")
        self.assertEqual(card.status, "inbox")

    def test_a_long_quick_add_gets_a_shortened_title_and_the_full_text(self):
        text = "please " + "make the composer taller " * 10
        self.send(type="board_create", tab="features", status="inbox", text=text)
        card = self.board.cards()[0]
        self.assertLessEqual(len(card.title), 82)
        self.assertIn(text.strip(), card.body)

    def test_an_owner_write_is_attributed_to_the_owner_in_the_thread(self):
        card_id = self.make_card()
        text = self.board.thread_path(card_id).read_text(encoding="utf-8")
        self.assertIn("author=owner", text)
        self.assertNotIn("author=agent", text)

    def test_a_named_author_is_used_and_the_actor_goes_back_to_owner(self):
        card_id = self.make_card()
        self.send(type="board_comment", card=card_id, text="from a collaborator", author="dana")
        self.assertIn("author=dana", self.board.thread_path(card_id).read_text(encoding="utf-8"))
        self.assertEqual(self.commands.tools.context.actor, "owner")

    def test_the_owner_is_not_stopped_by_the_duplicate_check_or_the_limits(self):
        first = self.make_card()
        second = self.make_card()
        self.assertNotEqual(first, second)

    def test_a_drag_between_columns_moves_the_card_and_emits_a_change(self):
        card_id = self.make_card()
        events = self.send(type="board_move", card=card_id, status="ready", reason="dragged")
        self.assertEqual(self.board.card_by_id(card_id).status, "ready")
        self.assertTrue([e for e in events if e["event"] == "board_changed"])
        self.assertIn("dragged", self.board.thread_path(card_id).read_text(encoding="utf-8"))

    def test_an_update_needs_the_hash_and_reports_a_conflict_as_an_error(self):
        card_id = self.make_card()
        events = self.send(type="board_update", card=card_id, base_hash="0" * 64,
                           patch={"fields": {"assignee": "agent"}})
        error = [e for e in events if e["event"] == "error"][0]
        self.assertEqual(error["code"], "board_conflict")
        self.assertIn("current_hash", error)

    def test_a_card_detail_read_round_trips_through_the_protocol(self):
        card_id = self.make_card()
        self.send(type="board_comment", card=card_id, text="a reply")
        detail = self.send(type="board_card_get", id="d1", card=card_id)[0]
        self.assertEqual(detail["event"], "board_card")
        self.assertEqual(detail["id"], "d1")
        self.assertEqual(detail["card_id"], card_id)
        self.assertIn("## Issue", detail["body"])
        self.assertEqual([e["text"] for e in detail["thread"]][-1], "a reply")
        update = self.send(type="board_update", card=card_id, base_hash=detail["hash"],
                           patch={"fields": {"labels": ["voice"]}})
        self.assertTrue([e for e in update if e["event"] == "board_written"])

    def test_a_card_detail_carries_the_qa_recommendation_computed_on_this_machine(self):
        # Protocol 19.15: the same `qa` block the agent's `board_read` returns, because it *is* that
        # read. Availability is fixed here so the test says nothing about the machine it runs on.
        card_id = self.make_card()
        self.commands.tools.context.preset = "anthropic"
        self.commands.tools.context.model = "claude-opus-5"
        self.send(type="board_move", card=card_id, status="needs-qa-llm", reason="landed",
                  evidence="docs/qa_evidence/x/")
        here = {"installed_guests": {"codex"}, "keys": {"glm-coding": True},
                "local_models": ()}
        with unittest.mock.patch.object(QA, "availability", lambda *a, **k: dict(here)):
            detail = [e for e in self.send(type="board_card_get", id="q1", card=card_id)
                      if e["event"] == "board_card"][0]
        self.assertEqual(detail["front"]["implemented_by"], "anthropic/claude-opus-5")
        block = detail["qa"]
        self.assertEqual(block["implementer_family"], "anthropic")
        self.assertEqual(block["recommended"]["runner"], "guest:codex")
        self.assertEqual([s["family"] for s in block["skipped"]], ["anthropic"])
        self.assertIn("commits", block)
        board = [e for e in self.send(type="board_open") if e["event"] == "board"][0]
        row = [r for r in board["cards"] if r["id"] == card_id][0]
        self.assertEqual(row["implemented_by"], "anthropic/claude-opus-5")
        self.assertIn("verified_by", row)
        self.assertNotIn("qa", row)                # the rows stay light; the block is per card

    def test_undo_restores_and_reports_the_change(self):
        card_id = self.make_card()
        written = [e for e in self.send(type="board_move", card=card_id, status="ready",
                                        reason="dragged") if e["event"] == "board_written"][0]
        events = self.send(type="board_undo", write_id=written["write_id"])
        self.assertTrue([e for e in events if e["event"] == "board_undone"])
        self.assertEqual(self.board.card_by_id(card_id).status, "inbox")

    def test_a_write_the_tools_refuse_comes_back_as_an_error_event(self):
        events = self.send(type="board_comment", card="AAAA", text="x")
        self.assertEqual([e["event"] for e in events], ["error"])
        self.assertEqual(events[0]["code"], "board_not_found")


# ------------------------------------------------------- the Switchboard agent

class AskTests(ProtocolTest):
    def test_the_question_is_recorded_before_the_agent_sees_it(self):
        card_id = self.make_card()
        self.cards.hold(card_id)                       # the turn waits; the card does not
        events = self.send(type="board_ask", id="a1", card=card_id, text="where should this run?")
        appended = [e for e in events if e["event"] == "board_thread_appended"][0]
        self.assertEqual(appended["card_id"], card_id)
        self.assertEqual(appended["author"], "owner")
        self.assertEqual([e.text for e in self.board.thread(card_id)][-1], "where should this run?")
        self.cards.agent(card_id).release()
        self.assertTrue(self.cards.wait())

    def test_the_first_question_seeds_the_conversation_from_the_card_and_its_thread(self):
        card_id = self.make_card()
        self.send(type="board_ask", card=card_id, text="where should this run?")
        prompt = self.cards.prompts[-1]["prompt"]
        self.assertEqual(self.cards.builds, [card_id])
        self.assertIn(f"[Switchboard card #{card_id}", prompt)
        self.assertIn("add voice transcribe mode", prompt)
        self.assertIn("--- thread", prompt)
        self.assertTrue(prompt.endswith("where should this run?"))

    def test_a_second_question_about_the_same_card_is_not_reseeded(self):
        card_id = self.make_card()
        self.send(type="board_ask", card=card_id, text="one")
        self.send(type="board_ask", card=card_id, text="two")
        self.assertEqual(self.cards.builds, [card_id])     # one conversation, kept
        self.assertEqual(self.cards.prompts[-1]["prompt"], "two")

    def test_switching_card_reseeds(self):
        first = self.make_card()
        second = self.make_card(title="Clickable paths", text="clicking a path opens a pane")
        self.send(type="board_ask", card=first, text="one")
        self.send(type="board_ask", card=second, text="two")
        # Each card has its own conversation now, so the second is seeded without disturbing
        # the first: asking about #first again continues where it left off (19.16).
        self.assertEqual(self.cards.builds, [first, second])
        self.assertIn(f"#{second}", self.cards.prompts[-1]["prompt"])
        self.send(type="board_ask", card=first, text="three")
        self.assertEqual(self.cards.builds, [first, second])
        self.assertEqual(self.cards.prompts[-1]["prompt"], "three")

    def test_an_edit_to_the_card_reseeds_the_conversation(self):
        card_id = self.make_card()
        self.send(type="board_ask", card=card_id, text="one")
        card = self.board.card_by_id(card_id)
        card.set("assignee", "agent")
        self.board.save(card)
        self.send(type="board_ask", card=card_id, text="two")
        self.assertEqual(self.cards.builds, [card_id, card_id])
        self.assertIn("assignee", self.cards.prompts[-1]["prompt"])

    def test_the_turn_events_are_tagged_with_the_card(self):
        card_id = self.make_card()
        events = self.send(type="board_ask", card=card_id, text="one")
        deltas = [e for e in events if e["event"] == "delta"]
        self.assertTrue(deltas)
        self.assertEqual({e.get("card_id") for e in deltas}, {card_id})
        self.assertEqual({e.get("mode") for e in deltas}, {"discuss"})
        done = [e for e in events if e["event"] == "done"][-1]
        self.assertEqual((done["card_id"], done["mode"]), (card_id, "discuss"))

    def test_the_answer_is_appended_to_the_thread_when_the_turn_finishes(self):
        card_id = self.make_card()
        self.cards.hold(card_id)
        self.send(type="board_ask", card=card_id, text="where should this run?")
        agent = self.cards.agent(card_id)
        agent.answer = ["Run it ", "in the cloud."]
        agent.release()
        self.assertTrue(self.cards.wait())
        entries = self.board.thread(card_id)
        self.assertEqual(entries[-1].text, "Run it in the cloud.")
        self.assertEqual(entries[-1].author, "agent")
        self.assertTrue([e for e in self.events if e["event"] == "board_thread_appended"
                         and e.get("author") == "agent"])

    def test_a_failed_turn_writes_nothing(self):
        card_id = self.make_card()
        self.cards.hold(card_id)
        self.send(type="board_ask", card=card_id, text="one")
        before = len(self.board.thread(card_id))
        agent = self.cards.agent(card_id)
        agent.answer, agent.outcome = ["half an answ"], "error"
        agent.release()
        self.assertTrue(self.cards.wait())
        self.assertEqual(len(self.board.thread(card_id)), before)

    def test_a_missing_card_and_empty_text_are_refused(self):
        card_id = self.make_card()
        with self.assertRaises(ValueError):
            self.commands.dispatch({"type": "board_ask", "card": "AAAA", "text": "x"})
        with self.assertRaises(ValueError):
            self.commands.dispatch({"type": "board_ask", "card": card_id, "text": "  "})

    def test_a_stale_board_block_caps_nothing(self):
        """An older GUI still sends `board.limits.max_card_turns`; the worker ignores it, and
        no number of running card turns is refused (owner, 2026-09-19, #0Z13)."""
        self.commands.configure(str(self.repo), {"board": {"limits": {"max_card_turns": 1}}})
        self.cards = fake_cards.CardAgents(self.commands, str(self.repo), self.events)
        first = self.make_card()
        second = self.make_card(title="Clickable paths", text="clicking a path opens a pane")
        self.cards.hold(first)
        self.cards.hold(second)
        self.send(type="board_ask", card=first, mode="plan")
        events = self.send(type="board_ask", id="a2", card=second, mode="plan")
        self.assertTrue(self.cards.wait_running(2))
        self.assertEqual([e for e in events if e.get("code") == "board_busy"], [])
        self.assertEqual(sorted(self.commands.cards.running_cards()), sorted([first, second]))
        for card_id in (first, second):
            self.cards.agent(card_id).release()
        self.assertTrue(self.cards.wait())

    def test_a_second_turn_on_the_same_card_is_refused_while_the_first_runs(self):
        card_id = self.make_card()
        self.cards.hold(card_id)
        self.send(type="board_ask", card=card_id, text="one")
        self.assertTrue(self.cards.wait_running())
        events = self.send(type="board_ask", id="a2", card=card_id, text="two")
        refusal = [e for e in events if e.get("code") == "board_busy"][0]
        self.assertEqual(refusal["card_id"], card_id)
        self.assertIn(f"#{card_id}", refusal["text"])
        # A refused ask leaves no trace on the card.
        self.assertEqual([e.text for e in self.board.thread(card_id)][-1], "one")
        self.cards.agent(card_id).release()
        self.assertTrue(self.cards.wait())

    def test_two_cards_are_planned_at_the_same_time(self):
        first = self.make_card()
        second = self.make_card(title="Clickable paths", text="clicking a path opens a pane")
        self.cards.hold(first)
        self.cards.hold(second)
        self.send(type="board_ask", card=first, mode="plan")
        self.send(type="board_ask", card=second, mode="plan")
        self.assertTrue(self.cards.wait_running(2))
        self.assertEqual(sorted(self.commands.cards.running_cards()), sorted([first, second]))
        # Each is planning its own card, with its own scope.
        for card_id in (first, second):
            scope = self.cards.tools[card_id].card_scope
            self.assertEqual((scope.mode, scope.card_id), ("plan", card_id))
        for card_id in (first, second):
            self.cards.agent(card_id).release()
        self.assertTrue(self.cards.wait())
        self.assertEqual(self.commands.cards.running_cards(), [])

    def test_the_fifth_card_runs_too_no_card_turn_is_refused_for_number(self):
        ids = [self.make_card(title=f"Card {n}", text=f"body {n}") for n in range(5)]
        for card_id in ids:
            self.cards.hold(card_id)
        for card_id in ids:
            events = self.send(type="board_ask", card=card_id, mode="plan")
            self.assertEqual([e for e in events if e.get("code") == "board_busy"], [])
        self.assertTrue(self.cards.wait_running(5))
        self.assertEqual(sorted(self.commands.cards.running_cards()), sorted(ids))
        for card_id in ids:
            self.cards.agent(card_id).release()
        self.assertTrue(self.cards.wait())

    def test_board_cancel_stops_one_card_and_leaves_the_other_running(self):
        first = self.make_card()
        second = self.make_card(title="Clickable paths", text="clicking a path opens a pane")
        self.cards.hold(first)
        self.cards.hold(second)
        self.send(type="board_ask", card=first, mode="plan")
        self.send(type="board_ask", card=second, mode="plan")
        self.assertTrue(self.cards.wait_running(2))
        events = self.send(type="board_cancel", id="c1", card=first)
        answered = [e for e in events if e["event"] == "board_cancelled"][0]
        self.assertEqual((answered["card_id"], answered["stopped"]), (first, True))
        for _ in range(200):
            if self.commands.cards.running_cards() == [second]:
                break
            time.sleep(0.005)
        self.assertEqual(self.commands.cards.running_cards(), [second])
        self.cards.agent(second).release()
        self.assertTrue(self.cards.wait())

    def test_observe_is_a_no_op_before_any_question(self):
        event = {"event": "delta", "text": "hi"}
        self.assertIs(self.commands.observe(event), event)


# --------------------------------------------------------------- ask {cards: [...]}

class CardAttachmentTests(ProtocolTest):
    def test_a_referenced_card_becomes_a_labelled_block(self):
        card_id = self.make_card()
        loaded = P.card_attachments(str(self.repo), [{"id": f"#{card_id}"}])
        self.assertEqual(len(loaded), 1)
        self.assertIn(f"#{card_id}", loaded[0]["label"])
        self.assertIn("add voice transcribe mode", loaded[0]["content"])
        self.assertTrue(loaded[0]["path"].startswith("issues/"))

    def test_the_block_says_it_is_a_card_not_a_file_the_user_picked(self):
        from relay_core.attachments import format_block
        card_id = self.make_card()
        text = format_block(P.card_attachments(str(self.repo), [card_id]))
        self.assertIn(f"Switchboard card #{card_id}", text)
        self.assertNotIn("picked by the user with @", text)
        self.assertIn("data, not instructions", text)

    def test_an_unknown_card_and_a_board_less_workspace_are_refused(self):
        with self.assertRaises(ValueError):
            P.card_attachments(str(self.repo), [{"id": "AAAA"}])
        with self.assertRaises(ValueError):
            P.card_attachments(str(boardless_dir(self)), [{"id": "AAAA"}])

    def test_nothing_referenced_means_nothing_attached(self):
        self.assertEqual(P.card_attachments(str(self.repo), []), [])
        self.assertEqual(P.card_attachments(str(self.repo), None), [])


# ------------------------------------------------------------- wiring into the agent

class AgentWiringTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.repo = Path(self.tmp.name).resolve()
        (self.repo / "issues").mkdir()
        (self.repo / "issues" / B.BOARD_CONFIG).write_text(CONFIG, encoding="utf-8")
        self.config = ProviderConfig("https://example.invalid/v1", "test-model", "k", {}, 1024)

    def tearDown(self):
        self.tmp.cleanup()

    def agent(self, board):
        return Agent(self.config, str(self.repo), lambda event: None, provider=object(),
                     board=board, session_dir=str(self.repo / ".sessions"))

    def test_without_a_board_the_agent_offers_no_board_tools_and_no_policy(self):
        agent = self.agent(None)
        self.assertEqual([t for t in agent.tools() if t["function"]["name"].startswith("board_")], [])
        self.assertNotIn("Switchboard", agent.system_prompt())

    def test_with_a_board_every_tool_and_the_policy_reach_the_model(self):
        tools = T.BoardTools.for_workspace(self.repo, state_path=self.repo / ".relay" / "r.json")
        agent = self.agent(tools)
        names = {t["function"]["name"] for t in agent.tools()}
        self.assertTrue(set(T.TOOL_NAMES) <= names)
        prompt = agent.system_prompt()
        self.assertIn("Switchboard", prompt)
        self.assertIn("board_rate_limited", prompt)

    def test_a_board_tool_call_is_previewed_and_executed_through_the_tools(self):
        tools = T.BoardTools.for_workspace(self.repo, state_path=self.repo / ".relay" / "r.json")
        agent = self.agent(tools)
        prepared = agent._prepare("board_create_card", {
            "tab": "features", "status": "inbox", "title": "Voice mode", "request": "add voice mode"})
        self.assertIn("SWITCHBOARD CREATE CARD", prepared.preview)
        result = agent._execute(prepared, {})
        self.assertIn("id", result)
        self.assertEqual(len(B.Board(self.repo / "issues", self.repo).cards()), 1)

    def test_plan_mode_keeps_the_reads_and_drops_the_writes(self):
        tools = T.BoardTools.for_workspace(self.repo, state_path=self.repo / ".relay" / "r.json")
        agent = self.agent(tools)
        agent.set_mode("plan")
        names = {t["function"]["name"] for t in agent.tools()}
        self.assertIn("board_list", names)
        with self.assertRaises(ValueError):
            agent._prepare("board_create_card", {"tab": "features", "status": "inbox",
                                                 "title": "T", "request": "r"})

    def test_a_turn_resets_the_per_turn_budget_and_stamps_the_model(self):
        tools = T.BoardTools.for_workspace(self.repo, state_path=self.repo / ".relay" / "r.json")
        agent = self.agent(tools)
        tools.creates_this_turn = 4
        agent.board.context.model = None
        # ask() would call the provider; exercise just the per-turn bookkeeping it does first.
        agent.sign_board()
        agent.board.begin_turn("t-7")
        self.assertEqual(tools.creates_this_turn, 0)
        self.assertEqual(tools.context.turn_id, "t-7")
        self.assertEqual(tools.context.model, "test-model")

    def test_the_board_context_learns_the_panes_preset_so_a_card_can_be_signed(self):
        # Card #T71W: the model alone cannot say who wrote a card — `deepseek-v4.1-flash` through
        # OpenRouter and the same model served locally are different things to QA.
        tools = T.BoardTools.for_workspace(self.repo, state_path=self.repo / ".relay" / "r.json")
        config = ProviderConfig("https://openrouter.ai/api/v1", "deepseek/deepseek-v4.1-flash",
                                "k", {}, 1024)
        agent = Agent(config, str(self.repo), lambda event: None, provider=object(),
                      board=tools, session_dir=str(self.repo / ".sessions"), preset_id="openrouter")
        agent.sign_board()
        self.assertEqual(tools.context.preset, "openrouter")
        self.assertEqual(tools.context.signature(), "deepseek/deepseek-v4.1-flash")


class ThreadSafetyTests(ProtocolTest):
    def test_concurrent_appends_keep_every_entry_and_stay_sorted(self):
        card_id = self.make_card()
        errors = []

        def work(n):
            try:
                for i in range(5):
                    self.board.append_thread(card_id, f"worker {n} entry {i}", author="agent",
                                             kind="note")
            except Exception as exc:                            # pragma: no cover - a real failure
                errors.append(exc)

        threads = [threading.Thread(target=work, args=(n,)) for n in range(4)]
        for thread in threads:
            thread.start()
        for thread in threads:
            thread.join()
        self.assertEqual(errors, [])
        entries = self.board.thread(card_id)
        self.assertEqual(len(entries), 21)                      # 1 creation event + 20 notes
        ids = [e.entry_id for e in entries]
        self.assertEqual(len(set(ids)), len(ids))
        self.assertEqual(ids, sorted(ids))


class KeylessWorkerTests(unittest.TestCase):
    """The real worker process: the Switchboard opens even when `configure` finds no provider key
    (only `board_ask` needs the agent). Before this, a keyless window showed "Loading" forever."""

    def test_board_open_answers_after_a_configure_that_found_no_key(self):
        import json
        import os
        import subprocess
        import sys
        root = Path(__file__).resolve().parents[1]
        with tempfile.TemporaryDirectory() as tmp:
            issues = Path(tmp) / "issues"
            issues.mkdir()
            (issues / B.BOARD_CONFIG).write_text(CONFIG, encoding="utf-8")
            env = {k: v for k, v in os.environ.items() if not k.endswith("_API_KEY")}
            env.update(RELAY_KEYRING="off", RELAY_PANE_ID="switchboard")
            messages = [{"type": "configure", "workspace": tmp, "agent_role": "switchboard",
                         "use_stored_key": True, "api_key": "", "preset": "kimi"},
                        {"type": "board_open", "id": "o1"}, {"type": "shutdown"}]
            proc = subprocess.run([sys.executable, "-S", str(root / "backend/worker.py")],
                                  input="".join(json.dumps(m) + "\n" for m in messages),
                                  text=True, capture_output=True, timeout=20, cwd=root, env=env)
        events = [json.loads(line) for line in proc.stdout.splitlines() if line.strip()]
        names = [e["event"] for e in events]
        self.assertIn("error", names)                        # the provider part did fail
        self.assertNotIn("configured", names)
        opened = [e for e in events if e["event"] == "board" and e.get("id") == "o1"]
        self.assertEqual(len(opened), 1, names)
        self.assertEqual(opened[0]["cards"], [])


# ------------------------------------------------ board_cleanup: the whole board at once

class StubBoardAgent:
    """Stands in for the Switchboard worker's Agent: it only has to carry the board tools."""

    def __init__(self, tools):
        self.board = tools
        self.config = ProviderConfig(api_key="", base_url="https://example.invalid", model="stub/one")
        self.session_id = "s-1"


class CleanupTests(ProtocolTest):
    """`board_cleanup` (protocol 19.9): one turn over the whole board, and its changelog."""

    def setUp(self):
        super().setUp()
        self.agent_tools = self.commands.agent_tools(str(self.repo), {})
        self.turns.agent = StubBoardAgent(self.agent_tools)

    def start(self, **kw):
        return self.send(type="board_cleanup", id="k1", **kw)

    def finish(self, *, outcome="done", text="Merged nothing."):
        self.events.clear()
        if text:
            self.commands.observe({"event": "delta", "text": text})
        self.commands.observe({"event": outcome, "turn_id": "t-9"})
        return self.events

    def test_it_starts_one_turn_with_the_brief_and_the_roster(self):
        card_id = self.make_card()
        events = self.start(scope="the features tab")
        started = [e for e in events if e["event"] == "board_cleanup_started"][0]
        self.assertEqual(started["id"], "k1")
        self.assertTrue(started["run_id"].startswith("c-"))
        self.assertEqual(started["cards"], 1)
        self.assertFalse(started["dry_run"])
        self.assertEqual(started["scope"], "the features tab")
        self.assertEqual(started["limits"]["max_writes_per_turn"],
                         T.CLEANUP_LIMITS["max_writes_per_turn"])
        prompt = self.turns.submitted[-1]["prompt"]
        self.assertIn("[Switchboard cleanup]", prompt)
        self.assertIn("board_merge_cards", prompt)              # the brief
        self.assertIn(f"#{card_id}", prompt)                    # the roster
        self.assertIn("the features tab", prompt)
        self.assertEqual(self.turns.resets, 1)                  # its own conversation

    def test_the_running_commentary_is_kept_apart_in_the_report(self):
        self.make_card()
        self.start()
        self.commands.observe({"event": "delta", "text": "Reading the inbox."})
        self.commands.observe({"event": "tool_started", "tool": "board_read"})
        self.commands.observe({"event": "delta", "text": "Done."})
        summary = [e for e in self.finish(text="") if e["event"] == "board_cleanup_summary"][0]
        self.assertEqual(summary["report"], "Reading the inbox.\n\nDone.")

    def test_the_turn_events_say_cleanup_and_never_a_card(self):
        self.make_card()
        run_id = [e for e in self.start() if e["event"] == "board_cleanup_started"][0]["run_id"]
        tagged = self.commands.observe({"event": "tool_started", "name": "board_list"})
        self.assertTrue(tagged["cleanup"])
        self.assertEqual(tagged["run_id"], run_id)
        self.assertNotIn("card_id", tagged)

    def test_it_ends_with_a_summary_then_the_board_diff(self):
        keep = self.make_card()
        gone = self.make_card("Dictation", "let me dictate into the box")
        self.start()
        self.agent_tools.run("board_merge_cards",
                             {"into": keep, "cards": [gone], "reason": "the same request"})
        events = self.finish(text="Merged #%s into #%s." % (gone, keep))
        names = [e["event"] for e in events]
        self.assertEqual(names[-2:], ["board_cleanup_summary", "board_changed"])
        summary = [e for e in events if e["event"] == "board_cleanup_summary"][0]
        self.assertEqual(summary["id"], "k1")
        self.assertEqual(summary["outcome"], "done")
        self.assertEqual(summary["counts"]["merge"], 1)
        self.assertEqual(summary["changes"][0]["action"], "merge")
        self.assertEqual(summary["changes"][0]["cards"], [gone])
        self.assertIn("Merged #", summary["report"])
        self.assertTrue((self.repo / summary["changelog"]).is_file())
        self.assertIn("| merge |", (self.repo / summary["changelog"]).read_text(encoding="utf-8"))

    def test_a_run_that_changed_nothing_leaves_no_file_behind(self):
        self.make_card()
        self.start()
        summary = [e for e in self.finish() if e["event"] == "board_cleanup_summary"][0]
        self.assertEqual(summary["counts"]["writes"], 0)
        self.assertEqual(summary["changelog"], "")

    def test_a_cancelled_run_still_reports_what_it_did(self):
        keep = self.make_card()
        self.start()
        self.agent_tools.run("board_move_card", {"id": keep, "status": "ready", "reason": "agreed"})
        summary = [e for e in self.finish(outcome="cancelled", text="")
                   if e["event"] == "board_cleanup_summary"][0]
        self.assertEqual(summary["outcome"], "cancelled")
        self.assertEqual(summary["counts"]["move"], 1)
        self.assertTrue((self.repo / summary["changelog"]).is_file())

    def test_a_dry_run_writes_no_card_and_reports_the_plan(self):
        card_id = self.make_card()
        before = self.board.card_by_id(card_id).path.read_bytes()
        self.start(dry_run=True)
        self.assertIn("THIS IS A DRY RUN", self.turns.submitted[-1]["prompt"])
        result = self.agent_tools.run("board_move_card",
                                      {"id": card_id, "status": "ready", "reason": "x"})
        self.assertEqual(result["code"], "board_cleanup_dry_run")
        summary = [e for e in self.finish(text="the plan") if e["event"] == "board_cleanup_summary"][0]
        self.assertTrue(summary["dry_run"])
        self.assertEqual(summary["counts"]["writes"], 0)
        self.assertEqual(summary["counts"]["proposed"], 1)
        self.assertTrue(summary["changes"][0]["proposed"])
        self.assertEqual(self.board.card_by_id(card_id).path.read_bytes(), before)

    def test_the_sections_delta_travels_with_the_summary(self):
        self.start()
        self.agent_tools.run("board_sections", {"columns": ["inbox", "ready", "done"],
                                                "reason": "the middle lanes are empty"})
        summary = [e for e in self.finish() if e["event"] == "board_cleanup_summary"][0]
        self.assertEqual(summary["sections"]["columns_after"], ["inbox", "ready", "done"])
        self.assertIn("discussing", summary["sections"]["columns_before"])

    def test_a_second_cleanup_and_a_card_ask_are_refused_while_one_runs(self):
        card_id = self.make_card()
        self.start()
        again = self.send(type="board_cleanup", id="k2")[0]
        self.assertEqual(again["event"], "error")
        self.assertEqual(again["code"], "board_busy")
        self.assertTrue(again["cleanup_running"])
        asked = self.send(type="board_ask", id="a2", card=card_id, text="hello?")[0]
        self.assertEqual(asked["code"], "board_busy")
        self.assertNotIn("hello?", [e.text for e in self.board.thread(card_id)])
        self.assertEqual(len(self.turns.submitted), 1)

    def test_a_cleanup_is_refused_while_a_card_question_runs(self):
        # Cards run in parallel with each other (19.16) but never with a cleanup: it merges,
        # splits and moves the very cards those turns are talking about.
        card_id = self.make_card()
        self.cards.hold(card_id)
        self.send(type="board_ask", card=card_id, text="where should this run?")
        self.assertTrue(self.cards.wait_running())
        refused = self.start()[0]
        self.assertEqual(refused["code"], "board_busy")
        self.assertEqual(refused["card_id"], card_id)
        self.assertEqual(refused["cards"], [card_id])
        self.assertTrue(refused["agent_busy"])
        self.cards.agent(card_id).release()
        self.assertTrue(self.cards.wait())

    def test_a_cleanup_without_a_configured_agent_is_refused(self):
        self.turns.agent = None
        with self.assertRaises(ValueError):
            self.commands.dispatch({"type": "board_cleanup", "id": "k1"})


# ------------------------------------------ board_ask {mode}: Discuss and Plan (#XS6Q)

class ModeTests(ProtocolTest):
    """Protocol 19.10: a card's Discuss and Plan are `board_ask` turns told apart by `mode`."""

    def setUp(self):
        super().setUp()
        self.agent_tools = self.commands.agent_tools(str(self.repo), {})
        self.turns.agent = StubBoardAgent(self.agent_tools)

    def scope(self, card_id):
        """The scope the turn on this card ran under (19.16: on that card's own tools)."""
        return self.cards.agent(card_id).scope_during

    def test_no_mode_is_a_discuss_and_the_thread_says_so(self):
        card_id = self.make_card()
        events = self.send(type="board_ask", id="a1", card=card_id, text="is this still wanted?")
        appended = self.of("board_thread_appended")[0]
        self.assertEqual(appended["mode"], "discuss")
        self.assertEqual(self.asked(card_id).attrs.get("mode"), "discuss")
        prompt = self.cards.prompts[-1]["prompt"]
        self.assertIn(f"[Discuss · #{card_id}]", prompt)
        self.assertTrue(prompt.endswith("is this still wanted?"))
        self.assertEqual(self.scope(card_id).mode, "discuss")
        self.assertTrue(events)

    def test_a_plan_needs_no_words_and_carries_the_plan_brief(self):
        card_id = self.make_card()
        self.send(type="board_ask", id="p1", card=card_id, mode="plan")
        entry = self.asked(card_id)
        self.assertEqual((entry.author, entry.attrs.get("mode"), entry.text),
                         ("owner", "plan", "Plan this card."))
        prompt = self.cards.prompts[-1]["prompt"]
        self.assertIn(f"[Plan · #{card_id}]", prompt)
        self.assertIn("## Plan", prompt)
        self.assertIn(f"[Switchboard card #{card_id}", prompt)      # seeded first
        self.assertEqual(self.scope(card_id).mode, "plan")
        self.assertEqual(self.scope(card_id).card_id, card_id)
        # And it is closed again once the turn's thread unwinds.
        self.assertIsNone(self.cards.tools[card_id].card_scope)

    def test_a_plan_with_a_note_passes_it_verbatim(self):
        card_id = self.make_card()
        self.send(type="board_ask", card=card_id, mode="plan", text="keep it to the backend")
        self.assertEqual(self.asked(card_id).text, "keep it to the backend")
        self.assertTrue(self.cards.prompts[-1]["prompt"].endswith("keep it to the backend"))

    def test_the_brief_is_sent_when_the_mode_changes_and_not_twice_for_discuss(self):
        card_id = self.make_card()
        self.cards.autowait = True
        self.send(type="board_ask", card=card_id, text="one")
        self.send(type="board_ask", card=card_id, text="two")
        self.assertEqual(self.cards.prompts[-1]["prompt"], "two")
        self.send(type="board_ask", card=card_id, mode="plan")
        self.assertIn("[Plan ·", self.cards.prompts[-1]["prompt"])
        self.send(type="board_ask", card=card_id, text="three")
        self.assertIn("[Discuss ·", self.cards.prompts[-1]["prompt"])

    def test_the_answer_and_the_turn_events_carry_the_mode_and_the_scope_ends(self):
        card_id = self.make_card()
        self.cards.hold(card_id)
        events = self.send(type="board_ask", card=card_id, mode="plan")
        self.cards.agent(card_id).answer = ["Planned."]
        self.cards.agent(card_id).release()
        self.assertTrue(self.cards.wait())
        self.assertEqual([e["mode"] for e in events if e["event"] == "delta"], ["plan"])
        done = [e for e in events if e["event"] == "done"][-1]
        self.assertEqual(done["mode"], "plan")
        self.assertIsNone(self.cards.tools[card_id].card_scope)
        entry = self.board.thread(card_id)[-1]
        self.assertEqual((entry.author, entry.attrs.get("mode"), entry.text), ("agent", "plan", "Planned."))
        self.assertEqual([e["mode"] for e in events if e["event"] == "board_thread_appended"
                          and e.get("author") == "agent"], ["plan"])

    def test_what_is_said_before_and_after_a_tool_call_stays_two_paragraphs(self):
        card_id = self.make_card()
        self.cards.hold(card_id)
        self.send(type="board_ask", card=card_id, mode="plan")
        agent = self.cards.agent(card_id)
        agent.answer = ["Writing the plan.", "The plan is on the card."]
        agent.tools_at = [(1, "board_update_card")]
        agent.release()
        self.assertTrue(self.cards.wait())
        self.assertEqual(self.board.thread(card_id)[-1].text,
                         "Writing the plan.\n\nThe plan is on the card.")

    def test_a_failed_or_stopped_turn_ends_the_scope_too(self):
        card_id = self.make_card()
        for outcome in ("error", "cancelled"):
            self.cards.hold(card_id)
            self.send(type="board_ask", card=card_id, mode="plan")
            agent = self.cards.agent(card_id)
            agent.outcome = outcome
            agent.release()
            self.assertTrue(self.cards.wait())
            self.assertIsNone(self.cards.tools[card_id].card_scope, outcome)
            self.cards.gates.pop(card_id, None)

    def test_an_unknown_mode_and_an_empty_discuss_are_refused_before_anything_is_written(self):
        card_id = self.make_card()
        before = len(self.board.thread(card_id))
        for request in ({"mode": "execute", "text": "go"}, {"mode": "discuss", "text": " "},
                        {"text": None}):
            with self.assertRaises(ValueError):
                self.commands.dispatch({"type": "board_ask", "card": card_id, **request})
        self.assertEqual(len(self.board.thread(card_id)), before)
        self.assertIsNone(self.commands.cards.session(card_id))

    def test_a_plan_is_refused_while_a_cleanup_runs(self):
        card_id = self.make_card()
        self.send(type="board_cleanup", id="k1", dry_run=True)
        before = len(self.board.thread(card_id))
        refused = self.send(type="board_ask", id="p2", card=card_id, mode="plan")[0]
        self.assertEqual(refused["code"], "board_busy")
        self.assertIn("the plan", refused["text"])
        self.assertEqual(len(self.board.thread(card_id)), before)


class CardScopeAgentTests(unittest.TestCase):
    """The worker's Agent offers only what the card turn's mode allows (protocol 19.10)."""

    setUp, tearDown, agent = AgentWiringTests.setUp, AgentWiringTests.tearDown, AgentWiringTests.agent

    def test_a_card_turn_offers_no_commands_no_writes_and_no_subagents(self):
        tools = T.BoardTools.for_workspace(self.repo, state_path=self.repo / ".relay" / "r.json")
        agent = self.agent(tools)
        tools.begin_card_turn("plan", "ABCD")
        names = {t["function"]["name"] for t in agent.tools()}
        self.assertIn("read_file", names)
        self.assertIn("search_files", names)
        self.assertFalse({"run_command", "write_file", "edit_file", "board_move_card"} & names)
        with self.assertRaises(ValueError) as refused:
            agent._prepare("run_command", {"command": "ls"})
        self.assertIn("Execute", str(refused.exception))
        prepared = agent._prepare("search_files", {"pattern": "x"})
        self.assertIn("matches", agent._execute(prepared, {}))
        tools.end_card_turn()
        self.assertIn("run_command", {t["function"]["name"] for t in agent.tools()})


# ------------------------------------------ the board is per project, not per process

class PerProjectTests(ProtocolTest):
    """Which board a worker is about (owner report, 2026-09-18: "the Switchboard is global").

    The backend used to take `<workspace>/issues` literally and to fall back to the process's
    cwd, so a pane in a subdirectory saw no board while a pane with no workspace at all saw the
    board of the directory Relay was launched from.  The rule is now the GUI's
    (`src/BoardWorkspace.h`): the nearest ancestor holding `issues/board.yaml`, from the given
    workspace and from nothing else.
    """

    def project(self, title="Other project card") -> Path:
        """A second repository, with a Switchboard of its own and no cards."""
        tmp = tempfile.TemporaryDirectory()
        self.addCleanup(tmp.cleanup)
        repo = Path(tmp.name).resolve()
        (repo / "issues").mkdir()
        (repo / "issues" / B.BOARD_CONFIG).write_text(CONFIG, encoding="utf-8")
        return repo

    def test_two_workspaces_keep_their_own_cards(self):
        mine = self.make_card(title="First project card")
        other = self.project()
        block = self.commands.configure(str(other), {})
        self.assertEqual(block["root"], str(other / "issues"))
        self.assertEqual(block["cards"], 0)
        self.send(type="board_create", id="r9", tab="features", status="inbox",
                  title="Other project card", text="only on the other board")
        board = self.send(type="board_open")[0]
        self.assertEqual([c["title"] for c in board["cards"]], ["Other project card"])
        self.assertEqual(board["root"], str(other / "issues"))
        # The first project is untouched, and pointing back at it shows its own card again.
        self.assertEqual(self.commands.configure(str(self.repo), {})["root"], str(self.root))
        back = self.send(type="board_open")[0]
        self.assertEqual([c["id"] for c in back["cards"]], [mine])
        self.assertEqual(len(B.Board(other / "issues", other).cards()), 1)

    def test_no_workspace_means_no_board_whatever_the_process_cwd_is(self):
        elsewhere = boardless_dir(self)
        here = os.getcwd()
        self.addCleanup(os.chdir, here)
        os.chdir(self.repo)          # the directory Relay was "launched from" has a board
        for workspace in (str(elsewhere), "", None):
            commands = P.BoardCommands(self.turns, self.events.append)
            self.assertIsNone(commands.configure(workspace, {}), workspace)
            self.assertIsNone(commands.agent_tools(workspace, {}), workspace)
            with self.assertRaises(ValueError):
                commands.dispatch({"type": "board_open"})
            with self.assertRaises(ValueError):
                P.card_attachments(workspace or "", [{"id": "AAAA"}])
        # An explicit `board.dir` still wins, workspace or no workspace.
        block = P.BoardCommands(self.turns, self.events.append).configure(
            "", {"board": {"dir": str(self.root)}})
        self.assertEqual(block["root"], str(self.root))

    def test_a_subdirectory_finds_its_ancestors_board_and_the_repo_is_the_project_root(self):
        card_id = self.make_card()
        deep = self.repo / "backend" / "relay_core"
        deep.mkdir(parents=True)
        block = self.commands.configure(str(deep), {})
        self.assertEqual(block["root"], str(self.root))
        self.assertEqual(block["workspace"], str(self.repo))
        board = self.send(type="board_open")[0]
        self.assertEqual(board["workspace"], str(self.repo))
        self.assertEqual([c["id"] for c in board["cards"]], [card_id])
        # `repo` is what the rate file, the cleanup changelog and every event path hang off.
        tools = self.commands.tools
        self.assertEqual(tools.board.repo, self.repo)
        self.assertEqual(tools.rate.path, self.repo / ".relay" / "board-rate.json")
        self.send(type="board_comment", card=card_id, text="from a subdirectory")
        self.assertTrue(self.of("board_activity")[0]["path"].startswith("issues/"))
        # The agent's instance and `#K7Q2` attachments answer from the same tree.
        agent_tools = self.commands.agent_tools(str(deep), {})
        self.assertEqual(agent_tools.board.root, self.root)
        self.assertEqual(agent_tools.board.repo, self.repo)
        self.assertEqual(len(P.card_attachments(str(deep), [card_id])), 1)

    def test_every_board_event_names_the_board_it_is_about(self):
        self.turns.agent = StubBoardAgent(self.commands.agent_tools(str(self.repo), {}))
        card_id = self.make_card()
        seen: dict[str, object] = {}

        def record(events):
            for event in events:
                if str(event.get("event", "")).startswith("board"):
                    seen[event["event"]] = event.get("root", "missing")

        record(self.send(type="board_open", id="o1"))
        record(self.send(type="board_refresh", id="o2"))
        record(self.send(type="board_check", id="o3"))
        record(self.send(type="board_card_get", id="o4", card=card_id))
        written = self.send(type="board_comment", id="w1", card=card_id, text="a note")
        record(written)
        write_id = [e for e in written if e["event"] == "board_written"][0]["write_id"]
        record(self.send(type="board_undo", id="u1", write_id=write_id))
        record(self.send(type="board_ask", id="a1", card=card_id, text="why?"))
        record(self.send(type="board_cleanup", id="k1", dry_run=True))
        self.events.clear()
        self.commands.observe({"event": "delta", "text": "Nothing to tidy."})
        self.commands.observe({"event": "done", "turn_id": "t-9"})
        record(self.events)
        self.assertEqual(set(seen), {"board", "board_changed", "board_problems", "board_card",
                                     "board_written", "board_activity", "board_undone",
                                     "board_thread_appended", "board_cleanup_started",
                                     "board_cleanup_summary"})
        self.assertEqual(set(seen.values()), {str(self.root)})

    def test_re_pointing_the_worker_forgets_the_previous_project(self):
        card_id = self.make_card()
        self.send(type="board_ask", card=card_id, text="why?")
        self.assertEqual(self.commands._ask_card, card_id)
        self.assertIsNotNone(self.commands._ask_hash)
        self.assertTrue(self.commands._snapshot)
        self.commands._ask_turn, self.commands._ask_text = "t-1", ["half an answer"]

        other = self.project()
        self.commands.configure(str(other), {})
        self.assertIsNone(self.commands._ask_card)
        self.assertIsNone(self.commands._ask_hash)
        self.assertIsNone(self.commands._ask_turn)
        self.assertEqual(self.commands._ask_text, [])
        self.assertIsNone(self.commands._brief_mode)
        self.assertEqual(self.commands._snapshot, {})
        # The card conversations of the board we left are gone with it, and the next question
        # seeds a fresh one (19.16).
        self.assertEqual(self.commands.cards.running_cards(), [])
        self.assertIsNone(self.commands.cards.session(card_id))
        built = len(self.cards.builds)
        new_card = self.make_card(title="Other card", text="on the other board")
        self.send(type="board_ask", card=new_card, text="and this?")
        self.assertEqual(len(self.cards.builds), built + 1)
        self.assertIn(f"[Switchboard card #{new_card}", self.cards.prompts[-1]["prompt"])
        # Losing the board entirely clears it too.
        self.commands.configure(str(boardless_dir(self)), {})
        self.assertIsNone(self.commands.tools)
        self.assertIsNone(self.commands._ask_card)
        self.assertEqual(self.commands._snapshot, {})

    def test_configuring_the_same_board_again_keeps_the_conversation(self):
        card_id = self.make_card()
        self.send(type="board_ask", card=card_id, text="why?")
        self.commands.configure(str(self.repo / "backend"), {})   # same project, deeper directory
        self.assertEqual(self.commands._ask_card, card_id)


class WorkerWorkspaceTests(unittest.TestCase):
    """The real worker, run from a directory that *does* have a board (like Relay's own).

    `request.get("workspace", os.getcwd())` let `workspace: ""` through, and `Path("") / "issues"`
    is relative: a `configure` that named no workspace silently opened the board of the directory
    Relay was launched from — this repository's 186 cards, in a window pointing somewhere else.
    """

    def worker(self, *requests, timeout=30) -> list[dict]:
        proc = subprocess.Popen([sys.executable, "-S", "-u", str(ROOT / "backend" / "worker.py")],
                                stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                stderr=subprocess.DEVNULL, text=True, cwd=ROOT,
                                env={**os.environ, "RELAY_KEYRING": "off"})
        events = []
        try:
            for request in requests:
                proc.stdin.write(json.dumps(request) + "\n")
            proc.stdin.write(json.dumps({"type": "shutdown"}) + "\n")
            proc.stdin.flush()
            timer = threading.Timer(timeout, proc.kill)
            timer.start()
            for line in proc.stdout:
                events.append(json.loads(line))
            timer.cancel()
        finally:
            proc.wait(timeout=5)
            for stream in (proc.stdin, proc.stdout):
                try:
                    stream.close()
                except (BrokenPipeError, OSError):
                    pass
        return events

    @staticmethod
    def request(workspace) -> dict:
        return {"type": "configure", "api_key": "k", "base_url": "http://127.0.0.1:9/v1",
                "model": "test/model", "workspace": workspace}

    def test_the_launch_directorys_board_is_never_adopted_by_a_workspaceless_configure(self):
        self.assertTrue((ROOT / "issues" / B.BOARD_CONFIG).is_file(), "the worker's cwd has a board")
        with tempfile.TemporaryDirectory() as tmp:
            project = Path(tmp).resolve()
            (project / "issues").mkdir()
            (project / "issues" / B.BOARD_CONFIG).write_text(CONFIG, encoding="utf-8")
            events = self.worker(self.request(""), self.request(str(project)))
        configured = [e for e in events if e["event"] == "configured"]
        self.assertEqual(len(configured), 2, events)
        self.assertNotIn("board", configured[0])
        self.assertEqual(configured[1]["board"]["root"], str(project / "issues"))
        self.assertEqual(configured[1]["board"]["workspace"], str(project))


# ------------------------------------- attaching a project to a pane that is already talking

class AttachTest(unittest.TestCase):
    """The harness for protocol 19.12: a pane, a live Agent, and projects to point it at."""

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.dir = Path(self.tmp.name).resolve()
        self.events = []
        self.turns = StubTurns()
        self.commands = P.BoardCommands(self.turns, self.events.append)
        self.config = ProviderConfig("https://example.invalid/v1", "test-model", "k", {}, 1024)

    # ---- fixtures
    def project(self, name: str, folder: str | None = None) -> Path:
        """A project directory, with a board in `folder` when one is named."""
        root = self.dir / name
        root.mkdir(parents=True, exist_ok=True)
        if folder:
            (root / folder).mkdir(parents=True)
            (root / folder / B.BOARD_CONFIG).write_text(CONFIG, encoding="utf-8")
        return root

    def agent(self, workspace: Path, request: dict | None = None):
        """A live Agent wired to this worker, as `configure` wires one."""
        agent = Agent(self.config, str(workspace), lambda event: None, provider=object(),
                      board=self.commands.agent_tools(str(workspace), request or {}),
                      session_dir=str(self.dir / ".sessions"))
        self.turns.agent = agent
        self.commands.bind_agent(agent)
        return agent

    # ---- helpers
    def send(self, **request):
        self.events.clear()
        self.commands.dispatch(request)
        return self.events

    def of(self, name):
        return [e for e in self.events if e["event"] == name]

    @staticmethod
    def board_tools(agent):
        return sorted(t["function"]["name"] for t in agent.tools()
                      if t["function"]["name"].startswith("board_"))

    @staticmethod
    def tree(root: Path) -> list[str]:
        """Every path under `root`, with the bytes of each file: a snapshot to compare against."""
        out = []
        for path in sorted(root.rglob("*")):
            rel = str(path.relative_to(root))
            out.append(rel if path.is_dir() else f"{rel}:{path.read_bytes()!r}")
        return out


class SetBoardTests(AttachTest):
    """`set_board`: the board changes, the conversation does not (protocol 19.12)."""

    def test_a_tab_gains_a_board_mid_conversation_and_keeps_its_conversation(self):
        here = self.project("nowhere")
        there = self.project("project", "switchboard")
        self.commands.configure(str(here), {})
        agent = self.agent(here)
        agent.messages.append({"role": "user", "content": "a question from before"})
        identity = (id(agent), id(agent.messages), len(agent.messages), agent.session_id)
        self.assertEqual(self.board_tools(agent), [])
        self.assertNotIn("Switchboard", agent.system_prompt())

        self.events.clear()
        self.commands.set_board({"type": "set_board", "id": "s1", "board": {"dir": str(there)}})
        state = self.of("board_state")[0]
        self.assertEqual(state["id"], "s1")
        self.assertEqual(state["applies"], "now")
        self.assertEqual(state["board"]["root"], str(there / "switchboard"))
        self.assertEqual(state["board"]["project"], str(there))
        self.assertTrue(state["board"]["exists"])
        self.assertEqual(state["board"]["state"], "ready")
        # The tools and the policy arrive; the conversation is the same object, unshortened.
        self.assertTrue(set(T.TOOL_NAMES) <= set(self.board_tools(agent)))
        self.assertIn("board_rate_limited", agent.system_prompt())
        self.assertEqual((id(agent), id(agent.messages), len(agent.messages), agent.session_id),
                         identity)
        self.assertEqual(agent.messages[-1]["content"], "a question from before")

    def test_board_null_detaches_and_the_tools_and_the_policy_go_with_it(self):
        there = self.project("project", "issues")
        self.commands.configure(str(there), {})
        agent = self.agent(there)
        agent.messages.append({"role": "user", "content": "still here"})
        identity = (id(agent.messages), len(agent.messages))
        self.assertTrue(set(T.TOOL_NAMES) <= set(self.board_tools(agent)))

        self.events.clear()
        self.commands.set_board({"type": "set_board", "id": "s2", "board": None})
        self.assertEqual(self.of("board_state")[0], {"event": "board_state", "id": "s2",
                                                     "board": None, "applies": "now"})
        self.assertEqual(self.board_tools(agent), [])
        self.assertNotIn("Switchboard", agent.system_prompt())
        self.assertEqual((id(agent.messages), len(agent.messages)), identity)
        with self.assertRaises(ValueError):
            self.commands.dispatch({"type": "board_open"})

    def test_attach_false_is_no_board_even_where_one_is_found(self):
        there = self.project("project", "issues")
        self.assertIsNone(self.commands.configure(str(there), {"board": {"attach": False}}))
        self.assertIsNone(self.commands.agent_tools(str(there), {"board": {"attach": False}}))
        agent = self.agent(there, {"board": {"attach": False}})
        self.assertEqual(self.board_tools(agent), [])
        self.assertNotIn("Switchboard", agent.system_prompt())
        with self.assertRaises(ValueError):
            self.commands.dispatch({"type": "board_open"})

    def test_a_set_board_mid_turn_lands_when_the_turn_ends(self):
        here = self.project("nowhere")
        there = self.project("project", "switchboard")
        self.commands.configure(str(here), {})
        agent = self.agent(here)
        self.turns.busy = True

        self.events.clear()
        self.commands.set_board({"type": "set_board", "id": "s3", "board": {"dir": str(there)}})
        said = self.of("board_state")[0]
        self.assertEqual(said["applies"], "turn_end")
        self.assertEqual(said["board"]["root"], str(there / "switchboard"))
        # The running turn keeps the tool set it started with.
        self.assertEqual(self.board_tools(agent), [])
        self.assertIsNone(self.commands.tools)

        self.turns.busy = False
        self.events.clear()
        self.commands.observe({"event": "done", "turn_id": "t-1"})
        landed = self.of("board_state")[0]
        self.assertEqual((landed["id"], landed["applies"], landed["at"]), ("s3", "now", "turn_end"))
        self.assertTrue(set(T.TOOL_NAMES) <= set(self.board_tools(agent)))

    def test_dir_may_name_the_project_or_the_board_folder(self):
        there = self.project("project", "issues")
        for named in (there, there / "issues"):
            block = self.commands.configure(str(self.dir), {"board": {"dir": str(named)}})
            self.assertEqual(block["root"], str(there / "issues"), named)
            self.assertEqual(block["workspace"], str(there), named)
            self.assertEqual(block["folder"], "issues", named)

    def test_the_board_folders_are_tried_in_order_nearest_ancestor_winning(self):
        both = self.project("both", "issues")
        (both / "switchboard").mkdir()
        (both / "switchboard" / B.BOARD_CONFIG).write_text(CONFIG, encoding="utf-8")
        self.assertEqual(T.find_board_root(both), both / "switchboard")
        # `.switchboard/` is first in `B.BOARD_FOLDERS`, so it wins over both older spellings.
        (both / ".switchboard").mkdir()
        (both / ".switchboard" / B.BOARD_CONFIG).write_text(CONFIG, encoding="utf-8")
        self.assertEqual(T.find_board_root(both), both / ".switchboard")
        self.assertEqual(T.named_board_root(both), both / ".switchboard")
        outer = self.project("outer", "switchboard")
        inner = outer / "inner"
        (inner / "issues").mkdir(parents=True)
        (inner / "issues" / B.BOARD_CONFIG).write_text(CONFIG, encoding="utf-8")
        deep = inner / "src"
        deep.mkdir()
        self.assertEqual(T.find_board_root(deep), inner / "issues")


class SectionMessageTests(AttachTest):
    """`board_sections` (19.17): the gear beside the section checkboxes, over the agent's tool.

    Add, remove, merge and rename are one message because they are one rewrite of `board.yaml`,
    and none of them touches a card: a section is a view of the statuses.
    """

    def point(self):
        project = self.project("sections", "switchboard")
        self.commands.configure(str(project), {"board": {"dir": str(project / "switchboard"),
                                                         "project": str(project)}})
        return project

    def send_sections(self, **kw):
        events = self.send(type="board_sections", id="s1", **kw)
        errors = [e for e in events if e["event"] == "error"]
        self.assertEqual(errors, [], errors)
        return [e for e in events if e["event"] == "board_written"][0]

    def test_merging_two_sections_is_one_write_and_the_panes_are_told(self):
        project = self.point()
        written = self.send_sections(
            columns=["inbox", "discussing", "ready", "in-progress", "needs-qa", "done"],
            column_statuses={"needs-qa": ["needs-qa-llm", "needs-qa-human", "needs-review",
                                          "needs-labels", "needs-ab"]},
            column_titles={"needs-qa": "Checks"})
        self.assertEqual(written["kind"], "board_sections")
        config = B.Board(project / "switchboard", project).config()
        self.assertNotIn("waiting", config["columns"])
        self.assertEqual(config["column_titles"], {"needs-qa": "Checks"})
        # Every pane on this board redraws from the file that was written, so the config the
        # GUI is handed carries the merge and the new name.
        block = self.commands._config()
        self.assertEqual(block["column_titles"], {"needs-qa": "Checks"})
        self.assertIn("needs-review", block["column_statuses"]["needs-qa"])
        self.assertNotIn("waiting", block["column_statuses"])

    def test_the_config_travels_with_every_change_not_only_the_open(self):
        self.point()
        self.send_sections(column_titles={"ready": "Up next"})
        changed = [e for e in self.send(type="board_refresh", id="r1")
                   if e["event"] in ("board_changed", "board")]
        self.assertTrue(changed)
        self.assertEqual(changed[-1]["config"]["column_titles"], {"ready": "Up next"})

    def test_a_section_the_board_invented_needs_its_statuses(self):
        self.point()
        events = self.send(type="board_sections", id="s1", columns=["triage", "ready", "done"])
        self.assertEqual([e["event"] for e in events if e["event"] == "error"], ["error"])
        errors = [e for e in events if e["event"] == "error"][0]
        self.assertIn("triage", errors["text"])

    def test_the_message_needs_something_to_change(self):
        self.point()
        with self.assertRaises(ValueError) as caught:
            self.commands.dispatch({"type": "board_sections", "id": "s1"})
        self.assertIn("columns", str(caught.exception))


class FolderMessageTests(AttachTest):
    """`board_folder {hidden}` (19.15): the one message that renames an existing board's folder."""

    def point(self, folder: str = "switchboard"):
        project = self.project("shown", folder)
        block = self.commands.configure(str(project), {"board": {"dir": str(project / folder),
                                                                "project": str(project)}})
        self.assertEqual(block["folder"], folder)
        return project

    def test_hiding_the_folder_moves_it_and_repoints_the_worker(self):
        project = self.point()
        changed = [e for e in self.send(type="board_folder", id="f1", hidden=True)
                   if e["event"] == "board_folder_changed"][0]
        self.assertEqual((changed["id"], changed["old"], changed["new"], changed["hidden"]),
                         ("f1", "switchboard", ".switchboard", True))
        self.assertEqual(changed["method"], "rename")           # no git here
        self.assertEqual(changed["root"], str(project / ".switchboard"))
        self.assertFalse((project / "switchboard").exists())
        # The worker is on the new root, so the next card is written there.
        self.assertEqual(changed["board"]["root"], str(project / ".switchboard"))
        self.assertEqual(changed["board"]["folder"], ".switchboard")
        self.assertEqual(self.commands.tools.board.root, project / ".switchboard")
        # And back: `hidden: false` shows it again.
        back = [e for e in self.send(type="board_folder", id="f2", hidden=False)
                if e["event"] == "board_folder_changed"][0]
        self.assertEqual((back["old"], back["new"], back["hidden"]),
                         (".switchboard", "switchboard", False))
        self.assertEqual(self.commands.tools.board.root, project / "switchboard")

    def test_an_issues_board_is_refused_and_nothing_moves(self):
        project = self.point("issues")
        with self.assertRaises(ValueError) as caught:
            self.commands.dispatch({"type": "board_folder", "id": "f1", "hidden": True})
        self.assertIn("issues/", str(caught.exception))
        self.assertTrue((project / "issues" / B.BOARD_CONFIG).is_file())
        self.assertFalse((project / ".switchboard").exists())

    def test_hidden_must_be_a_boolean_and_the_board_must_exist(self):
        self.point()
        for bad in ({}, {"hidden": "yes"}, {"hidden": 1}):
            with self.assertRaises(ValueError):
                self.commands.dispatch({"type": "board_folder", "id": "f1", **bad})
        project = self.project("fresh")
        self.commands.configure(str(project), {"board": {"project": str(project),
                                                         "state": "uninitialized"}})
        with self.assertRaises(ValueError):
            self.commands.dispatch({"type": "board_folder", "id": "f2", "hidden": True})
        self.assertEqual(self.tree(project), [])

    def test_a_turn_in_flight_refuses_the_move(self):
        self.point()
        self.turns.busy = True
        with self.assertRaises(ValueError) as caught:
            self.commands.dispatch({"type": "board_folder", "id": "f1", "hidden": True})
        self.assertIn("agent turn", str(caught.exception))


class NewBoardFolderTests(AttachTest):
    """`board.folder`: which folder a board this pane creates goes in (the Options toggle)."""

    def test_the_option_chooses_the_folder_a_new_board_would_go_in(self):
        project = self.project("fresh")
        for folder, expected in ((None, ".switchboard"), (".switchboard", ".switchboard"),
                                 ("switchboard", "switchboard")):
            block = self.commands.configure(str(project), {"board": {
                "project": str(project), "state": "uninitialized",
                **({"folder": folder} if folder else {})}})
            self.assertEqual(block["root"], str(project / expected), folder)
            self.assertEqual(self.tree(project), [], folder)    # still nothing on disk

    def test_a_folder_that_is_not_a_board_folder_is_refused(self):
        project = self.project("fresh")
        with self.assertRaises(ValueError):
            self.commands.configure(str(project), {"board": {"project": str(project),
                                                            "folder": "cards"}})
        with self.assertRaises(ValueError):
            self.commands.configure(str(project), {"board": {"project": str(project),
                                                            "folder": 7}})

    def test_the_folder_never_moves_a_board_that_already_exists(self):
        project = self.project("has-one", "switchboard")
        block = self.commands.configure(str(project), {"board": {"project": str(project),
                                                                 "folder": ".switchboard"}})
        self.assertEqual(block["root"], str(project / "switchboard"))
        self.assertFalse((project / ".switchboard").exists())


class InitTests(AttachTest):
    """Nothing is created until the user says yes (protocol 19.12)."""

    def uninitialized(self, name: str = "fresh", folder: str | None = None):
        project = self.project(name)
        request = {"board": {"project": str(project), "state": "uninitialized",
                             **({"folder": folder} if folder else {})}}
        block = self.commands.configure(str(project), request)
        return project, request, block

    def test_an_uninitialized_project_attaches_but_nothing_is_on_disk(self):
        project, request, block = self.uninitialized()
        # Hidden by default (owner, 2026-09-19); nothing on disk either way.
        self.assertEqual(block["root"], str(project / ".switchboard"))
        self.assertEqual(block["folder"], ".switchboard")
        self.assertEqual(block["state"], "uninitialized")
        self.assertFalse(block["exists"])
        self.assertEqual(block["cards"], 0)
        self.assertEqual(self.tree(project), [])

    def test_board_open_answers_an_empty_board_and_creates_nothing(self):
        project, _, _ = self.uninitialized()
        opened = [e for e in self.send(type="board_open", id="o1") if e["event"] == "board"][0]
        self.assertEqual(opened["cards"], [])
        self.assertFalse(opened["exists"])
        self.assertEqual(opened["state"], "uninitialized")
        self.assertEqual(opened["config"]["columns"], B.DEFAULT_CONFIG["columns"])
        self.assertEqual(opened["problems"], [])
        # Reading never creates: not the open, not a refresh, not the checks, not a card read.
        self.send(type="board_refresh", id="o2")
        self.send(type="board_check", id="o3")
        with self.assertRaises(ValueError):
            self.commands.dispatch({"type": "board_card_get", "id": "o4", "card": "AAAA"})
        self.assertEqual(self.tree(project), [])

    def test_the_agent_gets_the_creating_tool_and_a_one_line_note(self):
        project, request, _ = self.uninitialized()
        agent = self.agent(project, request)
        self.assertEqual(self.board_tools(agent), ["board_create_card"])
        prompt = agent.system_prompt()
        self.assertIn("this project has no Switchboard yet", prompt)
        self.assertNotIn("board_rate_limited", prompt)          # not the full policy block

    def test_the_owners_first_card_asks_first_and_lands_on_a_yes(self):
        project, _, _ = self.uninitialized()
        events = self.send(type="board_create", id="w1", tab="features", status="inbox",
                           text="the first card of this project")
        asked = [e for e in events if e["event"] == "board_init_request"]
        self.assertEqual(len(asked), 1, events)
        self.assertEqual(asked[0]["reason"], "card-command")
        self.assertEqual(asked[0]["project"], str(project))
        self.assertEqual(asked[0]["dir"], str(project / B.DEFAULT_BOARD_FOLDER))
        self.assertEqual(asked[0]["root"], asked[0]["dir"])
        self.assertEqual(asked[0]["request_id"], "w1")
        self.assertIn("the first card", asked[0]["title"])
        self.assertEqual(self.tree(project), [])                # still nothing, waiting on the user

        self.events.clear()
        self.commands.dispatch({"type": "board_init_answer", "id": asked[0]["id"], "accept": True})
        created = self.of("board_created")
        self.assertEqual(created[0]["root"], str(project / B.DEFAULT_BOARD_FOLDER))
        self.assertEqual(created[0]["workspace"], str(project))
        self.assertEqual(created[0]["project"], str(project))
        self.assertEqual(created[0]["files"], [".switchboard/board.yaml", ".switchboard/.gitignore",
                                               ".switchboard/threads/.gitkeep", ".gitattributes"])
        # The card the user typed is not lost: the parked write is replayed.
        written = self.of("board_written")
        self.assertEqual(written[0]["id"], "w1")
        card = B.Board(project / B.DEFAULT_BOARD_FOLDER, project).card_by_id(written[0]["card_id"])
        self.assertIn("the first card of this project", card.body)
        self.assertEqual(self.commands.state_block()["state"], "ready")

    def test_the_owners_first_card_is_refused_on_a_no_and_nothing_is_written(self):
        project, _, _ = self.uninitialized()
        events = self.send(type="board_create", id="w1", tab="features", status="inbox", text="a card")
        init_id = [e for e in events if e["event"] == "board_init_request"][0]["id"]
        self.events.clear()
        self.commands.dispatch({"type": "board_init_answer", "id": init_id, "accept": False})
        error = self.of("error")[0]
        self.assertEqual((error["id"], error["code"]), ("w1", "board_not_initialized"))
        self.assertEqual(self.of("board_created"), [])
        self.assertEqual(self.tree(project), [])
        # The no is remembered: a second card does not reopen the dialog.
        events = self.send(type="board_create", id="w2", tab="features", status="inbox", text="another")
        self.assertEqual([(e["event"], e.get("code")) for e in events],
                         [("error", "board_not_initialized")])
        self.assertEqual(self.tree(project), [])

    def test_the_agents_first_card_asks_on_the_turn_thread_and_lands_on_a_yes(self):
        project, request, _ = self.uninitialized()
        agent = self.agent(project, request)
        before = self.tree(project)
        result = self.answer_from_another_thread(
            lambda: agent.board.run("board_create_card", {"tab": "features", "status": "inbox",
                                                          "title": "Voice mode", "request": "add it"}),
            accept=True)
        asked = [e for e in self.events if e["event"] == "board_init_request"][0]
        self.assertEqual((asked["reason"], asked["title"]), ("agent-card", "Voice mode"))
        self.assertEqual(before, [])
        self.assertNotIn("error", result)
        self.assertEqual(agent.board.state, "ready")
        # The full tools and the full policy arrive in the same turn, and so does the owner's half.
        self.assertTrue(set(T.TOOL_NAMES) <= set(self.board_tools(agent)))
        self.assertIn("board_rate_limited", agent.system_prompt())
        self.assertEqual(self.commands.tools.state, "ready")
        self.assertEqual(len(B.Board(project / B.DEFAULT_BOARD_FOLDER, project).cards()), 1)

    def test_the_agents_first_card_is_told_no_and_does_not_ask_again(self):
        project, request, _ = self.uninitialized()
        agent = self.agent(project, request)
        result = self.answer_from_another_thread(
            lambda: agent.board.run("board_create_card", {"tab": "features", "status": "inbox",
                                                          "title": "Voice mode", "request": "add it"}),
            accept=False)
        self.assertEqual(result["code"], "board_not_initialized")
        self.assertIn("declined", result["error"])
        self.assertEqual(self.tree(project), [])
        # No second dialog, and the same plain result.
        self.events.clear()
        again = agent.board.run("board_create_card", {"tab": "features", "status": "inbox",
                                                      "title": "Again", "request": "x"})
        self.assertEqual(again["code"], "board_not_initialized")
        self.assertEqual([e for e in self.events if e["event"] == "board_init_request"], [])

    def test_stopping_the_turn_while_the_dialog_is_open_ends_the_tool_call(self):
        project, request, _ = self.uninitialized()
        agent = self.agent(project, request)
        outcome = {}

        def work():
            try:
                agent.board.run("board_create_card", {"tab": "features", "status": "inbox",
                                                      "title": "Voice mode", "request": "add it"})
            except BaseException as exc:                 # Cancelled, as every blocking tool raises
                outcome["raised"] = type(exc).__name__

        worker = threading.Thread(target=work)
        worker.start()
        self.wait_for_ask()
        agent.cancel_event.set()
        worker.join(5)
        self.assertFalse(worker.is_alive())
        self.assertEqual(outcome.get("raised"), "Cancelled")
        self.assertEqual(self.tree(project), [])

    def test_board_init_creates_the_board_outright_because_the_gui_already_asked(self):
        project, _, _ = self.uninitialized()
        events = self.send(type="board_init", id="i1", project=str(project))
        created = [e for e in events if e["event"] == "board_created"][0]
        self.assertEqual(created["root"], str(project / B.DEFAULT_BOARD_FOLDER))
        state = [e for e in events if e["event"] == "board_state"][0]
        self.assertEqual((state["id"], state["applies"]), ("i1", "now"))
        self.assertTrue(state["board"]["exists"])
        self.assertEqual(state["board"]["state"], "ready")
        self.assertEqual(sorted(p.name for p in (project / B.DEFAULT_BOARD_FOLDER).iterdir()),
                         [".gitignore", "board.yaml", "survey-state.json", "threads"])
        # A board created now owes the page agent's survey (19.18): the marker says "pending"
        # until the first `board_open` on it runs the survey turn.
        marker = project / B.DEFAULT_BOARD_FOLDER / "survey-state.json"
        self.assertEqual(json.loads(marker.read_text())["state"], "pending")
        # It is safe to send twice: a board that exists is not scaffolded again.
        events = self.send(type="board_init", id="i2", project=str(project))
        self.assertEqual([e["event"] for e in events], ["board_state"])

    def test_board_init_with_git_init_makes_a_repository_unless_there_is_one(self):
        """The project picker's "Initialize new project here" (#916B): `git init` when the directory
        is not inside a repository, and nothing — no re-init, no parent's config touched — when it
        is. The board is created either way, and the `board_state` says which it was."""
        project, _, _ = self.uninitialized()
        events = self.send(type="board_init", id="g1", project=str(project), git_init=True)
        state = [e for e in events if e["event"] == "board_state"][0]
        self.assertEqual(state["git"], "git repository initialized")
        self.assertTrue((project / ".git").is_dir())
        self.assertTrue((project / B.DEFAULT_BOARD_FOLDER / B.BOARD_CONFIG).is_file())
        head = (project / ".git" / "HEAD").read_text(encoding="utf-8")
        config = (project / ".git" / "config").read_text(encoding="utf-8")
        # A directory inside that repository stays a folder of it: no nested repository, and the
        # parent's HEAD and config are exactly as git init left them.
        nested = project / "sub"
        nested.mkdir()
        events = self.send(type="board_init", id="g2", project=str(nested), git_init=True)
        state = [e for e in events if e["event"] == "board_state"][0]
        self.assertIn("left alone", state["git"])
        self.assertIn(str(project), state["git"])
        self.assertFalse((nested / ".git").exists())
        self.assertEqual((project / ".git" / "HEAD").read_text(encoding="utf-8"), head)
        self.assertEqual((project / ".git" / "config").read_text(encoding="utf-8"), config)
        # A repository already: never re-initialised.
        second = self.project("second")
        subprocess.run(["git", "init", "-q", str(second)], check=True, capture_output=True)
        before = sorted(p.name for p in (second / ".git").iterdir())
        events = self.send(type="board_init", id="g3", project=str(second), git_init=True)
        state = [e for e in events if e["event"] == "board_state"][0]
        self.assertEqual(state["git"], "already a git repository")
        self.assertEqual(sorted(p.name for p in (second / ".git").iterdir()), before)
        # Without the flag nothing about git happens and the event carries no `git` at all.
        third = self.project("third")
        events = self.send(type="board_init", id="g4", project=str(third))
        state = [e for e in events if e["event"] == "board_state"][0]
        self.assertNotIn("git", state)
        self.assertFalse((third / ".git").exists())
        with self.assertRaises(ValueError):
            self.commands.dispatch({"type": "board_init", "id": "g5", "project": str(third), "git_init": "yes"})

    def test_the_project_tree_is_untouched_until_a_yes(self):
        """The whole read-only surface of the worker, against a byte-for-byte snapshot."""
        project, request, _ = self.uninitialized()
        (project / "src").mkdir()
        (project / "src" / "main.c").write_text("int main(void) { return 0; }\n", encoding="utf-8")
        agent = self.agent(project, request)
        before = self.tree(project)
        self.send(type="board_open", id="o1")
        self.send(type="board_refresh", id="o2")
        self.send(type="board_check", id="o3")
        self.commands.state_block()
        agent.system_prompt()
        agent.tools()
        for name in ("board_list", "board_read"):
            self.assertIn("code", agent.board.run(name, {"id": "AAAA"}))
        self.assertEqual(self.tree(project), before)
        self.assertEqual(before, ["src", "src/main.c:b'int main(void) { return 0; }\\n'"])

    # ---- driving the blocking half
    def wait_for_ask(self, timeout: float = 5.0) -> dict:
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            asked = [e for e in list(self.events) if e["event"] == "board_init_request"]
            if asked:
                return asked[0]
            time.sleep(0.01)
        raise AssertionError(f"no board_init_request in {self.events}")

    def answer_from_another_thread(self, call, *, accept: bool):
        """Run a blocking tool call on a turn thread and answer its dialog from this one."""
        self.events.clear()
        out = {}
        worker = threading.Thread(target=lambda: out.update(result=call()))
        worker.start()
        try:
            asked = self.wait_for_ask()
            self.commands.dispatch({"type": "board_init_answer", "id": asked["id"], "accept": accept})
        finally:
            worker.join(5)
        self.assertFalse(worker.is_alive())
        return out["result"]


class LegacyConfigureTests(AttachTest):
    """A `configure` with no `board` block behaves exactly as it did in 384fac4."""

    def test_a_configure_with_no_board_block_walks_up_and_finds_the_project_board(self):
        project = self.project("project", "issues")
        deep = project / "backend" / "relay_core"
        deep.mkdir(parents=True)
        block = self.commands.configure(str(deep), {})
        self.assertEqual(block["root"], str(project / "issues"))
        self.assertEqual(block["workspace"], str(project))
        self.assertEqual(block["cards"], 0)
        self.assertEqual(self.commands.tools.state, "ready")
        self.assertEqual(self.commands.tools.rate.path, project / ".relay" / "board-rate.json")

    def test_a_configure_with_no_board_block_and_no_board_creates_nothing_and_attaches_nothing(self):
        project = self.project("bare")
        self.assertIsNone(self.commands.configure(str(project), {}))
        self.assertIsNone(self.commands.agent_tools(str(project), {}))
        self.assertEqual(self.tree(project), [])
        with self.assertRaises(ValueError):
            self.commands.dispatch({"type": "board_open"})

    def test_a_configure_with_no_workspace_still_has_no_board(self):
        for workspace in ("", None):
            self.assertIsNone(self.commands.configure(workspace, {}), workspace)

    def test_the_configured_block_gained_fields_and_kept_the_old_ones(self):
        project = self.project("project", "issues")
        block = self.commands.configure(str(project), {})
        for key in ("dir", "root", "workspace", "autonomy", "limits", "cards"):
            self.assertIn(key, block)                   # everything 384fac4 sent
        self.assertEqual(block["project"], str(project))
        self.assertEqual(block["folder"], "issues")
        self.assertEqual(block["state"], "ready")
        self.assertTrue(block["exists"])


# ----------------------------------------------- initializing and importing (19.13)

class ProbeAndImportTests(ProtocolTest):
    """`project_probe`, `board_import_propose`, `board_import_apply`.

    The board is the one `ProtocolTest` built, in `<repo>/issues`; the project is `<repo>`.
    """

    def setUp(self):
        super().setUp()
        (self.repo / "TODO.md").write_text("# TODO\n\n- [ ] Wrap long lines\n"
                                           "- [ ] Ship the thing\n", encoding="utf-8")

    def error(self, **request):
        events = self.events
        events.clear()
        with self.assertRaises(ValueError) as raised:
            self.commands.dispatch(request)
        return str(raised.exception)

    # ---- project_probe
    def test_project_probe_answers_with_the_survey_and_writes_nothing(self):
        before = sorted(p.name for p in self.repo.iterdir())
        events = self.send(type="project_probe", id="p1", project=str(self.repo))
        [result] = self.of("project_probe_result")
        self.assertEqual(result["id"], "p1")
        self.assertEqual(result["project"], str(self.repo))
        self.assertEqual(result["version"], PP.PROBE_VERSION)
        self.assertEqual([f["kind"] for f in result["trackers"]], ["checklist"])
        self.assertTrue(result["board"]["present"])
        self.assertEqual(result["root"], str(self.root))
        self.assertEqual(sorted(p.name for p in self.repo.iterdir()), before)
        self.assertEqual([e for e in events if e["event"] == "error"], [])

    def test_project_probe_may_be_narrowed_to_some_trackers(self):
        self.send(type="project_probe", id="p1", project=str(self.repo), kinds=["beads"])
        self.assertEqual(self.of("project_probe_result")[0]["trackers"], [])
        self.assertIn("unknown tracker kind", self.error(
            type="project_probe", id="p1", project=str(self.repo), kinds=["jira"]))

    def test_project_probe_never_falls_back_to_the_process_directory(self):
        for request in ({}, {"project": ""}, {"project": "   "}, {"project": None}):
            message = self.error(type="project_probe", id="p1", **request)
            self.assertIn("needs `project`", message)
            self.assertIn("no default", message)

    def test_a_relative_project_and_a_file_are_both_refused(self):
        self.assertIn("absolute", self.error(type="project_probe", id="p1", project="./here"))
        self.assertIn("is not a directory",
                      self.error(type="project_probe", id="p1", project=str(self.repo / "TODO.md")))

    def test_project_probe_needs_no_board(self):
        elsewhere = boardless_dir(self)
        (elsewhere / "TODO.md").write_text("- [ ] alone\n", encoding="utf-8")
        commands = P.BoardCommands(StubTurns(), self.events.append)
        self.assertIsNone(commands.configure(str(elsewhere), {}))
        self.events.clear()
        commands.dispatch({"type": "project_probe", "id": "p1", "project": str(elsewhere)})
        [result] = self.of("project_probe_result")
        self.assertFalse(result["board"]["present"])
        self.assertNotIn("root", result)

    # ---- board_import_propose
    def test_propose_lists_the_cards_an_import_would_create(self):
        self.send(type="board_import_propose", id="i1", project=str(self.repo))
        [proposals] = self.of("board_import_proposals")
        self.assertEqual(proposals["id"], "i1")
        self.assertEqual(proposals["root"], str(self.root))
        self.assertEqual([p["title"] for p in proposals["proposals"]],
                         ["Wrap long lines", "Ship the thing"])
        self.assertEqual(proposals["skipped"], 0)
        self.assertEqual(self.board.cards(), [])            # nothing was written

    def test_propose_needs_a_project_too(self):
        self.assertIn("needs `project`", self.error(type="board_import_propose", id="i1"))

    # ---- board_import_apply
    def keys(self):
        self.send(type="board_import_propose", id="i1", project=str(self.repo))
        return [p["source_key"] for p in self.of("board_import_proposals")[0]["proposals"]]

    def test_apply_creates_the_ticked_cards_and_says_where_they_landed(self):
        keys = self.keys()
        events = self.send(type="board_import_apply", id="a1", project=str(self.repo),
                           keys=keys[:1])
        [imported] = self.of("board_imported")
        self.assertEqual(imported["id"], "a1")
        self.assertEqual(imported["root"], str(self.root))
        self.assertEqual(len(imported["cards"]), 1)
        card = imported["cards"][0]
        self.assertEqual(card["source_key"], keys[0])
        self.assertEqual(card["status"], "inbox")
        self.assertEqual(card["tab"], "features")
        self.assertTrue((self.repo / card["path"]).is_file())
        self.assertEqual(imported["skipped"], [])
        self.assertTrue([e for e in events if e["event"] == "board_changed"])
        self.assertEqual(len(self.board.cards()), 1)

    def test_a_key_that_is_already_imported_comes_back_as_skipped(self):
        keys = self.keys()
        self.send(type="board_import_apply", id="a1", project=str(self.repo), keys=keys)
        self.send(type="board_import_apply", id="a2", project=str(self.repo), keys=keys)
        self.assertEqual(self.of("board_imported")[0]["skipped"], keys)
        self.assertEqual(len(self.board.cards()), 2)

    def test_apply_takes_a_tab_and_refuses_one_this_board_has_not(self):
        keys = self.keys()
        self.send(type="board_import_apply", id="a1", project=str(self.repo), keys=keys[:1],
                  tab="bugs")
        self.assertEqual(self.of("board_imported")[0]["cards"][0]["tab"], "bugs")
        self.assertIn("unknown tab", self.error(type="board_import_apply", id="a2",
                                                project=str(self.repo), keys=keys[1:],
                                                tab="nowhere"))

    def test_apply_needs_keys(self):
        self.assertIn("needs `keys`", self.error(type="board_import_apply", id="a1",
                                                 project=str(self.repo)))
        self.assertIn("needs `keys`", self.error(type="board_import_apply", id="a1",
                                                 project=str(self.repo), keys=[]))

    def test_apply_refuses_a_project_this_pane_is_not_on(self):
        elsewhere = boardless_dir(self)
        message = self.error(type="board_import_apply", id="a1", project=str(elsewhere),
                             keys=["checklist:TODO.md#x"])
        self.assertIn("Point the pane at that project first", message)

    def test_apply_is_refused_while_the_switchboard_agent_is_busy(self):
        keys = self.keys()
        self.turns.busy = True
        events = self.send(type="board_import_apply", id="a1", project=str(self.repo), keys=keys)
        [error] = self.of("error")
        self.assertEqual(error["code"], "board_busy")
        self.assertEqual(error["id"], "a1")
        self.assertEqual(self.of("board_imported"), [])
        self.assertEqual(self.board.cards(), [])

    def test_apply_on_a_pane_with_no_board_says_so(self):
        elsewhere = boardless_dir(self)
        commands = P.BoardCommands(StubTurns(), self.events.append)
        commands.configure(str(elsewhere), {})
        with self.assertRaises(ValueError) as raised:
            commands.dispatch({"type": "board_import_apply", "id": "a1", "keys": ["k"]})
        self.assertEqual(str(raised.exception), P.NO_BOARD_ERROR)

    def test_apply_on_an_uninitialized_board_asks_for_board_init_first(self):
        elsewhere = boardless_dir(self)
        commands = P.BoardCommands(StubTurns(), self.events.append)
        commands.configure(str(elsewhere), {"board": {"project": str(elsewhere),
                                                      "state": "uninitialized"}})
        with self.assertRaises(ValueError) as raised:
            commands.dispatch({"type": "board_import_apply", "id": "a1", "keys": ["k"]})
        self.assertEqual(str(raised.exception), P.NOT_INITIALIZED_ERROR)
        self.assertFalse((elsewhere / B.DEFAULT_BOARD_FOLDER / B.BOARD_CONFIG).exists())


# -------------------------------------------------------- the GitHub sync (19.14)

class ForgeSyncProtocolTests(ProtocolTest):
    """`forge_sync_plan` / `forge_sync_run` against the in-process fake forge.

    The work runs on a thread, so every test waits for the one terminal event the message
    promises and then asserts there was exactly one.
    """

    def setUp(self):
        super().setUp()
        self.gh = FG.FakeGitHub()
        self.addCleanup(self.gh.close)
        previous = os.environ.get("GH_TOKEN")
        os.environ["GH_TOKEN"] = FG.TOKEN          # never a subprocess, never a real credential
        self.addCleanup(lambda: os.environ.__setitem__("GH_TOKEN", previous)
                        if previous is not None else os.environ.pop("GH_TOKEN", None))
        self.configure_github()

    def configure_github(self, extra: str = ""):
        (self.root / B.BOARD_CONFIG).write_text(
            CONFIG + f"github: {{repo: relay/terminal, base_url: '{self.gh.base_url}'{extra}}}\n",
            encoding="utf-8")

    def wait_for(self, *names, timeout=20.0):
        deadline = time.time() + timeout
        while time.time() < deadline:
            found = [e for e in list(self.events) if e.get("event") in names]
            if found:
                time.sleep(0.05)                   # let anything that follows it arrive too
                return found
            time.sleep(0.02)
        self.fail(f"no {names} event; got {[e.get('event') for e in self.events]}")

    def terminal(self, *names):
        found = self.wait_for(*names, "error")
        self.assertEqual(len(found), 1, f"expected exactly one terminal event, got {found}")
        return found[0]

    def error(self, **request):
        self.events.clear()
        with self.assertRaises(ValueError) as raised:
            self.commands.dispatch(request)
        return str(raised.exception)

    def test_a_plan_says_what_would_happen_and_writes_to_neither_side(self):
        self.make_card()
        self.events.clear()
        self.commands.dispatch({"type": "forge_sync_plan", "id": "s1"})
        planned = self.terminal("forge_sync_planned")
        self.assertEqual(planned["event"], "forge_sync_planned")
        self.assertEqual(planned["id"], "s1")
        self.assertEqual(planned["root"], str(self.root))
        self.assertEqual(planned["repo"], "relay/terminal")
        self.assertTrue(planned["dry_run"])
        self.assertEqual(planned["creates"], 1)
        self.assertEqual(planned["cap"], F.DEFAULT_CREATE_CAP)
        self.assertEqual(self.gh.writes, [])
        self.assertEqual([p["action"] for p in planned["cards"]], ["create"])

    def test_a_run_files_the_issue_reports_progress_and_ends_with_one_done(self):
        card_id = self.make_card()
        self.events.clear()
        self.commands.dispatch({"type": "forge_sync_run", "id": "s2"})
        done = self.terminal("forge_sync_done")
        self.assertEqual(done["id"], "s2")
        self.assertEqual(done["root"], str(self.root))
        self.assertEqual(done["pushed"], 1)
        self.assertEqual(len(self.gh.main.issues), 1)
        progress = [e for e in self.events if e["event"] == "forge_sync_progress"]
        self.assertEqual([(p["card"], p["done"], p["total"]) for p in progress], [(card_id, 1, 1)])
        self.assertEqual(progress[0]["root"], str(self.root))
        # A sync writes card files outside BoardTools, so the pane is told.
        self.assertTrue([e for e in self.events if e["event"] == "board_changed"])

    def test_the_repository_may_be_named_in_the_message(self):
        self.gh.add_repo("other/repo")
        (self.root / B.BOARD_CONFIG).write_text(CONFIG, encoding="utf-8")
        self.make_card()
        self.events.clear()
        self.commands.dispatch({"type": "forge_sync_plan", "id": "s3", "repo": "other/repo",
                                "base_url": self.gh.base_url})
        self.assertEqual(self.terminal("forge_sync_planned")["repo"], "other/repo")

    def test_a_board_with_no_repository_says_where_to_put_one(self):
        (self.root / B.BOARD_CONFIG).write_text(CONFIG, encoding="utf-8")
        message = self.error(type="forge_sync_plan", id="s4")
        self.assertIn("board.yaml", message)
        self.assertIn("github:", message)

    def test_a_forge_that_refuses_answers_one_error_with_no_token_in_it(self):
        self.gh.add_repo("dead/repo", has_issues=True)
        self.make_card()
        self.events.clear()
        self.commands.dispatch({"type": "forge_sync_run", "id": "s5", "repo": "missing/repo"})
        error = self.terminal("forge_sync_done")
        self.assertEqual(error["event"], "error")
        self.assertEqual(error["id"], "s5")
        self.assertEqual(error["root"], str(self.root))
        self.assertEqual(error["code"], "forge_unavailable")
        self.assertNotIn(FG.TOKEN, json.dumps(error))
        self.assertNotIn("Traceback", json.dumps(error))
        self.assertEqual(self.gh.writes, [])

    def test_a_missing_credential_is_its_own_code_so_the_gui_can_offer_a_sign_in(self):
        os.environ.pop("GH_TOKEN", None)
        self.make_card()
        self.events.clear()
        # No env token, and no `gh`/`git credential` either: the runner is what a provider
        # shells out with, and an empty answer is "nothing found".
        with unittest.mock.patch.object(GH, "_run", return_value=""):
            self.commands.dispatch({"type": "forge_sync_run", "id": "s12"})
            error = self.terminal("forge_sync_done")
        self.assertEqual(error["event"], "error")
        self.assertEqual(error["code"], "forge_auth")
        self.assertIn("GH_TOKEN", error["text"])
        self.assertEqual(self.gh.writes, [])

    def test_a_second_sync_while_one_is_running_is_refused(self):
        self.make_card()
        self.events.clear()
        self.commands._forge_run = "s6"            # a run in flight
        self.commands.dispatch({"type": "forge_sync_run", "id": "s7"})
        [error] = self.of("error")
        self.assertEqual(error["code"], "forge_busy")
        self.assertEqual(self.gh.writes, [])
        self.commands._forge_run = None

    def test_a_sync_is_refused_while_the_switchboard_agent_is_busy(self):
        self.make_card()
        self.turns.busy = True
        self.events.clear()
        self.commands.dispatch({"type": "forge_sync_plan", "id": "s8"})
        [error] = self.of("error")
        self.assertEqual(error["code"], "board_busy")
        self.assertEqual(self.gh.writes, [])

    def test_a_pane_with_no_board_and_an_uninitialized_one_both_refuse(self):
        elsewhere = boardless_dir(self)
        commands = P.BoardCommands(StubTurns(), self.events.append)
        commands.configure(str(elsewhere), {})
        with self.assertRaises(ValueError) as none:
            commands.dispatch({"type": "forge_sync_plan", "id": "s9"})
        self.assertEqual(str(none.exception), P.NO_BOARD_ERROR)
        commands.configure(str(elsewhere), {"board": {"project": str(elsewhere),
                                                      "state": "uninitialized"}})
        with self.assertRaises(ValueError) as fresh:
            commands.dispatch({"type": "forge_sync_plan", "id": "s10"})
        self.assertEqual(str(fresh.exception), P.NOT_INITIALIZED_ERROR)

    def test_a_bad_comment_kinds_block_is_reported_before_anything_is_sent(self):
        self.configure_github(", comment_kinds: [gossip]")
        self.make_card()
        self.assertIn("gossip", self.error(type="forge_sync_plan", id="s11"))
        self.assertEqual(self.gh.writes, [])


class ForgeSyncAcceptanceTests(ForgeSyncProtocolTests):
    """#GDQN's acceptance, walked end to end through the worker messages.

    A shared work card and its GitHub issue stay in sync both ways — body, comments, status,
    labels — across edits on either side, and a conflict is surfaced rather than lost. Every
    Relay-side step is a `dispatch` (so the thread, the guards and the events the GUI will one
    day render are all in the path); the GitHub side is `web_edit`/`web_comment`, which is a
    human in a browser. The engine tests cover each field alone (`tests/test_forge_sync.py`);
    this is the same walk at the level the Switchboard pane will drive it.
    """

    def sync_run(self, request_id: str) -> dict:
        self.events.clear()
        self.commands.dispatch({"type": "forge_sync_run", "id": request_id})
        done = self.terminal("forge_sync_done")
        self.assertEqual(done["event"], "forge_sync_done", done)
        self.assertEqual(done["errors"], [], done)
        return done

    def hash_of(self, card_id: str) -> str:
        card = self.board.card_by_id(card_id)
        self.assertIsNotNone(card, f"#{card_id} is gone")
        return B.file_hash(card.path)

    def update_card(self, card_id: str, request_id: str, **patch) -> None:
        """`board_update` with a fresh `base_hash`, the way the GUI sends it."""
        events = self.send(type="board_update", id=request_id, card=card_id,
                           base_hash=self.hash_of(card_id), patch=patch)
        self.assertTrue([e for e in events if e["event"] == "board_written"], events)

    def test_a_card_and_its_issue_stay_in_sync_both_ways(self):
        card_id = self.make_card(title="Voice mode", text="add voice transcribe mode",
                                 labels=["feature", "voice"])

        # ---- first run: the card becomes an issue with every mapped field
        done = self.sync_run("a1")
        self.assertEqual(done["pushed"], 1)
        self.assertEqual(done["conflicts"], [])
        issue = self.gh.main.issues[1]
        self.assertEqual(issue["title"], "Voice mode")
        self.assertIn("add voice transcribe mode", issue["body"])
        self.assertIn(f"<!-- relay-id: {card_id} -->", issue["body"])
        self.assertEqual(sorted(l["name"] for l in issue["labels"]),
                         ["feature", "status:inbox", "tab:features", "voice"])
        self.assertEqual(self.board.card_by_id(card_id).front["links"]["github"],
                         "relay/terminal#1")

        # ---- edited here: body, labels, status and a comment all reach the issue
        self.update_card(card_id, "a2",
                         fields={"labels": ["feature", "voice", "beta"]},
                         replace_section={"heading": "Issue",
                                          "text": "add voice transcribe mode, streaming too"})
        self.send(type="board_move", id="a3", card=card_id, status="ready",
                  reason="triaged")
        self.send(type="board_comment", id="a4", card=card_id, kind="note",
                  text="Recording works; it needs a review.")
        done = self.sync_run("a5")
        self.assertEqual(done["pushed"], 1)
        self.assertEqual(done["comments_out"], 1)
        issue = self.gh.main.issues[1]
        self.assertIn("streaming too", issue["body"])
        self.assertEqual(sorted(l["name"] for l in issue["labels"]),
                         ["beta", "feature", "status:ready", "tab:features", "voice"])
        [comment] = self.gh.comments_of(1)
        self.assertIn("it needs a review", comment["body"])
        self.assertIn("<!-- relay-entry:", comment["body"])

        # ---- edited there: body, labels, status and a comment all reach the card
        self.gh.web_edit(1, body=issue["body"].replace(
            "<!-- relay-sync -->", "Edited on the web.\n\n<!-- relay-sync -->"))
        self.gh.web_edit(1, labels=["feature", "tab:features", "status:in-progress", "ui"])
        self.gh.web_comment(1, "Filed from the web.", author="octocat")
        done = self.sync_run("a6")
        self.assertEqual(done["pulled"], 1)
        self.assertEqual(done["comments_in"], 1)
        card = self.board.card_by_id(card_id)
        self.assertIn("Edited on the web.", card.body)
        self.assertEqual(card.status, "in-progress")
        self.assertEqual(list(card.front.get("labels") or []), ["feature", "ui"])
        imported = self.board.thread(card_id)[-1]
        self.assertEqual(imported.author, "octocat")
        self.assertEqual(imported.attrs.get("via"), "github")
        self.assertIn("Filed from the web.", imported.text)
        self.assertTrue([e for e in self.events if e["event"] == "board_changed"])
        problems = [str(p) for p in self.board.check() if p.severity == "error"]
        self.assertEqual(problems, [])

        # ---- and a quiet sync after that touches nothing
        writes = len(self.gh.writes)
        done = self.sync_run("a7")
        self.assertEqual((done["pushed"], done["pulled"], done["comments_out"],
                          done["comments_in"]), (0, 0, 0, 0))
        self.assertEqual(len(self.gh.writes), writes)

    def test_a_conflict_is_surfaced_rather_than_lost(self):
        card_id = self.make_card(title="Voice mode", text="first words")
        self.sync_run("c1")
        number = self.board.card_by_id(card_id).front["links"]["github"].split("#")[1]

        # The same prose edited on both sides, differently.
        self.update_card(card_id, "c2", replace_section={"heading": "Issue",
                                                         "text": "local words"})
        self.gh.web_edit(int(number), body=self.gh.main.issues[int(number)]["body"].replace(
            "first words", "remote words"))
        done = self.sync_run("c3")
        # The prose carries its `## Issue` heading; the words are what matter.
        self.assertEqual([c["field"] for c in done["conflicts"]], ["prose"])
        self.assertIn("local words", done["conflicts"][0]["card"])
        self.assertIn("remote words", done["conflicts"][0]["issue"])
        # Neither side is written, and the conflict is on the card's thread with both versions.
        self.assertIn("local words", self.board.card_by_id(card_id).body)
        self.assertIn("remote words", self.gh.main.issues[int(number)]["body"])
        notes = [e for e in self.board.thread(card_id)
                 if e.kind == "note" and "sync conflict" in e.text]
        self.assertEqual(len(notes), 1)
        self.assertIn("local words", notes[0].text)
        self.assertIn("remote words", notes[0].text)

        # An unresolved conflict is reported again, but does not spam the thread.
        entries = len(self.board.thread(card_id))
        done = self.sync_run("c4")
        self.assertEqual([c["field"] for c in done["conflicts"]], ["prose"])
        self.assertEqual(len(self.board.thread(card_id)), entries)

        # Resolving it on one side lets the sync through.
        self.update_card(card_id, "c5", replace_section={"heading": "Issue",
                                                         "text": "remote words"})
        done = self.sync_run("c6")
        self.assertEqual(done["conflicts"], [])
        self.assertIn("remote words", self.board.card_by_id(card_id).body)
        self.assertIn("remote words", self.gh.main.issues[int(number)]["body"])
        problems = [str(p) for p in self.board.check() if p.severity == "error"]
        self.assertEqual(problems, [])


if __name__ == "__main__":       # pragma: no cover
    unittest.main()
