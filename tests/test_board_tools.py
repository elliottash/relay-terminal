# SPDX-License-Identifier: AGPL-3.0-or-later
"""Switchboard agent tools: the six `board_*` tools, their refusals and their guardrails.

Every test works in a temporary board; nothing here reads the repository's own `issues/` tree,
calls a model or touches the network or the keyring.
"""
import subprocess
import tempfile
import unittest
import unittest.mock
from pathlib import Path

from relay_core import board as B
from relay_core import board_protocol as P
from relay_core import board_tools as T
from relay_core import qa_verifiers as QA

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
    #: This pane's session token (protocol 19.19): what `board_claim` writes onto a card as
    #: `session` and onto its progress entry as `pane_token`. A subclass sets it to None to be
    #: a worker that has none — the Switchboard's own, or a GUI too old to send one.
    pane_token = "3f2504e0-4f89-11d3-9a0c-0305e82c3301"

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.repo = Path(self.tmp.name).resolve()
        self.root = self.repo / "issues"
        self.root.mkdir()
        (self.root / B.BOARD_CONFIG).write_text(self.config, encoding="utf-8")
        self.board = B.Board(self.root, self.repo)
        self.events = []
        self.tools = T.BoardTools(
            self.board, emit=self.events.append, autonomy=self.autonomy,
            context=T.ToolContext(actor="agent", model="anthropic/claude-opus-5", pane="2"),
            state_path=self.repo / ".relay" / "board-rate.json", pane_token=self.pane_token)
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
    def test_the_designed_tools_are_offered_and_nothing_else(self):
        self.assertEqual(T.TOOL_NAMES, ("board_list", "board_read", "board_create_card",
                                        "board_update_card", "board_move_card",
                                        "board_import_items", "board_comment", "board_claim"))

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

    def test_the_update_tool_says_the_priority_flag_is_settable(self):
        # #DPJB. The flag was already a writable field (#VKFV) — `BoardTools._update` clamps it
        # and 0 removes the key — but nothing in the model's own view of the tool said so, so a
        # card could only be flagged by an agent that guessed. The description names it now.
        spec = next(item["function"] for item in T.TOOL_SPECS
                    if item["function"]["name"] == "board_update_card")
        self.assertIn("priority", spec["description"])
        self.assertIn("0 clearing the flag", spec["description"])

    def test_the_policy_ships_next_to_the_module_and_names_the_rules(self):
        text = T.policy_text()
        self.assertNotIn("<!--", text)
        # `needs-qa-llm` left rule 5 with the owner's decision of 2026-09-20 (any pane may close
        # a card once the verdict is on it): the policy now names the lane the implementer lands
        # in and calls the rest "a QA lane".
        # `board_claim` and the `deliver` skill are rules 1 and 5 since v2 (#R9G7): the policy is
        # the only place a pane agent is told that work goes through a card it holds.
        for phrase in ("verbatim", "board_rate_limited", "needs-verification", "discussing",
                       "board_claim", "deliver"):
            self.assertIn(phrase, text)

    def test_model_family_tells_providers_apart(self):
        # Since #T71W the signature form and the free-text form of one model are one family: this
        # asserted "claude" for the free text and "anthropic" for the signature, which is exactly
        # the hole the independence rule fell through — each could close what the other wrote.
        self.assertEqual(T.model_family("anthropic/claude-opus-5"), "anthropic")
        self.assertEqual(T.model_family("Claude Opus 5 (pane 2)"),
                         T.model_family("anthropic/claude-opus-5"))
        self.assertEqual(T.model_family(None), "")
        self.assertNotEqual(T.model_family("openai/gpt-5"), T.model_family("anthropic/claude-opus-5"))
        # The aggregator is read through to the model it routes to.
        self.assertEqual(T.model_family("openrouter/deepseek-v4.1-flash"), "deepseek")


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
        # The pane's rows carry the card's whole text for its full-text filter; an agent's tool
        # result stays light — five kilobytes a card is not worth a list.
        self.assertTrue(all("text" not in r for r in result["cards"]))

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
        self.assertIn("## Issue", result["body"])
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
        # Six, not five: the first note moves the inbox card to Discussing (#3XZV) and that
        # move is an event entry of its own.
        self.assertEqual(result["thread_total"], 6)
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
        self.assertIn("## Issue", body)
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
        # "Request" is the old name of the section (owner, 2026-09-18: call it Issue); a caller
        # that still uses it edits the same section, and the card comes out saying `## Issue`.
        result = self.tools.run("board_update_card", {
            "id": self.card_id, "base_hash": self.hash,
            "replace_section": {"heading": "Request", "text": "add a voice transcription mode"}})
        self.assertEqual(result["logged_rewrites"], ["## Issue"])
        self.assertIn("## Issue", self.board.card_by_id(self.card_id).body)
        self.assertNotIn("## Request", self.board.card_by_id(self.card_id).body)
        self.assertIn("add a voice transcription mode", self.board.card_by_id(self.card_id).body)
        text = self.thread_text(self.card_id)
        self.assertIn("kind=rewrite", text)
        self.assertIn("add voice transcribe mode", text)          # the text as it was
        self.assertIn("add a voice transcription mode", text)     # the text as it is now
        self.assertIn("rewrite", self.kinds(self.card_id))

    def test_a_card_still_headed_request_is_read_and_edited_as_the_issue(self):
        # Cards filed before 2026-09-18 say `## Request`. Nothing rewrites them in bulk: the
        # reader takes either spelling, and the first edit settles that card on `## Issue`.
        card = self.board.card_by_id(self.card_id)
        card.body = card.body.replace("## Issue", "## Request")
        card.dirty = True
        self.board.save(card)
        read = self.tools.run("board_read", {"id": self.card_id})
        self.assertEqual(read["issue"], "add voice transcribe mode")
        self.assertEqual(read["issue_heading"], "Request")
        result = self.tools.run("board_update_card", {
            "id": self.card_id, "base_hash": read["hash"],
            "replace_section": {"heading": "Issue", "text": "add voice transcribe mode, like warp"}})
        self.assertNotIn("error", result, result)
        body = self.board.card_by_id(self.card_id).body
        self.assertIn("## Issue\nadd voice transcribe mode, like warp", body)
        self.assertNotIn("## Request", body)
        # The heading changed name, not place: the title above it is still there.
        self.assertTrue(body.startswith("# Voice transcription mode"))
        self.assertEqual(self.tools.run("board_read", {"id": self.card_id})["issue_heading"], "Issue")

    def test_read_offers_the_issue_text_so_the_pane_never_parses_markdown(self):
        read = self.tools.run("board_read", {"id": self.card_id})
        self.assertEqual(read["issue"], "add voice transcribe mode")
        self.assertEqual(read["issue_heading"], "Issue")
        self.assertIn("Issue", read["sections"])

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

    # ---- task dependencies (`blocked_by=`, format 2.5) -------------------------
    def tasks_of(self, card_id=None):
        return self.board.card_by_id(card_id or self.card_id).tasks()

    def test_a_new_item_can_be_blocked_by_another_new_item(self):
        # The importer's case: neither item exists yet, so the reference is a position in the
        # list being written and the ids are settled here.
        self.tools.run("board_update_card", {
            "id": self.card_id, "base_hash": self.hash,
            "tasks": [{"text": "Pick a backend"},
                      {"text": "Write the adapter", "blocked_by": [1]},
                      {"text": "Expire old sessions", "blocked_by": [2]}]})
        items = self.tasks_of()
        self.assertEqual([i.blocked_by for i in items],
                         [[], [items[0].item_id], [items[1].item_id]])
        line = self.board.card_by_id(self.card_id).body.splitlines()
        self.assertIn(f"blocked_by={items[0].item_id}", "\n".join(line))
        self.assertEqual(self.board.check(), [])

    def test_an_item_id_and_a_card_reference_are_both_accepted(self):
        other = self.create(title="Push to talk", request="right-alt")
        self.tools.run("board_update_card", {"id": self.card_id, "base_hash": self.hash,
                                             "tasks": [{"text": "First"}, {"text": "Second"}]})
        first = self.tasks_of()[0]
        current = self.tools.run("board_read", {"id": self.card_id})["hash"]
        self.tools.run("board_update_card", {
            "id": self.card_id, "base_hash": current,
            "tasks": [{"text": "First", "item_id": first.item_id},
                      {"text": "Second", "blocked_by": [first.item_id, f"#{other}"]}]})
        self.assertEqual(self.tasks_of()[1].blocked_by, [first.item_id, f"#{other}"])
        self.assertEqual(self.board.check(), [])

    def test_a_blocker_that_is_not_there_a_self_reference_and_a_cycle_are_all_refused(self):
        for tasks, why in (
                ([{"text": "a"}, {"text": "b", "blocked_by": [9]}], "out of range"),
                ([{"text": "a", "blocked_by": [1]}], "itself"),
                ([{"text": "a", "blocked_by": [2]}, {"text": "b", "blocked_by": [1]}], "cycle"),
                ([{"text": "a"}, {"text": "b", "blocked_by": ["zz"]}], "unknown item id"),
                ([{"text": "a"}, {"text": "b", "blocked_by": ["#nope"]}], "not a card id")):
            result = self.tools.run("board_update_card", {"id": self.card_id,
                                                          "base_hash": self.hash, "tasks": tasks})
            self.assertIn("error", result, why)
            self.assertEqual(self.tasks_of(), [], f"{why}: the card was written anyway")

    def test_an_item_that_says_nothing_keeps_the_marker_it_had(self):
        card = self.board.card_by_id(self.card_id)
        card.body = card.body.rstrip("\n") + "\n\n## Tasks\n- [ ] One <!-- t:a3 -->\n" \
                                              "- [ ] Two <!-- t:b7 blocked_by=a3 -->\n"
        B.atomic_write(card.path, card.to_text())
        current = self.tools.run("board_read", {"id": self.card_id})["hash"]
        self.tools.run("board_update_card", {
            "id": self.card_id, "base_hash": current,
            "tasks": [{"text": "One", "item_id": "a3"},
                      {"text": "Two renamed", "item_id": "b7"}]})
        self.assertEqual([i.blocked_by for i in self.tasks_of()], [[], ["a3"]])

    def test_board_read_reports_what_blocks_each_item(self):
        self.tools.run("board_update_card", {
            "id": self.card_id, "base_hash": self.hash,
            "tasks": [{"text": "One"}, {"text": "Two", "blocked_by": [1]}]})
        tasks = self.tools.run("board_read", {"id": self.card_id})["tasks"]
        self.assertEqual(tasks[1]["blocked_by"], [tasks[0]["item_id"]])


