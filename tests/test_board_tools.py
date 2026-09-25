# SPDX-License-Identifier: AGPL-3.0-or-later
"""Board agent tools: the six `board_*` tools, their refusals and their guardrails.

Every test works in a temporary board; nothing here reads the repository's own `issues/` tree,
calls a model or touches the network or the keyring.
"""
import getpass
import subprocess
import tempfile
from datetime import datetime, timedelta, timezone
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
    #: a worker that has none — the Board's own, or a GUI too old to send one.
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
            context=T.ToolContext(actor="agent", model="anthropic/claude-opus-5-5", pane="2"),
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

class MetadataTests(BoardToolsTest):
    def test_dates_owner_and_reverse_children_round_trip(self):
        parent = self.create(title="Parent")
        child = self.create(title="Child", request="Implement the child feature",
                            parent=parent, not_duplicate_of=[parent])
        current = self.tools.run("board_read", {"id": child})
        result = self.tools.run("board_update_card", {
            "id": child, "base_hash": current["hash"],
            "fields": {"owner": "Elliott", "due": "2026-10-01", "snooze": "2026-09-30"}})
        self.assertNotIn("error", result, result)
        read = self.tools.run("board_read", {"id": child})
        self.assertEqual(read["front"]["due"], {"date": "2026-10-01", "whose": "mine"})
        self.assertEqual(read["front"]["owner"], "Elliott")
        self.assertEqual(read["reverse"]["child_of"], parent)
        children = self.tools.run("board_read", {"id": parent})["children"]
        self.assertEqual([row["id"] for row in children], [child])

    def test_bad_resolution_and_parent_cycle_are_refused(self):
        parent = self.create(title="Parent")
        child = self.create(title="Child", request="Implement the child feature",
                            parent=parent, not_duplicate_of=[parent])
        read = self.tools.run("board_read", {"id": parent})
        bad = self.tools.run("board_update_card", {"id": parent,
            "base_hash": read["hash"], "fields": {"resolution": "unknown"}})
        self.assertEqual(bad["code"], "board_refused")
        cycle = self.tools.run("board_update_card", {"id": parent,
            "base_hash": read["hash"], "fields": {"parent": child}})
        self.assertEqual(cycle["code"], "board_refused")
        self.assertIn(child, cycle["error"])


class SpecTests(unittest.TestCase):
    def test_the_designed_tools_are_offered_and_nothing_else(self):
        self.assertEqual(T.TOOL_NAMES, ("board_list", "board_read", "board_create_card",
                                        "board_update_card", "board_move_card",
                                        "board_import_items", "board_comment", "board_claim",
                                        # protocol 31 (#7BM4): the tests a card names
                                        "tests_check", "tests_run",
                                        # protocol 32 (#AQ6X): the faults the machine tracks
                                        "board_signals",
                                        # protocol 31.10 (#JNYN): stage the card for its verifier
                                        "board_try",
                                        # #95VZ: a person-served case into the ledger
                                        "board_case",
                                        # #GREM: fold a duplicate mid-delivery, outside a cleanup
                                        "board_merge_cards"))

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
        self.assertEqual(T.model_family("anthropic/claude-opus-5-5"), "anthropic")
        self.assertEqual(T.model_family("Claude Opus 5.5 (pane 2)"),
                         T.model_family("anthropic/claude-opus-5-5"))
        self.assertEqual(T.model_family(None), "")
        self.assertNotEqual(T.model_family("openai/gpt-5"), T.model_family("anthropic/claude-opus-5-5"))
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
        # The hash is what board_update_card matches on, so it is exactly 64 lowercase hex
        # characters, never a short digest or a stray character at either end (#E6XC).
        self.assertRegex(result["hash"], r"^[0-9a-f]{64}$")
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

    def test_a_summary_opens_the_issue_with_the_request_quoted_and_attributed(self):
        # #EMWF: the Issue opens with the filing agent's summary; the request stays
        # verbatim in a `>` block that names who said it.
        card_id = self.create(summary="File cards with a descriptive Issue, keeping the quote.",
                              request="add voice transcribe mode")
        body = self.board.card_by_id(card_id).body
        self.assertIn("## Issue\nFile cards with a descriptive Issue, keeping the quote.\n\n"
                      "> add voice transcribe mode\n"
                      f"> — {getpass.getuser()} · ", body)

    def test_the_quote_links_the_session_it_came_from(self):
        session = "0f3ac2de91b4e8a6c0d5f17392ab4e76"
        self.tools.context.session_id = session
        card_id = self.create(summary="Descriptive summary.", request="add voice transcribe mode")
        body = self.board.card_by_id(card_id).body
        self.assertIn(f"[session:{session}](relay://session/{session})", body)

    def test_a_summary_without_a_request_writes_no_quote_block(self):
        card_id = self.create(summary="A fault the agent noticed: the watcher flags the harness home.",
                              request=None)
        body = self.board.card_by_id(card_id).body
        self.assertIn("## Issue\nA fault the agent noticed: the watcher flags the harness home.\n", body)
        self.assertNotIn("\n> ", body)

    def test_creating_with_neither_a_summary_nor_a_request_is_refused(self):
        result = self.tools.run("board_create_card", {"tab": "features", "status": "inbox",
                                                     "title": "Voice transcription mode"})
        self.assertIn("error", result)

    def test_creation_appends_a_thread_event_naming_the_actor_model_pane_and_turn(self):
        card_id = self.create()
        text = self.thread_text(card_id)
        self.assertIn("author=agent", text)
        self.assertIn("kind=event", text)
        self.assertIn("model=anthropic/claude-opus-5-5", text)
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

    def test_a_memory_card_and_an_alias_card_go_to_their_own_folders(self):
        memory = self.create(type="memory", status="active", title="Qt version",
                             request="this repo builds against Qt 6.4")
        alias = self.create(type="alias", status="active", title="Run the GUI checks",
                            request="xvfb-run ctest")
        self.assertEqual(self.board.card_by_id(memory).path.parent, self.root / B.MEMORY_FOLDER)
        self.assertEqual(self.board.card_by_id(alias).path.parent, self.root / B.ALIAS_FOLDER)
        self.assertEqual(self.board.card_by_id(alias).type, "alias")

    def test_there_is_no_plan_card_type(self):
        # Card #X7NB: the type is gone, so the tool refuses it by name.
        self.assertNotIn("plan", B.CARD_TYPES)
        error = self.tools.run("board_create_card", {
            "type": "plan", "tab": "features", "status": "inbox", "title": "T",
            "request": "r"})["error"]
        self.assertIn("type must be one of", error)
        self.assertNotIn("plan", error)

    def test_a_created_card_passes_the_format_check(self):
        self.create()
        # (a claimed card without a `verify` block is a `missing_verify` warning, #WFRA)
        self.assertEqual([str(p) for p in self.board.check() if p.code != "missing_verify"], [])


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
        # A miscopied hash is told apart from a stale one in one round-trip: the error names
        # the length it got and the ends it saw (#E6XC).
        miscopied = self.hash[:-1]
        error = self.tools.run("board_update_card", {
            "id": self.card_id, "base_hash": miscopied, "fields": {"assignee": "a"}})["error"]
        self.assertIn("63 characters", error)
        self.assertIn(f"{miscopied[:4]}…{miscopied[-4:]}", error)
        self.assertIn("board_read", error)

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
            "append_section": {"heading": "Planning notes", "text": "- one"}})
        current = self.tools.run("board_read", {"id": self.card_id})["hash"]
        result = self.tools.run("board_update_card", {
            "id": self.card_id, "base_hash": current,
            "replace_section": {"heading": "Planning notes", "text": "- two"}})
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
        # (a claimed card without a `verify` block is a `missing_verify` warning, #WFRA)
        self.assertEqual([str(p) for p in self.board.check() if p.code != "missing_verify"], [])

    def test_a_move_to_another_tab_moves_the_category(self):
        self.tools.run("board_move_card", {"id": self.card_id, "tab": "bugs", "reason": "it is a bug"})
        self.assertEqual(self.board.card_by_id(self.card_id).path.parent, self.root / "changes")

    def test_an_alias_card_is_retired_and_brought_back(self):
        # Card #W3KD: `aliases` is not a configured tab, so an alias card fell through to the
        # strict tab lookup and could not be moved at all — which meant it could never be
        # retired. A card type with a folder of its own ignores the tab, as memory does.
        alias = self.create(type="alias", status="active", title="Run the GUI checks",
                            request="xvfb-run ctest")
        self.assertEqual(self.board.card_by_id(alias).path.parent, self.root / B.ALIAS_FOLDER)
        self.tools.run("board_move_card", {"id": alias, "status": "retired",
                                           "reason": "not used any more"})
        card = self.board.card_by_id(alias)
        self.assertEqual(card.status, "retired")
        self.assertEqual(card.path.parent, self.root / B.ALIAS_FOLDER / "archive")
        self.tools.run("board_move_card", {"id": alias, "status": "active",
                                           "reason": "wanted again"})
        card = self.board.card_by_id(alias)
        self.assertEqual(card.status, "active")
        self.assertEqual(card.path.parent, self.root / B.ALIAS_FOLDER)
        self.assertEqual([str(x) for x in self.board.check()], [])

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
            "evidence": "docs/qa_evidence/2026-09-17-voice/", "implemented_by": "anthropic/claude-opus-5-5"})
        self.assertNotIn("error", ok)
        card = self.board.card_by_id(self.card_id)
        self.assertEqual(card.path.parent, self.root / "features" / "needs_qa_llm")
        self.assertEqual(card.front["implemented_by"], "anthropic/claude-opus-5-5")
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

    def test_a_resolution_does_not_close_a_qa_card(self):
        # #Z4HR: Verdict and Resolution are different claims at different stages -- a card
        # dropped on a changed mind has a resolution, and that is not "a verifier checked
        # this". The gate takes a verdict only.
        self._into_qa("openai/gpt-5")
        current = self.tools.run("board_read", {"id": self.card_id})["hash"]
        self.tools.run("board_update_card", {"id": self.card_id, "base_hash": current,
                                             "append_section": {"heading": "Resolution",
                                                                "text": "dropped; changed mind"}})
        result = self.tools.run("board_move_card", {"id": self.card_id, "status": "done",
                                                    "reason": "passed"})
        self.assertEqual(result["requires"], "verdict")
        self.assertEqual(self.board.card_by_id(self.card_id).status, "needs-qa-llm")

    def test_the_same_model_family_closes_it_once_the_verdict_is_there(self):
        # Owner, 2026-09-20 (#76DJ): the verdict is the gate, not the closer's model family.
        self._into_qa("anthropic/claude-opus-5-5")
        current = self.tools.run("board_read", {"id": self.card_id})["hash"]
        self.tools.run("board_update_card", {"id": self.card_id, "base_hash": current,
                                             "append_section": {"heading": "Verdict", "text": "pass"}})
        result = self.tools.run("board_move_card", {"id": self.card_id, "status": "done",
                                                    "reason": "passed"})
        self.assertNotIn("error", result)
        card = self.board.card_by_id(self.card_id)
        self.assertEqual(card.status, "done")
        self.assertEqual(card.front["verified_by"], "anthropic/claude-opus-5-5")

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
        self.sign("anthropic", "claude-opus-5-5")
        self.tools.run("board_move_card", {
            "id": self.card_id, "status": "needs-qa-llm", "reason": "landed",
            "evidence": "docs/qa_evidence/x/", "implemented_by": "a friendly robot"})
        self.assertEqual(self.board.card_by_id(self.card_id).front["implemented_by"],
                         "anthropic/claude-opus-5-5")

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

    def test_verifier_updates_and_qa_transition_preserve_the_implementer(self):
        self.sign("anthropic", "claude-fable-5.1")
        self.tools.run("board_move_card", {"id": self.card_id, "status": "needs-verification",
                                           "reason": "implemented"})

        self.sign("kimi-code", "k3")
        current = self.tools.run("board_read", {"id": self.card_id})["hash"]
        self.tools.run("board_update_card", {
            "id": self.card_id, "base_hash": current,
            "replace_section": {"heading": "QA checklist", "text": "all checks passed"}})
        self.assertEqual(self.board.card_by_id(self.card_id).front["implemented_by"],
                         "anthropic/claude-fable-5.1")

        self.tools.run("board_move_card", {
            "id": self.card_id, "status": "needs-qa-llm", "reason": "verified",
            "evidence": "docs/qa_evidence/x/", "implemented_by": "kimi/kimi-k3"})
        self.assertEqual(self.board.card_by_id(self.card_id).front["implemented_by"],
                         "anthropic/claude-fable-5.1")

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
        self.sign("guest:claude", "claude-opus-5-5-20260514")
        self.tools.run("board_move_card", {"id": self.card_id, "status": "in-progress",
                                           "reason": "starting"})
        self.assertEqual(self.board.card_by_id(self.card_id).front["implemented_by"],
                         "anthropic/claude-opus-5-5-20260514 via claude-code")
        # Claude Code is still Anthropic, so the verifier is chosen outside Anthropic.
        block = self.tools.run("board_read", {"id": self.card_id})["qa"]
        self.assertEqual(block["implementer_family"], "anthropic")
        self.assertEqual(block["recommended"]["runner"], "guest:codex")

    def test_board_read_names_the_verifier_for_a_card_that_has_an_implementer(self):
        self.sign("anthropic", "claude-opus-5-5")
        self.tools.run("board_move_card", {"id": self.card_id, "status": "needs-qa-llm",
                                           "reason": "landed", "evidence": "docs/qa_evidence/x/"})
        block = self.tools.run("board_read", {"id": self.card_id})["qa"]
        self.assertEqual(block["implemented_by"], "anthropic/claude-opus-5-5")
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
        self.sign("anthropic", "claude-opus-5-5")
        self.tools.run("board_move_card", {"id": self.card_id, "status": "in-progress",
                                           "reason": "starting"})
        row = next(r for r in self.tools.run("board_list", {})["cards"] if r["id"] == self.card_id)
        self.assertEqual(row["implemented_by"], "anthropic/claude-opus-5-5")
        self.assertIsNone(row["verified_by"])
        self.assertNotIn("qa", row)


# ------------------------------------------------- the self-close marker the done list folds on

