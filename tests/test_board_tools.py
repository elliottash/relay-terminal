# SPDX-License-Identifier: GPL-3.0-or-later
"""Switchboard agent tools: the six `board_*` tools, their refusals and their guardrails.

Every test works in a temporary board; nothing here reads the repository's own `issues/` tree,
calls a model or touches the network or the keyring.
"""
import subprocess
import tempfile
import unittest
from pathlib import Path

from relay_core import board as B
from relay_core import board_tools as T

CONFIG = """\
version: 1
tabs: [{id: features, folder: features}, {id: bugs, folder: changes},
  {id: planning, folder: planning},
  {id: done, filter: "status:done,dropped"}]
columns: [inbox, discussing, ready, in-progress, waiting, needs-qa, done]
agent: {autonomy: auto, max_creates_per_turn: 5}
"""


class BoardToolsTest(unittest.TestCase):
    """A fresh board plus an agent instance of the tools, in a temporary directory."""

    autonomy = None
    config = CONFIG

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.repo = Path(self.tmp.name)
        self.root = self.repo / "issues"
        self.root.mkdir()
        (self.root / B.BOARD_CONFIG).write_text(self.config, encoding="utf-8")
        self.board = B.Board(self.root, self.repo)
        self.events = []
        self.tools = T.BoardTools(
            self.board, emit=self.events.append, autonomy=self.autonomy,
            context=T.ToolContext(actor="agent", model="anthropic/claude-opus-5", pane="2"),
            state_path=self.repo / ".relay" / "board-rate.json")
        self.tools.begin_turn("t-1")

    def tearDown(self):
        self.tmp.cleanup()

    # helpers
    def create(self, title="Voice transcription mode", request="add voice transcribe mode", **kw):
        args = {"tab": "features", "status": "inbox", "title": title, "request": request}
        args.update(kw)
        result = self.tools.run("board_create_card", args)
        self.assertNotIn("error", result, result)
        return result["id"]

    def thread_text(self, card_id):
        path = self.board.thread_path(card_id)
        return path.read_text(encoding="utf-8") if path.exists() else ""

    def kinds(self, card_id):
        return [e.kind for e in self.board.thread(card_id)]


# --------------------------------------------------------------------------- specs

class SpecTests(unittest.TestCase):
    def test_the_six_designed_tools_are_offered_and_nothing_else(self):
        self.assertEqual(T.TOOL_NAMES, ("board_list", "board_read", "board_create_card",
                                        "board_update_card", "board_move_card", "board_comment"))

    def test_there_is_no_delete_tool(self):
        names = " ".join(T.TOOL_NAMES)
        self.assertNotIn("delete", names)
        self.assertNotIn("remove", names)

    def test_every_spec_is_a_closed_object_with_a_description(self):
        for item in T.TOOL_SPECS:
            function = item["function"]
            self.assertTrue(function["description"].strip())
            self.assertFalse(function["parameters"]["additionalProperties"])
            for name in function["parameters"]["required"]:
                self.assertIn(name, function["parameters"]["properties"], name)

    def test_the_policy_ships_next_to_the_module_and_names_the_rules(self):
        text = T.policy_text()
        self.assertNotIn("<!--", text)
        for phrase in ("verbatim", "board_rate_limited", "needs-qa-llm", "discussing"):
            self.assertIn(phrase, text)

    def test_model_family_tells_providers_apart(self):
        self.assertEqual(T.model_family("anthropic/claude-opus-5"), "anthropic")
        self.assertEqual(T.model_family("Claude Opus 5 (pane 2)"), "claude")
        self.assertEqual(T.model_family(None), "")
        self.assertNotEqual(T.model_family("openai/gpt-5"), T.model_family("anthropic/claude-opus-5"))


# --------------------------------------------------------------------- list and read