# --------------------------------------------------------------------------- move

class PriorityTests(BoardToolsTest):
    """The priority flag (card #VKFV): the pane's click path, and the agent's field."""

    def card_front(self, card_id):
        card = [c for c in self.board.cards() if c.id == card_id][0]
        return card.front

    def test_the_row_carries_the_flag(self):
        card_id = self.create()
        self.assertEqual(self.card_front(card_id).get("priority"), None)
        self.set_priority_ok(card_id, 2)
        rows = self.tools.run("board_list", {"status": "inbox"})["cards"]
        self.assertEqual([r for r in rows if r["id"] == card_id][0]["priority"], 2)

    def test_the_click_path_clamps_and_zero_clears_the_flag(self):
        card_id = self.create()
        result = self.tools.set_priority(card_id, 9)
        self.assertEqual(result["priority"], 3)
        self.assertEqual(self.card_front(card_id).get("priority"), 3)
        result = self.tools.set_priority(card_id, -9)
        self.assertEqual(result["priority"], -1)
        self.assertEqual(self.card_front(card_id).get("priority"), -1)
        result = self.tools.set_priority(card_id, 0)
        self.assertEqual(result["priority"], 0)
        self.assertNotIn("priority", self.card_front(card_id))
        self.assertIn("cleared this card's priority flag", self.thread_text(card_id))

    def test_a_bad_flag_is_refused_and_writes_nothing(self):
        card_id = self.create()
        with self.assertRaises(B.BoardError):
            self.tools.set_priority(card_id, "high")

    def test_a_flag_click_is_undoable(self):
        card_id = self.create()
        result = self.tools.set_priority(card_id, 3)
        undone = self.tools.undo(result["write_id"])
        self.assertEqual(undone["action"], "priority")
        self.assertNotIn("priority", self.card_front(card_id))

    def test_board_update_card_sets_the_flag_like_any_field(self):
        card_id = self.create()
        self.set_priority_ok(card_id, -1)
        digest = self.tools.run("board_read", {"id": card_id})["hash"]
        result = self.tools.run("board_update_card",
                                {"id": card_id, "base_hash": digest,
                                 "fields": {"priority": 2}})
        self.assertNotIn("error", result, result)
        self.assertEqual(self.card_front(card_id).get("priority"), 2)
        digest = self.tools.run("board_read", {"id": card_id})["hash"]
        result = self.tools.run("board_update_card",
                                {"id": card_id, "base_hash": digest, "fields": {"priority": 0}})
        self.assertNotIn("error", result, result)
        self.assertNotIn("priority", self.card_front(card_id))
        digest = self.tools.run("board_read", {"id": card_id})["hash"]
        result = self.tools.run("board_update_card",
                                {"id": card_id, "base_hash": digest, "fields": {"priority": "soon"}})
        self.assertEqual(result["code"], "board_refused")
        self.assertEqual(result["field"], "priority")

    def set_priority_ok(self, card_id, priority):
        result = self.tools.set_priority(card_id, priority)
        self.assertNotIn("error", result, result)
        return result


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
        # A worker that cannot name itself (a guest writing through the bridge): only then is the
        # agent's own `implemented_by` argument asked for, and only then can it be missing.
        self.tools.context.model = self.tools.context.preset = None
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
        self.assertEqual(card.front["implemented_by"], "anthropic/claude-opus-5")
        self.assertIn("docs/qa_evidence/2026-09-17-voice/", card.front["links"]["evidence"])
        self.assertIn("evidence docs/qa_evidence", self.thread_text(self.card_id))

    def _into_qa(self, implementer):
        """Land the card in the QA lane as `implementer` would: the worker stamps its own
        signature now (#T71W), so the pane that lands it *is* the implementer."""
        was = self.tools.context.model
        self.tools.context.model = implementer
        self.tools.run("board_move_card", {
            "id": self.card_id, "status": "needs-qa-llm", "reason": "landed",
            "evidence": "docs/qa_evidence/x/"})
        self.tools.context.model = was

    def test_closing_a_qa_card_needs_a_verdict_section(self):
        self._into_qa("openai/gpt-5")
        result = self.tools.run("board_move_card", {"id": self.card_id, "status": "done",
                                                    "reason": "passed"})
        self.assertEqual(result["requires"], "verdict")
        self.assertEqual(self.board.card_by_id(self.card_id).status, "needs-qa-llm")

    def test_the_same_model_family_closes_it_once_the_verdict_is_there(self):
        # Owner, 2026-09-20 (#76DJ): the verdict is the gate, not the closer's model family.
        self._into_qa("anthropic/claude-opus-5")
        current = self.tools.run("board_read", {"id": self.card_id})["hash"]
        self.tools.run("board_update_card", {"id": self.card_id, "base_hash": current,
                                             "append_section": {"heading": "Verdict", "text": "pass"}})
        result = self.tools.run("board_move_card", {"id": self.card_id, "status": "done",
                                                    "reason": "passed"})
        self.assertNotIn("error", result)
        card = self.board.card_by_id(self.card_id)
        self.assertEqual(card.status, "done")
        self.assertEqual(card.front["verified_by"], "anthropic/claude-opus-5")

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


# ------------------------------------------------------- the QA signature and the verifier