class SelfCloseTests(BoardToolsTest):
    """Card #93WR: a *medium* card (`board_policy.md` v3) is one the agent closes itself, straight
    to `done` from a stage before QA. Relay marks that by stamping `verified_by` with the closing
    pane's own signature, so **self-closed is `verified_by` == `implemented_by`** — the one thing
    the done lists fold on, and the reason the equality has to hold exactly when one pane both
    wrote and closed the card, and never otherwise."""

    def setUp(self):
        super().setUp()
        self.card_id = self.create()

    def sign(self, preset, model):
        self.tools.context.preset, self.tools.context.model = preset, model

    def front(self):
        return self.board.card_by_id(self.card_id).front

    def row(self):
        return next(r for r in self.tools.run("board_list", {})["cards"]
                    if r["id"] == self.card_id)

    def test_an_agent_close_from_executing_stamps_verified_by_with_its_own_signature(self):
        self.sign("anthropic", "claude-opus-5-5")
        self.tools.run("board_move_card", {"id": self.card_id, "status": "executing",
                                           "reason": "taking it"})
        self.assertEqual(self.front()["implemented_by"], "anthropic/claude-opus-5-5")
        self.assertIsNone(self.front().get("verified_by"))
        result = self.tools.run("board_move_card", {"id": self.card_id, "status": "done",
                                                   "reason": "landed in abc1234"})
        self.assertNotIn("error", result)
        front = self.front()
        self.assertEqual(front["status"], "done")
        self.assertEqual(front["verified_by"], "anthropic/claude-opus-5-5")
        self.assertEqual(front["verified_by"], front["implemented_by"])   # i.e. self-closed
        self.assertIn("verified_by anthropic/claude-opus-5-5", self.thread_text(self.card_id))
        # And the row the GUI folds on carries both, with nothing else to ask for.
        row = self.row()
        self.assertEqual(row["implemented_by"], "anthropic/claude-opus-5-5")
        self.assertEqual(row["verified_by"], "anthropic/claude-opus-5-5")
        # It is not a broken card: it never entered a QA lane, so there is nothing to report.
        self.assertEqual(self.board.check(), [])

    def test_in_progress_closes_the_same_way(self):
        # `in-progress` is the older spelling of the same stage and is still valid (#3XZV).
        self.sign("glm-coding", "glm-5.3")
        self.tools.run("board_move_card", {"id": self.card_id, "status": "in-progress",
                                           "reason": "starting"})
        self.tools.run("board_move_card", {"id": self.card_id, "status": "done",
                                           "reason": "landed"})
        front = self.front()
        self.assertEqual(front["verified_by"], "glm/glm-5.3")
        self.assertEqual(front["verified_by"], front["implemented_by"])

    def test_a_card_the_agent_created_and_closed_without_claiming_gets_both_stamps(self):
        # A small card written and finished inside one stretch of work never passed through
        # `executing`, so it has no implementer: the close is what gives it one, and the two
        # stamps still match, which is what makes it fold.
        self.sign("openai", "gpt-6-astra")
        self.assertIsNone(self.front().get("implemented_by"))
        self.tools.run("board_move_card", {"id": self.card_id, "status": "done",
                                           "reason": "done in this turn"})
        front = self.front()
        self.assertEqual(front["implemented_by"], "openai/gpt-6-astra")
        self.assertEqual(front["verified_by"], "openai/gpt-6-astra")
        thread = self.thread_text(self.card_id)
        self.assertIn("implemented_by openai/gpt-6-astra", thread)
        self.assertIn("verified_by openai/gpt-6-astra", thread)

    def test_a_card_another_pane_implemented_is_not_self_closed(self):
        # The equality is the whole marker, so a cross-pane close must break it: the implementer
        # keeps its signature and only `verified_by` is this pane's. This card stays an ordinary
        # done card and is never folded away.
        self.sign("openai", "gpt-6-astra")
        self.tools.run("board_move_card", {"id": self.card_id, "status": "executing",
                                           "reason": "taking it"})
        self.sign("anthropic", "claude-opus-5-5")
        self.tools.run("board_move_card", {"id": self.card_id, "status": "done",
                                           "reason": "finished it off"})
        front = self.front()
        self.assertEqual(front["implemented_by"], "openai/gpt-6-astra")
        self.assertEqual(front["verified_by"], "anthropic/claude-opus-5-5")
        self.assertNotEqual(front["verified_by"], front["implemented_by"])

    def test_the_owners_hand_close_from_the_switchboard_stamps_nothing(self):
        # The owner-side tools are `actor="owner"` and never learn a preset or a model
        # (`board_protocol._build` versus `Agent.sign_board`), so a drag onto Done leaves the card
        # unsigned and unfolded. Both halves of the test matter: with no signature *and* with one,
        # in case an owner-side context ever learns its model.
        self.sign("anthropic", "claude-opus-5-5")
        self.tools.run("board_move_card", {"id": self.card_id, "status": "executing",
                                           "reason": "taking it"})
        self.tools.context.actor = T.OWNER_ACTOR
        self.sign(None, None)
        self.tools.run("board_move_card", {"id": self.card_id, "status": "done",
                                           "reason": "moved in the Board"})
        self.assertIsNone(self.front().get("verified_by"))
        self.assertNotIn("verified_by", self.thread_text(self.card_id))
        # Reopened and hand-closed again by an owner who does have a signature: still unstamped.
        self.sign("anthropic", "claude-opus-5-5")
        self.tools.run("board_move_card", {"id": self.card_id, "status": "executing",
                                          "reason": "reopened in the Board"})
        self.tools.run("board_move_card", {"id": self.card_id, "status": "done",
                                           "reason": "moved in the Board"})
        self.assertIsNone(self.front().get("verified_by"))

    def test_dropping_a_card_stamps_nothing(self):
        # A dropped card verifies nothing — it was abandoned, not shipped — here as in the QA
        # branch, where `dropped` has never been stamped either.
        self.sign("anthropic", "claude-opus-5-5")
        self.tools.run("board_move_card", {"id": self.card_id, "status": "executing",
                                           "reason": "taking it"})
        self.tools.run("board_move_card", {"id": self.card_id, "status": "dropped",
                                           "reason": "the owner does not want it"})
        self.assertEqual(self.front()["status"], "dropped")
        self.assertIsNone(self.front().get("verified_by"))

    def test_a_close_with_no_signature_at_all_stamps_nothing(self):
        # A guest CLI writing through the bridge knows neither preset nor model, so there is
        # nothing to stamp and nothing is guessed (#T71W).
        self.sign(None, None)
        self.tools.run("board_move_card", {"id": self.card_id, "status": "done",
                                           "reason": "landed"})
        self.assertIsNone(self.front().get("verified_by"))

    def test_undo_takes_the_stamp_back_with_the_status(self):
        # The stamp rides the status change's own write, so Ctrl+Z on the move puts both back —
        # there is no way to be left `executing` and verified, or `done` and not.
        self.sign("anthropic", "claude-opus-5-5")
        self.tools.run("board_move_card", {"id": self.card_id, "status": "executing",
                                           "reason": "taking it"})
        result = self.tools.run("board_move_card", {"id": self.card_id, "status": "done",
                                                   "reason": "landed"})
        self.assertEqual(self.front()["verified_by"], "anthropic/claude-opus-5-5")
        self.tools.undo(result["write_id"])
        front = self.front()
        self.assertEqual(front["status"], "executing")
        self.assertIsNone(front.get("verified_by"))
        self.assertEqual(front["implemented_by"], "anthropic/claude-opus-5-5")   # the earlier write

    def test_a_qa_lane_close_is_unchanged_and_the_verdict_gate_still_holds(self):
        # The self-close branch is reached only when the card did *not* come out of a QA lane, so
        # nothing here can be used to close a QA card without a verdict.
        self.sign("openai", "gpt-6-astra")
        self.tools.run("board_move_card", {"id": self.card_id, "status": "needs-qa-llm",
                                           "reason": "landed", "evidence": "docs/qa_evidence/x/"})
        self.sign("glm-coding", "glm-5.3")
        refused = self.tools.run("board_move_card", {"id": self.card_id, "status": "done",
                                                     "reason": "passed"})
        self.assertEqual(refused["requires"], "verdict")
        self.assertIsNone(self.front().get("verified_by"))
        current = self.tools.run("board_read", {"id": self.card_id})["hash"]
        self.tools.run("board_update_card", {"id": self.card_id, "base_hash": current,
                                             "append_section": {"heading": "Verdict", "text": "pass"}})
        self.tools.run("board_move_card", {"id": self.card_id, "status": "done",
                                           "reason": "verified"})
        front = self.front()
        self.assertEqual(front["implemented_by"], "openai/gpt-6-astra")
        self.assertEqual(front["verified_by"], "glm/glm-5.3")

    def test_a_self_closed_card_is_no_qa_violation_and_still_gets_a_recommendation(self):
        # Nothing in `board.check()` or in the `qa` block treats "verified by the implementer" as
        # a fault: a self-closed card never entered a QA lane, and the recommendation is advice
        # about who *could* look at it, which is still worth answering.
        patch = unittest.mock.patch.object(
            QA, "availability",
            lambda *a, **k: {"installed_guests": {"codex"}, "keys": {}, "local_models": ()})
        patch.start()
        self.addCleanup(patch.stop)
        self.sign("anthropic", "claude-opus-5-5")
        self.tools.run("board_move_card", {"id": self.card_id, "status": "executing",
                                           "reason": "taking it"})
        self.tools.run("board_move_card", {"id": self.card_id, "status": "done",
                                           "reason": "landed"})
        self.assertEqual(self.board.check(), [])
        block = self.tools.run("board_read", {"id": self.card_id})["qa"]
        self.assertEqual(block["implementer_family"], "anthropic")
        self.assertEqual(block["verified_by"], "anthropic/claude-opus-5-5")
        self.assertEqual(block["verifier_family"], "anthropic")
        self.assertEqual(block["recommended"]["runner"], "guest:codex")


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
            "text": "Executing (abcd1234) · handed to a new terminal pane beside the Board"})
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
        # (a claimed card without a `verify` block is a `missing_verify` warning, #WFRA)
        self.assertEqual([str(p) for p in self.board.check() if p.code != "missing_verify"], [])

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
        # (a claimed card without a `verify` block is a `missing_verify` warning, #WFRA)
        self.assertEqual([str(p) for p in self.board.check() if p.code != "missing_verify"], [])


# ------------------------------------------------------------------------- claim

class ClaimTests(BoardToolsTest):
    """`board_claim` (#R9G7): one call does what the Board's Execute button does."""

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
        # (a claimed card without a `verify` block is a `missing_verify` warning, #WFRA)
        self.assertEqual([str(p) for p in self.board.check() if p.code != "missing_verify"], [])

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
        # (a claimed card without a `verify` block is a `missing_verify` warning, #WFRA)
        self.assertEqual([str(p) for p in self.board.check() if p.code != "missing_verify"], [])

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
        # (a claimed card without a `verify` block is a `missing_verify` warning, #WFRA)
        self.assertEqual([str(p) for p in self.board.check() if p.code != "missing_verify"], [])

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
        # (a claimed card without a `verify` block is a `missing_verify` warning, #WFRA)
        self.assertEqual([str(p) for p in self.board.check() if p.code != "missing_verify"], [])

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
        self.assertIn(f"[Board card #{self.card_id}", block)
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
        # `board_list`'s rows and the pane's rows are one builder (`_row`): the Board draws
        # the chip from this field, and an agent listing the board sees a card is taken.
        rows = self.tools.run("board_list", {"query": ""})["cards"]
        self.assertIsNone(next(r for r in rows if r["id"] == self.card_id)["session"])
        self.tools.run("board_claim", {"id": self.card_id})
        rows = self.tools.run("board_list", {"query": ""})["cards"]
        self.assertEqual(next(r for r in rows if r["id"] == self.card_id)["session"],
                         self.pane_token)

    def test_a_cleanup_cannot_claim_a_card(self):
        # A cleanup is the Board worker tidying the whole board (19.9): no pane of its own,
        # and moving a card to Executing is not tidying.
        self.tools.begin_cleanup("c-1")
        refused = self.tools.run("board_claim", {"id": self.card_id})
        self.assertEqual(refused["code"], "board_refused")
        self.assertIn("cleanup", refused["error"])
        self.assertEqual(self.board.card_by_id(self.card_id).status, "inbox")

    def test_only_a_work_card_is_claimed(self):
        memory = self.tools.run("board_create_card", {
            "status": "active", "type": "memory", "title": "The keyring rule",
            "request": "tests run with RELAY_KEYRING=off"})
        self.assertIn("only a work card",
                      self.tools.run("board_claim", {"id": memory["id"]})["error"])


class ClaimPromptTests(BoardToolsTest):
    def test_the_prompt_names_this_session_and_the_cards_it_holds(self):
        note = T.session_note(self.tools)
        self.assertIn(f"Your Board session: {self.pane_token[:8]}.", note)
        self.assertNotIn("You hold:", note)
        card_id = self.create()
        self.tools.run("board_claim", {"id": card_id})
        self.assertIn(f"You hold: #{card_id}.", T.session_note(self.tools))

    def test_what_this_pane_holds_is_not_in_the_middle_of_the_prompt(self):
        # The claim line is the one part of the Board block that changes while the
        # conversation runs, so `Agent.system_prompt` carries it at the very end (#GMCF): the
        # policy above it has to stay byte-identical for a prompt cache to survive a claim.
        before = T.prompt_section(self.tools)
        self.tools.run("board_claim", {"id": self.create()})
        self.assertEqual(T.prompt_section(self.tools), before)
        self.assertNotIn("Your Board session:", before)
        self.assertNotIn("You hold:", before)

    def test_a_worker_with_no_token_says_nothing_about_a_session(self):
        tools = T.BoardTools(self.board, autonomy="auto",
                             state_path=self.repo / ".relay" / "none.json")
        self.assertEqual(T.session_note(tools), "")
        self.assertNotIn("Your Board session:", T.prompt_section(tools))

    def test_a_board_that_is_off_or_uninitialized_has_no_session_note(self):
        self.assertEqual(T.session_note(None), "")
        self.tools.state = "uninitialized"
        self.assertEqual(T.session_note(self.tools), "")