class ListAndReadTests(BoardToolsTest):
    def test_list_returns_one_row_per_card(self):
        first = self.create()
        second = self.create(title="Clickable paths", request="clicking a path opens a pane")
        result = self.tools.run("board_list", {})
        self.assertEqual({row["id"] for row in result["cards"]}, {first, second})
        row = next(r for r in result["cards"] if r["id"] == first)
        for key in ("id", "title", "status", "tab", "labels", "assignee", "waiting_on", "thread_entries"):
            self.assertIn(key, row)

    def test_list_filters_by_tab_status_labels_and_query(self):
        voice = self.create(labels=["voice"])
        self.create(title="Clickable paths", request="clicking a path opens a pane", tab="bugs")
        bugs = [r["id"] for r in self.tools.run("board_list", {"tab": "bugs"})["cards"]]
        self.assertEqual(len(bugs), 1)
        self.assertNotIn(voice, bugs)
        self.assertEqual([r["id"] for r in self.tools.run("board_list", {"labels": ["voice"]})["cards"]], [voice])
        self.assertEqual([r["id"] for r in self.tools.run("board_list", {"query": "transcribe"})["cards"]], [voice])
        self.assertEqual(self.tools.run("board_list", {"status": "done"})["cards"], [])

    def test_list_refuses_an_unknown_tab_and_an_out_of_range_limit(self):
        self.assertEqual(self.tools.run("board_list", {"tab": "nope"})["code"], "board_refused")
        self.assertIn("error", self.tools.run("board_list", {"limit": 500}))
        self.assertIn("error", self.tools.run("board_list", {"nonsense": 1}))

    def test_list_truncates_at_the_limit_and_says_so(self):
        for title in ("Voice mode", "Clickable paths", "Tab colours", "Session search"):
            self.create(title=title, request=f"please build {title.lower()} for me")
        result = self.tools.run("board_list", {"limit": 2})
        self.assertEqual(len(result["cards"]), 2)
        self.assertEqual(result["total"], 4)
        self.assertTrue(result["truncated"])

    def test_read_returns_the_hash_the_update_tool_needs(self):
        card_id = self.create()
        result = self.tools.run("board_read", {"id": card_id})
        self.assertEqual(result["hash"], B.file_hash(self.board.card_by_id(card_id).path))
        self.assertIn("## Request", result["body"])
        self.assertEqual(result["thread_total"], 1)

    def test_read_accepts_the_hash_prefixed_form_and_refuses_a_bad_id(self):
        card_id = self.create()
        self.assertEqual(self.tools.run("board_read", {"id": f"#{card_id.lower()}"})["id"], card_id)
        self.assertIn("not a card id", self.tools.run("board_read", {"id": "zz"})["error"])
        self.assertEqual(self.tools.run("board_read", {"id": "AAAA"})["code"], "board_not_found")

    def test_read_caps_the_thread_tail(self):
        card_id = self.create()
        for n in range(4):
            self.tools.run("board_comment", {"id": card_id, "kind": "note", "text": f"note {n}"})
        result = self.tools.run("board_read", {"id": card_id, "thread_entries": 2})
        self.assertEqual(len(result["thread"]), 2)
        self.assertEqual(result["thread_total"], 5)
        self.assertEqual([e["text"] for e in result["thread"]], ["note 2", "note 3"])


# ------------------------------------------------------------------------- create

class CreateTests(BoardToolsTest):
    def test_a_card_lands_in_the_tab_folder_with_the_request_verbatim(self):
        card_id = self.create(request="add voice transcribe mode (microphone icon). like warp")
        card = self.board.card_by_id(card_id)
        self.assertEqual(card.path.parent, self.root / "features")
        self.assertIn("add voice transcribe mode (microphone icon). like warp", card.body)
        self.assertEqual(card.status, "inbox")
        self.assertEqual(card.title, "Voice transcription mode")
        self.assertTrue(B.valid_id(card.id))
        self.assertTrue(B.valid_rank(card.rank))

    def test_creation_appends_a_thread_event_naming_the_actor_model_pane_and_turn(self):
        card_id = self.create()
        text = self.thread_text(card_id)
        self.assertIn("author=agent", text)
        self.assertIn("kind=event", text)
        self.assertIn("model=anthropic/claude-opus-5", text)
        self.assertIn("pane=2", text)
        self.assertIn("turn=t-1", text)

    def test_a_status_puts_the_file_in_the_matching_state_folder(self):
        card_id = self.create(status="deferred")
        self.assertEqual(self.board.card_by_id(card_id).path.parent, self.root / "features" / "deferred")

    def test_a_fuzzy_duplicate_is_refused_until_the_agent_says_it_checked(self):
        first = self.create()
        again = self.tools.run("board_create_card", {
            "tab": "features", "status": "inbox", "title": "Voice transcription mode",
            "request": "add voice transcribe mode"})
        self.assertEqual(again["code"], "board_possible_duplicate")
        self.assertEqual([d["id"] for d in again["possible_duplicates"]], [first])
        forced = self.tools.run("board_create_card", {
            "tab": "features", "status": "inbox", "title": "Voice transcription mode",
            "request": "add voice transcribe mode", "not_duplicate_of": [first]})
        self.assertNotIn("error", forced)
        self.assertNotEqual(forced["id"], first)

    def test_a_closed_card_does_not_block_a_new_one(self):
        card_id = self.create()
        card = self.board.card_by_id(card_id)
        card.set("status", "dropped")
        self.board.save(card)
        self.assertEqual(self.tools.duplicates("Voice transcription mode", "add voice transcribe mode"), [])

    def test_two_cards_with_the_same_title_get_different_file_names(self):
        first = self.create()
        second = self.create(request="a completely different ask about tab colours",
                             title="Voice transcription mode", not_duplicate_of=[first])
        self.assertNotEqual(self.board.card_by_id(first).path, self.board.card_by_id(second).path)

    def test_an_unknown_tab_a_filter_tab_and_a_bad_status_are_refused(self):
        self.assertIn("unknown tab", self.tools.run("board_create_card", {
            "tab": "nope", "status": "inbox", "title": "T", "request": "r"})["error"])
        self.assertIn("filter across categories", self.tools.run("board_create_card", {
            "tab": "done", "status": "inbox", "title": "T", "request": "r"})["error"])
        self.assertIn("unknown work status", self.tools.run("board_create_card", {
            "tab": "features", "status": "shipped", "title": "T", "request": "r"})["error"])

    def test_an_empty_request_or_title_is_refused(self):
        self.assertIn("error", self.tools.run("board_create_card", {
            "tab": "features", "status": "inbox", "title": "  ", "request": "r"}))
        self.assertIn("error", self.tools.run("board_create_card", {
            "tab": "features", "status": "inbox", "title": "T", "request": ""}))

    def test_a_plan_card_and_a_memory_card_go_to_their_own_folders(self):
        plan = self.create(type="plan", status="draft", title="Voice mode plan",
                           request="how we will build voice mode")
        memory = self.create(type="memory", status="active", title="Qt version",
                             request="this repo builds against Qt 6.4")
        self.assertEqual(self.board.card_by_id(plan).path.parent, self.root / B.PLAN_FOLDER)
        self.assertEqual(self.board.card_by_id(memory).path.parent, self.root / B.MEMORY_FOLDER)
        self.assertEqual(self.board.card_by_id(plan).type, "plan")

    def test_a_created_card_passes_the_format_check(self):
        self.create()
        self.assertEqual([str(p) for p in self.board.check()], [])