class SignatureTests(BoardToolsTest):
    """Card #T71W: the worker stamps who implemented and who verified, and `board_read` says who
    should verify next. The recommendation is computed with a fixed availability, so these tests
    say nothing about the machine they run on."""

    HERE = {"installed_guests": {"codex"}, "keys": {"glm-coding": True},
            "local_models": ()}

    def setUp(self):
        super().setUp()
        self.card_id = self.create()
        patch = unittest.mock.patch.object(QA, "availability", lambda *a, **k: dict(self.HERE))
        patch.start()
        self.addCleanup(patch.stop)

    def sign(self, preset, model):
        self.tools.context.preset, self.tools.context.model = preset, model

    def test_starting_work_stamps_the_panes_own_signature_nobody_types_it(self):
        self.sign("openrouter", "deepseek/deepseek-v4.1-flash")
        self.tools.run("board_move_card", {"id": self.card_id, "status": "in-progress",
                                           "reason": "starting"})
        card = self.board.card_by_id(self.card_id)
        self.assertEqual(card.front["implemented_by"], "deepseek/deepseek-v4.1-flash")
        self.assertIn("implemented_by deepseek/deepseek-v4.1-flash", self.thread_text(self.card_id))

    def test_the_worker_stamp_wins_over_what_the_agent_typed(self):
        self.sign("anthropic", "claude-opus-5")
        self.tools.run("board_move_card", {
            "id": self.card_id, "status": "needs-qa-llm", "reason": "landed",
            "evidence": "docs/qa_evidence/x/", "implemented_by": "a friendly robot"})
        self.assertEqual(self.board.card_by_id(self.card_id).front["implemented_by"],
                         "anthropic/claude-opus-5")

    def test_a_guest_writing_through_the_bridge_still_names_itself(self):
        self.sign(None, None)
        self.tools.run("board_move_card", {
            "id": self.card_id, "status": "needs-qa-llm", "reason": "landed",
            "evidence": "docs/qa_evidence/x/", "implemented_by": "openai/codex"})
        self.assertEqual(self.board.card_by_id(self.card_id).front["implemented_by"], "openai/codex")

    def test_closing_a_qa_card_stamps_verified_by_with_the_closers_signature(self):
        self.sign("openai", "gpt-6-astra")
        self.tools.run("board_move_card", {"id": self.card_id, "status": "needs-qa-llm",
                                           "reason": "landed", "evidence": "docs/qa_evidence/x/"})
        self.sign("glm-coding", "glm-5.3")
        current = self.tools.run("board_read", {"id": self.card_id})["hash"]
        self.tools.run("board_update_card", {"id": self.card_id, "base_hash": current,
                                             "append_section": {"heading": "Verdict", "text": "pass"}})
        result = self.tools.run("board_move_card", {"id": self.card_id, "status": "done",
                                                    "reason": "verified"})
        self.assertNotIn("error", result)
        card = self.board.card_by_id(self.card_id)
        self.assertEqual(card.front["implemented_by"], "openai/gpt-6-astra")
        self.assertEqual(card.front["verified_by"], "glm/glm-5.3")
        self.assertIn("verified_by glm/glm-5.3", self.thread_text(self.card_id))
        self.assertIn("verified_by", B.FIELD_ORDER)          # and it survives a rewrite of the file
        self.assertEqual(self.board.check(), [])

    def _with_verdict(self):
        current = self.tools.run("board_read", {"id": self.card_id})["hash"]
        self.tools.run("board_update_card", {"id": self.card_id, "base_hash": current,
                                             "append_section": {"heading": "Verdict", "text": "pass"}})

    def test_relay_free_cards_close_like_any_other_once_verified(self):
        # The signature stays `relay-free/…` (the upstream gateway never leaks in); since #76DJ
        # (owner, 2026-09-20) the family that implemented may close it once the verdict is there.
        self.sign("relay-free", "relay-main")
        self.tools.run("board_move_card", {"id": self.card_id, "status": "needs-qa-llm",
                                           "reason": "landed", "evidence": "docs/qa_evidence/x/"})
        self.assertEqual(self.board.card_by_id(self.card_id).front["implemented_by"],
                         "relay-free/relay-main")
        self.sign("glm-coding", "glm-5.3")
        self._with_verdict()
        result = self.tools.run("board_move_card", {"id": self.card_id, "status": "done",
                                                    "reason": "verified"})
        self.assertNotIn("error", result)
        self.assertEqual(self.board.card_by_id(self.card_id).front["verified_by"], "glm/glm-5.3")

    def test_relay_free_may_not_close_a_card_at_all(self):
        # Owner, 2026-09-19: verifying is not part of the free plan, whatever the upstream is.
        self.sign("openai", "gpt-6-astra")
        self.tools.run("board_move_card", {"id": self.card_id, "status": "needs-qa-llm",
                                           "reason": "landed", "evidence": "docs/qa_evidence/x/"})
        self.sign("relay-free", "relay-lite")      # Gemini today: a different family, still refused
        self._with_verdict()
        refused = self.tools.run("board_move_card", {"id": self.card_id, "status": "done",
                                                     "reason": "verified"})
        self.assertEqual(refused["requires"], "independent_model")
        self.assertIn("not available on Relay Free", refused["error"])
        self.assertEqual(self.board.card_by_id(self.card_id).status, "needs-qa-llm")

    def test_a_guest_pane_signs_the_model_the_harness_reported(self):
        self.sign("guest:claude", "claude-opus-5-20260514")
        self.tools.run("board_move_card", {"id": self.card_id, "status": "in-progress",
                                           "reason": "starting"})
        self.assertEqual(self.board.card_by_id(self.card_id).front["implemented_by"],
                         "anthropic/claude-opus-5-20260514 via claude-code")
        # Claude Code is still Anthropic, so the verifier is chosen outside Anthropic.
        block = self.tools.run("board_read", {"id": self.card_id})["qa"]
        self.assertEqual(block["implementer_family"], "anthropic")
        self.assertEqual(block["recommended"]["runner"], "guest:codex")

    def test_board_read_names_the_verifier_for_a_card_that_has_an_implementer(self):
        self.sign("anthropic", "claude-opus-5")
        self.tools.run("board_move_card", {"id": self.card_id, "status": "needs-qa-llm",
                                           "reason": "landed", "evidence": "docs/qa_evidence/x/"})
        block = self.tools.run("board_read", {"id": self.card_id})["qa"]
        self.assertEqual(block["implemented_by"], "anthropic/claude-opus-5")
        self.assertEqual(block["implementer_family"], "anthropic")
        self.assertEqual(block["recommended"]["runner"], "guest:codex")
        self.assertEqual(block["recommended"]["model"], "codex")
        self.assertEqual([a["runner"] for a in block["alternates"]][0], "preset:glm-coding")
        self.assertEqual([s["family"] for s in block["skipped"]], ["anthropic"])
        # Relay Free says why it is not on offer rather than simply not being there.
        self.assertIn(dict(QA.RELAY_FREE_ROW), block["unavailable"])
        self.assertEqual(block["commits"], [])               # a temporary board is not a git repo

    def test_a_card_with_no_implementer_carries_no_recommendation_at_all(self):
        self.assertNotIn("qa", self.tools.run("board_read", {"id": self.card_id}))

    def test_the_row_carries_both_signatures_but_not_the_recommendation(self):
        self.sign("anthropic", "claude-opus-5")
        self.tools.run("board_move_card", {"id": self.card_id, "status": "in-progress",
                                           "reason": "starting"})
        row = next(r for r in self.tools.run("board_list", {})["cards"] if r["id"] == self.card_id)
        self.assertEqual(row["implemented_by"], "anthropic/claude-opus-5")
        self.assertIsNone(row["verified_by"])
        self.assertNotIn("qa", row)


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

    def test_a_pane_token_rides_on_the_entry(self):
        # Execute's hand-off names the pane it opened (#HKAP): the token rides in the entry's
        # attrs — what the GUI draws as a link that reveals that pane — and the entry marker
        # round-trips it. An empty token is dropped: a comment that handed over no pane
        # (or an old worker) gets no attr at all.
        result = self.tools.run("board_comment", {
            "id": self.card_id, "kind": "progress", "pane_token": "abcd1234efgh5678",
            "text": "Executing (abcd1234) · handed to a new terminal pane beside the Switchboard"})
        self.assertNotIn("error", result)
        # The first non-event entry also moves the card to Discussing (#3XZV), so find the
        # entry by kind rather than taking the last one.
        entry = next(e for e in self.board.thread(self.card_id) if e.kind == "progress")
        self.assertEqual(entry.attrs["pane_token"], "abcd1234efgh5678")
        self.assertIn("pane_token=abcd1234efgh5678", self.thread_text(self.card_id))
        plain = self.tools.run("board_comment", {"id": self.card_id, "kind": "note",
                                                 "text": "no pane", "pane_token": ""})
        self.assertNotIn("error", plain)
        note = next(e for e in self.board.thread(self.card_id) if e.text == "no pane")
        self.assertNotIn("pane_token", note.attrs)
        self.assertEqual([str(p) for p in self.board.check()], [])

    def test_a_pane_token_the_entry_marker_cannot_hold_is_refused(self):
        # The attrs live inside an HTML comment: whitespace would split one, '>' would close
        # it, and 64 characters is the cap the protocol sets (19.10).
        for bad in ("a b", "a>b", "x" * 65):
            self.assertIn("pane_token", self.tools.run(
                "board_comment", {"id": self.card_id, "kind": "progress", "text": "x",
                                  "pane_token": bad})["error"], bad)
        # Nothing was written: the thread still holds only the creation event.
        self.assertEqual([e.kind for e in self.board.thread(self.card_id)], ["event"])

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


# ------------------------------------------------------------------------- claim

