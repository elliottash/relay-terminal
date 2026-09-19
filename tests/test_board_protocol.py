# SPDX-License-Identifier: GPL-3.0-or-later
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

import fake_github as FG
from relay_core import board as B
from relay_core import board_protocol as P
from relay_core import board_tools as T
from relay_core import forge_github as GH
from relay_core import forge_sync as F
from relay_core import project_probe as PP
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
        card_id = self.make_card()
        self.send(type="board_ask", card=card_id, text="where should this run?")
        self.turns.busy = True
        refused = self.start()[0]
        self.assertEqual(refused["code"], "board_busy")
        self.assertEqual(refused["card_id"], card_id)
        self.assertTrue(refused["agent_busy"])

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

    def test_no_mode_is_a_discuss_and_the_thread_says_so(self):
        card_id = self.make_card()
        events = self.send(type="board_ask", id="a1", card=card_id, text="is this still wanted?")
        appended = self.of("board_thread_appended")[0]
        self.assertEqual(appended["mode"], "discuss")
        self.assertEqual(self.board.thread(card_id)[-1].attrs.get("mode"), "discuss")
        prompt = self.turns.submitted[-1]["prompt"]
        self.assertIn(f"[Discuss · #{card_id}]", prompt)
        self.assertTrue(prompt.endswith("is this still wanted?"))
        self.assertEqual(self.agent_tools.card_scope.mode, "discuss")
        self.assertTrue(events)

    def test_a_plan_needs_no_words_and_carries_the_plan_brief(self):
        card_id = self.make_card()
        self.send(type="board_ask", id="p1", card=card_id, mode="plan")
        entry = self.board.thread(card_id)[-1]
        self.assertEqual((entry.author, entry.attrs.get("mode"), entry.text),
                         ("owner", "plan", "Plan this card."))
        prompt = self.turns.submitted[-1]["prompt"]
        self.assertIn(f"[Plan · #{card_id}]", prompt)
        self.assertIn("## Plan", prompt)
        self.assertIn(f"[Switchboard card #{card_id}", prompt)      # seeded first
        self.assertEqual(self.agent_tools.card_scope.mode, "plan")
        self.assertEqual(self.agent_tools.card_scope.card_id, card_id)

    def test_a_plan_with_a_note_passes_it_verbatim(self):
        card_id = self.make_card()
        self.send(type="board_ask", card=card_id, mode="plan", text="keep it to the backend")
        self.assertEqual(self.board.thread(card_id)[-1].text, "keep it to the backend")
        self.assertTrue(self.turns.submitted[-1]["prompt"].endswith("keep it to the backend"))

    def test_the_brief_is_sent_when_the_mode_changes_and_not_twice_for_discuss(self):
        card_id = self.make_card()
        self.send(type="board_ask", card=card_id, text="one")
        self.commands.observe({"event": "done", "turn_id": "t-1"})
        self.send(type="board_ask", card=card_id, text="two")
        self.assertEqual(self.turns.submitted[-1]["prompt"], "two")
        self.commands.observe({"event": "done", "turn_id": "t-2"})
        self.send(type="board_ask", card=card_id, mode="plan")
        self.assertIn("[Plan ·", self.turns.submitted[-1]["prompt"])
        self.commands.observe({"event": "done", "turn_id": "t-3"})
        self.send(type="board_ask", card=card_id, text="three")
        self.assertIn("[Discuss ·", self.turns.submitted[-1]["prompt"])

    def test_the_answer_and_the_turn_events_carry_the_mode_and_the_scope_ends(self):
        card_id = self.make_card()
        self.send(type="board_ask", card=card_id, mode="plan")
        self.assertEqual(self.commands.observe({"event": "delta", "text": "Planned."})["mode"], "plan")
        self.events.clear()
        done = self.commands.observe({"event": "done", "turn_id": "t-4"})
        self.assertEqual(done["mode"], "plan")
        self.assertIsNone(self.agent_tools.card_scope)
        entry = self.board.thread(card_id)[-1]
        self.assertEqual((entry.author, entry.attrs.get("mode"), entry.text), ("agent", "plan", "Planned."))
        self.assertEqual(self.of("board_thread_appended")[0]["mode"], "plan")

    def test_what_is_said_before_and_after_a_tool_call_stays_two_paragraphs(self):
        card_id = self.make_card()
        self.send(type="board_ask", card=card_id, mode="plan")
        self.commands.observe({"event": "delta", "text": "Writing the plan."})
        self.commands.observe({"event": "tool_started", "name": "board_update_card"})
        self.commands.observe({"event": "delta", "text": "The plan is on the card."})
        self.commands.observe({"event": "done", "turn_id": "t-5"})
        self.assertEqual(self.board.thread(card_id)[-1].text,
                         "Writing the plan.\n\nThe plan is on the card.")

    def test_a_failed_or_stopped_turn_ends_the_scope_too(self):
        card_id = self.make_card()
        for outcome in ("error", "cancelled"):
            self.send(type="board_ask", card=card_id, mode="plan")
            self.commands.observe({"event": outcome})
            self.assertIsNone(self.agent_tools.card_scope, outcome)

    def test_an_unknown_mode_and_an_empty_discuss_are_refused_before_anything_is_written(self):
        card_id = self.make_card()
        before = len(self.board.thread(card_id))
        for request in ({"mode": "execute", "text": "go"}, {"mode": "discuss", "text": " "},
                        {"text": None}):
            with self.assertRaises(ValueError):
                self.commands.dispatch({"type": "board_ask", "card": card_id, **request})
        self.assertEqual(len(self.board.thread(card_id)), before)
        self.assertIsNone(self.agent_tools.card_scope)

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
        # The next question seeds a fresh conversation instead of continuing the old card's.
        resets = self.turns.resets
        new_card = self.make_card(title="Other card", text="on the other board")
        self.send(type="board_ask", card=new_card, text="and this?")
        self.assertEqual(self.turns.resets, resets + 1)
        self.assertIn(f"[Switchboard card #{new_card}", self.turns.submitted[-1]["prompt"])
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

    def test_a_board_folder_is_switchboard_first_then_issues_nearest_ancestor_winning(self):
        both = self.project("both", "issues")
        (both / "switchboard").mkdir()
        (both / "switchboard" / B.BOARD_CONFIG).write_text(CONFIG, encoding="utf-8")
        self.assertEqual(T.find_board_root(both), both / "switchboard")
        outer = self.project("outer", "switchboard")
        inner = outer / "inner"
        (inner / "issues").mkdir(parents=True)
        (inner / "issues" / B.BOARD_CONFIG).write_text(CONFIG, encoding="utf-8")
        deep = inner / "src"
        deep.mkdir()
        self.assertEqual(T.find_board_root(deep), inner / "issues")