# ------------------------------------------------------------------------- update

class UpdateTests(BoardToolsTest):
    def setUp(self):
        super().setUp()
        self.card_id = self.create()
        self.hash = self.tools.run("board_read", {"id": self.card_id})["hash"]

    def test_fields_and_sections_are_written_and_logged(self):
        result = self.tools.run("board_update_card", {
            "id": self.card_id, "base_hash": self.hash,
            "fields": {"labels": ["voice", "mvp"], "assignee": "agent"},
            "append_section": {"heading": "Findings", "text": "- whisper.cpp exists"}})
        card = self.board.card_by_id(self.card_id)
        self.assertEqual(card.front["labels"], ["voice", "mvp"])
        self.assertIn("## Findings", card.body)
        self.assertIn("- whisper.cpp exists", card.body)
        self.assertEqual(result["hash"], B.file_hash(card.path))
        self.assertIn("- ✦ agent updated this card", self.thread_text(self.card_id))

    def test_appending_twice_keeps_both_paragraphs_and_the_rest_of_the_body(self):
        for text in ("- first", "- second"):
            current = self.tools.run("board_read", {"id": self.card_id})["hash"]
            self.tools.run("board_update_card", {"id": self.card_id, "base_hash": current,
                                                 "append_section": {"heading": "Findings", "text": text}})
        body = self.board.card_by_id(self.card_id).body
        self.assertIn("- first", body)
        self.assertIn("- second", body)
        self.assertIn("## Request", body)
        self.assertEqual(body.count("## Findings"), 1)

    def test_a_stale_hash_is_a_conflict_and_nothing_is_overwritten(self):
        before = self.board.card_by_id(self.card_id).path.read_bytes()
        result = self.tools.run("board_update_card", {
            "id": self.card_id, "base_hash": "0" * 64, "fields": {"assignee": "agent"}})
        self.assertEqual(result["code"], "board_conflict")
        self.assertEqual(result["current_hash"], self.hash)
        self.assertEqual(self.board.card_by_id(self.card_id).path.read_bytes(), before)

    def test_a_hash_that_went_stale_between_read_and_write_is_a_conflict(self):
        card = self.board.card_by_id(self.card_id)
        card.set("milestone", "desktop-alpha")
        self.board.save(card)
        result = self.tools.run("board_update_card", {
            "id": self.card_id, "base_hash": self.hash, "fields": {"assignee": "agent"}})
        self.assertEqual(result["code"], "board_conflict")

    def test_base_hash_is_required_and_must_look_like_a_hash(self):
        self.assertIn("base_hash", self.tools.run("board_update_card", {"id": self.card_id})["error"])
        self.assertIn("base_hash", self.tools.run("board_update_card", {
            "id": self.card_id, "base_hash": "short", "fields": {"assignee": "a"}})["error"])

    def test_the_record_fields_are_never_writable(self):
        for field, value in (("id", "AAAA"), ("type", "plan"), ("created", "2020-01-01"),
                             ("source", "made up"), ("rank", "zz"), ("status", "done"),
                             ("private", True)):
            result = self.tools.run("board_update_card", {
                "id": self.card_id, "base_hash": self.hash, "fields": {field: value}})
            self.assertEqual(result["code"], "board_refused", field)
            self.assertEqual(result["field"], field)
        self.assertEqual(B.file_hash(self.board.card_by_id(self.card_id).path), self.hash)

    def test_a_field_that_is_not_part_of_this_card_type_is_refused(self):
        result = self.tools.run("board_update_card", {
            "id": self.card_id, "base_hash": self.hash, "fields": {"topic": "conventions"}})
        self.assertEqual(result["code"], "board_refused")
        self.assertIn("not a field of a work card", result["error"])

    def test_a_field_set_to_null_is_removed(self):
        self.tools.run("board_update_card", {"id": self.card_id, "base_hash": self.hash,
                                             "fields": {"assignee": "agent"}})
        current = self.tools.run("board_read", {"id": self.card_id})["hash"]
        self.tools.run("board_update_card", {"id": self.card_id, "base_hash": current,
                                             "fields": {"assignee": None}})
        self.assertNotIn("assignee", self.board.card_by_id(self.card_id).front)

    def test_an_update_that_changes_nothing_is_refused(self):
        self.assertIn("nothing to change", self.tools.run(
            "board_update_card", {"id": self.card_id, "base_hash": self.hash})["error"])

    # ---- owner text (decision 12.3) -------------------------------------------
    def test_rewriting_the_users_own_request_is_allowed_and_logs_both_texts(self):
        result = self.tools.run("board_update_card", {
            "id": self.card_id, "base_hash": self.hash,
            "replace_section": {"heading": "Request", "text": "add a voice transcription mode"}})
        self.assertEqual(result["logged_rewrites"], ["## Request"])
        self.assertIn("add a voice transcription mode", self.board.card_by_id(self.card_id).body)
        text = self.thread_text(self.card_id)
        self.assertIn("kind=rewrite", text)
        self.assertIn("add voice transcribe mode", text)          # the text as it was
        self.assertIn("add a voice transcription mode", text)     # the text as it is now
        self.assertIn("rewrite", self.kinds(self.card_id))

    def test_rewriting_the_title_logs_both_titles(self):
        self.tools.run("board_update_card", {"id": self.card_id, "base_hash": self.hash,
                                             "title": "Voice mode (hold Right Alt)"})
        self.assertEqual(self.board.card_by_id(self.card_id).title, "Voice mode (hold Right Alt)")
        text = self.thread_text(self.card_id)
        self.assertIn("rewrote title", text)
        self.assertIn("Voice transcription mode", text)

    def test_writing_an_agent_section_is_not_logged_as_a_rewrite(self):
        self.tools.run("board_update_card", {
            "id": self.card_id, "base_hash": self.hash,
            "append_section": {"heading": "Findings", "text": "- one"}})
        current = self.tools.run("board_read", {"id": self.card_id})["hash"]
        result = self.tools.run("board_update_card", {
            "id": self.card_id, "base_hash": current,
            "replace_section": {"heading": "Findings", "text": "- two"}})
        self.assertEqual(result["logged_rewrites"], [])
        self.assertNotIn("rewrite", self.kinds(self.card_id))

    def test_the_designed_replace_agent_section_argument_still_works(self):
        result = self.tools.run("board_update_card", {
            "id": self.card_id, "base_hash": self.hash,
            "replace_agent_section": {"heading": "Findings", "text": "- from the designed name"}})
        self.assertNotIn("error", result)
        self.assertIn("- from the designed name", self.board.card_by_id(self.card_id).body)

    # ---- tasks ---------------------------------------------------------------
    def test_tasks_are_written_with_markers_and_reread(self):
        self.tools.run("board_update_card", {
            "id": self.card_id, "base_hash": self.hash,
            "tasks": [{"text": "Record audio", "status": "in-progress"},
                      {"text": "Call the provider", "status": "done"}]})
        items = self.board.card_by_id(self.card_id).tasks()
        self.assertEqual([i.text for i in items], ["Record audio", "Call the provider"])
        self.assertEqual([i.status for i in items], ["in-progress", "done"])
        self.assertTrue(all(B.valid_item_id(i.item_id) for i in items))

    def test_an_existing_task_keeps_its_id_when_it_is_named(self):
        self.tools.run("board_update_card", {"id": self.card_id, "base_hash": self.hash,
                                             "tasks": [{"text": "Record audio"}]})
        first = self.board.card_by_id(self.card_id).tasks()[0]
        current = self.tools.run("board_read", {"id": self.card_id})["hash"]
        self.tools.run("board_update_card", {
            "id": self.card_id, "base_hash": current,
            "tasks": [{"text": "Record audio", "status": "done", "item_id": first.item_id}]})
        again = self.board.card_by_id(self.card_id).tasks()[0]
        self.assertEqual(again.item_id, first.item_id)
        self.assertTrue(again.done)

    def test_a_bad_task_status_is_refused(self):
        self.assertIn("error", self.tools.run("board_update_card", {
            "id": self.card_id, "base_hash": self.hash,
            "tasks": [{"text": "x", "status": "nearly"}]}))