class ClaimMessageTests(BoardToolsTest):
    """The same claim as a GUI→worker message: `board_claim` (protocol 19.3, 19.19).

    Execute's hand-off (19.10) is this one message now, and its `pane_token` is the pane the card
    was handed to — never the worker's own, since the Board worker has no pane of its own.
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


# ------------------------------------------------------------------------- release

class ReleaseOnCloseTests(BoardToolsTest):
    """Closing a card drops its claim (#R9G7, owner 2026-09-20: "auto-release on done and on closed").

    The release sits in `_move`, the one function both the `board_move_card` tool and the
    Board's `board_move` message go through, so neither can close a card and leave it held.
    """

    class Turns:
        agent = None
        busy = False

        def now_or_later(self, now, later):
            return now()

    def setUp(self):
        super().setUp()
        self.card_id = self.create()
        self.tools.run("board_claim", {"id": self.card_id})
        self.assertEqual(self.board.card_by_id(self.card_id).front["session"], self.pane_token)

    def move(self, status, **extra):
        result = self.tools.run("board_move_card", {"id": self.card_id, "status": status,
                                                   "reason": "closing it", **extra})
        self.assertNotIn("error", result, result)
        return result

    def test_moving_a_claimed_card_to_done_drops_the_session(self):
        result = self.move("done")
        card = self.board.card_by_id(self.card_id)
        self.assertEqual(card.status, "done")
        self.assertNotIn("session", card.front)
        # One write, one thread entry: the move's own line says the claim went with it.
        self.assertIn(f"session {self.pane_token[:8]} released", result["summary"])
        entries = [e for e in self.board.thread(self.card_id) if e.kind == "event"]
        self.assertIn(f"session {self.pane_token[:8]} released", entries[-1].text)
        self.assertEqual(self.tools.claimed, [])
        # (a claimed card without a `verify` block is a `missing_verify` warning, #WFRA)
        self.assertEqual([str(p) for p in self.board.check() if p.code != "missing_verify"], [])

    def test_moving_a_claimed_card_to_dropped_drops_the_session(self):
        self.move("dropped")
        self.assertNotIn("session", self.board.card_by_id(self.card_id).front)
        self.assertEqual(self.tools.claimed, [])

    def test_a_move_that_does_not_close_the_card_keeps_the_session(self):
        # `needs-verification` is work handed on, not work over: the pane that did it stays
        # recorded, which is what `implemented_by` and the row's chip are read from.
        result = self.move("needs-verification", evidence="docs/qa_evidence/2026-09-20-x/")
        self.assertEqual(self.board.card_by_id(self.card_id).front["session"], self.pane_token)
        self.assertNotIn("released", result["summary"])
        self.assertEqual(self.tools.claimed, [self.card_id])

    def test_the_gui_move_message_drops_it_too(self):
        commands = P.BoardCommands(self.Turns(), self.events.append)
        commands.configure(str(self.repo), {"pane_token": self.pane_token})
        self.addCleanup(commands.cards.drop)
        self.events.clear()
        commands.dispatch({"type": "board_move", "id": "m1", "card": self.card_id,
                           "status": "done", "reason": "dragged to Done"})
        written = [e for e in self.events if e["event"] == "board_written"]
        self.assertEqual(len(written), 1, self.events)
        self.assertIn(f"session {self.pane_token[:8]} released", written[0]["summary"])
        card = self.board.card_by_id(self.card_id)
        self.assertEqual(card.status, "done")
        self.assertNotIn("session", card.front)

    def test_a_closed_card_that_was_never_claimed_is_unchanged(self):
        other = self.create(title="Nobody holds this", request="an unclaimed second ask")
        result = self.tools.run("board_move_card", {"id": other, "status": "dropped",
                                                   "reason": "not doing it"})
        self.assertNotIn("released", result["summary"])
        self.assertEqual(result["summary"], "Inbox → Dropped")

    def test_undo_puts_the_claim_back_with_the_status(self):
        # The drop rides the move's own write, so it rides the move's own undo: Ctrl+Z on a
        # mistaken close leaves the card held exactly as it was.
        result = self.move("done")
        self.tools.undo(result["write_id"])
        card = self.board.card_by_id(self.card_id)
        self.assertEqual(card.status, "executing")
        self.assertEqual(card.front["session"], self.pane_token)

    def test_merging_a_claimed_card_away_releases_it(self):
        # `board_merge_cards` writes each source as `dropped` itself, bypassing the move.
        into = self.create(title="The card that survives", request="the surviving ask")
        self.tools.begin_cleanup("c-1")
        result = self.tools.run("board_merge_cards", {"into": into, "cards": [self.card_id],
                                                     "reason": "the same work"})
        self.assertNotIn("error", result, result)
        card = self.board.card_by_id(self.card_id)
        self.assertEqual(card.status, "dropped")
        self.assertNotIn("session", card.front)
        entries = [e for e in self.board.thread(self.card_id) if e.kind == "event"]
        self.assertIn(f"session {self.pane_token[:8]} released", entries[-1].text)

    def test_splitting_a_claimed_card_closed_releases_it(self):
        self.tools.begin_cleanup("c-1")
        result = self.tools.run("board_split_card", {
            "id": self.card_id, "reason": "two unrelated things", "close": True,
            "parts": [{"title": "The first piece", "request": "the first half of the ask"},
                      {"title": "The second piece", "request": "the second half of the ask"}]})
        self.assertNotIn("error", result, result)
        card = self.board.card_by_id(self.card_id)
        self.assertEqual(card.status, "dropped")
        self.assertNotIn("session", card.front)
        entries = [e for e in self.board.thread(self.card_id) if e.kind == "event"]
        self.assertIn(f"session {self.pane_token[:8]} released", entries[-1].text)

    def test_a_split_that_leaves_the_card_open_keeps_the_claim(self):
        self.tools.begin_cleanup("c-1")
        self.tools.run("board_split_card", {
            "id": self.card_id, "reason": "two unrelated things",
            "parts": [{"title": "The first piece", "request": "the first half of the ask"},
                      {"title": "The second piece", "request": "the second half of the ask"}]})
        self.assertEqual(self.board.card_by_id(self.card_id).front["session"], self.pane_token)


class ReleaseClaimsTests(BoardToolsTest):
    """`BoardTools.release_claims`: the pane closed, so what it held is nobody's (#R9G7, 19.19)."""

    other_token = "9c1d77ab-2e40-4f01-8f55-6b0aa1c4de33"

    def other_tools(self):
        tools = T.BoardTools(self.board, autonomy="auto",
                            context=T.ToolContext(actor="agent", model="x/y", pane="9"),
                            state_path=self.repo / ".relay" / "other.json",
                            pane_token=self.other_token)
        tools.begin_turn("t-other")
        return tools

    def test_it_releases_this_panes_executing_cards_and_says_so_on_the_thread(self):
        mine = self.create(title="Mine to do", request="the ask this pane claimed")
        self.tools.run("board_claim", {"id": mine})
        released = self.tools.release_claims("the pane closed")
        self.assertEqual(released, [mine])
        card = self.board.card_by_id(mine)
        self.assertNotIn("session", card.front)
        # Status and assignee stay: the work is in flight, only the pane that held it has gone.
        self.assertEqual(card.status, "executing")
        self.assertEqual(card.front["assignee"], "agent")
        entry = self.board.thread(mine)[-1]
        self.assertEqual(entry.kind, "event")
        self.assertEqual(entry.text.splitlines()[0],
                         f"Released ({self.pane_token[:8]}) · the pane closed")
        self.assertEqual(self.tools.claimed, [])
        # (a claimed card without a `verify` block is a `missing_verify` warning, #WFRA)
        self.assertEqual([str(p) for p in self.board.check() if p.code != "missing_verify"], [])

    def test_an_in_progress_card_is_released_as_well(self):
        # `in-progress` is the same thing on a board configured before the stage statuses.
        card_id = self.create(title="On an older board", request="a card in the older column")
        self.tools.run("board_claim", {"id": card_id})
        self.tools.run("board_move_card", {"id": card_id, "status": "in-progress",
                                           "reason": "the older column"})
        self.assertEqual(self.tools.release_claims("the pane closed"), [card_id])
        self.assertNotIn("session", self.board.card_by_id(card_id).front)

    def test_another_sessions_claim_is_left_alone(self):
        theirs = self.create(title="Theirs to do", request="the ask the other pane claimed")
        self.other_tools().run("board_claim", {"id": theirs})
        self.assertEqual(self.tools.release_claims("the pane closed"), [])
        self.assertEqual(self.board.card_by_id(theirs).front["session"], self.other_token)
        # Nothing was appended to a card this pane never held.
        self.assertNotIn("Released", self.thread_text(theirs))

    def test_a_card_that_is_not_executing_keeps_the_session_as_the_record(self):
        # Past `executing` the field is history, not a claim (19.19), and history is not dropped:
        # `needs-verification` and a closed card both record which pane did the work.
        landed = self.create(title="Already landed", request="the ask that is landed")
        self.tools.run("board_claim", {"id": landed})
        self.tools.run("board_move_card", {"id": landed, "status": "needs-verification",
                                           "reason": "landed", "evidence": "docs/qa_evidence/x/"})
        self.assertEqual(self.tools.release_claims("the pane closed"), [])
        self.assertEqual(self.board.card_by_id(landed).front["session"], self.pane_token)

    def test_it_releases_every_card_this_pane_holds(self):
        first = self.create(title="The first one", request="the first ask this pane claimed")
        second = self.create(title="The second one", request="a second, different ask claimed here")
        for card_id in (first, second):
            self.tools.run("board_claim", {"id": card_id})
        self.assertEqual(sorted(self.tools.release_claims("the pane closed")),
                         sorted([first, second]))
        for card_id in (first, second):
            self.assertNotIn("session", self.board.card_by_id(card_id).front)

    def test_a_worker_with_no_pane_token_does_nothing(self):
        card_id = self.create()
        self.tools.run("board_claim", {"id": card_id})
        tools = T.BoardTools(self.board, autonomy="auto",
                            context=T.ToolContext(actor="agent", model="x/y", pane="9"),
                            state_path=self.repo / ".relay" / "none.json")
        self.assertEqual(tools.release_claims("the pane closed"), [])
        self.assertEqual(self.board.card_by_id(card_id).front["session"], self.pane_token)

    def test_a_card_it_cannot_read_is_skipped_and_the_rest_are_released(self):
        # A closing pane has 1.5 s and one job: one bad file must not cost the other cards their
        # release.  `broken.md` sorts before the claimed card's folder, so the failure comes first.
        mine = self.create(title="Mine to do", request="the ask this pane claimed")
        self.tools.run("board_claim", {"id": mine})
        (self.root / "features" / "broken.md").mkdir(parents=True)
        with unittest.mock.patch("sys.stderr") as err:
            released = self.tools.release_claims("the pane closed")
        (self.root / "features" / "broken.md").rmdir()
        self.assertEqual(released, [mine])
        self.assertNotIn("session", self.board.card_by_id(mine).front)
        self.assertIn("broken.md", "".join(str(c.args[0]) for c in err.write.call_args_list
                                          if c.args))

    def test_a_board_with_no_cards_is_a_no_op(self):
        self.assertEqual(self.tools.release_claims("the pane closed"), [])


class ReleaseOnPaneCloseTests(BoardToolsTest):
    """`BoardCommands.release_claims`: what the worker's `shutdown` and a `set_board` call (19.19)."""

    class Turns:
        agent = None
        busy = False

        def now_or_later(self, now, later):
            return now()

    def commands(self, request=None):
        commands = P.BoardCommands(self.Turns(), self.events.append)
        commands.configure(str(self.repo), request or {"pane_token": self.pane_token})
        self.addCleanup(commands.cards.drop)
        return commands

    def test_the_workers_release_goes_through_the_owner_side_tools(self):
        card_id = self.create()
        commands = self.commands()
        commands.tools.context.actor = "agent"
        commands.tools.run("board_claim", {"id": card_id})
        self.assertEqual(commands.release_claims("the pane closed"), [card_id])
        self.assertNotIn("session", self.board.card_by_id(card_id).front)

    def test_a_worker_with_no_board_releases_nothing_and_does_not_raise(self):
        commands = P.BoardCommands(self.Turns(), self.events.append)
        self.assertEqual(commands.release_claims("the pane closed"), [])

    def test_a_failing_release_never_fails_the_shutdown(self):
        commands = self.commands()
        with unittest.mock.patch.object(commands.tools, "release_claims",
                                        side_effect=OSError("disk gone")):
            self.assertEqual(commands.release_claims("the pane closed"), [])

    def test_set_board_to_another_project_releases_what_this_pane_held_here(self):
        # Protocol 19.11: the tools holding the claim are thrown away, so the claim goes first.
        card_id = self.create()
        commands = self.commands()
        commands.tools.context.actor = "agent"
        commands.tools.run("board_claim", {"id": card_id})
        elsewhere = self.repo / "other"
        (elsewhere / "issues").mkdir(parents=True)
        (elsewhere / "issues" / B.BOARD_CONFIG).write_text(self.config, encoding="utf-8")
        commands.set_board({"id": "s1", "board": {"dir": str(elsewhere / "issues")}})
        self.assertEqual(commands.tools.board.root, elsewhere / "issues")
        card = self.board.card_by_id(card_id)
        self.assertNotIn("session", card.front)
        self.assertEqual(card.status, "executing")
        self.assertEqual(self.board.thread(card_id)[-1].text.splitlines()[0],
                         f"Released ({self.pane_token[:8]}) · the pane moved to another project")

    def test_pointing_the_worker_at_the_same_board_again_keeps_the_claim(self):
        card_id = self.create()
        commands = self.commands()
        commands.tools.context.actor = "agent"
        commands.tools.run("board_claim", {"id": card_id})
        commands.set_board({"id": "s2", "board": {"dir": str(self.root)}})
        self.assertEqual(self.board.card_by_id(card_id).front["session"], self.pane_token)


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
    def test_the_turn_threshold_warns_once_and_creation_continues(self):
        self.tools.limits["max_creates_per_turn"] = 2
        self.create(title="One", request="the first distinct ask")
        self.create(title="Two", request="a second and quite different ask")
        result = self.tools.run("board_create_card", {
            "tab": "features", "status": "inbox", "title": "Three",
            "request": "a third ask, unrelated to the others"})
        self.assertNotIn("error", result)
        self.assertEqual(len(result["warnings"]), 1)
        self.assertIn("this turn", result["warnings"][0])
        activity = [e for e in self.events if e.get("event") == "board_activity"]
        self.assertIn("⚠", activity[-1]["summary"])
        fourth = self.tools.run("board_create_card", {
            "tab": "features", "status": "inbox", "title": "Four",
            "request": "a fourth request about something else again"})
        self.assertNotIn("error", fourth)
        self.assertNotIn("warnings", fourth)                  # once per crossing, not per card
        self.assertEqual(len(self.board.cards()), 4)

    def test_the_turn_warning_resets_on_the_next_turn(self):
        self.tools.limits["max_creates_per_turn"] = 1
        self.create(title="One", request="the first distinct ask")
        self.assertIn("warnings", self.tools.run("board_create_card", {
            "tab": "features", "status": "inbox", "title": "Two",
            "request": "a second ask"}))
        self.tools.begin_turn("t-2")
        self.assertNotIn("warnings", self.tools.run("board_create_card", {
            "tab": "features", "status": "inbox", "title": "Three",
            "request": "a third and quite different ask"}))
        self.assertEqual(len(self.board.cards()), 3)

    def test_other_writes_are_capped_per_turn(self):
        card_id = self.create()
        self.tools.limits["max_writes_per_turn"] = 2      # the create already used one
        self.tools.run("board_comment", {"id": card_id, "kind": "note", "text": "one"})
        result = self.tools.run("board_comment", {"id": card_id, "kind": "note", "text": "two"})
        self.assertEqual(result["code"], "board_rate_limited")
        # The creation, the one note that fitted, and the inbox → Discussing move that note
        # earned (#3XZV). The refused second note wrote nothing.
        self.assertEqual(len(self.board.thread(card_id)), 3)

    def test_the_hour_threshold_is_shared_across_panes_and_only_warns(self):
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
        self.assertNotIn("error", result)
        self.assertEqual(len(result["warnings"]), 1)
        self.assertIn("last hour", result["warnings"][0])
        self.assertEqual(len(self.board.cards()), 3)

    def test_an_hour_later_the_hourly_warning_can_fire_again(self):
        now = [1000.0]
        tools = T.BoardTools(self.board, autonomy="auto", clock=lambda: now[0],
                             state_path=self.repo / ".relay" / "board-rate.json")
        tools.limits["max_creates_per_hour"] = 1
        tools.begin_turn("t-1")
        tools.run("board_create_card", {"tab": "features", "status": "inbox", "title": "One",
                                        "request": "the first distinct ask"})
        over = tools.run("board_create_card", {"tab": "features", "status": "inbox", "title": "Two",
                                               "request": "a second and quite different ask"})
        self.assertIn("warnings", over)
        now[0] += 3601
        tools.begin_turn("t-2")
        fresh = tools.run("board_create_card", {
            "tab": "features", "status": "inbox", "title": "Three",
            "request": "a third request about something unrelated"})
        self.assertNotIn("error", fresh)
        self.assertNotIn("warnings", fresh)

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
        self.assertEqual(activity[0]["model"], "anthropic/claude-opus-5-5")
        self.assertEqual(activity[0]["turn_id"], "t-1")
        self.assertEqual(activity[0]["undo_seconds"], T.UNDO_SECONDS)
        self.assertTrue(activity[0]["path"].startswith("issues/"))
        self.assertTrue(any(e["event"] == "board_changed" for e in self.events))

    def test_a_refused_write_emits_nothing(self):
        self.tools.run("board_comment", {"id": "AAAA", "kind": "note", "text": "x"})
        self.assertEqual(self.events, [])

    def test_a_preview_is_produced_for_every_tool(self):
        for name in T.TOOL_NAMES:
            self.assertTrue(self.tools.preview(name, {"id": "K7Q2"}).startswith("BOARD "))


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
    """The tools only a `board_cleanup` turn gets (protocol 19.9): split and sections.

    `board_merge_cards` was one of these until #GREM (owner decision, 2026-09-25): a pane
    folding the duplicate it just found mid-delivery is board hygiene, like `board_move_card`,
    so it moved into the ordinary set.
    """

    def test_they_are_refused_and_unadvertised_outside_a_cleanup(self):
        offered = {s["function"]["name"] for s in self.tools.tool_specs()}
        self.assertEqual(offered, set(T.TOOL_NAMES))
        for name in T.CLEANUP_TOOL_NAMES:
            result = self.tools.run(name, {"reason": "x"})
            self.assertEqual(result.get("code"), "board_refused", name)
        self.tools.begin_cleanup("c-1")
        offered = {s["function"]["name"] for s in self.tools.tool_specs()}
        self.assertEqual(offered, set(T.TOOL_NAMES) | set(T.CLEANUP_TOOL_NAMES))

    def test_a_pane_merges_a_duplicate_without_a_cleanup(self):
        keep = self.create("Voice mode", "add voice transcribe mode")
        gone = self.create("Dictation", "let me dictate into the box")
        offered = {s["function"]["name"] for s in self.tools.tool_specs()}
        self.assertIn("board_merge_cards", offered)
        self.assertNotIn("board_split_card", offered)
        result = self.tools.run("board_merge_cards",
                                {"into": keep, "cards": [gone], "reason": "the same request"})
        self.assertNotIn("error", result, result)
        self.assertIsNotNone(self.board.card_by_id(gone))          # nothing was deleted
        self.assertEqual(self.board.card_by_id(gone).status, "dropped")
        self.assertIn("let me dictate into the box", self.board.card_by_id(keep).body)
        self.assertIn("merged this card into", self.thread_text(gone))

    def test_a_cleanup_raises_the_per_turn_ceilings(self):
        self.assertEqual(self.tools.limit("max_writes_per_turn"), 100)
        self.tools.begin_cleanup("c-1")
        self.assertEqual(self.tools.limit("max_writes_per_turn"),
                         T.CLEANUP_LIMITS["max_writes_per_turn"])
        self.tools.end_cleanup("done")
        self.assertEqual(self.tools.limit("max_writes_per_turn"), 100)

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
        self.assertIn("anthropic/claude-opus-5-5", text)

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

    def test_a_card_turn_does_not_move_a_consoles_tool_list(self):
        """Card #CTRN: `CardScope.tool_specs` is gone — what a mode may call is refused instead.

        This is the hole the decision is really about. A card console opens a `CardScope` for
        the length of each turn, and while "is this a console" was read off that same slot, the
        board's half of the tool list changed under the turn — a re-prefill of every cached
        request below it, for a rule that is enforced at call time anyway. `allows` and
        `refusal` are untouched, which is what the cases above check.
        """
        card_id = self.create()
        self.tools.begin_console()
        console = [s["function"]["name"] for s in self.tools.tool_specs()]
        self.assertIn("board_merge_cards", console)
        for mode in ("discuss", "plan"):
            self.tools.begin_card_turn(mode, card_id)
            self.assertEqual([s["function"]["name"] for s in self.tools.tool_specs()], console, mode)
            self.assertTrue(self.tools.handles("search_files"))
            self.tools.end_card_turn()
            self.assertEqual([s["function"]["name"] for s in self.tools.tool_specs()], console, mode)
            self.assertTrue(self.tools.handles("search_files"))
        # And the stage rule is where it always was: on the scope, refused when it is called.
        self.tools.begin_card_turn("plan", card_id)
        refused = self.tools.run("board_move_card", {"id": card_id, "status": "ready", "reason": "r"})
        self.assertEqual(refused["code"], "board_mode_refused")
        self.assertFalse(T.CardScope("plan", card_id).allows("run_command"))
        self.assertFalse(hasattr(T.CardScope("plan", card_id), "tool_specs"))


class RefineTurnScopeTests(BoardToolsTest):
    """#6W9X: a Refine turn writes links.related, known labels, a missing `## Done means` and a
    note on its own card, and nothing else — and it moves nothing, not even by commenting."""

    def read_hash(self, card_id):
        return self.tools.run("board_read", {"id": card_id})["hash"]

    def links(self, card_id):
        return dict(self.board.card_by_id(card_id).front.get("links") or {})

    def test_refine_writes_related_links_known_labels_and_a_missing_done_means(self):
        other = self.create(title="Clickable paths", request="clicking a path opens a pane",
                            labels=["feature", "gui"])
        mine = self.create(not_duplicate_of=[other])
        self.tools.begin_card_turn("refine", mine)
        links = self.links(mine)
        links["related"] = [other]
        result = self.tools.run("board_update_card", {
            "id": mine, "base_hash": self.read_hash(mine),
            "fields": {"links": links, "labels": ["feature", "gui"]}})
        self.assertNotIn("error", result, result)
        result = self.tools.run("board_update_card", {
            "id": mine, "base_hash": self.read_hash(mine),
            "replace_section": {"heading": "Done means", "text": "Voice is typed into the box."}})
        self.assertNotIn("error", result, result)
        card = self.board.card_by_id(mine)
        self.assertEqual(card.front["links"]["related"], [other])
        self.assertEqual(card.front["labels"], ["feature", "gui"])
        self.assertIn("## Done means\nVoice is typed into the box.", card.body)

    def test_refine_leaves_an_existing_done_means_alone(self):
        card_id = self.create()
        self.tools.begin_card_turn("plan", card_id)
        self.tools.run("board_update_card", {"id": card_id, "base_hash": self.read_hash(card_id),
                                             "replace_section": {"heading": "Done means", "text": "x"}})
        self.tools.begin_card_turn("refine", card_id)
        refused = self.tools.run("board_update_card", {
            "id": card_id, "base_hash": self.read_hash(card_id),
            "replace_section": {"heading": "Done means", "text": "mine instead"}})
        self.assertEqual(refused["code"], "board_mode_refused")

    def test_refine_cannot_touch_the_issue_the_plan_the_title_or_invent_a_label(self):
        card_id = self.create()
        self.tools.begin_card_turn("refine", card_id)
        for patch in ({"title": "Something else"},
                      {"replace_section": {"heading": "Issue", "text": "new words"}},
                      {"replace_section": {"heading": "Plan", "text": "1. do it"}},
                      {"append_section": {"heading": "Plan", "text": "1. do it"}},
                      {"fields": {"labels": ["feature", "brandnewword"]}},
                      {"fields": {"assignee": "me"}}):
            result = self.tools.run("board_update_card",
                                    {"id": card_id, "base_hash": self.read_hash(card_id), **patch})
            self.assertEqual(result.get("code"), "board_mode_refused", patch)
        card = self.board.card_by_id(card_id)
        self.assertEqual(card.title, "Voice transcription mode")
        self.assertNotIn("## Plan", card.body)
        self.assertIn("add voice transcribe mode", card.body)

    def test_refine_may_change_only_the_related_key_of_links(self):
        card_id = self.create()
        self.tools.begin_card_turn("refine", card_id)
        partial = {"related": []}                       # would drop commits, evidence, plans
        changed = dict(self.links(card_id), commits=["abc1234"])
        for links in (partial, changed):
            refused = self.tools.run("board_update_card", {
                "id": card_id, "base_hash": self.read_hash(card_id), "fields": {"links": links}})
            self.assertEqual(refused.get("code"), "board_mode_refused", links)
            self.assertIn("related", refused["error"])

    def test_refine_touches_no_other_card_and_moves_nothing(self):
        mine = self.create()
        other = self.create(title="Clickable paths", request="clicking a path opens a pane",
                            not_duplicate_of=[mine])
        self.tools.begin_card_turn("refine", mine)
        self.assertEqual(self.tools.run("board_comment", {"id": other, "kind": "note", "text": "hi"})["code"],
                         "board_mode_refused")
        self.assertEqual(self.tools.run("board_update_card", {
            "id": other, "base_hash": self.read_hash(other),
            "replace_section": {"heading": "Done means", "text": "x"}})["code"], "board_mode_refused")
        self.assertEqual(self.tools.run("board_move_card", {"id": mine, "status": "ready", "reason": "r"})["code"],
                         "board_mode_refused")
        self.assertEqual(self.tools.run("board_create_card", {
            "tab": "features", "status": "inbox", "title": "T", "request": "r"})["code"], "board_mode_refused")
        # Its note lands, and — unlike any other first comment — leaves the inbox card in the inbox.
        noted = self.tools.run("board_comment", {"id": mine, "kind": "note", "text": "Same as: none found."})
        self.assertNotIn("error", noted, noted)
        self.assertEqual(self.board.card_by_id(mine).status, "inbox")

    def test_refine_refusals_name_the_mode_and_the_right_buttons(self):
        card_id = self.create()
        scope = self.tools.begin_card_turn("refine", card_id)
        self.assertIn("Refine turn", scope.refusal("write_file"))
        refused = self.tools.run("board_update_card", {
            "id": card_id, "base_hash": self.read_hash(card_id),
            "replace_section": {"heading": "Issue", "text": "x"}})
        self.assertIn("Discuss", refused["error"])
        self.assertIn("Plan", refused["error"])

    def test_the_refine_brief_searches_closed_cards_and_never_touches_the_issue(self):
        brief = T.card_brief("refine")
        for word in ("done", "dropped", "Done means", "links.related", "## Issue", "## Plan"):
            self.assertIn(word, brief)
        self.assertLess(brief.index("Search the whole board"), brief.index("Write, in this order"))


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
    """A project with no Board yet (protocol 19.12): one tool, one line, nothing on disk."""

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
        self.assertIn("this project has no Board yet", note)
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
        self.assertEqual(files[0], ".board/board.yaml")
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
        # `board/` unless the pane's `board.folder` names an older spelling (owner, 2026-09-21).
        self.assertEqual(T.named_board_root(self.dir), self.dir / ".board")
        self.assertEqual(T.named_board_root(self.dir, "board"), self.dir / "board")
        self.assertEqual(T.named_board_root(self.dir, "switchboard"), self.dir / "switchboard")
        self.assertEqual(T.named_board_root(self.dir, ".switchboard"), self.dir / ".switchboard")
        self.assertEqual(T.named_board_root(self.dir, "nonsense"), self.dir / ".board")
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
        current = self.board("both", "board")
        self.assertEqual(T.find_board_root(self.dir / "both"), current)
        self.assertEqual(T.named_board_root(self.dir / "both"), current)

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


class TestsToolsTest(BoardToolsTest):
    """`tests_check` and `tests_run`, the two agent-facing tools of protocol 31 (#7BM4).

    What they answer over is covered end to end in `tests/test_tests_protocol.py`, with a fake
    `ctest` and a real unittest module; here it is the registration, the card section and the
    refusals a model can hit by typing.
    """

    def test_both_tools_are_offered_to_a_pane_and_to_the_page_agent(self):
        names = [spec["function"]["name"] for spec in self.tools.tool_specs()]
        self.assertIn("tests_check", names)
        self.assertIn("tests_run", names)
        self.assertTrue(self.tools.handles("tests_check"))
        scope = self.tools.begin_chat_turn()
        self.assertTrue(scope.allows("tests_check"))
        self.assertTrue(scope.allows("tests_run"))
        self.tools.end_chat_turn()

    def test_agent_sections_is_the_card_body_schema(self):
        # #Z4HR: one section per workflow stage, minus the owner's own `## Issue`; the old
        # thirteen-name allowlist (`implementer check`, `findings`, ...) is gone.
        self.assertEqual(T.AGENT_SECTIONS, frozenset(B.CARD_SECTIONS) - {"issue"})
        self.assertNotIn("implementer check", T.AGENT_SECTIONS)
        self.assertNotIn("issue", T.AGENT_SECTIONS)

    def test_rewriting_a_heading_outside_the_schema_logs_the_rewrite(self):
        # No bulk migration (#Z4HR): an old card converts when it is next touched, and the old
        # heading is owner text, so the rewrite lands in the thread.
        card_id = self.create()
        current = self.tools.run("board_read", {"id": card_id})["hash"]
        self.tools.run("board_update_card", {"id": card_id, "base_hash": current,
                                             "append_section": {"heading": "Implementer check",
                                                                "text": "looked at it"}})
        current = self.tools.run("board_read", {"id": card_id})["hash"]
        result = self.tools.run("board_update_card", {
            "id": card_id, "base_hash": current,
            "replace_section": {"heading": "Implementer check", "text": "ran the tests"}})
        self.assertEqual(result["logged_rewrites"], ["## Implementer check"])

    def test_tests_is_a_section_the_agent_writes_without_a_rewrite_entry(self):
        self.assertIn("tests", T.AGENT_SECTIONS)
        card_id = self.create()
        card_hash = self.tools.run("board_read", {"id": card_id})["hash"]
        result = self.tools.run("board_update_card", {
            "id": card_id, "base_hash": card_hash,
            "append_section": {"heading": "Tests", "text": "- `ctest -R voice`"}})
        self.assertEqual(result["logged_rewrites"], [])
        self.assertIn("## Tests", self.board.card_by_id(card_id).body)

    def test_a_card_with_no_tests_section_gets_one_sentence_and_one_action(self):
        card_id = self.create()
        result = self.tools.run("tests_check", {"card": card_id})
        self.assertNotIn("error", result)
        self.assertEqual(result["card"], card_id)
        self.assertEqual([f["verdict"] for f in result["findings"]], ["no-tests"])
        self.assertIn("has no `## Tests` section", result["text"])
        self.assertEqual(result["actions"], ["Add the tests this card's commits touched"])

    def test_a_listed_test_that_is_not_in_the_project_is_named(self):
        card_id = self.create()
        card_hash = self.tools.run("board_read", {"id": card_id})["hash"]
        self.tools.run("board_update_card", {
            "id": card_id, "base_hash": card_hash,
            "append_section": {"heading": "Tests", "text": "- `ctest -R nosuchtest`"}})
        result = self.tools.run("tests_check", {"card": card_id})
        # #PR4Q: a retired check is `not-applicable` with an advisory notice beside it, and the
        # agent's tool answers with the same four statuses the card page shows.
        self.assertEqual([f["verdict"] for f in result["findings"]], ["retired"])
        self.assertEqual([row["status"] for row in result["statuses"]], ["not-applicable"])
        self.assertIn("nosuchtest", result["text"])

    def test_tests_check_needs_a_card_that_exists(self):
        self.assertIn("error", self.tools.run("tests_check", {"card": "ZZZ9"}))
        self.assertEqual(self.tools.run("tests_check", {})["code"], "board_refused")

    def test_tests_run_refuses_an_empty_list_and_too_many_ids(self):
        empty = self.tools.run("tests_run", {"ids": []})
        self.assertIn("no way to run the whole suite", empty["error"])
        many = self.tools.run("tests_run", {"ids": [f"ctest:t{n}" for n in range(60)]})
        self.assertEqual(many["code"], "tests_refused")
        self.assertIn("at most 50", many["error"])

    def test_tests_run_refuses_ids_that_name_nothing_runnable(self):
        result = self.tools.run("tests_run", {"ids": ["manual: docs/qa_evidence/2026-09-20-x/"]})
        self.assertEqual(result["code"], "tests_refused")
        self.assertIn("recorded by hand", result["error"])

    def test_the_handlers_are_pointed_at_this_board(self):
        commands = self.tools._tests()
        self.assertEqual(Path(commands.project), self.repo)
        self.assertEqual(Path(commands.board_root), self.root)
        self.assertIs(self.tools._tests(), commands)


# --------------------------------------------------------------- signals (protocol 32, #AQ6X)

class SignalToolTests(BoardToolsTest):
    """`board_signals`: the list, the four writes, their refusals, and the release on pane close.

    The executions are written straight into the history store — this is the tool's behaviour, not
    the fold's, which `tests/test_signals.py` covers — so no test here runs a test.
    """

    def setUp(self):
        super().setUp()
        from relay_core import signals as S
        from relay_core import test_history as H
        self.S, self.H = S, H
        self.store = H.default_path(self.repo, self.root)
        self.signal_log = S.default_path(self.repo, self.root)

    def fail(self, key="ctest:panelayout", days=(1, 2), message="AssertionError: 3 != 17"):
        """Two consecutive failing executions of `key`: enough to open its signal."""
        rows = [self.H.Execution(ts=f"2026-09-{day:02d}T10:00:00Z", id=key, result="fail",
                                 runner=key.split(":")[0], run_id=f"r{day}", commit=f"c{day}",
                                 message=message, excerpt=message + "\n  at Pane::layout")
                for day in days]
        self.H.append(rows, self.store)
        return rows

    def run_event(self, run_id, session):
        self.S.append_event({"ts": "2026-09-01T10:00:00Z", "action": "run", "run_id": run_id,
                             "session": session}, self.signal_log)

    def actions(self):
        return [(e["action"], e.get("key")) for e in self.S.read_events(self.signal_log)]

    # ---- list ----------------------------------------------------------------
    def test_a_board_with_no_signals_lists_none_in_a_sentence(self):
        result = self.tools.run("board_signals", {"action": "list"})
        self.assertNotIn("error", result, result)
        self.assertEqual(result["open"], [])
        self.assertEqual((result["pending_count"], result["dismissed_count"]), (0, 0))
        self.assertIn("every check", result["text"])

    def test_the_list_carries_the_open_signal_and_its_excerpt(self):
        self.fail()
        result = self.tools.run("board_signals", {"action": "list"})
        self.assertEqual([row["key"] for row in result["open"]], ["ctest:panelayout"])
        self.assertEqual(result["open"][0]["count"], 2)
        self.assertIn("ctest:panelayout", result["text"])
        self.assertIn("AssertionError", result["text"])

    def test_one_failure_is_pending_and_not_listed_as_open(self):
        self.fail(days=(1,))
        result = self.tools.run("board_signals", {"action": "list"})
        self.assertEqual(result["open"], [])
        self.assertEqual(result["pending_count"], 1)

    # ---- refusals ------------------------------------------------------------
    def test_an_unknown_action_and_an_unknown_argument_are_refused(self):
        self.assertIn("error", self.tools.run("board_signals", {"action": "resolve"}))
        self.assertIn("error", self.tools.run("board_signals", {"action": "list", "wat": 1}))

    def test_every_action_but_list_needs_a_key(self):
        for action in ("claim", "release", "dismiss", "promote"):
            with self.subTest(action=action):
                result = self.tools.run("board_signals", {"action": action})
                self.assertIn("needs `key`", result["error"])

    def test_a_key_with_no_open_signal_is_refused_by_name(self):
        result = self.tools.run("board_signals", {"action": "claim", "key": "ctest:nothing"})
        self.assertEqual(result["code"], "signal_not_found")
        self.assertEqual(result["key"], "ctest:nothing")

    # ---- claim ---------------------------------------------------------------
    def test_a_claim_writes_this_panes_token_and_says_what_closes_it(self):
        self.fail()
        result = self.tools.run("board_signals", {"action": "claim", "key": "ctest:panelayout"})
        self.assertTrue(result["claimed"])
        self.assertEqual(result["session"], self.pane_token)
        self.assertIn("2 consecutive passing executions", result["text"])
        self.assertEqual(self.actions(), [("claim", "ctest:panelayout")])
        listed = self.tools.run("board_signals", {"action": "list"})
        self.assertEqual(listed["open"][0]["session"], self.pane_token)

    def test_a_signal_another_session_holds_is_refused_and_force_takes_it(self):
        self.fail()
        other = T.BoardTools(self.board, autonomy="auto",
                             context=T.ToolContext(actor="agent", model="x/y", pane="9"),
                             state_path=self.repo / ".relay" / "other.json",
                             pane_token="9c1d77ab-2e40-4f01-8f55-6b0aa1c4de33")
        other.begin_turn("t-other")
        self.assertNotIn("error", other.run("board_signals", {"action": "claim",
                                                              "key": "ctest:panelayout"}))
        refused = self.tools.run("board_signals", {"action": "claim", "key": "ctest:panelayout"})
        self.assertEqual(refused["code"], "board_claimed_elsewhere")
        self.assertEqual(refused["session"], "9c1d77ab")
        self.assertEqual(refused["key"], "ctest:panelayout")
        taken = self.tools.run("board_signals", {"action": "claim", "key": "ctest:panelayout",
                                                "force": True})
        self.assertEqual(taken["session"], self.pane_token)

    # ---- release -------------------------------------------------------------
    def test_a_release_frees_it_and_gave_up_files_the_card(self):
        self.fail()
        self.tools.run("board_signals", {"action": "claim", "key": "ctest:panelayout"})
        plain = self.tools.run("board_signals", {"action": "release", "key": "ctest:panelayout",
                                                 "reason": "the user asked for something else"})
        self.assertTrue(plain["released"])
        self.assertNotIn("card", plain)
        self.tools.run("board_signals", {"action": "claim", "key": "ctest:panelayout"})
        gave_up = self.tools.run("board_signals", {"action": "release", "key": "ctest:panelayout",
                                                  "reason": self.S.GAVE_UP})
        self.assertTrue(gave_up["released"])
        self.assertTrue(gave_up["promoted"])
        card = self.board.card_by_id(gave_up["card"])
        self.assertEqual(card.front["links"]["signal"], "ctest:panelayout")

    # ---- dismiss -------------------------------------------------------------
    def test_an_agent_dismissal_is_limited_to_two_reasons_a_comment_and_a_week(self):
        self.fail()
        future = (datetime.now(timezone.utc) + timedelta(days=3)).strftime("%Y-%m-%d")
        ok = self.tools.run("board_signals", {"action": "dismiss", "key": "ctest:panelayout",
                                              "reason": "environmental", "comment": "socket path",
                                              "until": future})
        self.assertTrue(ok["dismissed"])
        self.assertIn("blocks no card", ok["text"])
        self.assertEqual(self.tools.run("board_signals", {"action": "list"})["dismissed_count"], 1)
        far = (datetime.now(timezone.utc) + timedelta(days=30)).strftime("%Y-%m-%d")
        for args, code in (
                ({"reason": "wont-fix", "comment": "c", "until": future}, "signal_reason"),
                ({"reason": "environmental", "comment": "", "until": future}, "signal_comment"),
                ({"reason": "environmental", "comment": "c"}, "signal_until"),
                ({"reason": "environmental", "comment": "c", "until": far}, "signal_until")):
            with self.subTest(code=code):
                refused = self.tools.run("board_signals",
                                         {"action": "dismiss", "key": "ctest:panelayout", **args})
                self.assertEqual(refused["code"], code, refused)

    # ---- promote -------------------------------------------------------------
    def test_a_promotion_writes_a_bug_card_with_the_excerpt_and_the_signal_section(self):
        self.fail()
        result = self.tools.run("board_signals", {"action": "promote", "key": "ctest:panelayout"})
        self.assertTrue(result["promoted"])
        card = self.board.card_by_id(result["card"])
        self.assertEqual(card.type, "work")
        self.assertEqual(card.status, "inbox")
        self.assertEqual(self.tools._tab_of(card), "bugs")
        self.assertIn(self.S.SIGNAL_LABEL, card.front["labels"])
        self.assertIn("bug", card.front["labels"])
        self.assertEqual(card.front["links"]["signal"], "ctest:panelayout")
        self.assertIn("AssertionError: 3 != 17", B.section_text(card.body, B.ISSUE_HEADING))
        section = B.section_text(card.body, self.S.SIGNAL_HEADING)
        self.assertIn("`ctest:panelayout`", section)
        self.assertIn("closing this card does not close the signal", section)
        # (a claimed card without a `verify` block is a `missing_verify` warning, #WFRA)
        self.assertEqual([str(p) for p in self.board.check() if p.code != "missing_verify"], [])
        # …and the signal now names the card, so a second promotion is a no-op.
        again = self.tools.run("board_signals", {"action": "promote", "key": "ctest:panelayout"})
        self.assertFalse(again["promoted"])
        self.assertEqual(again["card"], result["card"])

    def test_the_sixth_open_promoted_card_is_refused_so_the_backlog_does_not_fill(self):
        for index in range(self.S.MAX_PROMOTED_OPEN):
            key = f"ctest:k{index}"
            self.fail(key=key, message=f"kind{index} broke")
            self.assertTrue(self.tools.run("board_signals",
                                           {"action": "promote", "key": key})["promoted"])
        self.fail(key="ctest:one-too-many", message="kindX broke")
        refused = self.tools.run("board_signals", {"action": "promote",
                                                   "key": "ctest:one-too-many"})
        self.assertEqual(refused["code"], "signal_promote_cap")
        self.assertEqual(refused["limit"], self.S.MAX_PROMOTED_OPEN)

    def test_the_signal_section_is_rewritten_in_place_not_stacked_up(self):
        self.fail()
        promoted = self.tools.run("board_signals", {"action": "promote", "key": "ctest:panelayout"})
        # The claim changes what the section says, so the write refreshes the card itself: the
        # machine's paragraph on a card a person reads is never a state behind its signal.
        self.tools.run("board_signals", {"action": "claim", "key": "ctest:panelayout"})
        body = self.board.card_by_id(promoted["card"]).body
        self.assertEqual(body.count(f"## {self.S.SIGNAL_HEADING}"), 1)
        self.assertIn("claimed by session", body)
        # Idempotent: nothing to write a second time.
        signals = self.S.state(self.repo, self.root)
        self.assertFalse(self.S.rewrite_section(self.board, signals["ctest:panelayout"]))
        # (a claimed card without a `verify` block is a `missing_verify` warning, #WFRA)
        self.assertEqual([str(p) for p in self.board.check() if p.code != "missing_verify"], [])

    # ---- the gate ------------------------------------------------------------
    def gated_card(self, status="needs-verification", session=None):
        card_id = self.create(title="A card that broke something")
        card = self.board.card_by_id(card_id)
        card.set("status", status)
        if session is not None:
            card.set("session", session)
        self.board.save(card)
        return card_id

    def test_a_card_cannot_leave_needs_verification_under_a_signal_its_own_run_opened(self):
        self.fail()
        self.run_event("r1", self.pane_token)
        card_id = self.gated_card(session=self.pane_token)
        refused = self.tools.run("board_move_card", {"id": card_id, "status": "done",
                                                    "reason": "shipped"})
        self.assertEqual(refused["code"], "board_signal_open")
        self.assertEqual([s["key"] for s in refused["signals"]], ["ctest:panelayout"])
        self.assertEqual(self.board.card_by_id(card_id).status, "needs-verification")

    def test_a_signal_open_before_this_card_is_listed_and_refuses_nothing(self):
        self.fail()
        self.run_event("r1", "somebody-elses-pane")
        card_id = self.gated_card(session=self.pane_token)
        moved = self.tools.run("board_move_card", {"id": card_id, "status": "done",
                                                  "reason": "shipped"})
        self.assertNotIn("error", moved, moved)
        self.assertEqual(self.board.card_by_id(card_id).status, "done")

    def test_the_card_a_signal_was_promoted_from_is_gated_by_that_signal(self):
        self.fail()
        promoted = self.tools.run("board_signals", {"action": "promote", "key": "ctest:panelayout"})
        card = self.board.card_by_id(promoted["card"])
        card.set("status", "needs-verification")
        self.board.save(card)
        refused = self.tools.run("board_move_card", {"id": promoted["card"], "status": "done",
                                                    "reason": "shipped"})
        self.assertEqual(refused["code"], "board_signal_open")

    def test_a_dismissed_signal_never_blocks_which_is_the_owners_override(self):
        self.fail()
        self.run_event("r1", self.pane_token)
        future = (datetime.now(timezone.utc) + timedelta(days=3)).strftime("%Y-%m-%d")
        self.tools.run("board_signals", {"action": "dismiss", "key": "ctest:panelayout",
                                         "reason": "environmental", "comment": "c",
                                         "until": future})
        card_id = self.gated_card(session=self.pane_token)
        moved = self.tools.run("board_move_card", {"id": card_id, "status": "done",
                                                   "reason": "shipped"})
        self.assertNotIn("error", moved, moved)

    def test_a_move_that_does_not_leave_needs_verification_is_never_gated(self):
        self.fail()
        self.run_event("r1", self.pane_token)
        card_id = self.gated_card(status="inbox", session=self.pane_token)
        moved = self.tools.run("board_move_card", {"id": card_id, "status": "executing",
                                                  "reason": "starting"})
        self.assertNotIn("error", moved, moved)

    def test_a_board_with_no_signal_store_gates_nothing(self):
        card_id = self.gated_card(session=self.pane_token)
        moved = self.tools.run("board_move_card", {"id": card_id, "status": "done",
                                                  "reason": "shipped"})
        self.assertNotIn("error", moved, moved)

    # ---- the pane closing ----------------------------------------------------
    def test_release_claims_gives_back_the_signals_this_pane_held_too(self):
        self.fail()
        self.fail(key="ctest:other", message="kindX broke")
        self.tools.run("board_signals", {"action": "claim", "key": "ctest:panelayout"})
        card_id = self.create()
        self.tools.run("board_claim", {"id": card_id})
        released = self.tools.release_claims("the pane closed")
        self.assertEqual(released, [card_id])
        signals = self.S.state(self.repo, self.root)
        self.assertEqual(signals["ctest:panelayout"].session, "")
        self.assertIn(("release", "ctest:panelayout"), self.actions())
        self.assertNotIn(("release", "ctest:other"), self.actions())

    def test_a_worker_with_no_pane_token_releases_no_signal(self):
        self.fail()
        tools = T.BoardTools(self.board, state_path=self.repo / ".relay" / "none.json",
                             pane_token=None)
        tools.begin_turn("t-none")
        self.assertEqual(tools.release_signal_claims(), [])

    # ---- the numbers the tool's own description quotes ----------------------
    def test_the_spec_quotes_the_rule_it_describes(self):
        self.assertEqual(T.S_AGENT_DISMISS_MAX_DAYS, self.S.AGENT_DISMISS_MAX_DAYS)
        self.assertEqual(T.SIGNAL_ACTIONS, ("list", "claim", "release", "dismiss", "promote"))
        spec = next(item["function"] for item in T.TOOL_SPECS
                    if item["function"]["name"] == "board_signals")
        self.assertEqual(spec["parameters"]["properties"]["action"]["enum"],
                         list(T.SIGNAL_ACTIONS))
        self.assertIn("board_claimed_elsewhere", spec["description"])
        self.assertIn("7 days", spec["description"])


# ------------------------------------------------- expectations and the verification record

class DoneMeansSectionTests(BoardToolsTest):
    """#WC3E: the sections the schema grew, and the two rules that hang off them.

    One test per `## Done means` bullet of the card, on the pieces that live in the tools:
    the headings `check` and `AGENT_SECTIONS` recognise, the Plan turn's second section, and
    the refusal to close a card over a question a person has not answered.
    """

    # ---- the heading list is complete (bullet 3)
    def test_the_new_sections_are_in_the_schema_and_agent_writable(self):
        for heading in ("done means", "human qa", "profile", "try it"):
            self.assertIn(heading, B.CARD_SECTIONS, heading)
            self.assertIn(heading, T.AGENT_SECTIONS, heading)
        # and `Done means` comes before `Plan`, because it is written before the work
        self.assertLess(B.CARD_SECTIONS.index("done means"), B.CARD_SECTIONS.index("plan"))

    def test_check_no_longer_warns_on_them(self):
        card_id = self.create()
        card_hash = self.tools.run("board_read", {"id": card_id})["hash"]
        for heading in ("Done means", "Human QA", "Profile", "Try it"):
            card_hash = self.tools.run("board_update_card", {
                "id": card_id, "base_hash": card_hash,
                "append_section": {"heading": heading, "text": "x"}})["hash"]
        self.assertEqual([p.code for p in self.board.check() if p.code == "unknown_section"], [])

    def test_writing_them_is_not_logged_as_a_rewrite_of_the_owners_words(self):
        card_id = self.create()
        card_hash = self.tools.run("board_read", {"id": card_id})["hash"]
        result = self.tools.run("board_update_card", {
            "id": card_id, "base_hash": card_hash,
            "append_section": {"heading": "Done means", "text": "- the key records\nFailure: silence"}})
        self.assertEqual(result["logged_rewrites"], [])
        self.assertIn("## Done means", self.board.card_by_id(card_id).body)

    # ---- a Plan turn writes it (bullet 1)
    def test_a_plan_turn_may_write_done_means_beside_the_plan_and_nothing_else(self):
        card_id = self.create()
        self.tools.begin_card_turn("plan", card_id)
        self.addCleanup(self.tools.end_card_turn)

        def card_hash():
            return self.tools.run("board_read", {"id": card_id})["hash"]

        result = self.tools.run("board_update_card", {
            "id": card_id, "base_hash": card_hash(),
            "replace_section": {"heading": "Done means", "text": "- it records\nFailure: silence"}})
        self.assertNotIn("error", result, result)
        plan = self.tools.run("board_update_card", {
            "id": card_id, "base_hash": card_hash(),
            "replace_section": {"heading": "Plan", "text": "1. do it"}})
        self.assertNotIn("error", plan, plan)
        refused = self.tools.run("board_update_card", {
            "id": card_id, "base_hash": card_hash(),
            "replace_section": {"heading": "Execution Summary", "text": "built it"}})
        self.assertEqual(refused["code"], "board_mode_refused")
        self.assertIn("Done means", refused["error"])
        body = self.board.card_by_id(card_id).body
        self.assertIn("## Done means", body)
        self.assertNotIn("## Execution Summary", body)

    # ---- the Plan brief says so, and the policy no longer contradicts it (bullet 3)
    def test_the_plan_brief_asks_for_done_means_before_the_plan(self):
        brief = T.card_brief("plan")
        self.assertIn("Done means", brief)
        self.assertLess(brief.index("Done means"), brief.index("What a plan holds"))

    def test_the_policy_says_the_implementer_writes_no_checklist(self):
        # The policy block is read before every board call and is budgeted (#GMCF decision 8,
        # tests/test_system_prompt.py), so it carries the rule and `board_move_card`'s own
        # description carries the detail. Both are checked here, because the split is the point:
        # nothing was dropped, it was tiered.
        text = T.policy_text()
        self.assertIn("no\n   `## QA checklist`", text)
        self.assertIn("Done means", text)
        self.assertIn("Human QA", text)
        self.assertIn("move cards within your authority", text)   # agents move cards
        move = next(item["function"] for item in T.TOOL_SPECS
                    if item["function"]["name"] == "board_move_card")["description"]
        # the contradictions Codex listed, settled in the text rather than left to the reader
        self.assertIn("recommendation, not a rule", move)         # model family
        self.assertIn("the implementer writes none", move)
        self.assertIn("`Answer:`", move)

    # ---- an unanswered human question (bullet 4)
    def test_an_answer_is_an_indented_line_under_its_numbered_question(self):
        body = ("## Human QA\n"
                "1. Is the threshold right?\n"
                "    Answer: yes, leave it.\n"
                "2. Should the denial say so in the composer?\n")
        self.assertEqual(T.unanswered_human_qa(body),
                         ["2. Should the denial say so in the composer?"])
        self.assertEqual(T.unanswered_human_qa("## Human QA\nprose, no question\n"), [])
        self.assertEqual(T.unanswered_human_qa("## Tasks\n1. not this section\n"), [])

    def test_an_agent_cannot_move_a_card_to_done_over_an_open_question(self):
        card_id = self.create()
        card_hash = self.tools.run("board_read", {"id": card_id})["hash"]
        self.tools.run("board_update_card", {
            "id": card_id, "base_hash": card_hash,
            "append_section": {"heading": "Human QA",
                               "text": "1. Does the wording read right to you?"}})
        refused = self.tools.run("board_move_card", {"id": card_id, "status": "done",
                                                     "reason": "looks fine"})
        self.assertEqual(refused["code"], "board_refused")
        self.assertEqual(refused["requires"], "human_qa_answer")
        self.assertIn("Human QA", refused["error"])
        self.assertEqual(self.board.card_by_id(card_id).status, "inbox")

    def test_the_same_move_goes_through_once_the_question_is_answered(self):
        card_id = self.create()
        card_hash = self.tools.run("board_read", {"id": card_id})["hash"]
        self.tools.run("board_update_card", {
            "id": card_id, "base_hash": card_hash,
            "append_section": {"heading": "Human QA",
                               "text": "1. Does the wording read right to you?\n"
                                       "    Answer: yes (owner, 2026-09-21)"}})
        result = self.tools.run("board_move_card", {"id": card_id, "status": "done",
                                                    "reason": "answered"})
        self.assertNotIn("error", result, result)
        self.assertEqual(self.board.card_by_id(card_id).status, "done")

    def test_the_owner_may_still_close_it(self):
        # "a card with an open judgement waits for the person" -- for the *person*, not forever.
        card_id = self.create()
        card_hash = self.tools.run("board_read", {"id": card_id})["hash"]
        self.tools.run("board_update_card", {
            "id": card_id, "base_hash": card_hash,
            "append_section": {"heading": "Human QA", "text": "1. Is this right?"}})
        self.tools.context.actor = T.OWNER_ACTOR
        result = self.tools.run("board_move_card", {"id": card_id, "status": "done",
                                                    "reason": "I looked at it"})
        self.assertNotIn("error", result, result)
        self.assertEqual(self.board.card_by_id(card_id).status, "done")


class VerifyFieldTests(BoardToolsTest):
    """`fields.verify` on board_update_card, the claim reminder and where the block is read (#WFRA)."""

    BLOCK = {"artifact": "code", "primary": "script", "also": ["ai-text"], "human": "optional",
             "criteria": "the refusal reads as one sentence", "effort": "low"}

    def update(self, card_id, **kw):
        card_hash = self.tools.run("board_read", {"id": card_id})["hash"]
        return self.tools.run("board_update_card", {"id": card_id, "base_hash": card_hash, **kw})

    def test_fields_verify_is_validated_and_read_back_normalized(self):
        card_id = self.create()
        result = self.update(card_id, fields={"verify": dict(self.BLOCK, also="ai-text")})
        self.assertNotIn("error", result, result)
        self.assertTrue(any(c.startswith("verify: (unset) → ") for c in result["changes"]), result["changes"])
        read = self.tools.run("board_read", {"id": card_id})
        self.assertEqual(read["front"]["verify"], dict(self.BLOCK, sign_off="none"))
        self.assertNotIn("verify_error", read)
        self.assertIsInstance(read["front"]["verify"]["also"], list)
        # The file holds the normalized block, on one line of front matter.
        self.assertIn("verify: {artifact: code, primary: script, also: [ai-text], human: optional, "
                      "criteria: the refusal reads as one sentence, sign_off: none, effort: low}",
                      self.board.card_by_id(card_id).path.read_text())
        # null removes it, like any other field.
        self.assertNotIn("error", self.update(card_id, fields={"verify": None}))
        self.assertNotIn("verify", self.tools.run("board_read", {"id": card_id})["front"])

    def test_a_bad_key_or_value_is_refused_by_name(self):
        card_id = self.create()
        for value, phrase in (({**self.BLOCK, "primary": "vibes"}, "verify.primary 'vibes' is not one of"),
                              ({**self.BLOCK, "sample": "1/10", "grade": "A"}, "verify has no key 'grade'"),
                              ({"artifact": "code", "primary": "script"}, "verify is missing 'effort'"),
                              ({**self.BLOCK, "human": "required", "criteria": None},
                               "verify.criteria is required when verify.human is 'required'"),
                              ("script", "verify must be a mapping")):
            refused = self.update(card_id, fields={"verify": value})
            self.assertEqual(refused.get("code"), "board_refused", refused)
            self.assertEqual(refused.get("field"), "verify")
            self.assertIn(phrase, refused["error"])
        self.assertNotIn("verify", self.board.card_by_id(card_id).front)

    def test_a_stored_block_that_does_not_validate_is_named_on_read(self):
        card_id = self.create()
        card = self.board.card_by_id(card_id)
        card.set("verify", {"artifact": "code", "primary": "vibes", "effort": "low"})
        self.board.save(card)
        read = self.tools.run("board_read", {"id": card_id})
        self.assertIn("verify.primary 'vibes'", read["verify_error"])
        self.assertEqual(read["front"]["verify"]["primary"], "vibes")   # as written, not dropped
        # The row stays clean (owner steer 2026-09-23): nothing of the block is user-facing.
        row = next(r for r in self.tools.run("board_list", {})["cards"] if r["id"] == card_id)
        self.assertNotIn("verify", row)
        self.assertNotIn("unverified_until", row)

    def test_a_claim_without_a_block_carries_a_one_line_reminder(self):
        card_id = self.create()
        result = self.tools.run("board_claim", {"id": card_id})
        self.assertNotIn("error", result, result)
        self.assertIn(f"#{card_id} has no `verify` block", result["reminder"])
        self.assertIn("## Done means", result["reminder"])
        self.assertNotIn("\n", result["reminder"])
        # Owner steer 2026-09-23: the reminder lives in the tool result alone, never in the
        # thread or any user-facing text.
        self.assertNotIn("has no `verify` block", self.thread_text(card_id))
        self.assertNotIn("error", self.update(card_id, fields={"verify": self.BLOCK}))
        again = self.tools.run("board_claim", {"id": card_id})
        self.assertNotIn("error", again, again)
        self.assertNotIn("reminder", again)

    def test_list_rows_carry_only_the_deferred_note(self):
        # Owner steer 2026-09-23: rows carry no verify summary (agent-facing in board_read),
        # only `unverified_until` on a deferred card.
        plain = self.create(title="Has block")
        self.update(plain, fields={"verify": dict(self.BLOCK, primary="probe", human="required")})
        empty = self.create(title="No block", request="a second request without a block")
        deferred = self.create(title="Deferred", request="a third request, deferred")
        self.update(deferred, fields={"verify": dict(self.BLOCK, deferred="until the pilot runs")})
        rows = {r["id"]: r for r in self.tools.run("board_list", {})["cards"]}
        self.assertNotIn("verify", rows[plain])
        self.assertNotIn("unverified_until", rows[plain])
        self.assertNotIn("unverified_until", rows[empty])
        self.assertEqual(rows[deferred]["unverified_until"], "the pilot runs")

    def test_the_policy_the_skill_and_the_tool_say_to_propose_it(self):
        policy = T.policy_text()
        self.assertIn("propose `verify` beside `## Done means`", policy)
        self.assertIn("ladder order, with effort", policy)
        update = next(item["function"] for item in T.TOOL_SPECS
                      if item["function"]["name"] == "board_update_card")["description"]
        self.assertIn("`verify`", update)
        self.assertIn("refused naming a bad key or value", update)
        from relay_core import skills as skills_mod
        deliver = (Path(skills_mod.bundled_dir()) / "deliver" / "SKILL.md").read_text("utf-8")
        self.assertIn("propose the `verify` block", deliver)
        self.assertLess(deliver.index("propose the `verify` block"), deliver.index("## 5. Run"))
        for word in ("`script`", "`probe`", "`metric`", "`ai-text`", "`ai-visual`", "`level`",
                     "`pairwise`", "`person`", "`world`", "effort: low|medium|high"):
            self.assertIn(word, deliver)


class VerifyGateTests(BoardToolsTest):
    """The `verify` block's refusals and passes on board_move_card (#1AA6)."""

    def prepare(self, verify=None, sections=(), title="Voice transcription mode"):
        card_id = self.create(title=title, request=f"add {title.lower()}")
        card_hash = self.tools.run("board_read", {"id": card_id})["hash"]
        fields = {"verify": {"artifact": "code", "primary": "script", "effort": "low", **verify}} if verify else {}
        args = {"id": card_id, "base_hash": card_hash, **({"fields": fields} if fields else {})}
        result = self.tools.run("board_update_card", args)
        self.assertNotIn("error", result, result)
        for heading, text in sections:
            card_hash = self.tools.run("board_read", {"id": card_id})["hash"]
            result = self.tools.run("board_update_card", {"id": card_id, "base_hash": card_hash,
                                                          "append_section": {"heading": heading, "text": text}})
            self.assertNotIn("error", result, result)
        return card_id

    def move(self, card_id, status, **kw):
        return self.tools.run("board_move_card", {"id": card_id, "status": status, "reason": "landing", **kw})

    def test_review_row_has_only_an_actionable_priority(self):
        card_id = self.prepare({"human": "required", "criteria": "read the result", "stakes": "rework"})
        card = self.board.card_by_id(card_id)
        card.set("status", "needs-verification")
        row = self.tools._row(card, {})
        self.assertGreater(row["review_priority"], 0)
        self.assertNotIn("criteria", row)
        self.assertNotIn("verify", row)
        card.body += "\n## Human QA\n1. Read it?\n    Answer: yes\n"
        self.assertEqual(self.tools._row(card, {})["review_priority"], 0)

    # ---- human: required
    def test_human_required_refuses_done_without_an_answered_question_and_offers_the_lane(self):
        card_id = self.prepare({"human": "required", "criteria": "the strip reads in one line"})
        refused = self.move(card_id, "done")
        self.assertEqual(refused["code"], "board_refused")
        self.assertEqual(refused["requires"], "human_qa_answer")
        self.assertEqual(refused["offer"], "needs-qa-human")
        self.assertIn(f"#{card_id} needs the person's answer", refused["error"])
        self.assertIn("the strip reads in one line", refused["error"])
        self.assertNotIn(". ", refused["error"])             # one sentence, owner steer
        self.assertTrue(refused["error"].endswith("."))
        self.assertEqual(self.board.card_by_id(card_id).status, "inbox")
        # The lane it offers is open.
        result = self.move(card_id, "needs-qa-human", evidence="docs/qa_evidence/2026-09-23-x/")
        self.assertNotIn("error", result, result)

    def test_human_required_passes_once_a_question_carries_an_answer(self):
        card_id = self.prepare({"human": "required", "criteria": "reads right"},
                               [("Human QA", "1. Reads right?\n    Answer: yes (owner, 2026-09-23)")])
        result = self.move(card_id, "done")
        self.assertNotIn("error", result, result)
        self.assertEqual(self.board.card_by_id(card_id).status, "done")

    def test_an_open_question_still_blocks_and_the_refusal_names_the_block(self):
        card_id = self.prepare({"human": "required", "criteria": "reads right"},
                               [("Human QA", "1. Reads right?")])
        refused = self.move(card_id, "done")
        self.assertEqual(refused["requires"], "human_qa_answer")
        self.assertIn("(verify.human: required)", refused["error"])
        self.assertEqual(refused["offer"], "needs-qa-human")

    def test_optional_and_none_do_not_gate(self):
        for human in ("optional", "none"):
            card_id = self.prepare({"human": human, "criteria": "reads right"}, title=f"Human {human}")
            result = self.move(card_id, "done")
            self.assertNotIn("error", result, (human, result))

    def test_the_owner_is_the_person_and_may_close_it(self):
        card_id = self.prepare({"human": "required", "criteria": "reads right"})
        self.tools.context.actor = T.OWNER_ACTOR
        result = self.move(card_id, "done")
        self.assertNotIn("error", result, result)

    # ---- sign_off
    def test_a_sign_off_needs_a_receipt_line(self):
        card_id = self.prepare({"sign_off": "publish"})
        refused = self.move(card_id, "done")
        self.assertEqual(refused["code"], "board_refused")
        self.assertEqual(refused["requires"], "receipt")
        self.assertEqual(refused["sign_off"], "publish")
        self.assertIn(f"#{card_id} needs a publish sign-off", refused["error"])
        self.assertIn("`Receipt:`", refused["error"])
        self.assertNotIn(". ", refused["error"])             # one sentence, owner steer
        self.assertTrue(refused["error"].endswith("."))
        card_hash = self.tools.run("board_read", {"id": card_id})["hash"]
        self.tools.run("board_update_card", {"id": card_id, "base_hash": card_hash,
                                             "append_section": {"heading": "Execution Summary",
                                                                "text": "Published.\nReceipt: owner clicked Publish at 10:12, site live"}})
        result = self.move(card_id, "done")
        self.assertNotIn("error", result, result)

    def test_a_receipt_in_the_verdict_counts_and_the_issue_does_not(self):
        card_id = self.prepare({"sign_off": "money"}, [("Issue", "Receipt: not the place")])
        self.assertEqual(self.move(card_id, "done")["requires"], "receipt")
        card_hash = self.tools.run("board_read", {"id": card_id})["hash"]
        self.tools.run("board_update_card", {"id": card_id, "base_hash": card_hash,
                                             "append_section": {"heading": "Verdict", "text": "pass\nReceipt: transfer 4411"}})
        self.assertNotIn("error", self.move(card_id, "done"))

    # ---- deferred
    def test_deferred_refuses_done_and_the_qa_lanes_and_says_until_when(self):
        card_id = self.prepare({"deferred": "until the pilot runs (owner: Sam)"})
        for status in ("done", "needs-qa-llm", "needs-qa-human"):
            refused = self.move(card_id, status, evidence="docs/qa_evidence/2026-09-23-x/")
            self.assertEqual(refused.get("code"), "board_refused", (status, refused))
            self.assertEqual(refused["requires"], "verify_deferred")
            self.assertIn(f"#{card_id} is unverified until the pilot runs (owner: Sam)", refused["error"])
            self.assertEqual(refused["until"], "the pilot runs (owner: Sam)")
            self.assertNotIn(". ", refused["error"])         # one sentence, owner steer
            self.assertTrue(refused["error"].endswith("."))
        self.assertEqual(self.board.card_by_id(card_id).status, "inbox")
        # Landing for a verifier is still open: deferring is about the verdict, not the work.
        self.assertNotIn("error", self.move(card_id, "needs-verification"))
        row = next(r for r in self.tools.run("board_list", {})["cards"] if r["id"] == card_id)
        self.assertEqual(row["unverified_until"], "the pilot runs (owner: Sam)")
        # The owner too: a deferred card is a fact about the world, not a judgement.
        self.tools.context.actor = T.OWNER_ACTOR
        self.assertEqual(self.move(card_id, "done")["requires"], "verify_deferred")

    def test_clearing_deferred_is_the_way_on_and_the_thread_says_who(self):
        card_id = self.prepare({"deferred": "until the pilot runs"})
        card_hash = self.tools.run("board_read", {"id": card_id})["hash"]
        result = self.tools.run("board_update_card", {
            "id": card_id, "base_hash": card_hash,
            "fields": {"verify": {"artifact": "code", "primary": "script", "effort": "low"}}})
        self.assertNotIn("error", result, result)
        self.assertIn("verify.deferred cleared by agent (was until the pilot runs)", result["changes"])
        text = self.thread_text(card_id)
        self.assertIn("verify.deferred cleared by agent", text)
        self.assertIn("author=agent", text.split("verify.deferred cleared")[0].rsplit("<!-- relay:entry", 1)[1])
        self.assertNotIn("error", self.move(card_id, "done"))
        self.assertEqual(self.board.card_by_id(card_id).status, "done")

    def test_an_unreadable_block_cannot_verify_anything(self):
        card_id = self.create()
        card = self.board.card_by_id(card_id)
        card.set("verify", {"artifact": "code", "primary": "vibes", "effort": "low"})
        self.board.save(card)
        refused = self.move(card_id, "done")
        self.assertEqual(refused["requires"], "verify")
        self.assertIn("verify.primary 'vibes'", refused["error"])

    def test_the_move_tool_and_the_skill_say_what_verified_means(self):
        move = next(item["function"] for item in T.TOOL_SPECS
                    if item["function"]["name"] == "board_move_card")["description"]
        for phrase in ("`human: required`", "needs-qa-human", "`Receipt:`", "`deferred`"):
            self.assertIn(phrase, move)
        from relay_core import skills as skills_mod
        deliver = (Path(skills_mod.bundled_dir()) / "deliver" / "SKILL.md").read_text("utf-8")
        self.assertIn("`relay_core.board.verified`", deliver)
        self.assertIn("unverified until", deliver)
        self.assertIn("needs it met: evidence, the person's answer, a receipt, not deferred", T.policy_text())


class VerifyDefaultTests(BoardToolsTest):
    """#MSJ0: a card claimed or updated while the turn has loaded a profiled skill inherits the
    profile's verify keys as its `verify` block; an explicit `fields.verify` always wins."""

    REFEREE = ("artifact: text\nprimary: ai-text\nalso: person\nhuman: required\n"
               "criteria: every major point is addressed\nsign_off: none\neffort: high\n"
               "regularity: routine\nrot: low\nconfidential: yes")
    HEALTH = "artifact: system\nprimary: probe\nhuman: none\neffort: low\nexecutable: yes"

    def setUp(self):
        super().setUp()
        self.card_id = self.create()

    def load(self, skill_id, profile_text):
        from relay_core.skills import parse_profile
        self.tools.context.skill_loaded(skill_id, parse_profile(profile_text))

    def update(self, **args):
        current = self.tools.run("board_read", {"id": self.card_id})["hash"]
        return self.tools.run("board_update_card", {"id": self.card_id, "base_hash": current, **args})

    def test_a_claim_defaults_verify_from_the_one_loaded_profiled_skill(self):
        self.load("referee-report", self.REFEREE)
        result = self.tools.run("board_claim", {"id": self.card_id})
        self.assertNotIn("error", result, result)
        self.assertEqual(result["verify_defaulted_from"], "referee-report")
        self.assertEqual(result["note"], "verify defaulted from skill referee-report")
        self.assertNotIn("reminder", result)
        self.assertIn("verify defaulted from skill referee-report", result["summary"])
        # Only the verify keys cross over, normalized by validate_verify; the server-only
        # factors (regularity, rot, confidential) stay on the skill.  The default then meets
        # the QA policy floor like any proposal (#C3Q2): under the default policy an `ai-text`
        # primary may not gate, so `person` (the next non-AI rung in `also`) becomes primary
        # and `ai-text` stays as a supplementary rung; the result's `qa_policy` notes say so.
        self.assertEqual(self.board.card_by_id(self.card_id).front["verify"],
                         {"artifact": "text", "primary": "person", "also": ["ai-text"],
                          "human": "required", "criteria": "every major point is addressed",
                          "sign_off": "none", "effort": "high"})
        self.assertEqual(result["qa_policy"], ["qa policy: primary ai-text → person, ai-text "
                                               "kept in also (AI gating is off)"])
        read = self.tools.run("board_read", {"id": self.card_id})
        self.assertEqual(read["front"]["verify"]["primary"], "person")
        self.assertEqual([str(p) for p in self.board.check()], [])

    def test_an_update_defaults_verify_when_the_card_has_none(self):
        self.load("server-health-check", self.HEALTH)
        result = self.update(append_section={"heading": "Findings", "text": "- disk at 91%"})
        self.assertNotIn("error", result, result)
        self.assertEqual(result["verify_defaulted_from"], "server-health-check")
        self.assertEqual(result["note"], "verify defaulted from skill server-health-check")
        self.assertIn("verify defaulted from skill server-health-check", result["changes"])
        card = self.board.card_by_id(self.card_id)
        self.assertEqual(card.front["verify"], {"artifact": "system", "primary": "probe", "also": [],
                                                "human": "none", "sign_off": "none", "effort": "low"})
        self.assertIn("verify defaulted from skill server-health-check", self.thread_text(self.card_id))
        # A second update finds the block in place and says nothing more.
        again = self.update(append_section={"heading": "Findings", "text": "- rotated the log"})
        self.assertNotIn("verify_defaulted_from", again)
        self.assertNotIn("note", again)

    def test_an_explicit_fields_verify_wins_and_gets_no_note(self):
        self.load("server-health-check", self.HEALTH)
        mine = {"artifact": "code", "primary": "script", "effort": "medium"}
        result = self.update(fields={"verify": mine})
        self.assertNotIn("error", result, result)
        self.assertNotIn("verify_defaulted_from", result)
        self.assertNotIn("note", result)
        self.assertEqual(self.board.card_by_id(self.card_id).front["verify"]["primary"], "script")
        # And a claim afterwards leaves the explicit block alone.
        claim = self.tools.run("board_claim", {"id": self.card_id})
        self.assertNotIn("verify_defaulted_from", claim)
        self.assertEqual(self.board.card_by_id(self.card_id).front["verify"]["primary"], "script")

    def test_two_profiled_skills_are_not_guessed_between(self):
        self.load("referee-report", self.REFEREE)
        self.load("server-health-check", self.HEALTH)
        result = self.tools.run("board_claim", {"id": self.card_id})
        self.assertNotIn("error", result, result)
        self.assertNotIn("verify_defaulted_from", result)
        self.assertEqual(result["note"], "verify not defaulted: skills referee-report, "
                                         "server-health-check each carry a profile; set fields.verify yourself")
        self.assertIn("reminder", result)
        self.assertNotIn("verify", self.board.card_by_id(self.card_id).front)
        update = self.update(append_section={"heading": "Findings", "text": "- x"})
        self.assertNotIn("verify_defaulted_from", update)
        self.assertNotIn("verify", self.board.card_by_id(self.card_id).front)

    def test_a_profile_without_verify_keys_or_no_profile_defaults_nothing(self):
        self.load("plain", "")
        self.load("server-only", "regularity: novel\nexecutable: no\nrot: high")
        result = self.tools.run("board_claim", {"id": self.card_id})
        self.assertNotIn("error", result, result)
        self.assertNotIn("verify_defaulted_from", result)
        self.assertNotIn("note", result)
        self.assertIn("reminder", result)
        self.assertNotIn("verify", self.board.card_by_id(self.card_id).front)

    def test_a_profile_that_is_not_a_valid_block_is_reported_not_written(self):
        # `human: required` needs `criteria`; the profile is the skill author's to fix.
        self.load("half", "artifact: text\nprimary: ai-text\nhuman: required\neffort: low")
        result = self.tools.run("board_claim", {"id": self.card_id})
        self.assertNotIn("error", result, result)
        self.assertNotIn("verify_defaulted_from", result)
        self.assertIn("verify not defaulted from skill half: verify.criteria is required", result["note"])
        self.assertNotIn("verify", self.board.card_by_id(self.card_id).front)

    def test_begin_turn_forgets_the_skills_of_the_last_turn(self):
        self.load("server-health-check", self.HEALTH)
        self.tools.begin_turn("t-2")
        result = self.tools.run("board_claim", {"id": self.card_id})
        self.assertNotIn("verify_defaulted_from", result)
        self.assertIn("reminder", result)

    def test_the_bundled_deliver_profile_defaults_a_claim(self):
        from relay_core import skills as skills_mod
        index = skills_mod.SkillIndex.load([Path(skills_mod.bundled_dir())])
        loaded = index.load_skill("deliver")
        self.tools.context.skill_loaded(loaded["skill"], loaded["profile"])
        result = self.tools.run("board_claim", {"id": self.card_id})
        self.assertEqual(result["verify_defaulted_from"], "deliver")
        verify = self.board.card_by_id(self.card_id).front["verify"]
        self.assertEqual((verify["primary"], verify["human"], verify["effort"], verify["stakes"], verify["blast"]),
                         ("script", "none", "medium", "rework", "capability"))
        self.assertNotIn("regularity", verify)


class QaPolicyFloorTests(BoardToolsTest):
    """The QA policy floor (#C3Q2) as the tools apply it: silently, before a `verify` block is
    stored, reported only in the tool result; and the one switch, ask against automatic."""

    def setUp(self):
        super().setUp()
        self.card_id = self.create(title="Reconcile the invoices", request="reconcile the invoices")

    def tools_with(self, qa=None, config=None, turn="t-2"):
        """A second instance on the same board: `qa` is what `configure.qa` sends, `config` a
        whole board.yaml to write first (the project layer)."""
        if config is not None:
            (self.root / B.BOARD_CONFIG).write_text(config, encoding="utf-8")
        tools = T.BoardTools(
            self.board, emit=self.events.append, qa=qa,
            context=T.ToolContext(actor="agent", model="anthropic/claude-opus-5-5", pane="3"),
            state_path=self.repo / ".relay" / "board-rate.json", pane_token=self.pane_token)
        tools.begin_turn(turn)
        return tools

    def set_verify(self, block, tools=None):
        tools = tools or self.tools
        current = tools.run("board_read", {"id": self.card_id})["hash"]
        return tools.run("board_update_card", {"id": self.card_id, "base_hash": current,
                                               "fields": {"verify": block}})

    def verify_of(self):
        return self.board.card_by_id(self.card_id).front.get("verify")

    def move(self, status, tools=None, **extra):
        return (tools or self.tools).run("board_move_card", {"id": self.card_id, "status": status,
                                                             "reason": f"to {status}", **extra})

    # ---- the three silent rules, through fields.verify
    def test_stakes_at_the_floor_raise_human_to_required(self):
        # The failure the card names: a `stakes: money`, `human: none` block accepted as written.
        result = self.set_verify({"artifact": "number", "primary": "script", "effort": "low",
                                  "stakes": "money", "human": "none"})
        self.assertNotIn("error", result, result)
        verify = self.verify_of()
        self.assertEqual(verify["human"], "required")
        self.assertTrue(verify["criteria"])
        self.assertEqual(len(result["qa_policy"]), 2)
        self.assertIn("human none → required (stakes money is at or above the floor money)",
                      result["qa_policy"][0])
        # Stored already floored: the change line names the block that stands, and the notes
        # are the agent's alone -- none of them reach the thread.
        self.assertIn("required", "".join(result["changes"]))
        self.assertNotIn("qa policy", self.thread_text(self.card_id))
        self.assertEqual([str(p) for p in self.board.check()], [])

    def test_an_ai_primary_is_downgraded_to_also(self):
        result = self.set_verify({"artifact": "text", "primary": "ai-text", "also": ["probe"],
                                  "effort": "low"})
        self.assertNotIn("error", result, result)
        verify = self.verify_of()
        self.assertEqual(verify["primary"], "probe")
        self.assertEqual(verify["also"], ["ai-text"])
        self.assertEqual(verify["human"], "none")
        self.assertEqual(result["qa_policy"], ["qa policy: primary ai-text → probe, ai-text kept "
                                               "in also (AI gating is off)"])
        # Nothing else to fall back on: person, and then a person must look.
        result = self.set_verify({"artifact": "visual", "primary": "ai-visual", "effort": "low"})
        verify = self.verify_of()
        self.assertEqual((verify["primary"], verify["also"], verify["human"]),
                         ("person", ["ai-visual"], "required"))

    def test_a_sample_is_dropped_when_sampling_is_off(self):
        result = self.set_verify({"artifact": "code", "primary": "script", "effort": "low",
                                  "sample": "1/10 after 30"})
        self.assertNotIn("error", result, result)
        self.assertNotIn("sample", self.verify_of())
        self.assertEqual(result["qa_policy"], ["qa policy: sample '1/10 after 30' dropped "
                                               "(sampling is off)"])

    def test_a_block_within_the_floor_gets_no_note(self):
        result = self.set_verify({"artifact": "code", "primary": "script", "also": ["ai-text"],
                                  "effort": "low", "stakes": "rework"})
        self.assertNotIn("error", result, result)
        self.assertNotIn("qa_policy", result)
        self.assertEqual(self.verify_of()["also"], ["ai-text"])

    def test_board_read_states_the_effective_floor(self):
        line = self.tools.run("board_read", {"id": self.card_id})["qa_policy"]
        self.assertEqual(line, "verification ask: every card waits in needs-verification for the "
                               "user to close; human required from stakes money; AI may gate "
                               "never; sampling never")

    # ---- the layers
    def test_configure_qa_is_the_global_layer(self):
        tools = self.tools_with(qa={"verification": "automatic"})
        self.assertEqual(tools.qa_policy.verification, "automatic")
        self.assertEqual(tools.qa_policy.source["verification"], "global")
        self.assertIn("verification automatic (Options)",
                      tools.run("board_read", {"id": self.card_id})["qa_policy"])
        # `parse_board` carries it with the board block, which is how `configure` hands it on.
        self.assertEqual(P.parse_board({"qa": {"verification": "automatic"}})["qa"],
                         {"verification": "automatic"})
        self.assertIsNone(P.parse_board({"qa": "automatic"})["qa"])
        self.assertIsNone(P.parse_board({})["qa"])

    def test_the_project_qa_block_overrides_the_global_switch(self):
        tools = self.tools_with(qa={"verification": "automatic"},
                                config=CONFIG + "qa: {verification: ask, ask_at_stakes: never, "
                                                "sample_after: always}\n")
        policy = tools.qa_policy
        self.assertEqual((policy.verification, policy.ask_at_stakes, policy.sample_after),
                         ("ask", "never", "always"))
        self.assertEqual(policy.source["verification"], "project")
        self.assertEqual(policy.ai_may_gate_after, "never")            # untouched: the default
        self.assertIn("verification ask (board.yaml)",
                      tools.run("board_read", {"id": self.card_id})["qa_policy"])
        # And the project floor is the one applied: money stakes no longer need a person, a
        # sample stands.
        result = self.set_verify({"artifact": "number", "primary": "script", "effort": "low",
                                  "stakes": "money", "sample": "1/10"}, tools)
        self.assertNotIn("error", result, result)
        self.assertNotIn("qa_policy", result)
        self.assertEqual((self.verify_of()["human"], self.verify_of()["sample"]), ("none", "1/10"))

    def test_a_bad_project_value_is_named_and_ignored(self):
        tools = self.tools_with(config=CONFIG + "qa: {verification: sometimes}\n")
        self.assertEqual(tools.qa_policy.verification, "ask")
        line = tools.run("board_read", {"id": self.card_id})["qa_policy"]
        self.assertIn("ignored: board.yaml qa.verification 'sometimes' is not a value it takes", line)

    # ---- ask against automatic
    def land(self, tools=None):
        """The card in needs-verification with a plan that needs no person."""
        self.set_verify({"artifact": "code", "primary": "script", "effort": "low"}, tools)
        self.move("executing", tools)
        result = self.move("needs-verification", tools)
        self.assertNotIn("error", result, result)

    def test_in_ask_mode_a_verifier_does_not_close_a_card_needing_no_person(self):
        # The failure the card names: an ask-mode card closed by a verifier.
        self.land()
        verifier = self.tools_with()
        result = self.move("done", verifier)
        self.assertEqual(result["code"], "board_refused")
        self.assertEqual(result["requires"], "user_close")
        self.assertEqual(result["offer"], "needs-verification")
        self.assertIn("Verification: ask me before closing any card", result["error"])
        self.assertEqual(self.board.card_by_id(self.card_id).status, "needs-verification")
        # `optional` is still "needs no person".
        self.set_verify({"artifact": "code", "primary": "script", "effort": "low",
                         "human": "optional", "criteria": "reads well"}, verifier)
        self.assertEqual(self.move("done", verifier)["code"], "board_refused")

    def test_in_automatic_mode_the_verifiers_pass_closes_it_and_says_so(self):
        self.land()
        verifier = self.tools_with(qa={"verification": "automatic"})
        result = self.move("done", verifier)
        self.assertNotIn("error", result, result)
        self.assertEqual(result["status"], "done")
        self.assertEqual(result["qa_policy"], "closed automatically: Verification is automatic "
                                              "and the plan needs no person (verify.human: none)")
        self.assertEqual(self.board.card_by_id(self.card_id).status, "done")

    def test_the_gate_is_only_the_verifiers_close(self):
        # The owner's own close from the Board is never this gate's.
        self.land()
        owner = self.tools_with()
        owner.context.actor = T.OWNER_ACTOR
        self.assertEqual(self.move("done", owner)["status"], "done")
        # Nor a self-close out of executing (the medium tier), nor a card with no plan.
        other = self.create(title="Rename a flag", request="rename it")
        self.tools.run("board_move_card", {"id": other, "status": "executing", "reason": "on it"})
        current = self.tools.run("board_read", {"id": other})["hash"]
        self.tools.run("board_update_card", {"id": other, "base_hash": current, "fields": {
            "verify": {"artifact": "code", "primary": "script", "effort": "low"}}})
        result = self.tools.run("board_move_card", {"id": other, "status": "done", "reason": "tests pass"})
        self.assertEqual(result["status"], "done")
        third = self.create(title="Older card", request="from before the block")
        for status in ("executing", "needs-verification", "done"):
            result = self.tools.run("board_move_card", {"id": third, "status": status, "reason": status})
            self.assertNotIn("error", result, result)
        self.assertNotIn("qa_policy", result)

    def test_a_person_required_card_is_the_verify_gates_not_this_ones(self):
        self.set_verify({"artifact": "text", "primary": "level", "effort": "low",
                         "human": "required", "criteria": "the summary is fair"})
        self.move("executing")
        self.move("needs-verification")
        verifier = self.tools_with(qa={"verification": "automatic"})
        result = self.move("done", verifier)
        self.assertEqual(result["requires"], "human_qa_answer")
        self.assertEqual(result["offer"], "needs-qa-human")


if __name__ == "__main__":       # pragma: no cover
    unittest.main()


# --------------------------------------------------------------------------- the case ledger (#95VZ)

class CaseLedgerTests(BoardToolsTest):
    """Every writer leaves one row in `cases.jsonl`; `board_case` logs a person's case; the
    readers filter it and keep confidential rows to the board's own workspace."""

    REFEREE = ("artifact: text\nprimary: ai-text\nalso: person\nhuman: required\n"
               "criteria: every major point is addressed\neffort: high\nrot: high\nconfidential: yes")
    HEALTH = "artifact: system\nprimary: probe\neffort: low\nrot: medium"

    def setUp(self):
        super().setUp()
        self.card_id = self.create(title="Referee the EJ paper", request="referee EJ-1234")

    def load(self, skill_id, profile_text, version="sha256:abc"):
        from relay_core.skills import parse_profile
        self.tools.context.skill_loaded(skill_id, parse_profile(profile_text), version)

    def rows(self, **filters):
        from relay_core import cases
        return cases.read(self.root, **filters)

    def update(self, **args):
        current = self.tools.run("board_read", {"id": self.card_id})["hash"]
        return self.tools.run("board_update_card", {"id": self.card_id, "base_hash": current, **args})

    def move(self, status, **args):
        return self.tools.run("board_move_card", {"id": self.card_id, "status": status,
                                                  "reason": f"to {status}", **args})

    def test_board_case_logs_a_person_served_case_and_says_how_many(self):
        result = self.tools.run("board_case", {"server": "referee-report", "cost": "3 h",
                                               "input": "Referee-Work/EJ-1234.pdf"})
        self.assertNotIn("error", result, result)
        self.assertEqual((result["server"], result["served_by"], result["cases"]), ("referee-report", "person", 1))
        self.assertFalse(result["third_case_hint"])
        self.assertNotIn("hint", result)
        (row,) = self.rows()
        self.assertEqual(row["id"], result["case"])
        self.assertEqual(row["cost"], {"seconds": 10800.0})
        self.assertEqual(row["input"], "Referee-Work/EJ-1234.pdf")
        self.assertEqual(row["verdict"], {"result": "pending", "who": "person", "revision": 1})
        self.assertEqual(row["server_version"], "")
        self.assertFalse(row["confidential"])
        # No thread entry, no board event: the row is agent-facing (owner steer 2026-09-23).
        self.assertEqual(self.thread_text(self.card_id).count("case"), 0)
        self.assertFalse([e for e in self.events if e.get("event") == "board_activity"
                          and "case" in str(e.get("summary", ""))])

    def test_the_third_person_served_case_in_90_days_carries_the_hint_once(self):
        from relay_core import cases
        for _ in range(2):
            self.tools.run("board_case", {"server": "person", "cost": "1 h", "input": "matter A"})
        third = self.tools.run("board_case", {"server": "person", "cost": "1 h", "input": "matter B"})
        self.assertTrue(third["third_case_hint"])
        self.assertEqual(third["hint"], cases.hint_line("person"))
        self.assertIn("build a server? (/deliver)", third["hint"])
        fourth = self.tools.run("board_case", {"server": "person", "cost": "1 h"})
        self.assertFalse(fourth["third_case_hint"])
        # And a case served by a model does not count towards it.
        self.assertEqual(self.tools.run("board_case", {"server": "other", "served_by": "openai/gpt-5-6"})["third_case_hint"], False)
        self.assertEqual(len(self.rows()), 5)
        self.assertEqual(self.board.check(), [])          # the ledger is not a card

    def test_board_case_under_a_confidential_profile_drops_the_input(self):
        self.load("referee-report", self.REFEREE, "sha256:deadbeef")
        result = self.tools.run("board_case", {"server": "referee-report", "card": self.card_id,
                                               "input": "the patient's file", "verdict": "pass"})
        self.assertNotIn("error", result, result)
        self.assertTrue(result["confidential"])
        (row,) = self.rows()
        self.assertNotIn("input", row)
        self.assertEqual(row["server_version"], "sha256:deadbeef")
        self.assertEqual(row["card"], self.card_id)
        self.assertEqual(row["verdict"]["result"], "pass")
        text = (self.root / "cases.jsonl").read_text(encoding="utf-8")
        self.assertNotIn("patient", text)

    def test_board_case_refuses_what_it_cannot_record(self):
        self.assertIn("server", self.tools.run("board_case", {})["error"])
        self.assertIn("cost", self.tools.run("board_case", {"server": "x", "cost": "a while"})["error"])
        self.assertEqual(self.tools.run("board_case", {"server": "x", "card": "ZZZZ"})["code"], "board_not_found")
        self.assertIn("board_case takes", self.tools.run("board_case", {"server": "x", "who": "me"})["error"])
        self.assertIn("never content", self.tools.run("board_case", {"server": "x", "input": "x" * 300})["error"])
        self.assertEqual(self.rows(), [])
        # A read-only turn writes no row either.
        self.tools.readonly = True
        self.assertEqual(self.tools.run("board_case", {"server": "x"})["code"], "board_readonly_turn")

    def test_a_verdict_section_writes_a_decided_row_naming_the_cards_server(self):
        self.load("referee-report", self.REFEREE, "sha256:v1")
        self.tools.run("board_claim", {"id": self.card_id})
        self.tools.begin_turn("t-2")                       # the verifier's turn loads nothing
        result = self.update(replace_section={"heading": "Verdict", "text": "Pass. Every point is addressed."})
        self.assertNotIn("error", result, result)
        (row,) = self.rows(card=self.card_id)
        self.assertEqual(result["case"], row["id"])
        # Without a loaded skill the server is what the ledger already knows about the card —
        # here nothing — so the card itself is the server, and the verdict is the first word.
        self.assertEqual(row["server"], f"card:{self.card_id}")
        self.assertEqual(row["verdict"], {"result": "pass", "who": "anthropic/claude-opus-5-5", "revision": 1})
        self.assertEqual(row["signal"], {"mode": "person", "result": "pass"})
        fail = self.update(append_section={"heading": "Verdict", "text": "**FAIL**: the gate let it through"})
        self.assertEqual(self.rows(card=self.card_id)[-1]["verdict"]["result"], "fail")
        self.assertIn("case", fail)
        # An ordinary section is not a verdict and writes nothing.
        self.update(append_section={"heading": "Findings", "text": "- x"})
        self.assertEqual(len(self.rows()), 2)

    def test_a_verdict_row_reuses_the_server_of_the_turn_that_served_the_card(self):
        self.tools.begin_turn("t-1b")                      # setUp's create was the last turn's
        self.load("server-health-check", self.HEALTH, "sha256:h1")
        self.tools.record_turn_cases(outcome="done", cost={"seconds": 12.5, "tokens": 900})
        self.tools.begin_turn("t-2")
        self.update(replace_section={"heading": "Verdict", "text": "passed"})
        rows = self.rows(card=self.card_id)
        self.assertEqual(len(rows), 1)                     # the turn row named no card…
        turn_row = self.rows(server="server-health-check")[0]
        self.assertIsNone(turn_row.get("card"))
        # …so the verdict row falls back to the card as its own server.
        self.assertEqual(rows[0]["server"], f"card:{self.card_id}")
        # But a turn that wrote to the card names it, and the later verdict reuses that server.
        self.tools.begin_turn("t-3")
        self.load("server-health-check", self.HEALTH, "sha256:h1")
        self.update(append_section={"heading": "Findings", "text": "- disk ok"})
        self.tools.record_turn_cases(outcome="done", cost={"seconds": 3})
        self.tools.begin_turn("t-4")
        self.update(replace_section={"heading": "Verdict", "text": "pass"})
        newest = self.rows(card=self.card_id)[-1]
        self.assertEqual((newest["server"], newest["server_version"]), ("server-health-check", "sha256:h1"))

    def test_a_turn_that_loaded_a_profiled_skill_leaves_a_pending_row(self):
        self.load("referee-report", self.REFEREE, "sha256:r1")
        self.load("plain", "")                              # no profile: no row
        self.update(append_section={"heading": "Findings", "text": "- read it"})
        self.tools.context.session_id = "s-9"
        rows = self.tools.record_turn_cases(outcome="done",
                                            cost={"seconds": 40.0, "tokens": 12000, "money": 0.04})
        self.assertEqual(len(rows), 1)
        (row,) = self.rows()
        self.assertEqual(row["server"], "referee-report")
        self.assertEqual(row["server_version"], "sha256:r1")
        self.assertEqual(row["served_by"], "anthropic/claude-opus-5-5")
        self.assertEqual(row["card"], self.card_id)
        self.assertEqual(row["cost"], {"seconds": 40.0, "tokens": 12000, "money": 0.04})
        self.assertEqual(row["signal"], {"mode": "ai-text", "result": "done"})
        self.assertEqual(row["verdict"]["result"], "pending")
        self.assertTrue(row["confidential"])                # the profile says so: no input
        self.assertNotIn("input", row)
        # A turn with no profiled skill writes nothing.
        self.tools.begin_turn("t-2")
        self.assertEqual(self.tools.record_turn_cases(), [])
        self.assertEqual(len(self.rows()), 1)

    def test_a_move_to_done_is_a_pass_and_back_a_stage_is_a_fail(self):
        self.tools.run("board_claim", {"id": self.card_id})
        self.update(append_section={"heading": "Execution Summary", "text": "built"})
        self.move("needs-verification", evidence="docs/qa_evidence/x/")
        self.assertEqual(self.rows(), [])                   # not a verdict
        back = self.move("executing")
        self.assertNotIn("error", back, back)
        self.assertEqual(self.rows()[-1]["verdict"]["result"], "fail")
        self.assertEqual(self.rows()[-1]["card"], self.card_id)
        self.move("needs-verification", evidence="docs/qa_evidence/x/")
        self.update(append_section={"heading": "Verdict", "text": "pass"})
        self.tools.context.actor = "owner"
        self.tools.context.model = self.tools.context.preset = None
        done = self.move("done")
        self.assertNotIn("error", done, done)
        self.assertIn("case", done)
        results = [r["verdict"]["result"] for r in self.rows(card=self.card_id)]
        self.assertEqual(results, ["fail", "pass", "pass"])
        self.assertEqual(self.rows()[-1]["served_by"], "owner")

    def test_board_list_cases_filters_by_server_and_card_and_hides_confidential_rows_elsewhere(self):
        self.tools.run("board_case", {"server": "referee-report", "input": "EJ-1", "card": self.card_id})
        self.tools.run("board_case", {"server": "rent", "input": "September"})
        self.load("referee-report", self.REFEREE)
        self.tools.run("board_case", {"server": "referee-report", "input": "secret"})
        listed = self.tools.run("board_list", {"cases": True})
        self.assertEqual(listed["total"], 3)
        self.assertEqual([r["server"] for r in listed["cases"]], ["referee-report", "rent", "referee-report"])
        self.assertEqual(listed["path"], "issues/cases.jsonl")
        self.assertFalse(listed["confidential_hidden"])
        by_server = self.tools.run("board_list", {"cases": True, "server": "rent"})
        self.assertEqual([r["input"] for r in by_server["cases"]], ["September"])
        by_card = self.tools.run("board_list", {"cases": True, "card": self.card_id.lower()})
        self.assertEqual(by_card["total"], 1)
        limited = self.tools.run("board_list", {"cases": True, "limit": 1})
        self.assertTrue(limited["truncated"])
        self.assertEqual(len(limited["cases"]), 1)
        self.assertIn("pass cases: true", self.tools.run("board_list", {"server": "rent"})["error"])
        self.assertIn("filters cards", self.tools.run("board_list", {"cases": True, "tab": "features"})["error"])
        # A pane whose workspace is another project sees the same ledger minus the confidential row.
        with tempfile.TemporaryDirectory() as other:
            elsewhere = T.BoardTools(self.board, emit=self.events.append, workspace=other,
                                     context=T.ToolContext(actor="agent"),
                                     state_path=self.repo / ".relay" / "rate2.json")
            self.assertFalse(elsewhere.own_workspace())
            seen = elsewhere.run("board_list", {"cases": True})
            self.assertEqual(seen["total"], 2)
            self.assertTrue(seen["confidential_hidden"])
            self.assertTrue(all(not r["confidential"] for r in seen["cases"]))
        inside = T.BoardTools(self.board, emit=self.events.append, workspace=self.repo / "src",
                              context=T.ToolContext(actor="agent"),
                              state_path=self.repo / ".relay" / "rate3.json")
        self.assertTrue(inside.own_workspace())
        self.assertEqual(inside.run("board_list", {"cases": True})["total"], 3)

    def test_the_qa_floor_counts_passing_cases_from_the_ledger(self):
        from relay_core import cases
        (self.root / B.BOARD_CONFIG).write_text(self.config + "qa: {ai_may_gate_after: 2}\n", encoding="utf-8")
        self.tools = T.BoardTools(self.board, emit=self.events.append,
                                  context=T.ToolContext(actor="agent", model="anthropic/claude-opus-5-5"),
                                  state_path=self.repo / ".relay" / "rate4.json")
        self.tools.begin_turn("t-1")
        self.load("referee-report", self.REFEREE)
        block = {"artifact": "text", "primary": "ai-text", "also": ["person"], "effort": "low"}
        # Not yet: the ledger has no passing case of this server, so AI may not gate.
        first = self.update(fields={"verify": block})
        self.assertIn("qa policy: primary ai-text → person, ai-text kept in also (AI gating is off)", first["qa_policy"])
        for _ in range(2):
            cases.append(self.root, cases.new_record("referee-report", served_by="openai/gpt-5-6", verdict="pass"))
        # Two passing cases: the count is reached; what still stops it is the lineage rule,
        # which this card cannot meet without an independent verifier on record.
        second = self.update(fields={"verify": {**block, "effort": "medium"}})
        self.assertNotIn("error", second, second)
        self.assertIn("names no verifier outside the author's lineage", " ".join(second["qa_policy"]))
        self.assertNotIn("AI gating is off", " ".join(second["qa_policy"]))


class RegistryTests(BoardToolsTest):
    """#9FX8: the `skills_registry` request answers with one row per visible skill, and the
    confidential-row rule follows `own_workspace`: ids on this board's own workspace, dropped
    off it."""

    def setUp(self):
        super().setUp()
        from relay_core import cases
        self.skill = self.repo / '.relay' / 'skills' / 'referee'
        self.skill.mkdir(parents=True)
        (self.skill / 'SKILL.md').write_text(
            '---\nname: referee\ndescription: Referee\nprofile: |\n  artifact: text\n'
            '  primary: ai-text\n  rot: high\n---\nBody\n', encoding='utf-8')
        cases.append(self.root, cases.new_record('referee', served_by='person', verdict='pass',
                                                 card='K1Q2', input='docs/ref.pdf'))
        cases.append(self.root, cases.new_record('referee', served_by='person', verdict='pass',
                                                 card='K1Q2', input='secret.pdf', confidential=True))

    def observe(self, workspace=None):
        from types import SimpleNamespace
        from relay_core.observe_protocol import ObserveCommands
        agent = SimpleNamespace(board=self.tools, executor=SimpleNamespace(skills=None))
        events = []
        commands = ObserveCommands(SimpleNamespace(agent=agent, busy=False), events.append)
        request = {"type": "skills_registry", "id": 7}
        if workspace is not None:
            request["workspace"] = workspace
        commands.handle("skills_registry", request)
        return [e for e in events if e.get("event") == "skills_registry"][0]

    def test_the_request_answers_one_row_per_skill(self):
        event = self.observe(workspace=str(self.repo))
        self.assertEqual(event["id"], 7)
        self.assertIn("skipped", event)
        rows = [item for item in event["items"] if item["id"] == "referee"]
        self.assertEqual(len(rows), 1)
        row = rows[0]
        self.assertEqual(row["source"], "project-relay")
        self.assertTrue(row["project"])
        self.assertEqual(row["stats"]["cases"], 2)
        self.assertEqual(row["cards"], ["K1Q2"])
        self.assertEqual(len(row["cases"]), 2)                     # own workspace: both rows serve
        self.assertTrue(row["cases"][1]["confidential"])
        self.assertNotIn("input", row["cases"][1])                 # confidential: ids only
        self.assertEqual(row["cases"][0]["input"], "docs/ref.pdf")
        self.assertTrue(row["version"].startswith("sha256:"))
        self.assertTrue(row["stale_reason"])

    def test_off_the_boards_own_workspace_the_confidential_row_is_dropped(self):
        with tempfile.TemporaryDirectory() as elsewhere:   # outside the board's repo
            self.tools = T.BoardTools(
                self.board, emit=self.events.append, autonomy=self.autonomy,
                context=T.ToolContext(actor="agent", model="anthropic/claude-opus-5-5", pane="2"),
                state_path=self.repo / ".relay" / "board-rate.json", pane_token=self.pane_token,
                workspace=str(elsewhere))
            event = self.observe(workspace=str(self.repo))
            row = [item for item in event["items"] if item["id"] == "referee"][0]
            shown = row["cases"]
            self.assertEqual(len(shown), 1)
            self.assertFalse(shown[0]["confidential"])
            self.assertTrue(all("input" not in case for case in shown))  # ids only off-workspace
            self.assertEqual(row["stats"]["cases"], 2)                   # statistics still count both