class InitTests(AttachTest):
    """Nothing is created until the user says yes (protocol 19.12)."""

    def uninitialized(self, name: str = "fresh"):
        project = self.project(name)
        request = {"board": {"project": str(project), "state": "uninitialized"}}
        block = self.commands.configure(str(project), request)
        return project, request, block

    def test_an_uninitialized_project_attaches_but_nothing_is_on_disk(self):
        project, request, block = self.uninitialized()
        self.assertEqual(block["root"], str(project / "switchboard"))
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
        self.assertEqual(asked[0]["dir"], str(project / "switchboard"))
        self.assertEqual(asked[0]["root"], asked[0]["dir"])
        self.assertEqual(asked[0]["request_id"], "w1")
        self.assertIn("the first card", asked[0]["title"])
        self.assertEqual(self.tree(project), [])                # still nothing, waiting on the user

        self.events.clear()
        self.commands.dispatch({"type": "board_init_answer", "id": asked[0]["id"], "accept": True})
        created = self.of("board_created")
        self.assertEqual(created[0]["root"], str(project / "switchboard"))
        self.assertEqual(created[0]["workspace"], str(project))
        self.assertEqual(created[0]["project"], str(project))
        self.assertEqual(created[0]["files"], ["switchboard/board.yaml", "switchboard/.gitignore",
                                               "switchboard/threads/.gitkeep", ".gitattributes"])
        # The card the user typed is not lost: the parked write is replayed.
        written = self.of("board_written")
        self.assertEqual(written[0]["id"], "w1")
        card = B.Board(project / "switchboard", project).card_by_id(written[0]["card_id"])
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
        self.assertEqual(len(B.Board(project / "switchboard", project).cards()), 1)

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
        self.assertEqual(created["root"], str(project / "switchboard"))
        state = [e for e in events if e["event"] == "board_state"][0]
        self.assertEqual((state["id"], state["applies"]), ("i1", "now"))
        self.assertTrue(state["board"]["exists"])
        self.assertEqual(state["board"]["state"], "ready")
        self.assertEqual(sorted(p.name for p in (project / "switchboard").iterdir()),
                         [".gitignore", "board.yaml", "threads"])
        # It is safe to send twice: a board that exists is not scaffolded again.
        events = self.send(type="board_init", id="i2", project=str(project))
        self.assertEqual([e["event"] for e in events], ["board_state"])

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
        self.assertFalse((elsewhere / "switchboard" / B.BOARD_CONFIG).exists())


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


if __name__ == "__main__":       # pragma: no cover
    unittest.main()