# --------------------------------------------------------------------------- move

class MoveTests(BoardToolsTest):
    def setUp(self):
        super().setUp()
        self.card_id = self.create()

    def test_a_move_changes_the_status_and_the_folder_and_logs_the_reason(self):
        result = self.tools.run("board_move_card", {
            "id": self.card_id, "status": "in-progress", "reason": "starting now"})
        card = self.board.card_by_id(self.card_id)
        self.assertEqual(card.status, "in-progress")
        self.assertEqual(card.path.parent, self.root / "features")
        self.assertTrue(result["moved"] is False or result["moved"] is True)
        self.assertIn("starting now", self.thread_text(self.card_id))
        self.assertIn("Inbox → In progress", self.thread_text(self.card_id))

    def test_a_move_to_a_state_folder_moves_the_file(self):
        self.tools.run("board_move_card", {"id": self.card_id, "status": "deferred",
                                           "reason": "not now"})
        self.assertEqual(self.board.card_by_id(self.card_id).path.parent,
                         self.root / "features" / "deferred")
        self.assertEqual([str(p) for p in self.board.check()], [])

    def test_a_move_to_another_tab_moves_the_category(self):
        self.tools.run("board_move_card", {"id": self.card_id, "tab": "bugs", "reason": "it is a bug"})
        self.assertEqual(self.board.card_by_id(self.card_id).path.parent, self.root / "changes")

    def test_a_reason_is_always_required(self):
        self.assertIn("reason", self.tools.run(
            "board_move_card", {"id": self.card_id, "status": "ready"})["error"])

    def test_a_qa_lane_needs_evidence_and_an_implementer(self):
        without = self.tools.run("board_move_card", {"id": self.card_id, "status": "needs-qa-llm",
                                                     "reason": "landed"})
        self.assertEqual(without["requires"], "evidence")
        no_model = self.tools.run("board_move_card", {
            "id": self.card_id, "status": "needs-qa-llm", "reason": "landed",
            "evidence": "docs/qa_evidence/2026-09-17-voice/"})
        self.assertEqual(no_model["requires"], "implemented_by")
        self.assertEqual(self.board.card_by_id(self.card_id).status, "inbox")
        ok = self.tools.run("board_move_card", {
            "id": self.card_id, "status": "needs-qa-llm", "reason": "landed",
            "evidence": "docs/qa_evidence/2026-09-17-voice/", "implemented_by": "anthropic/claude-opus-5"})
        self.assertNotIn("error", ok)
        card = self.board.card_by_id(self.card_id)
        self.assertEqual(card.path.parent, self.root / "features" / "needs_qa_llm")
        self.assertIn("docs/qa_evidence/2026-09-17-voice/", card.front["links"]["evidence"])
        self.assertIn("evidence docs/qa_evidence", self.thread_text(self.card_id))

    def _into_qa(self, implementer):
        self.tools.run("board_move_card", {
            "id": self.card_id, "status": "needs-qa-llm", "reason": "landed",
            "evidence": "docs/qa_evidence/x/", "implemented_by": implementer})

    def test_closing_a_qa_card_needs_a_verdict_section(self):
        self._into_qa("openai/gpt-5")
        result = self.tools.run("board_move_card", {"id": self.card_id, "status": "done",
                                                    "reason": "passed"})
        self.assertEqual(result["requires"], "verdict")
        self.assertEqual(self.board.card_by_id(self.card_id).status, "needs-qa-llm")

    def test_the_same_model_family_may_not_close_what_it_implemented(self):
        self._into_qa("anthropic/claude-opus-5")
        current = self.tools.run("board_read", {"id": self.card_id})["hash"]
        self.tools.run("board_update_card", {"id": self.card_id, "base_hash": current,
                                             "append_section": {"heading": "Verdict", "text": "pass"}})
        result = self.tools.run("board_move_card", {"id": self.card_id, "status": "done",
                                                    "reason": "passed"})
        self.assertEqual(result["requires"], "independent_model")
        self.assertEqual(self.board.card_by_id(self.card_id).status, "needs-qa-llm")

    def test_a_different_model_family_closes_it_with_a_verdict(self):
        self._into_qa("openai/gpt-5")
        current = self.tools.run("board_read", {"id": self.card_id})["hash"]
        self.tools.run("board_update_card", {"id": self.card_id, "base_hash": current,
                                             "append_section": {"heading": "Verdict", "text": "pass"}})
        result = self.tools.run("board_move_card", {"id": self.card_id, "status": "done",
                                                    "reason": "verified"})
        self.assertNotIn("error", result)
        card = self.board.card_by_id(self.card_id)
        self.assertEqual(card.status, "done")
        self.assertEqual(card.path.parent, self.root / "features" / "done")

    def test_before_and_after_reorder_inside_a_column(self):
        second = self.create(title="Clickable paths", request="clicking a path opens a pane")
        third = self.create(title="Tab colours", request="let me colour a tab")
        self.tools.run("board_move_card", {"id": third, "after": self.card_id, "before": second,
                                           "reason": "reordered"})
        ranks = {c.id: c.rank for c in self.board.cards()}
        self.assertLess(ranks[self.card_id], ranks[third])
        self.assertLess(ranks[third], ranks[second])

    def test_reordering_against_a_card_in_another_column_is_refused(self):
        other = self.create(title="Tab colours", request="let me colour a tab", status="ready")
        result = self.tools.run("board_move_card", {"id": self.card_id, "before": other,
                                                    "reason": "nope"})
        self.assertIn("not a card in the", result["error"])


