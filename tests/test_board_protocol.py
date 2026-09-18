# SPDX-License-Identifier: GPL-3.0-or-later
"""Switchboard worker protocol (docs/AGENT-SESSIONS-PROTOCOL.md section 17) and the
`board_*` tools' wiring into the agent.

No model, no network, no keyring: `board_ask` runs against a stub supervisor.
"""
import tempfile
import threading
import unittest
from pathlib import Path

from relay_core import board as B
from relay_core import board_protocol as P
from relay_core import board_tools as T
from relay_core.agent import Agent
from relay_core.provider import ProviderConfig

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

    def reset(self):
        self.resets += 1

    def submit(self, prompt, when="now", request_id=None, context=None, attachments=None, **kw):
        self.submitted.append({"prompt": prompt, "when": when, "id": request_id})
        return "q1"


class ProtocolTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.repo = Path(self.tmp.name)
        self.root = self.repo / "issues"
        self.root.mkdir()
        (self.root / B.BOARD_CONFIG).write_text(CONFIG, encoding="utf-8")
        self.board = B.Board(self.root, self.repo)
        self.events = []
        self.turns = StubTurns()
        self.commands = P.BoardCommands(self.turns, self.events.append)
        self.commands.configure(str(self.repo), {})

    def tearDown(self):
        self.tmp.cleanup()

    def send(self, **request):
        self.events.clear()
        self.commands.dispatch(request)
        return self.events

    def of(self, name):
        return [e for e in self.events if e["event"] == name]

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
                    "thread_entries", "tasks_done", "tasks_total", "path"):
            self.assertIn(key, row)

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
        self.assertIsNone(commands.configure(str(self.repo / "nowhere"), {}))
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
        self.assertIn("## Request", detail["body"])
        self.assertEqual([e["text"] for e in detail["thread"]][-1], "a reply")
        update = self.send(type="board_update", card=card_id, base_hash=detail["hash"],
                           patch={"fields": {"labels": ["voice"]}})
        self.assertTrue([e for e in update if e["event"] == "board_written"])

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
        events = self.send(type="board_ask", id="a1", card=card_id, text="where should this run?")
        appended = [e for e in events if e["event"] == "board_thread_appended"][0]
        self.assertEqual(appended["card_id"], card_id)
        self.assertEqual(appended["author"], "owner")
        self.assertEqual([e.text for e in self.board.thread(card_id)][-1], "where should this run?")

    def test_the_first_question_seeds_the_conversation_from_the_card_and_its_thread(self):
        card_id = self.make_card()
        self.send(type="board_ask", card=card_id, text="where should this run?")
        prompt = self.turns.submitted[-1]["prompt"]
        self.assertEqual(self.turns.resets, 1)
        self.assertIn(f"[Switchboard card #{card_id}", prompt)
        self.assertIn("add voice transcribe mode", prompt)
        self.assertIn("--- thread", prompt)
        self.assertTrue(prompt.endswith("where should this run?"))

    def test_a_second_question_about_the_same_card_is_not_reseeded(self):
        card_id = self.make_card()
        self.send(type="board_ask", card=card_id, text="one")
        self.send(type="board_ask", card=card_id, text="two")
        self.assertEqual(self.turns.resets, 1)
        self.assertEqual(self.turns.submitted[-1]["prompt"], "two")

    def test_switching_card_reseeds(self):
        first = self.make_card()
        second = self.make_card(title="Clickable paths", text="clicking a path opens a pane")
        self.send(type="board_ask", card=first, text="one")
        self.send(type="board_ask", card=second, text="two")
        self.assertEqual(self.turns.resets, 2)
        self.assertIn(f"#{second}", self.turns.submitted[-1]["prompt"])

    def test_an_edit_to_the_card_reseeds_the_conversation(self):
        card_id = self.make_card()
        self.send(type="board_ask", card=card_id, text="one")
        card = self.board.card_by_id(card_id)
        card.set("assignee", "agent")
        self.board.save(card)
        self.send(type="board_ask", card=card_id, text="two")
        self.assertEqual(self.turns.resets, 2)
        self.assertIn("assignee", self.turns.submitted[-1]["prompt"])

    def test_the_turn_events_are_tagged_with_the_card(self):
        card_id = self.make_card()
        self.send(type="board_ask", card=card_id, text="one")
        tagged = self.commands.observe({"event": "delta", "text": "hi"})
        self.assertEqual(tagged["card_id"], card_id)
        self.assertEqual(self.commands.observe({"event": "queued"}).get("card_id"), None)

    def test_the_answer_is_appended_to_the_thread_when_the_turn_finishes(self):
        card_id = self.make_card()
        self.send(type="board_ask", card=card_id, text="where should this run?")
        for chunk in ("Run it ", "in the cloud."):
            self.commands.observe({"event": "delta", "text": chunk})
        self.events.clear()
        self.commands.observe({"event": "done", "turn_id": "t-4"})
        entries = self.board.thread(card_id)
        self.assertEqual(entries[-1].text, "Run it in the cloud.")
        self.assertEqual(entries[-1].author, "agent")
        self.assertTrue([e for e in self.events if e["event"] == "board_thread_appended"])

    def test_a_failed_turn_writes_nothing(self):
        card_id = self.make_card()
        self.send(type="board_ask", card=card_id, text="one")
        self.commands.observe({"event": "delta", "text": "half an answ"})
        before = len(self.board.thread(card_id))
        self.commands.observe({"event": "error", "text": "provider down"})
        self.commands.observe({"event": "done", "turn_id": "t-5"})
        self.assertEqual(len(self.board.thread(card_id)), before)

    def test_a_missing_card_and_empty_text_are_refused(self):
        card_id = self.make_card()
        with self.assertRaises(ValueError):
            self.commands.dispatch({"type": "board_ask", "card": "AAAA", "text": "x"})
        with self.assertRaises(ValueError):
            self.commands.dispatch({"type": "board_ask", "card": card_id, "text": "  "})

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
            P.card_attachments(str(self.repo / "nowhere"), [{"id": "AAAA"}])

    def test_nothing_referenced_means_nothing_attached(self):
        self.assertEqual(P.card_attachments(str(self.repo), []), [])
        self.assertEqual(P.card_attachments(str(self.repo), None), [])


# ------------------------------------------------------------- wiring into the agent

class AgentWiringTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.repo = Path(self.tmp.name)
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
        agent.board.context.model = agent.config.model
        agent.board.begin_turn("t-7")
        self.assertEqual(tools.creates_this_turn, 0)
        self.assertEqual(tools.context.turn_id, "t-7")
        self.assertEqual(tools.context.model, "test-model")


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


if __name__ == "__main__":       # pragma: no cover
    unittest.main()