class ClaimTests(BoardToolsTest):
    """`board_claim` (#R9G7): one call does what the Switchboard's Execute button does."""

    def setUp(self):
        super().setUp()
        self.card_id = self.create()

    def progress_entries(self, card_id=None):
        return [e for e in self.board.thread(card_id or self.card_id) if e.kind == "progress"]

    def test_a_claim_sets_assignee_status_and_session_and_links_the_pane(self):
        result = self.tools.run("board_claim", {"id": self.card_id,
                                                "note": "starting on the parser"})
        self.assertNotIn("error", result, result)
        self.assertTrue(result["claimed"])
        self.assertEqual(result["status"], "executing")
        self.assertEqual(result["session"], self.pane_token)
        card = self.board.card_by_id(self.card_id)
        self.assertEqual(card.front["assignee"], "agent")
        self.assertEqual(card.status, "executing")
        self.assertEqual(card.front["session"], self.pane_token)
        # The progress entry is the link back to the pane: first line names the token's first
        # eight characters, exactly as Execute's `Executing (xxxxxxxx) · …` does, and the token
        # itself rides in the entry's attrs for the GUI to draw as `relay-pane:<token>`.
        entry = self.progress_entries()[-1]
        self.assertEqual(entry.attrs["pane_token"], self.pane_token)
        self.assertEqual(entry.text.splitlines()[0],
                         f"Claimed ({self.pane_token[:8]}) · working on it from a terminal pane")
        self.assertIn("starting on the parser", entry.text)
        self.assertIn(f"pane_token={self.pane_token}", self.thread_text(self.card_id))
        self.assertEqual([str(p) for p in self.board.check()], [])

    def test_a_claim_is_one_undoable_write_and_one_activity_event(self):
        before = len(self.events)
        result = self.tools.run("board_claim", {"id": self.card_id})
        actions = [e for e in self.events[before:] if e.get("event") == "board_activity"]
        self.assertEqual([e["action"] for e in actions], ["claim"])
        undone = self.tools.undo(result["write_id"])
        self.assertEqual(undone["id"], self.card_id)
        card = self.board.card_by_id(self.card_id)
        self.assertNotIn("session", card.front)
        self.assertEqual(card.status, "inbox")

    def test_claiming_a_card_this_pane_already_holds_is_idempotent(self):
        self.tools.run("board_claim", {"id": self.card_id})
        again = self.tools.run("board_claim", {"id": self.card_id, "note": "still me"})
        self.assertNotIn("error", again, again)
        self.assertEqual(again["status"], "executing")
        self.assertEqual(again["session"], self.pane_token)
        card = self.board.card_by_id(self.card_id)
        self.assertEqual(card.front["session"], self.pane_token)
        self.assertEqual(card.status, "executing")
        self.assertEqual(again["summary"], "already claimed by this pane")
        # Two claims, two progress entries; the card is claimed once.
        self.assertEqual(len(self.progress_entries()), 2)
        self.assertEqual(self.tools.claimed, [self.card_id])
        self.assertEqual([str(p) for p in self.board.check()], [])

    def test_a_card_held_by_another_session_is_refused_and_force_takes_it(self):
        other = T.BoardTools(self.board, autonomy="auto",
                             context=T.ToolContext(actor="agent", model="x/y", pane="9"),
                             state_path=self.repo / ".relay" / "other.json",
                             pane_token="9c1d77ab-2e40-4f01-8f55-6b0aa1c4de33")
        other.begin_turn("t-other")
        self.assertNotIn("error", other.run("board_claim", {"id": self.card_id}))

        refused = self.tools.run("board_claim", {"id": self.card_id})
        self.assertEqual(refused["code"], "board_claimed_elsewhere")
        self.assertEqual(refused["session"], "9c1d77ab")
        self.assertEqual(refused["status"], "executing")
        self.assertTrue(refused["latest_entry"], refused)
        self.assertIn("force", refused["error"])
        # Nothing was written by the refused call.
        self.assertEqual(self.board.card_by_id(self.card_id).front["session"], "9c1d77ab-2e40-4f01-8f55-6b0aa1c4de33")

        taken = self.tools.run("board_claim", {"id": self.card_id, "force": True,
                                               "note": "the user said to take it over"})
        self.assertNotIn("error", taken, taken)
        self.assertEqual(taken["session"], self.pane_token)
        self.assertEqual(self.board.card_by_id(self.card_id).front["session"], self.pane_token)
        self.assertEqual([str(p) for p in self.board.check()], [])

    def test_a_session_that_is_not_executing_does_not_hold_the_card(self):
        # Only executing/in-progress means held: a card whose work was landed carries the
        # session that did it, and the next request about it is claimed without force.
        self.tools.run("board_claim", {"id": self.card_id})
        self.tools.run("board_move_card", {"id": self.card_id, "status": "needs-verification",
                                           "reason": "landed"})
        other = T.BoardTools(self.board, autonomy="auto",
                             context=T.ToolContext(actor="agent", model="x/y", pane="9"),
                             state_path=self.repo / ".relay" / "other.json",
                             pane_token="9c1d77ab-2e40-4f01-8f55-6b0aa1c4de33")
        other.begin_turn("t-other")
        result = other.run("board_claim", {"id": self.card_id})
        self.assertNotIn("error", result, result)
        self.assertEqual(result["session"], "9c1d77ab-2e40-4f01-8f55-6b0aa1c4de33")

    def test_a_worker_with_no_pane_token_claims_without_a_session(self):
        tools = T.BoardTools(self.board, autonomy="auto",
                             context=T.ToolContext(actor="agent", model="x/y", pane="9"),
                             state_path=self.repo / ".relay" / "none.json")
        tools.begin_turn("t-none")
        result = tools.run("board_claim", {"id": self.card_id})
        self.assertNotIn("error", result, result)
        self.assertIsNone(result["session"])
        self.assertIn("no pane session token", result["warning"])
        card = self.board.card_by_id(self.card_id)
        self.assertNotIn("session", card.front)
        self.assertEqual(card.front["assignee"], "agent")
        self.assertEqual(card.status, "executing")
        entry = self.progress_entries()[-1]
        self.assertNotIn("pane_token", entry.attrs)
        self.assertEqual(entry.text, "Claimed · working on it from a terminal pane")
        self.assertEqual([str(p) for p in self.board.check()], [])

    def test_the_result_carries_the_whole_card_block(self):
        self.tools.run("board_update_card", {
            "id": self.card_id, "base_hash": self.tools.run("board_read", {"id": self.card_id})["hash"],
            "tasks": [{"text": "Write the parser"}]})
        result = self.tools.run("board_claim", {"id": self.card_id})
        block = result["card"]
        # The same block `ask {cards: [...]}` builds (19.6), so a card that arrives with the
        # prompt and a card handed back by a claim read identically.
        self.assertEqual(block, P.seed_block(self.board,
                                             self.board.card_by_id(self.card_id)))
        self.assertIn(f"[Switchboard card #{self.card_id}", block)
        self.assertIn("--- card front matter ---", block)
        self.assertIn(self.pane_token, block)
        self.assertIn("Write the parser", block)
        self.assertIn("--- thread (last", block)

    def test_the_tool_takes_only_its_own_arguments(self):
        self.assertIn("board_claim takes", self.tools.run(
            "board_claim", {"id": self.card_id, "status": "done"})["error"])
        self.assertIn("force must be", self.tools.run(
            "board_claim", {"id": self.card_id, "force": "yes"})["error"])
        self.assertEqual(self.tools.run("board_claim", {"id": "ZZZZ"})["code"], "board_not_found")

    def test_a_model_can_never_type_the_session_field(self):
        # The token is the tool's to write, from `configure`: a card cannot be taken by
        # putting somebody else's token (or your own) in a front matter patch.
        read = self.tools.run("board_read", {"id": self.card_id})
        refused = self.tools.run("board_update_card", {
            "id": self.card_id, "base_hash": read["hash"], "fields": {"session": "deadbeef"}})
        self.assertEqual(refused["code"], "board_refused")
        self.assertEqual(refused["field"], "session")
        self.assertNotIn("session", self.board.card_by_id(self.card_id).front)

    def test_the_row_carries_the_session_so_the_board_shows_who_holds_the_card(self):
        # `board_list`'s rows and the pane's rows are one builder (`_row`): the Switchboard draws
        # the chip from this field, and an agent listing the board sees a card is taken.
        rows = self.tools.run("board_list", {"query": ""})["cards"]
        self.assertIsNone(next(r for r in rows if r["id"] == self.card_id)["session"])
        self.tools.run("board_claim", {"id": self.card_id})
        rows = self.tools.run("board_list", {"query": ""})["cards"]
        self.assertEqual(next(r for r in rows if r["id"] == self.card_id)["session"],
                         self.pane_token)

    def test_a_cleanup_cannot_claim_a_card(self):
        # A cleanup is the Switchboard worker tidying the whole board (19.9): no pane of its own,
        # and moving a card to Executing is not tidying.
        self.tools.begin_cleanup("c-1")
        refused = self.tools.run("board_claim", {"id": self.card_id})
        self.assertEqual(refused["code"], "board_refused")
        self.assertIn("cleanup", refused["error"])
        self.assertEqual(self.board.card_by_id(self.card_id).status, "inbox")

    def test_only_a_work_card_is_claimed(self):
        plan = self.tools.run("board_create_card", {
            "tab": "planning", "status": "draft", "type": "plan", "title": "A plan",
            "request": "plan the thing"})
        self.assertIn("only a work card", self.tools.run("board_claim", {"id": plan["id"]})["error"])


class ClaimPromptTests(BoardToolsTest):
    def test_the_prompt_names_this_session_and_the_cards_it_holds(self):
        text = T.prompt_section(self.tools)
        self.assertIn(f"Your session: {self.pane_token[:8]}.", text)
        self.assertNotIn("You hold:", text)
        card_id = self.create()
        self.tools.run("board_claim", {"id": card_id})
        self.assertIn(f"You hold: #{card_id}.", T.prompt_section(self.tools))

    def test_a_worker_with_no_token_says_nothing_about_a_session(self):
        tools = T.BoardTools(self.board, autonomy="auto",
                             state_path=self.repo / ".relay" / "none.json")
        self.assertNotIn("Your session:", T.prompt_section(tools))