# ------------------------------------------------------------------------ comment

class CommentTests(BoardToolsTest):
    def setUp(self):
        super().setUp()
        self.card_id = self.create()

    def test_every_kind_appends_one_entry(self):
        for kind in T.COMMENT_KINDS:
            text = 'the owner said "do it"' if kind == "decision" else f"a {kind}"
            result = self.tools.run("board_comment", {"id": self.card_id, "kind": kind, "text": text})
            self.assertNotIn("error", result, kind)
        kinds = self.kinds(self.card_id)
        for kind in T.COMMENT_KINDS:
            self.assertIn(kind, kinds)

    def test_an_unknown_kind_is_refused(self):
        self.assertIn("kind must be", self.tools.run(
            "board_comment", {"id": self.card_id, "kind": "shout", "text": "x"})["error"])

    def test_a_decision_has_to_quote_the_user(self):
        result = self.tools.run("board_comment", {"id": self.card_id, "kind": "decision",
                                                  "text": "the owner wants the cloud model"})
        self.assertEqual(result["requires"], "verbatim_quote")
        self.assertEqual(len(self.board.thread(self.card_id)), 1)
        ok = self.tools.run("board_comment", {
            "id": self.card_id, "kind": "decision",
            "text": '2026-09-17, owner: "cloud is fine" → default to the cloud model'})
        self.assertNotIn("error", ok)

    def test_the_thread_is_only_ever_appended_to(self):
        first = self.thread_text(self.card_id)
        self.tools.run("board_comment", {"id": self.card_id, "kind": "note", "text": "hello"})
        self.assertTrue(self.thread_text(self.card_id).startswith(first))

    def test_entries_stay_in_id_order_even_within_one_second(self):
        for n in range(8):
            self.tools.run("board_comment", {"id": self.card_id, "kind": "note", "text": f"n{n}"})
        ids = [e.entry_id for e in self.board.thread(self.card_id)]
        self.assertEqual(ids, sorted(ids))
        self.assertEqual(len(set(ids)), len(ids))
        self.assertEqual([str(p) for p in self.board.check()], [])


# ------------------------------------------------------------------------- limits

class LimitTests(BoardToolsTest):
    def test_creates_are_capped_per_turn_and_the_cap_comes_from_board_yaml(self):
        self.tools.limits["max_creates_per_turn"] = 2
        self.create(title="One", request="the first distinct ask")
        self.create(title="Two", request="a second and quite different ask")
        result = self.tools.run("board_create_card", {
            "tab": "features", "status": "inbox", "title": "Three",
            "request": "a third ask, unrelated to the others"})
        self.assertEqual(result["code"], "board_rate_limited")
        self.assertEqual(result["scope"], "turn")
        self.assertEqual(len(self.board.cards()), 2)

    def test_the_turn_budget_resets_on_the_next_turn(self):
        self.tools.limits["max_creates_per_turn"] = 1
        self.create(title="One", request="the first distinct ask")
        self.assertEqual(self.tools.run("board_create_card", {
            "tab": "features", "status": "inbox", "title": "Two",
            "request": "a second ask"})["code"], "board_rate_limited")
        self.tools.begin_turn("t-2")
        self.create(title="Two", request="a second and quite different ask")
        self.assertEqual(len(self.board.cards()), 2)

    def test_other_writes_are_capped_per_turn(self):
        card_id = self.create()
        self.tools.limits["max_writes_per_turn"] = 2      # the create already used one
        self.tools.run("board_comment", {"id": card_id, "kind": "note", "text": "one"})
        result = self.tools.run("board_comment", {"id": card_id, "kind": "note", "text": "two"})
        self.assertEqual(result["code"], "board_rate_limited")
        self.assertEqual(len(self.board.thread(card_id)), 2)

    def test_creates_are_capped_per_hour_across_panes_of_one_workspace(self):
        self.tools.limits["max_creates_per_hour"] = 2
        self.tools.limits["max_creates_per_turn"] = 50
        self.create(title="One", request="the first distinct ask")
        self.create(title="Two", request="a second and quite different ask")
        other_pane = T.BoardTools(self.board, autonomy="auto",
                                  state_path=self.repo / ".relay" / "board-rate.json")
        other_pane.limits["max_creates_per_hour"] = 2
        other_pane.begin_turn("t-9")
        result = other_pane.run("board_create_card", {
            "tab": "features", "status": "inbox", "title": "Three",
            "request": "a third ask from another pane entirely"})
        self.assertEqual(result["code"], "board_rate_limited")
        self.assertEqual(result["scope"], "hour")

    def test_an_hour_later_the_hourly_budget_is_free_again(self):
        now = [1000.0]
        tools = T.BoardTools(self.board, autonomy="auto", clock=lambda: now[0],
                             state_path=self.repo / ".relay" / "board-rate.json")
        tools.limits["max_creates_per_hour"] = 1
        tools.begin_turn("t-1")
        tools.run("board_create_card", {"tab": "features", "status": "inbox", "title": "One",
                                        "request": "the first distinct ask"})
        blocked = tools.run("board_create_card", {"tab": "features", "status": "inbox", "title": "Two",
                                                  "request": "a second and quite different ask"})
        self.assertEqual(blocked["code"], "board_rate_limited")
        now[0] += 3601
        tools.begin_turn("t-2")
        self.assertNotIn("error", tools.run("board_create_card", {
            "tab": "features", "status": "inbox", "title": "Two",
            "request": "a second and quite different ask"}))

    def test_a_limit_in_board_yaml_is_honoured(self):
        (self.root / B.BOARD_CONFIG).write_text(
            CONFIG.replace("max_creates_per_turn: 5", "max_creates_per_turn: 1"), encoding="utf-8")
        tools = T.BoardTools(self.board, state_path=self.repo / ".relay" / "r.json")
        self.assertEqual(tools.limits["max_creates_per_turn"], 1)