class ClaimMessageTests(BoardToolsTest):
    """The same claim as a GUI→worker message: `board_claim` (protocol 19.3, 19.19).

    Execute's hand-off (19.10) is this one message now, and its `pane_token` is the pane the card
    was handed to — never the worker's own, since the Switchboard worker has no pane of its own.
    """

    class Turns:
        agent = None
        busy = False

        def now_or_later(self, now, later):
            return now()

    def commands(self, request=None):
        commands = P.BoardCommands(self.Turns(), self.events.append)
        commands.configure(str(self.repo), request or {})
        self.addCleanup(commands.cards.drop)
        return commands

    def send(self, commands, **request):
        self.events.clear()
        commands.dispatch(request)
        return self.events

    def test_the_message_claims_the_card_for_the_pane_it_names(self):
        card_id = self.create()
        commands = self.commands()
        token = "b1c4e5f6-1111-4222-8333-444455556666"
        events = self.send(commands, type="board_claim", id="x1", card=card_id,
                           pane_token=token, text="on it")
        written = [e for e in events if e["event"] == "board_written"]
        self.assertEqual(len(written), 1, events)
        self.assertEqual((written[0]["id"], written[0]["kind"], written[0]["card_id"]),
                         ("x1", "board_claim", card_id))
        self.assertEqual(written[0]["session"], token)
        self.assertTrue(written[0]["write_id"])
        # The card block is the model's; it does not go down the pipe with the reply.
        self.assertNotIn("card", written[0])
        # The board tells the pane the row changed, exactly as a move does.
        self.assertTrue([e for e in events if e["event"] == "board_changed"])
        card = self.board.card_by_id(card_id)
        self.assertEqual((card.status, card.front["assignee"], card.front["session"]),
                         ("executing", "agent", token))
        entry = [e for e in self.board.thread(card_id) if e.kind == "progress"][-1]
        self.assertEqual(entry.attrs["pane_token"], token)
        self.assertIn("Claimed (b1c4e5f6)", entry.text)
        self.assertIn("on it", entry.text)

    def test_the_messages_token_beats_the_one_configure_gave_the_worker(self):
        card_id = self.create()
        commands = self.commands({"pane_token": "aaaaaaaa-0000-4000-8000-000000000000"})
        self.send(commands, type="board_claim", card=card_id,
                  pane_token="bbbbbbbb-0000-4000-8000-000000000000")
        self.assertEqual(self.board.card_by_id(card_id).front["session"],
                         "bbbbbbbb-0000-4000-8000-000000000000")
        # And the worker keeps its own token for the turns its agent runs.
        self.assertEqual(commands.tools.pane_token, "aaaaaaaa-0000-4000-8000-000000000000")

    def test_a_card_another_session_holds_is_refused_through_the_error_event(self):
        card_id = self.create()
        commands = self.commands()
        self.send(commands, type="board_claim", card=card_id,
                  pane_token="aaaaaaaa-0000-4000-8000-000000000000")
        events = self.send(commands, type="board_claim", id="x2", card=card_id,
                           pane_token="bbbbbbbb-0000-4000-8000-000000000000")
        errors = [e for e in events if e["event"] == "error"]
        self.assertEqual(len(errors), 1, events)
        self.assertEqual((errors[0]["id"], errors[0]["code"]), ("x2", "board_claimed_elsewhere"))
        self.assertIn("aaaaaaaa", errors[0]["text"])
        # …and the owner may take it over.
        self.send(commands, type="board_claim", card=card_id, force=True,
                  pane_token="bbbbbbbb-0000-4000-8000-000000000000")
        self.assertEqual(self.board.card_by_id(card_id).front["session"],
                         "bbbbbbbb-0000-4000-8000-000000000000")

    def test_configure_carries_the_panes_token_into_both_halves_of_the_tools(self):
        commands = self.commands({"pane_token": self.pane_token})
        self.assertEqual(commands.pane_token, self.pane_token)
        self.assertEqual(commands.tools.pane_token, self.pane_token)
        agent = commands.agent_tools(str(self.repo), {})
        self.assertEqual(agent.pane_token, self.pane_token)
        # A token the entry marker could not hold is a protocol error, not a quiet drop.
        with self.assertRaises(ValueError):
            self.commands({"pane_token": "not a token"})


class PaneTokenTests(unittest.TestCase):
    def test_a_token_the_entry_marker_cannot_hold_is_refused(self):
        for bad in ("a b", "a>b", "x" * 65):
            with self.assertRaises(T.BoardToolError):
                T.check_pane_token(bad)
        self.assertIsNone(T.check_pane_token(None))
        self.assertIsNone(T.check_pane_token("  "))
        self.assertEqual(T.check_pane_token(" abc "), "abc")


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
        # The creation, the one note that fitted, and the inbox → Discussing move that note
        # earned (#3XZV). The refused second note wrote nothing.
        self.assertEqual(len(self.board.thread(card_id)), 3)

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
        # A tree of its own: a subdirectory of this repo now finds the repo's board, as the GUI
        # has always done, so "no board" has to be somewhere with no board above it either.
        with tempfile.TemporaryDirectory() as elsewhere:
            self.assertIsNone(T.BoardTools.for_workspace(Path(elsewhere).resolve()))
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

    def test_rule_3_covers_wherever_the_agent_asks(self):
        # #WT9V: the board page's chat was a second place a question could hide in, so rule 3
        # names it — the card is where a question waits wherever the agent asks.
        text = T.prompt_section(self.tools)
        self.assertIn("board page's chat", text)
        self.assertIn("the question waits", text)

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

    def test_a_delete_removes_the_card_and_its_thread_and_returns_a_write_id(self):
        card_id = self.create()
        self.tools.run("board_comment", {"id": card_id, "kind": "note", "text": "a line"})
        card = self.board.card_by_id(card_id)
        path, thread = card.path, self.board.thread_path(card_id)
        self.assertTrue(path.exists() and thread.exists())
        result = self.tools.delete_card(card_id, "a scratch card")
        self.assertEqual(result["id"], card_id)
        self.assertTrue(result["removed"])
        self.assertTrue(result["write_id"])
        self.assertFalse(path.exists())
        self.assertFalse(thread.exists())
        self.assertIsNone(self.board.card_by_id(card_id))
        # The change the panes hear names the card in `removed`, never as an upsert: there is
        # no card there to read.
        changed = [e for e in self.events if e["event"] == "board_changed"][-1]
        self.assertEqual(changed["removed"], [card_id])
        self.assertEqual(changed["upserts"], [])

    def test_a_delete_is_undoable_and_both_files_come_back_byte_for_byte(self):
        card_id = self.create()
        self.tools.run("board_comment", {"id": card_id, "kind": "note", "text": "keep me"})
        card = self.board.card_by_id(card_id)
        card_bytes, thread_bytes = card.path.read_bytes(), self.board.thread_path(card_id).read_bytes()
        result = self.tools.delete_card(card_id)
        undone = self.tools.undo(result["write_id"])
        self.assertEqual(undone["action"], "delete")
        self.assertFalse(undone["removed"])
        self.assertEqual(card.path.read_bytes(), card_bytes)
        self.assertEqual(self.board.thread_path(card_id).read_bytes(), thread_bytes)
        # No "undid" line either: the card is exactly as it was, not annotated (the delete
        # had no thread to append to, so undo appends nothing).
        self.assertNotIn("undid", thread_bytes.decode("utf-8"))

    def test_a_delete_takes_the_other_privacy_variants_thread_too_and_undo_brings_it_back(self):
        card_id = self.create()
        private = self.board.thread_path(card_id, private=True)
        private.parent.mkdir(parents=True, exist_ok=True)
        private.write_bytes(b"private thread bytes")
        result = self.tools.delete_card(card_id)
        self.assertFalse(private.exists())
        self.tools.undo(result["write_id"])
        self.assertEqual(private.read_bytes(), b"private thread bytes")

    def test_an_undo_recreates_a_folder_that_went_with_the_last_card_in_it(self):
        card_id = self.create()
        folder = self.board.card_by_id(card_id).path.parent
        result = self.tools.delete_card(card_id)
        # Nothing of the card is left; the empty folder may have gone the way of a checkout
        # that prunes empty directories — undo still has to put the card back.
        self.assertFalse(any(folder.iterdir()))
        folder.rmdir()
        self.tools.undo(result["write_id"])
        self.assertEqual(self.board.card_by_id(card_id).id, card_id)

    def test_a_delete_of_an_unknown_card_is_refused(self):
        with self.assertRaises(T.BoardToolError):
            self.tools.delete_card("AAAA")

    def test_the_agent_still_has_no_delete_tool(self):
        # The owner's delete is a method, not a tool (#CYM9): the offered names are unchanged.
        self.assertNotIn("board_delete_card", T.TOOL_NAMES + T.CLEANUP_TOOL_NAMES + T.WRITE_TOOLS)

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


# ------------------------------------------------------------- whole-board cleanup