class AutonomyTests(BoardToolsTest):
    def test_autonomy_off_refuses_every_write_but_still_reads(self):
        tools = T.BoardTools(self.board, autonomy="off", state_path=self.repo / ".relay" / "r.json")
        result = tools.run("board_create_card", {"tab": "features", "status": "inbox",
                                                 "title": "T", "request": "r"})
        self.assertEqual(result["code"], "board_autonomy_off")
        self.assertNotIn("error", tools.run("board_list", {}))
        self.assertEqual(self.board.cards(), [])

    def test_for_workspace_returns_nothing_without_a_board_and_nothing_when_off(self):
        self.assertIsNone(T.BoardTools.for_workspace(self.repo / "nowhere"))
        self.assertIsNotNone(T.BoardTools.for_workspace(self.repo))
        (self.root / B.BOARD_CONFIG).write_text(
            CONFIG.replace("autonomy: auto", "autonomy: off"), encoding="utf-8")
        self.assertIsNone(T.BoardTools.for_workspace(self.repo))

    def test_the_owner_instance_skips_the_limits_and_the_duplicate_check(self):
        owner = T.BoardTools(self.board, enforce_limits=False, duplicate_check=False,
                             context=T.ToolContext(actor="owner"),
                             state_path=self.repo / ".relay" / "r.json")
        owner.limits["max_creates_per_turn"] = 0
        first = owner.run("board_create_card", {"tab": "features", "status": "inbox",
                                                "title": "Voice mode", "request": "add voice mode"})
        again = owner.run("board_create_card", {"tab": "features", "status": "inbox",
                                                "title": "Voice mode", "request": "add voice mode"})
        self.assertNotIn("error", first)
        self.assertNotIn("error", again)
        self.assertIn("author=owner", self.thread_text(first["id"]))


class PromptSectionTests(BoardToolsTest):
    def test_the_policy_is_offered_with_the_tab_list_and_the_autonomy(self):
        text = T.prompt_section(self.tools)
        self.assertIn("issues/board.yaml", text)
        self.assertIn("features", text)
        self.assertIn("Autonomy: auto", text)
        self.assertIn("board_rate_limited", text)

    def test_suggest_mode_says_the_writes_are_proposals(self):
        tools = T.BoardTools(self.board, autonomy="suggest", state_path=self.repo / ".relay" / "r.json")
        self.assertIn("proposals", T.prompt_section(tools))

    def test_no_board_means_no_prompt_text(self):
        self.assertEqual(T.prompt_section(None), "")
        self.assertEqual(T.prompt_section(T.BoardTools(
            self.board, autonomy="off", state_path=self.repo / ".relay" / "r.json")), "")


# --------------------------------------------------------------------------- undo