class CleanupToolTests(BoardToolsTest):
    """The three tools only a `board_cleanup` turn gets (protocol 19.9)."""

    def test_they_are_refused_and_unadvertised_outside_a_cleanup(self):
        offered = {s["function"]["name"] for s in self.tools.tool_specs()}
        self.assertEqual(offered, set(T.TOOL_NAMES))
        for name in T.CLEANUP_TOOL_NAMES:
            result = self.tools.run(name, {"reason": "x"})
            self.assertEqual(result.get("code"), "board_refused", name)
        self.tools.begin_cleanup("c-1")
        offered = {s["function"]["name"] for s in self.tools.tool_specs()}
        self.assertEqual(offered, set(T.TOOL_NAMES) | set(T.CLEANUP_TOOL_NAMES))

    def test_a_cleanup_raises_the_per_turn_ceilings(self):
        self.assertEqual(self.tools.limit("max_writes_per_turn"), 20)
        self.tools.begin_cleanup("c-1")
        self.assertEqual(self.tools.limit("max_writes_per_turn"),
                         T.CLEANUP_LIMITS["max_writes_per_turn"])
        self.tools.end_cleanup("done")
        self.assertEqual(self.tools.limit("max_writes_per_turn"), 20)

    def test_merging_keeps_both_cards_and_logs_the_change(self):
        keep = self.create("Voice mode", "add voice transcribe mode")
        gone = self.create("Dictation", "let me dictate into the box")
        self.tools.run("board_comment", {"id": gone, "kind": "note", "text": "same thing"})
        self.tools.begin_cleanup("c-1")
        result = self.tools.run("board_merge_cards",
                                {"into": keep, "cards": [gone], "reason": "the same request"})
        self.assertNotIn("error", result, result)
        self.assertEqual(result["merged"][0]["id"], gone)
        self.assertIsNotNone(self.board.card_by_id(gone))          # nothing was deleted
        self.assertEqual(self.board.card_by_id(gone).status, "dropped")
        self.assertIn("let me dictate into the box", self.board.card_by_id(keep).body)
        self.assertIn("merged", self.thread_text(keep))
        self.assertIn("merged this card into", self.thread_text(gone))
        log = self.tools.end_cleanup("done", "done.")
        self.assertEqual(log.counts()["merge"], 1)
        self.assertEqual(log.changes[-1].cards, [gone])

    def test_a_merge_is_undone_in_full(self):
        keep = self.create("Voice mode", "add voice transcribe mode")
        gone = self.create("Dictation", "let me dictate into the box")
        was = self.board.card_by_id(gone).path
        self.tools.begin_cleanup("c-1")
        result = self.tools.run("board_merge_cards",
                                {"into": keep, "cards": [gone], "reason": "the same request"})
        undone = self.tools.undo(result["write_id"])
        self.assertEqual(undone["also_restored"], 1)
        back = self.board.card_by_id(gone)
        self.assertEqual(back.path, was)
        self.assertEqual(back.status, "inbox")
        self.assertNotIn("## Merged in", self.board.card_by_id(keep).body)

    def test_a_split_makes_one_card_per_piece_with_the_users_own_words(self):
        card = self.create("Two things", "the tabs flicker and also add a clock")
        self.tools.begin_cleanup("c-1")
        result = self.tools.run("board_split_card", {
            "id": card, "reason": "two unrelated asks", "close": True,
            "parts": [{"title": "Tabs flicker", "request": "the tabs flicker", "labels": ["bug"]},
                      {"title": "A clock", "request": "also add a clock", "labels": ["feature"]}]})
        self.assertNotIn("error", result, result)
        ids = [c["id"] for c in result["children"]]
        self.assertEqual(len(ids), 2)
        self.assertEqual(self.board.card_by_id(card).status, "dropped")
        clock = self.board.card_by_id(ids[1])
        self.assertIn("also add a clock", clock.body)
        self.assertEqual(clock.front["parent"], card)
        self.assertEqual(clock.front["labels"], ["feature"])
        self.assertIn("split this card out of", self.thread_text(ids[1]))
        self.assertEqual(self.board.check(), [])

    def test_a_split_needs_two_parts_and_an_open_card(self):
        card = self.create()
        self.tools.begin_cleanup("c-1")
        one = self.tools.run("board_split_card", {"id": card, "reason": "x",
                                                  "parts": [{"title": "a", "request": "b"}]})
        self.assertIn("2 to 10", one["error"])
        self.tools.run("board_move_card", {"id": card, "status": "dropped", "reason": "no"})
        closed = self.tools.run("board_split_card", {"id": card, "reason": "x", "parts": [
            {"title": "a", "request": "b"}, {"title": "c", "request": "d"}]})
        self.assertIn("nothing to split", closed["error"])

    def test_sections_rewrite_board_yaml_and_keep_it_readable(self):
        self.tools.begin_cleanup("c-1")
        result = self.tools.run("board_sections", {
            "columns": ["inbox", "ready", "in-progress", "needs-qa", "done"],
            "reason": "the waiting lane has been empty for weeks"})
        self.assertNotIn("error", result, result)
        self.assertEqual(self.board.config()["columns"],
                         ["inbox", "ready", "in-progress", "needs-qa", "done"])
        self.assertEqual(self.board.config()["tabs"], B.Board(self.root, self.repo).tabs())

    def test_a_tab_whose_folder_still_holds_cards_cannot_be_dropped(self):
        self.create()
        self.tools.begin_cleanup("c-1")
        result = self.tools.run("board_sections", {
            "tabs": [{"id": "bugs", "folder": "changes"}], "reason": "fewer tabs"})
        self.assertEqual(result["code"], "board_refused")
        self.assertIn("features", result["error"])
        self.assertEqual(len(self.board.tabs()), 4)               # board.yaml is untouched

    def test_an_unknown_column_is_refused(self):
        self.tools.begin_cleanup("c-1")
        result = self.tools.run("board_sections", {"columns": ["inbox", "someday"], "reason": "x"})
        self.assertEqual(result["code"], "board_refused")
        self.assertIn("someday", result["error"])

    # ---- add, remove, merge, rename: the four the gear offers (#VZ69 follow-on) ----

    def test_two_sections_merge_into_one_that_collects_both(self):
        """Merge = one section with both sets of statuses, the other left out of columns."""
        self.tools.begin_cleanup("c-1")
        result = self.tools.run("board_sections", {
            "columns": ["inbox", "discussing", "ready", "in-progress", "needs-qa", "done"],
            "column_statuses": {"needs-qa": ["needs-qa-llm", "needs-qa-human", "needs-review",
                                             "needs-labels", "needs-ab"]},
            "column_titles": {"needs-qa": "Checks"},
            "reason": "one lane for everything that is waiting on a check"})
        self.assertNotIn("error", result, result)
        config = self.board.config()
        self.assertNotIn("waiting", config["columns"])
        self.assertEqual(B.column_statuses_of(config, "needs-qa"),
                         ["needs-qa-llm", "needs-qa-human", "needs-review", "needs-labels", "needs-ab"])
        self.assertEqual(B.column_title_of(config, "needs-qa"), "Checks")
        # It reads back: a board.yaml the board cannot parse is never written.
        self.assertEqual(B.Board(self.root, self.repo).config()["column_titles"], {"needs-qa": "Checks"})

    def test_a_section_of_the_boards_own_needs_statuses_of_its_own(self):
        self.tools.begin_cleanup("c-1")
        refused = self.tools.run("board_sections", {"columns": ["inbox", "triage"], "reason": "x"})
        self.assertEqual(refused["code"], "board_refused")
        self.assertIn("triage", refused["error"])
        # With statuses it is a section like any other, and the id is free-form.
        self.tools.begin_cleanup("c-2")
        result = self.tools.run("board_sections", {
            "columns": ["triage", "ready", "in-progress", "needs-qa", "done"],
            "column_statuses": {"triage": ["inbox", "discussing"]},
            "column_titles": {"triage": "Triage"}, "reason": "one lane before it is agreed"})
        self.assertNotIn("error", result, result)
        self.assertEqual(B.column_statuses_of(self.board.config(), "triage"), ["inbox", "discussing"])

    def test_one_status_belongs_to_one_section(self):
        self.tools.begin_cleanup("c-1")
        result = self.tools.run("board_sections", {
            "columns": ["inbox", "triage", "ready", "done"],
            "column_statuses": {"triage": ["inbox", "discussing"]},
            "reason": "x"})
        self.assertEqual(result["code"], "board_refused")
        self.assertIn("inbox", result["error"])
        self.assertNotIn("triage", self.board.config().get("columns") or [])

    def test_a_section_cannot_collect_a_status_that_does_not_exist(self):
        self.tools.begin_cleanup("c-1")
        result = self.tools.run("board_sections", {
            "columns": ["inbox", "someday", "done"],
            "column_statuses": {"someday": ["maybe-later"]}, "reason": "x"})
        self.assertEqual(result["code"], "board_refused")
        self.assertIn("maybe-later", result["error"])

    def test_renaming_a_section_moves_no_card_and_an_empty_name_puts_it_back(self):
        card_id = self.create(status="ready")
        path = next(c.path for c in self.board.cards() if c.id == card_id)
        before = path.read_bytes()
        self.tools.begin_cleanup("c-1")
        self.tools.run("board_sections", {"column_titles": {"ready": "Up next"}, "reason": "clearer"})
        self.assertEqual(B.column_title_of(self.board.config(), "ready"), "Up next")
        self.assertEqual(path.read_bytes(), before)            # the card is untouched
        self.tools.begin_cleanup("c-2")
        self.tools.run("board_sections", {"column_titles": {}, "reason": "back to the default"})
        self.assertIsNone(B.column_title_of(self.board.config(), "ready"))

    def test_a_section_name_is_one_short_line(self):
        self.tools.begin_cleanup("c-1")
        result = self.tools.run("board_sections",
                                {"column_titles": {"ready": "x" * 60}, "reason": "x"})
        self.assertEqual(result["code"], "board_refused")
        self.assertIn("40", result["error"])

    def test_dropping_a_section_is_undoable_like_any_other_write(self):
        self.tools.begin_cleanup("c-1")
        result = self.tools.run("board_sections", {
            "columns": ["inbox", "ready", "in-progress", "needs-qa", "done"], "reason": "fewer lanes"})
        self.assertNotIn("error", result, result)
        self.assertNotIn("waiting", self.board.config()["columns"])
        self.tools.undo(result["write_id"])
        self.assertIn("waiting", self.board.config()["columns"])