class UndoTests(BoardToolsTest):
    def test_undoing_a_creation_removes_an_uncommitted_card_and_its_thread(self):
        result = self.tools.run("board_create_card", {
            "tab": "features", "status": "inbox", "title": "Voice mode", "request": "add voice mode"})
        path = self.board.card_by_id(result["id"]).path
        undone = self.tools.undo(result["write_id"])
        self.assertTrue(undone["removed"])
        self.assertFalse(path.exists())
        self.assertFalse(self.board.thread_path(result["id"]).exists())
        self.assertEqual(self.board.cards(), [])

    def test_undoing_an_update_puts_the_old_bytes_back_and_records_the_undo(self):
        card_id = self.create()
        before = self.board.card_by_id(card_id).path.read_bytes()
        current = self.tools.run("board_read", {"id": card_id})["hash"]
        result = self.tools.run("board_update_card", {"id": card_id, "base_hash": current,
                                                      "fields": {"assignee": "agent"}})
        self.tools.undo(result["write_id"])
        self.assertEqual(self.board.card_by_id(card_id).path.read_bytes(), before)
        text = self.thread_text(card_id)
        self.assertIn("undid", text)
        self.assertNotIn("assignee", text.split("undid")[0].split("created this card")[-1])

    def test_undoing_a_move_puts_the_file_back(self):
        card_id = self.create()
        result = self.tools.run("board_move_card", {"id": card_id, "status": "deferred",
                                                    "reason": "not now"})
        self.tools.undo(result["write_id"])
        card = self.board.card_by_id(card_id)
        self.assertEqual(card.status, "inbox")
        self.assertEqual(card.path.parent, self.root / "features")

    def test_undoing_a_comment_drops_the_entry(self):
        card_id = self.create()
        result = self.tools.run("board_comment", {"id": card_id, "kind": "note", "text": "oops"})
        self.tools.undo(result["write_id"])
        texts = [e.text for e in self.board.thread(card_id)]
        self.assertNotIn("oops", texts)

    def test_a_write_is_undone_only_once_and_an_unknown_id_is_refused(self):
        card_id = self.create()
        result = self.tools.run("board_comment", {"id": card_id, "kind": "note", "text": "x"})
        self.tools.undo(result["write_id"])
        with self.assertRaises(T.BoardToolError):
            self.tools.undo(result["write_id"])
        with self.assertRaises(T.BoardToolError):
            self.tools.undo("w-nope")

    def test_a_committed_card_is_never_removed_by_undo(self):
        try:
            subprocess.run(["git", "init", "-q"], cwd=self.repo, check=True, timeout=30)
            subprocess.run(["git", "config", "user.email", "t@example.invalid"], cwd=self.repo, check=True, timeout=30)
            subprocess.run(["git", "config", "user.name", "T"], cwd=self.repo, check=True, timeout=30)
        except (OSError, subprocess.SubprocessError):       # pragma: no cover - git missing
            self.skipTest("git is not available")
        result = self.tools.run("board_create_card", {
            "tab": "features", "status": "inbox", "title": "Voice mode", "request": "add voice mode"})
        subprocess.run(["git", "add", "-A"], cwd=self.repo, check=True, timeout=30)
        subprocess.run(["git", "commit", "-qm", "card"], cwd=self.repo, check=True, timeout=30)
        with self.assertRaises(T.BoardToolError):
            self.tools.undo(result["write_id"])
        self.assertTrue(self.board.card_by_id(result["id"]) is not None)


# ------------------------------------------------------------------------ events

class ActivityEventTests(BoardToolsTest):
    def test_every_write_emits_an_activity_line_and_a_change(self):
        card_id = self.create()
        activity = [e for e in self.events if e["event"] == "board_activity"]
        self.assertEqual(len(activity), 1)
        self.assertEqual(activity[0]["action"], "create")
        self.assertEqual(activity[0]["id"], card_id)
        self.assertEqual(activity[0]["actor"], "agent")
        self.assertEqual(activity[0]["model"], "anthropic/claude-opus-5")
        self.assertEqual(activity[0]["turn_id"], "t-1")
        self.assertEqual(activity[0]["undo_seconds"], T.UNDO_SECONDS)
        self.assertTrue(activity[0]["path"].startswith("issues/"))
        self.assertTrue(any(e["event"] == "board_changed" for e in self.events))

    def test_a_refused_write_emits_nothing(self):
        self.tools.run("board_comment", {"id": "AAAA", "kind": "note", "text": "x"})
        self.assertEqual(self.events, [])

    def test_a_preview_is_produced_for_every_tool(self):
        for name in T.TOOL_NAMES:
            self.assertTrue(self.tools.preview(name, {"id": "K7Q2"}).startswith("SWITCHBOARD "))


class SectionWriterTests(unittest.TestCase):
    """`_write_section` has to leave every byte it does not own alone."""

    BODY = "# Title\n\n## Request\nthe ask\n\n## Decisions\n- one\n"

    def test_appending_to_a_middle_section_keeps_the_following_one(self):
        out = T._write_section(self.BODY, "Request", "more", replace=False)
        self.assertIn("the ask\nmore\n", out)
        self.assertIn("## Decisions\n- one\n", out)

    def test_replacing_keeps_the_heading_and_the_following_section(self):
        out = T._write_section(self.BODY, "Request", "new ask", replace=True)
        self.assertNotIn("the ask", out)
        self.assertIn("## Request\nnew ask\n", out)
        self.assertIn("## Decisions\n- one\n", out)

    def test_an_unknown_heading_is_appended_at_the_end(self):
        out = T._write_section(self.BODY, "Findings", "- found", replace=False)
        self.assertTrue(out.rstrip().endswith("## Findings\n- found"))
        self.assertIn("## Decisions", out)

    def test_headings_are_matched_without_case(self):
        self.assertEqual(T._section_text(self.BODY, "request").strip(), "the ask")


if __name__ == "__main__":       # pragma: no cover
    unittest.main()