class CleanupLogTests(BoardToolsTest):
    """A cleanup is only as reviewable as its changelog."""

    def test_a_dry_run_writes_nothing_and_records_the_plan(self):
        card = self.create()
        before = (self.board.card_by_id(card).path).read_bytes()
        self.tools.begin_cleanup("c-1", dry_run=True)
        result = self.tools.run("board_move_card", {"id": card, "status": "ready", "reason": "x"})
        self.assertEqual(result["code"], "board_cleanup_dry_run")
        self.assertEqual((self.board.card_by_id(card).path).read_bytes(), before)
        log = self.tools.end_cleanup("done", "here is the plan")
        self.assertEqual(log.counts()["writes"], 0)
        self.assertEqual(log.counts()["proposed"], 1)
        self.assertEqual(log.refusals, [])            # the plan is not a list of problems
        self.assertTrue(log.changes[0].proposed)
        self.assertIn("status: ready", log.changes[0].summary)

    def test_the_changelog_lands_in_a_dated_evidence_folder_and_lists_every_change(self):
        keep = self.create("Voice mode", "add voice transcribe mode")
        gone = self.create("Dictation", "let me dictate into the box")
        self.tools.begin_cleanup("c-1")
        self.tools.run("board_merge_cards", {"into": keep, "cards": [gone], "reason": "the same"})
        self.tools.run("board_move_card", {"id": keep, "status": "ready", "reason": "agreed"})
        log = self.tools.end_cleanup("done", "Merged #%s into #%s." % (gone, keep))
        rel = log.write(self.repo)
        self.assertTrue(rel.startswith("docs/qa_evidence/"))
        self.assertIn("-switchboard-cleanup/cleanup-", rel)
        text = (self.repo / rel).read_text(encoding="utf-8")
        self.assertIn("| merge |", text)
        self.assertIn("| move |", text)
        self.assertIn(f"#{gone}", text)
        self.assertIn("Merged #%s into #%s." % (gone, keep), text)
        self.assertIn("anthropic/claude-opus-5", text)

    def test_a_refusal_is_kept_so_the_changelog_says_what_was_attempted(self):
        self.tools.begin_cleanup("c-1")
        self.tools.run("board_merge_cards", {"into": "ZZZZ", "cards": ["YYYY"], "reason": "x"})
        log = self.tools.end_cleanup("error")
        self.assertEqual(log.refusals[0]["tool"], "board_merge_cards")
        self.assertIn("Refused", log.markdown())

    def test_the_brief_ships_beside_the_policy_and_names_the_rules(self):
        text = T.cleanup_brief()
        self.assertNotIn("<!--", text)
        for phrase in ("board_merge_cards", "board_split_card", "board_sections",
                       "bug_intake.txt", "verbatim", "Left alone"):
            self.assertIn(phrase, text)



# ------------------------------------------------- card turns: Discuss and Plan (#XS6Q)

class CardTurnScopeTests(BoardToolsTest):
    """Protocol 19.10: what a Discuss or a Plan turn on a card may touch, enforced by the tools."""

    def read_hash(self, card_id):
        return self.tools.run("board_read", {"id": card_id})["hash"]

    def test_a_plan_turn_writes_its_own_cards_plan_section(self):
        card_id = self.create()
        self.tools.begin_card_turn("plan", card_id)
        result = self.tools.run("board_update_card", {
            "id": card_id, "base_hash": self.read_hash(card_id),
            "replace_section": {"heading": "Plan", "text": "1. Record audio.\n2. Transcribe it."}})
        self.assertNotIn("error", result, result)
        self.assertIn("## Plan\n1. Record audio.", self.board.card_by_id(card_id).body)

    def test_a_plan_that_repeats_its_heading_is_written_once(self):
        card_id = self.create()
        self.tools.begin_card_turn("plan", card_id)
        self.tools.run("board_update_card", {
            "id": card_id, "base_hash": self.read_hash(card_id),
            "replace_section": {"heading": "Plan", "text": "## Plan\n\n### Goal\nFix it."}})
        body = self.board.card_by_id(card_id).body
        self.assertEqual(body.count("## Plan"), 1, body)
        self.assertIn("### Goal\nFix it.", body)

    def test_a_plan_turn_cannot_retitle_relabel_or_touch_the_issue(self):
        card_id = self.create()
        self.tools.begin_card_turn("plan", card_id)
        for patch in ({"title": "Something else"}, {"fields": {"labels": ["bug"]}},
                      {"replace_section": {"heading": "Issue", "text": "new words"}},
                      {"replace_section": {"heading": "Plan", "text": "x"}, "title": "Also this"}):
            result = self.tools.run("board_update_card",
                                    {"id": card_id, "base_hash": self.read_hash(card_id), **patch})
            self.assertEqual(result.get("code"), "board_mode_refused", patch)
        self.assertEqual(self.board.card_by_id(card_id).title, "Voice transcription mode")

    def test_a_plan_turn_touches_no_other_card_and_moves_nothing(self):
        mine = self.create()
        other = self.create(title="Clickable paths", request="clicking a path opens a pane",
                            not_duplicate_of=[mine])
        self.tools.begin_card_turn("plan", mine)
        refused = self.tools.run("board_update_card", {
            "id": other, "base_hash": self.read_hash(other),
            "replace_section": {"heading": "Plan", "text": "x"}})
        self.assertEqual(refused["code"], "board_mode_refused")
        self.assertEqual(self.tools.run("board_comment", {"id": other, "kind": "note", "text": "hi"})["code"],
                         "board_mode_refused")
        self.assertEqual(self.tools.run("board_move_card", {"id": mine, "status": "ready", "reason": "r"})["code"],
                         "board_mode_refused")
        self.assertEqual(self.tools.run("board_create_card", {
            "tab": "features", "status": "inbox", "title": "T", "request": "r"})["code"], "board_mode_refused")
        # A question for the owner on the card being planned is allowed.
        asked = self.tools.run("board_comment", {"id": mine, "kind": "question", "text": "1. which?"})
        self.assertNotIn("error", asked, asked)

    def test_a_discuss_turn_may_rewrite_the_card_and_the_thread_keeps_the_old_text(self):
        card_id = self.create()
        self.tools.begin_card_turn("discuss", card_id)
        result = self.tools.run("board_update_card", {
            "id": card_id, "base_hash": self.read_hash(card_id), "title": "Voice input (hold Right Alt)",
            "fields": {"labels": ["feature", "voice"]},
            "replace_section": {"heading": "Issue", "text": "hold right alt to talk"}})
        self.assertNotIn("error", result, result)
        self.assertIn("rewrite", self.kinds(card_id))
        self.assertIn("add voice transcribe mode", self.thread_text(card_id))
        moved = self.tools.run("board_move_card", {"id": card_id, "status": "discussing", "reason": "talked"})
        self.assertNotIn("error", moved, moved)

    def test_the_turn_ends_and_the_tools_are_whole_again(self):
        card_id = self.create()
        self.tools.begin_card_turn("plan", card_id)
        self.assertTrue(self.tools.handles("search_files"))
        self.tools.end_card_turn()
        self.assertFalse(self.tools.handles("search_files"))
        moved = self.tools.run("board_move_card", {"id": card_id, "status": "ready", "reason": "r"})
        self.assertNotIn("error", moved, moved)

    def test_an_unknown_mode_is_refused(self):
        with self.assertRaises(T.BoardToolError):
            self.tools.begin_card_turn("execute", "ABCD")

    def test_the_offered_tools_are_read_only_files_search_and_the_modes_board_tools(self):
        executor = [T.spec(n, "d", {}, []) for n in
                    ("run_command", "read_file", "list_directory", "write_file", "edit_file",
                     "command_output", "stop_command", "load_skill", "run_in_terminal")]
        plan = {t["function"]["name"] for t in T.CardScope("plan", "ABCD").tool_specs(executor)}
        self.assertEqual(plan, {"read_file", "list_directory", "load_skill", "search_files",
                                "board_list", "board_read", "board_update_card", "board_comment"})
        discuss = {t["function"]["name"] for t in T.CardScope("discuss", "ABCD").tool_specs(executor)}
        self.assertEqual(discuss - plan, {"board_create_card", "board_move_card"})
        self.assertFalse({"run_command", "write_file", "edit_file", "run_in_terminal"} & discuss)


class SearchFilesTests(BoardToolsTest):
    def setUp(self):
        super().setUp()
        (self.repo / "src").mkdir()
        (self.repo / "src" / "a.py").write_text("def board_ask():\n    return 1\n", encoding="utf-8")
        (self.repo / "src" / "b.cpp").write_text("void boardAsk() {}\n", encoding="utf-8")
        (self.repo / ".env").write_text("board_ask=secret\n", encoding="utf-8")
        (self.repo / ".git").mkdir()
        (self.repo / ".git" / "x").write_text("board_ask\n", encoding="utf-8")
        (self.repo / "bin.dat").write_bytes(b"\0board_ask")
        card_id = self.create()
        self.tools.begin_card_turn("plan", card_id)

    def search(self, **args):
        return self.tools.run("search_files", args)

    def test_it_finds_lines_with_their_path_and_number(self):
        result = self.search(pattern="board_?ask")
        self.assertIn("src/a.py:1: def board_ask():", result["matches"])
        self.assertIn("src/b.cpp:1: void boardAsk() {}", result["matches"])

    def test_it_skips_secrets_git_and_binaries(self):
        joined = "\n".join(self.search(pattern="board_ask")["matches"])
        self.assertNotIn(".env", joined)
        self.assertNotIn(".git", joined)
        self.assertNotIn("bin.dat", joined)

    def test_a_glob_and_a_path_narrow_it(self):
        self.assertEqual([m.split(":")[0] for m in self.search(pattern="board", glob="*.cpp")["matches"]],
                         ["src/b.cpp"])
        self.assertEqual(self.search(pattern="board", path="src/a.py")["count"], 1)

    def test_it_stays_inside_the_workspace(self):
        self.assertIn("error", self.search(pattern="x", path="../"))
        self.assertIn("error", self.search(pattern="("))

    def test_it_is_not_a_tool_outside_a_card_turn(self):
        self.tools.end_card_turn()
        with self.assertRaises(T.BoardToolError):
            self.search(pattern="x")


class UninitializedTests(unittest.TestCase):
    """A project with no Switchboard yet (protocol 19.12): one tool, one line, nothing on disk."""

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.project = Path(self.tmp.name).resolve()
        self.events = []
        self.init = T.BoardInit(self.events.append)
        self.tools = T.BoardTools(T.board_at(self.project / B.DEFAULT_BOARD_FOLDER),
                                  emit=self.events.append, state="uninitialized", init=self.init)

    def test_it_offers_only_the_tool_that_can_create_one(self):
        self.assertEqual([s["function"]["name"] for s in self.tools.tool_specs()],
                         ["board_create_card"])
        self.assertFalse(self.tools.exists())

    def test_the_prompt_block_is_one_line_and_not_the_policy(self):
        note = T.prompt_section(self.tools)
        self.assertIn("this project has no Switchboard yet", note)
        self.assertNotIn("board_rate_limited", note)
        self.assertEqual(note.strip().count("\n"), 0)

    def test_every_other_board_tool_says_there_is_nothing_to_read(self):
        for name in ("board_list", "board_read", "board_update_card", "board_move_card",
                     "board_comment"):
            result = self.tools.run(name, {"id": "AAAA"})
            self.assertEqual(result.get("code"), "board_not_initialized", name)
        self.assertEqual(list(self.project.rglob("*")), [])

    def test_a_declined_project_answers_without_asking_again(self):
        self.init.declined = True
        result = self.tools.run("board_create_card", {"tab": "features", "status": "inbox",
                                                      "title": "T", "request": "r"})
        self.assertEqual(result["code"], "board_not_initialized")
        self.assertEqual([e for e in self.events if e["event"] == "board_init_request"], [])
        self.assertEqual(list(self.project.rglob("*")), [])

    def test_creating_the_board_announces_it_and_promotes_the_tools(self):
        followed = []
        self.tools.on_created = lambda: followed.append(True)
        files = self.tools.create_board()
        self.assertEqual(files[0], ".switchboard/board.yaml")
        created = [e for e in self.events if e["event"] == "board_created"][0]
        self.assertEqual(created["root"], str(self.project / B.DEFAULT_BOARD_FOLDER))
        self.assertEqual(created["workspace"], str(self.project))
        self.assertEqual(created["project"], str(self.project))
        self.assertEqual(self.tools.state, "ready")
        self.assertTrue(self.tools.exists())
        self.assertTrue(set(T.TOOL_NAMES) <=
                        {s["function"]["name"] for s in self.tools.tool_specs()})
        self.assertEqual(followed, [True])


class BoardFolderResolutionTests(unittest.TestCase):
    """What `board.dir` and a workspace walk resolve to, which `relay::boardRootFor` mirrors."""

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.dir = Path(self.tmp.name).resolve()

    def board(self, *parts) -> Path:
        folder = self.dir.joinpath(*parts)
        folder.mkdir(parents=True)
        (folder / B.BOARD_CONFIG).write_text(CONFIG, encoding="utf-8")
        return folder

    def test_a_dir_that_holds_no_board_is_where_a_new_one_would_go(self):
        # Hidden unless the pane's `board.folder` says otherwise (the Options toggle).
        self.assertEqual(T.named_board_root(self.dir), self.dir / ".switchboard")
        self.assertEqual(T.named_board_root(self.dir, "switchboard"), self.dir / "switchboard")
        self.assertEqual(T.named_board_root(self.dir, ".switchboard"), self.dir / ".switchboard")
        self.assertEqual(T.named_board_root(self.dir, "nonsense"), self.dir / ".switchboard")
        # A directory already named like a board folder is taken as one, whatever the option says.
        for name in B.BOARD_FOLDERS:
            self.assertEqual(T.named_board_root(self.dir / name, "switchboard"), self.dir / name)
        self.assertIsNone(T.find_board_root(self.dir))

    def test_an_existing_board_is_found_under_any_name_from_the_project_or_the_folder(self):
        for index, name in enumerate(B.BOARD_FOLDERS):
            project = self.dir / f"p{index}"
            folder = self.board(f"p{index}", name)
            self.assertEqual(T.named_board_root(project), folder, name)
            self.assertEqual(T.named_board_root(folder), folder, name)
            self.assertEqual(T.find_board_root(project), folder, name)
            self.assertEqual(T.board_at(folder).repo, project, name)

    def test_the_first_of_board_folders_wins_in_a_project_that_has_several(self):
        self.board("both", "issues")
        shown = self.board("both", "switchboard")
        self.assertEqual(T.find_board_root(self.dir / "both"), shown)
        self.assertEqual(T.named_board_root(self.dir / "both"), shown)
        hidden = self.board("both", ".switchboard")
        self.assertEqual(T.find_board_root(self.dir / "both"), hidden)
        self.assertEqual(T.named_board_root(self.dir / "both"), hidden)

    def test_a_hidden_board_is_found_from_a_subdirectory_like_any_other(self):
        inner = self.board("proj", ".switchboard")
        deep = self.dir / "proj" / "src" / "deep"
        deep.mkdir(parents=True)
        self.assertEqual(T.find_board_root(deep), inner)
        self.assertEqual(T.board_at(inner).repo, self.dir / "proj")

    def test_the_nearest_ancestor_wins_whatever_its_spelling(self):
        self.board("outer", "switchboard")
        inner = self.board("outer", "inner", "issues")
        deep = self.dir / "outer" / "inner" / "src" / "deep"
        deep.mkdir(parents=True)
        self.assertEqual(T.find_board_root(deep), inner)


# ------------------------------------------- the placement helpers both writers share

class SharedPlacementTests(BoardToolsTest):
    """`board.tab_of` / `category_for_tab` / `write_new_card` / `card_target_path`.

    They were one copy in `BoardTools` and another in `forge_sync`; these tests are here so the
    de-duplication is a change of address and not a change of behaviour.
    """

    def test_the_tools_and_the_module_agree_about_tabs_and_folders(self):
        card_id = self.create()
        card = self.board.card_by_id(card_id)
        self.assertEqual(self.tools._tab_of(card), "features")
        self.assertEqual(B.tab_of(self.board, card), self.tools._tab_of(card))
        self.assertEqual(self.tools._category_for_tab("bugs"), "changes")
        self.assertEqual(B.category_for_tab(self.board, "bugs", strict=True), "changes")

    def test_an_unknown_tab_and_a_filter_tab_are_refused_in_the_same_words(self):
        with self.assertRaises(T.BoardToolError) as unknown:
            self.tools._category_for_tab("nope")
        self.assertIn("unknown tab 'nope'; this board has:", str(unknown.exception))
        with self.assertRaises(T.BoardToolError) as filtered:
            self.tools._category_for_tab("done")
        self.assertIn("is a filter across categories", str(filtered.exception))
        # A reader takes a tab at face value; only a writer is strict.
        self.assertEqual(B.category_for_tab(self.board, "nope"), "nope")

    def test_a_created_card_lands_where_write_new_card_puts_it(self):
        first = self.board.card_by_id(self.create(title="Same title"))
        second = self.board.card_by_id(
            self.create(title="Same title", not_duplicate_of=[first.id]))
        self.assertEqual(first.path.parent, self.root / "features")
        self.assertEqual(second.path.parent, first.path.parent)
        self.assertNotEqual(first.path, second.path)      # the free-name loop, once

    def test_a_move_uses_card_target_path(self):
        card_id = self.create()
        card = self.board.card_by_id(card_id)
        card.set("status", "deferred")
        self.assertEqual(B.card_target_path(self.board, card, "features"),
                         self.root / "features" / "deferred" / card.path.name)
        self.tools.run("board_move_card", {"id": card_id, "status": "deferred", "reason": "go"})
        self.assertEqual(self.board.card_by_id(card_id).path.parent,
                         self.root / "features" / "deferred")

    def test_card_target_path_is_none_when_the_card_is_already_there(self):
        card = self.board.card_by_id(self.create())
        self.assertIsNone(B.card_target_path(self.board, card, "features"))


if __name__ == "__main__":       # pragma: no cover
    unittest.main()
