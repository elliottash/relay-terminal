# SPDX-License-Identifier: AGPL-3.0-or-later
"""The Switchboard on a paired device: the hub's half (docs/REMOTE-PROTOCOL.md section 17, #SWPH).

The two cleaners on their own first — what a device may ask and what it may be told — then the hub
over real sockets: a rendezvous, a Noise session per client and a GUI whose side of the pipe is a
list. What is tested is the path a phone's request actually takes, including the view device, the
partner and the guest who must never see any of it, and the push a card waiting on the owner earns.
"""
import asyncio
import contextlib
import copy
import json
import sys
import tempfile
import time
import unittest
from pathlib import Path
from types import SimpleNamespace

from remote import board_state, client as client_mod, gui_host, guests as guests_mod
from remote import host as host_mod, identity as identity_mod, noise, notify, wire
from rendezvous.server import Store, build

ROOT = Path(__file__).resolve().parent.parent
APP_DIR = ROOT / "app"
HOME = "/home/elliott/repos/relay-terminal"


def board_event(**changes) -> dict:
    """A `board` as the desktop's worker emits it (sessions protocol 19.2), paths and all."""
    event = {
        "event": "board", "id": "remote-1", "rev": 7, "root": f"{HOME}/issues",
        "workspace": HOME, "project": HOME, "state": "ready", "exists": True,
        "config": {"tabs": [{"id": "features", "folder": "features"},
                            {"id": "done", "filter": "status:done,dropped"}],
                   "columns": ["inbox", "planned"], "statuses": ["inbox", "planned"],
                   "column_statuses": {"needs-qa": ["needs-qa-llm", "needs-qa-human"]}},
        "cards": [row("K7Q2", waiting_on="agent"), row("M3XJ", waiting_on="owner")],
        "cards_total": 2, "more": False,
        "problems": [{"code": "bad_front", "path": "features/x.md", "severity": "error",
                      "message": f"cannot read {HOME}/issues/features/x.md: line 3"}],
    }
    event.update(changes)
    return event


def row(card: str, **changes) -> dict:
    out = {"id": card, "title": f"Card {card}", "type": "work", "status": "inbox", "section": None,
           "tab": "features", "labels": ["remote"], "assignee": "owner", "waiting_on": None,
           "rank": "6a", "private": False, "priority": 0,
           "path": f"issues/features/2026-09-20-{card.lower()}.md", "thread_entries": 2,
           "tasks_done": 0, "tasks_total": 3, "created": "2026-09-20",
           "updated": "2026-09-20T10:00:00Z", "session": "0123456789abcdef0123456789abcdef"}
    out.update(changes)
    return out


def card_event(card="K7Q2", **changes) -> dict:
    event = {"event": "board_card", "id": "remote-2", "root": f"{HOME}/issues", "card_id": card,
             "hash": "9f2c", "path": f"issues/features/{card}.md",
             "front": {"id": card, "status": "inbox", "waiting_on": "agent",
                       "links": {"evidence": ["docs/qa_evidence/x/"], "commits": ["f04edfd0"]}},
             "title": f"Card {card}", "body": f"## Issue\n\nsee {HOME}/remote/host.py\n",
             "sections": ["Issue", "Plan"], "issue": "see the hub", "issue_heading": "Issue",
             "tasks": [{"item_id": "t:s2", "text": "the hub", "status": "open", "done": False,
                        "depth": 0, "card": None, "blocked_by": []}],
             "thread_total": 1,
             "thread": [{"entry_id": "20260920T100000Z-a1", "author": "owner", "kind": "note",
                         "attrs": {"pane": "switchboard",
                                   "pane_token": "0123456789abcdef0123456789abcdef"},
                         "text": "is it easy for touch?"}]}
    event.update(changes)
    return event


# ---- what a device may ask -------------------------------------------------------------------------

class RequestTests(unittest.TestCase):
    def test_every_request_of_the_contract_passes_with_its_shape(self):
        self.assertEqual({example["type"] for example in board_state.EXAMPLE_REQUESTS},
                         set(board_state.REQUESTS))
        for example in board_state.EXAMPLE_REQUESTS:
            cleaned = board_state.clean_request(copy.deepcopy(example))
            self.assertEqual(cleaned, example, example["type"])
            self.assertEqual(set(cleaned) - {"type"}, set(board_state.REQUESTS[example["type"]])
                             & set(cleaned), example["type"])

    def test_the_contract_names_exactly_these_eleven(self):
        # `board_resume` joined the list with card #7JD1: a device that can stop a card's turn
        # has to be able to run its queue again, and the Resume button is the desktop's.
        self.assertEqual(sorted(board_state.REQUESTS), sorted([
            "board_open", "board_refresh", "board_card_get", "board_search", "board_comment",
            "board_move", "board_create", "board_ask", "board_cancel", "board_resume",
            "board_action"]))
        self.assertEqual(board_state.READS, {"board_open", "board_refresh", "board_card_get",
                                             "board_search"})

    def test_defaults(self):
        self.assertEqual(board_state.clean_request({"type": "board_comment", "id": "K7Q2",
                                                    "text": "ok"})["kind"], "note")
        ask = board_state.clean_request({"type": "board_ask", "id": "K7Q2", "text": "why?"})
        self.assertEqual(ask["mode"], "discuss")
        plan = board_state.clean_request({"type": "board_ask", "id": "K7Q2", "mode": "plan"})
        self.assertEqual(plan, {"type": "board_ask", "id": "K7Q2", "mode": "plan", "text": ""})
        create = board_state.clean_request({"type": "board_create", "title": "typed on the bus"})
        self.assertEqual(create, {"type": "board_create", "title": "typed on the bus",
                                  "request": "", "labels": []})

    def test_an_unknown_field_is_not_copied(self):
        cleaned = board_state.clean_request({"type": "board_move", "id": "K7Q2", "status": "done",
                                             "author": "mallory", "evidence": "trust me",
                                             "tab": "bugs", "before": "M3XJ", "force": True})
        self.assertEqual(cleaned, {"type": "board_move", "id": "K7Q2", "status": "done",
                                   "reason": ""})

    def test_the_never_list_is_refused_by_name(self):
        for kind in ("board_delete", "board_folder", "board_folder_hide", "board_folder_anything",
                     "board_cleanup", "board_claim", "set_board", "board_init", "board_init_answer",
                     "board_import_apply", "board_import_propose", "project_probe",
                     "forge_sync_plan", "forge_sync_run", "board_update", "board_undo",
                     "board_check", "configure"):
            with self.assertRaises(board_state.Refused, msg=kind) as refused:
                board_state.clean_request({"type": kind, "id": "K7Q2"})
            self.assertEqual(refused.exception.code, "not_permitted", kind)
            self.assertNotIn(kind, board_state.REQUESTS)

    def test_an_unknown_type_is_refused(self):
        for bad in (None, [], "board_open", {}, {"type": 7}, {"type": ""}, {"type": "board_chat"},
                    {"type": "ask"}, {"type": "store_key"}, {"type": "BOARD_OPEN"}):
            with self.assertRaises(board_state.Refused, msg=bad):
                board_state.clean_request(bad)

    def test_a_path_by_any_name_is_refused(self):
        for name in ("path", "root", "folder", "file", "dir", "cwd", "workspace", "project",
                     "Path", "card_path", "board_root", "session_dir"):
            with self.assertRaises(board_state.Refused, msg=name) as refused:
                board_state.clean_request({"type": "board_card_get", "id": "K7Q2", name: "x"})
            self.assertEqual(refused.exception.code, "not_permitted", name)
        # …at any depth, and in a request that takes no fields at all.
        with self.assertRaises(board_state.Refused):
            board_state.clean_request({"type": "board_open", "options": {"where": {"dir": "issues"}}})
        with self.assertRaises(board_state.Refused):
            board_state.clean_request({"type": "board_create", "title": "t",
                                       "labels": [{"file": "x"}]})

    def test_an_absolute_path_in_a_field_that_is_not_on_the_list_is_refused(self):
        for value in ("/home/elliott/.ssh/id_ed25519", "~/notes/todo.md", "C:\\Users\\e\\x.md",
                      "file:///etc/passwd", ["/etc/passwd"], {"to": "/srv/board/issues"}):
            with self.assertRaises(board_state.Refused, msg=value) as refused:
                board_state.clean_request({"type": "board_open", "where": value})
            self.assertEqual(refused.exception.code, "not_permitted", value)
        # The owner's own words may mention a path: a comment is text, and text is on the list.
        kept = board_state.clean_request({"type": "board_comment", "id": "K7Q2",
                                          "text": "/home/elliott/repos/relay-terminal/remote"})
        self.assertEqual(kept["text"], "/home/elliott/repos/relay-terminal/remote")
        # A slash command is not a path, and neither is an unlisted word.
        board_state.clean_request({"type": "board_open", "hint": "/deliver"})

    def test_a_card_id_has_the_boards_alphabet(self):
        for bad in ("", "k7q2", "K7Q", "K7Q22", "#K7Q2", "KIQ2", "KLQ2", "KOQ2", "KUQ2",
                    "../x", "K7 2", 7, None, ["K7Q2"]):
            for kind in ("board_card_get", "board_cancel", "board_resume"):
                with self.assertRaises(board_state.Refused, msg=(kind, bad)):
                    board_state.clean_request({"type": kind, "id": bad})
        for good in ("K7Q2", "0000", "ZZZZ", "SWPH", "3XZV"):
            self.assertEqual(board_state.clean_request({"type": "board_cancel", "id": good})["id"], good)

    def test_a_status_is_one_of_the_boards(self):
        for bad in ("", "Done", "finished", "needs-qa", "../done", None, 3, ["done"]):
            with self.assertRaises(board_state.Refused, msg=bad):
                board_state.clean_request({"type": "board_move", "id": "K7Q2", "status": bad})
        for good in board_state.STATUSES:
            board_state.clean_request({"type": "board_move", "id": "K7Q2", "status": good})

    def test_enumerations(self):
        for request in ({"type": "board_comment", "id": "K7Q2", "text": "x", "kind": "rewrite"},
                        {"type": "board_comment", "id": "K7Q2", "text": "x", "kind": "evidence"},
                        {"type": "board_comment", "id": "K7Q2", "text": "x", "kind": "progress"},
                        {"type": "board_cancel"},
                        {"type": "board_comment", "id": "K7Q2", "text": "x", "kind": "event"},
                        {"type": "board_ask", "id": "K7Q2", "text": "x", "mode": "execute"},
                        {"type": "board_action", "id": "K7Q2", "action": "delete"},
                        {"type": "board_action", "id": "K7Q2"},
                        {"type": "board_create", "title": "t", "tab": "../changes"},
                        {"type": "board_create", "title": "t", "tab": "Features"},
                        {"type": "board_create", "title": "t", "labels": "remote"},
                        {"type": "board_create", "title": "t", "labels": ["x" * 41]},
                        {"type": "board_create", "title": "t", "labels": ["a"] * 17},
                        {"type": "board_create"},
                        {"type": "board_create", "title": "  ", "request": "\n"}):
            with self.assertRaises(board_state.Refused, msg=request):
                board_state.clean_request(request)

    def test_over_long_text_is_refused_not_cut(self):
        for request in (
                {"type": "board_comment", "id": "K7Q2", "text": "x" * (board_state.TEXT_MAX + 1)},
                {"type": "board_ask", "id": "K7Q2", "text": "x" * (board_state.TEXT_MAX + 1)},
                {"type": "board_create", "title": "x" * (board_state.TITLE_MAX + 1)},
                {"type": "board_create", "title": "t", "request": "x" * (board_state.TEXT_MAX + 1)},
                {"type": "board_search", "query": "x" * (board_state.QUERY_MAX + 1)},
                {"type": "board_move", "id": "K7Q2", "status": "done",
                 "reason": "x" * (board_state.REASON_MAX + 1)}):
            with self.assertRaises(board_state.Refused, msg=request["type"]):
                board_state.clean_request(request)
        self.assertEqual((board_state.TEXT_MAX, board_state.TITLE_MAX, board_state.QUERY_MAX,
                          board_state.REASON_MAX), (8000, 200, 200, 500))
        at_the_cap = board_state.clean_request({"type": "board_comment", "id": "K7Q2",
                                                "text": "x" * board_state.TEXT_MAX})
        self.assertEqual(len(at_the_cap["text"]), board_state.TEXT_MAX)

    def test_text_must_be_text_and_a_discussion_needs_words(self):
        for request in ({"type": "board_comment", "id": "K7Q2", "text": {"a": 1}},
                        {"type": "board_comment", "id": "K7Q2", "text": ""},
                        {"type": "board_comment", "id": "K7Q2"},
                        {"type": "board_ask", "id": "K7Q2", "text": "  ", "mode": "discuss"},
                        {"type": "board_search", "query": 7}):
            with self.assertRaises(board_state.Refused, msg=request):
                board_state.clean_request(request)
        cleaned = board_state.clean_request({"type": "board_comment", "id": "K7Q2",
                                             "text": "two\nlines\x00\x1b[31m"})
        self.assertEqual(cleaned["text"], "two\nlines[31m")

    def test_a_rid_is_a_number_or_a_short_token(self):
        for good in (0, 1, 2 ** 53, "r1", "b:42", "a" * 64):
            self.assertEqual(board_state.rid_of({"rid": good}), good)
        for bad in (None, True, -1, 2 ** 53 + 1, 1.5, "", "a" * 65, "../x", "a b", [1], {"n": 1}):
            with self.assertRaises(wire.WireError, msg=bad):
                board_state.rid_of({"rid": bad})


class PinnedNamesTests(unittest.TestCase):
    """The hub does not import the backend, so what it pins is compared with it here."""

    @classmethod
    def setUpClass(cls):
        sys.path.insert(0, str(ROOT / "backend"))
        try:
            from relay_core import board, board_tools
        except Exception as error:                  # a checkout without the backend's dependencies
            raise unittest.SkipTest(f"backend/relay_core does not import here: {error}")
        finally:
            sys.path.remove(str(ROOT / "backend"))
        cls.board, cls.board_tools = board, board_tools

    def test_the_statuses_are_the_boards(self):
        self.assertEqual(tuple(board_state.STATUSES), tuple(self.board.ALL_STATUSES))

    def test_the_card_id_is_the_boards(self):
        self.assertEqual(board_state.CARD_ID.pattern, self.board.ID_RE.pattern)

    def test_the_comment_kinds_are_the_persons_three_of_the_board_tools(self):
        self.assertEqual(board_state.COMMENT_KINDS, ("note", "question", "decision"))
        self.assertLessEqual(set(board_state.COMMENT_KINDS), set(self.board_tools.COMMENT_KINDS))


# ---- what a device may be told -----------------------------------------------------------------------

class EventTests(unittest.TestCase):
    def test_only_the_contracts_event_types_pass(self):
        for name in ("board", "board_cards", "board_card", "board_changed", "board_thread_appended",
                     "board_written", "board_activity", "board_cancelled", "board_resumed",
                     "board_busy",
                     "board_conflict", "error", "board_action_result", "board_search",
                     "board_chat_state", "board_chat_queued"):
            self.assertEqual(board_state.clean_event({"event": name})["event"], name)
        for name in ("board_created", "board_state", "board_init_request", "board_folder_changed",
                     "board_import_proposals", "board_imported", "project_probe_result",
                     "forge_sync_planned", "forge_sync_done", "board_problems", "board_undone",
                     "board_cleanup_summary", "board_survey", "configured", "presets", "key_stored",
                     "session_info", "delta", "tool_result", "made_up", "", None, 7):
            self.assertIsNone(board_state.clean_event({"event": name}), name)
        for bad in (None, [], "board", {"t": "board"}):
            self.assertIsNone(board_state.clean_event(bad))

    def test_no_path_leaves_a_board(self):
        cleaned = board_state.clean_event(board_event())
        text = json.dumps(cleaned)
        for leak in ("/home", "elliott/repos", "issues/features", '"path"', '"root"', '"folder"',
                     '"workspace"', '"project"', "features/x.md"):
            self.assertNotIn(leak, text, leak)
        self.assertEqual([card["id"] for card in cleaned["cards"]], ["K7Q2", "M3XJ"])
        self.assertEqual(cleaned["cards"][1]["waiting_on"], "owner")
        self.assertEqual(cleaned["config"]["tabs"], [{"id": "features"},
                                                     {"id": "done", "filter": "status:done,dropped"}])
        self.assertEqual(cleaned["config"]["column_statuses"],
                         {"needs-qa": ["needs-qa-llm", "needs-qa-human"]})
        self.assertEqual(cleaned["problems"][0]["message"], "cannot read [path] line 3")
        self.assertEqual((cleaned["rev"], cleaned["more"], cleaned["exists"]), (7, False, True))

    def test_paths_are_dropped_at_every_depth(self):
        event = {"event": "board_written", "id": "r1", "kind": "move", "card_id": "K7Q2",
                 "path": "issues/x.md", "result": {"file": "x.md", "deep": [{"cwd": "/tmp/x", "dir": "d",
                                                  "kept": 1, "session_dir": "/x/y",
                                                  "where": "/var/lib/relay/board/x.md"}]},
                 "status": "planned", "hash": "ab12"}
        cleaned = board_state.clean_event(event)
        self.assertEqual(cleaned, {"event": "board_written", "id": "r1", "kind": "move",
                                   "card_id": "K7Q2", "result": {"deep": [{"kept": 1}]},
                                   "status": "planned", "hash": "ab12"})

    def test_a_card_passes_as_the_owners_text(self):
        cleaned = board_state.clean_event(card_event())
        self.assertIn(f"see {HOME}/remote/host.py", cleaned["body"])     # his notes, already in git
        self.assertEqual(cleaned["thread"][0]["text"], "is it easy for touch?")
        self.assertEqual(cleaned["front"]["links"]["evidence"], ["docs/qa_evidence/x/"])
        self.assertEqual(cleaned["tasks"][0]["item_id"], "t:s2")
        self.assertNotIn("path", cleaned)
        self.assertNotIn("root", cleaned)
        # A pane's session token is a desktop-local name: the eight characters the chip draws.
        self.assertEqual(cleaned["thread"][0]["attrs"]["pane_token"], "01234567")
        self.assertEqual(board_state.clean_event(board_event())["cards"][0]["session"], "01234567")

    def test_the_board_is_named_by_an_opaque_key_and_the_projects_own_name(self):
        first = board_state.clean_event(board_event())
        again = board_state.clean_event(card_event())
        other = board_state.clean_event(board_event(root="/srv/other/issues", project="/srv/other"))
        self.assertRegex(first["board_key"], r"^[0-9a-f]{12}$")
        self.assertEqual(first["board_key"], again["board_key"])
        self.assertNotEqual(first["board_key"], other["board_key"])
        self.assertEqual(first["board_name"], "relay-terminal")
        self.assertEqual(other["board_name"], "other")
        self.assertNotIn("board_key", board_state.clean_event({"event": "board_action_result"}))

    def test_a_desktop_written_event_has_its_paths_cut_out(self):
        error = board_state.clean_event({
            "event": "error", "id": "r9", "code": "board_conflict", "card_id": "K7Q2",
            "text": f"the card changed on disk: {HOME}/issues/features/x.md (re-read it)",
            "message": "~/repos/relay-terminal/issues/x.md is newer", "current_hash": "9f2c",
            "where": f"{HOME}/issues"})
        self.assertEqual(error["text"], "the card changed on disk: [path] (re-read it)")
        self.assertEqual(error["message"], "[path] is newer")
        self.assertNotIn("where", error)
        self.assertEqual(error["code"], "board_conflict")
        result = board_state.clean_event({"event": "board_action_result", "card_id": "K7Q2",
                                          "action": "execute", "ok": True,
                                          "title": f"Opened a pane in {HOME}"})
        self.assertEqual(result["title"], "Opened a pane in [path]")
        # A URL is not a path.
        kept = board_state.clean_event({"event": "error", "text": "see https://example.com/a/b"})
        self.assertEqual(kept["text"], "see https://example.com/a/b")

    def test_the_desktop_bridges_own_payloads_pass(self):
        """As src/BoardRemote.cpp sends them (docs/qa_evidence/2026-09-21-swph-board-bridge). On an
        event `id` is the bridge's request id and `card_id` the card — except `board_activity` and
        `board_action_result`, where `id` is the card."""
        refused = {"code": "board_refused", "event": "error", "request": "board_delete",
                   "text": "A paired device may not send board_delete."}
        self.assertEqual(board_state.clean_event(refused), refused)
        for code in ("remote_off", "board_not_found", "board_not_initialized"):
            self.assertEqual(board_state.clean_event({**refused, "code": code})["code"], code)
        search = {"event": "board_search", "id": "remote-9", "ids": ["CZDS"], "query": "dictated"}
        self.assertEqual(board_state.clean_event(search), search)
        named = {"event": "board_changed", "removed": [], "upserts": ["K7Q2"],
                 "write_id": "w-1a0c1d61b4e-162e"}
        self.assertEqual(board_state.clean_event(named), named)
        self.assertEqual(board_state.waiting_changes(named), ([], [], None))
        activity = {"action": "comment", "actor": "owner", "event": "board_activity", "id": "K7Q2",
                    "model": None, "pane": "switchboard", "turn_id": None, "undo_seconds": 30,
                    "summary": "decision: owner, from Elliott's iPhone: “Yes, go ahead.”",
                    "write_id": "w-1a0c1d61b4e-162e"}
        self.assertEqual(board_state.clean_event(activity), activity)
        result = board_state.clean_event({
            "action": "execute", "event": "board_action_result", "id": "K7Q2", "ok": True,
            "message": "#K7Q2 is executing in a new pane.",
            "pane": "d6f19588-fa87-4ed5-a67e-a03b68951a0a"})
        self.assertEqual(result, {"action": "execute", "event": "board_action_result", "id": "K7Q2",
                                  "ok": True, "message": "#K7Q2 is executing in a new pane.",
                                  "pane": "d6f19588"})
        written = board_state.clean_event({
            "card_id": "K7Q2", "claimed": True, "event": "board_written", "id": "remote-11",
            "kind": "board_claim", "session": "d6f19588-fa87-4ed5-a67e-a03b68951a0a",
            "status": "executing"})
        self.assertEqual(written["session"], "d6f19588")
        # The card is never read from an event's `id`: this one's is the bridge's request id.
        card = board_state.clean_event({"event": "board_card", "id": "remote-2", "card_id": "K7Q2",
                                        "front": {"id": "K7Q2", "waiting_on": "owner"}, "body": "b"})
        self.assertEqual(board_state.waiting_changes(card), ([("K7Q2", "owner")], [], None))

    def test_a_key_is_redacted_wherever_it_is(self):
        cleaned = board_state.clean_event(card_event(
            body="the key is sk-proj-AAAAAAAAAAAAAAAAAAAAAAAA ok"))
        self.assertNotIn("sk-proj", json.dumps(cleaned))
        self.assertIn("[redacted]", cleaned["body"])

    def test_caps(self):
        self.assertEqual((board_state.BODY_MAX, board_state.ENTRY_MAX, board_state.CARDS_MAX,
                          board_state.ENTRIES_MAX), (200_000, 20_000, 500, 500))
        entry = {"entry_id": "e", "author": "agent", "kind": "note", "attrs": {}, "text": "y" * 30_000}
        cleaned = board_state.clean_event(card_event(body="x" * 300_000, thread=[entry] * 3))
        self.assertEqual(len(cleaned["body"]), board_state.BODY_MAX)
        self.assertEqual([len(item["text"]) for item in cleaned["thread"]],
                         [board_state.ENTRY_MAX] * 3)
        self.assertTrue(cleaned["truncated"])
        many = board_state.clean_event(board_event(cards=[row("K7Q2")] * 700))
        self.assertEqual(len(many["cards"]), board_state.CARDS_MAX)
        self.assertTrue(many["truncated"])
        long_thread = [{"entry_id": f"e{i}", "text": "t"} for i in range(700)]
        newest = board_state.clean_event(card_event(thread=long_thread))
        self.assertEqual(len(newest["thread"]), board_state.ENTRIES_MAX)
        self.assertEqual(newest["thread"][-1]["entry_id"], "e699", "a thread keeps its newest")
        self.assertNotIn("truncated", board_state.clean_event(card_event()))

    def test_an_event_always_fits_one_wire_message(self):
        entry = {"entry_id": "e", "author": "agent", "kind": "note", "attrs": {}, "text": "y" * 19_000}
        cleaned = board_state.clean_event(card_event(body="x" * 199_000, issue="x" * 199_000,
                                                     thread=[dict(entry, entry_id=f"e{i}")
                                                             for i in range(60)]))
        self.assertTrue(cleaned["truncated"])
        self.assertLessEqual(len(wire.encode({"t": "board_event", "rid": 1, "event": cleaned})),
                             wire.MAX_MESSAGE)
        self.assertEqual(cleaned["thread"][-1]["entry_id"], "e59", "the newest entries are the ones kept")
        self.assertEqual(len(cleaned["body"]), 199_000)

    def test_depth_and_strange_values(self):
        deep = current = {}
        for _ in range(30):
            current["next"] = {}
            current = current["next"]
        cleaned = board_state.clean_event({"event": "board_activity", "deep": deep,
                                           "nan": float("nan"), "fn": object(), 7: "numeric key",
                                           "/etc/passwd/x": 1, "ok": 1.5})
        self.assertTrue(cleaned["truncated"])
        self.assertEqual(cleaned["ok"], 1.5)
        for gone in ("nan", "fn", 7, "/etc/passwd/x"):
            self.assertNotIn(gone, cleaned)
        json.dumps(cleaned)


# ---- the hub, over real sockets ------------------------------------------------------------------------

def run(coroutine, timeout=90):
    return asyncio.run(asyncio.wait_for(coroutine, timeout))


class Harness:
    """A hub over a GuiPaneSource with one pane, a guest store, and the GUI's pipe as a list."""

    def __init__(self, capability=wire.FULL):
        self.capability = capability
        self.to_gui: list[dict] = []

    async def __aenter__(self):
        self.temporary = tempfile.TemporaryDirectory()
        directory = Path(self.temporary.name)
        self.directory = directory
        self.store = Store(":memory:")
        self.server = build(self.store, static_root=APP_DIR)
        await self.server.start("127.0.0.1", 0)
        self.base = f"http://127.0.0.1:{self.server.port}"
        self.identity = identity_mod.Identity.create(directory)
        self.devices = identity_mod.DeviceStore(directory)
        self.guests = guests_mod.GuestStore(directory, devices=self.devices)
        self.source = gui_host.GuiPaneSource(self.to_gui.append)
        self.source.set_pane({"id": "p1", "title": "relay", "cwd": "/home/elliott", "rows": 24,
                              "cols": 80, "status": "idle"})

        async def approver(request):
            return True, self.capability

        async def knock_approver(request):
            return True, request.role

        self.host = host_mod.Host(self.identity, self.devices, self.source, app_base=self.base,
                                  approver=approver, knock_approver=knock_approver,
                                  guests=self.guests, name="test desktop")
        await self.host.register(self.base)
        self.serving = asyncio.create_task(self.host.serve())
        for _ in range(100):
            await asyncio.sleep(0.02)
            if self.host.socket is not None:
                break
        return self

    async def __aexit__(self, *exc):
        await self.host.stop()
        self.serving.cancel()
        with contextlib.suppress(asyncio.CancelledError):
            await self.serving
        await self.server.close()
        self.store.close()
        self.temporary.cleanup()

    async def device(self, capability=wire.FULL, name="iPhone"):
        self.capability = capability
        url, _ = await self.host.open_pairing()
        pairing_client = client_mod.Client(self.base)
        record = await pairing_client.pair(url, name=name, platform="Safari")
        await pairing_client.close()
        client = client_mod.Client(self.base)
        client.welcome = await client.connect(record)
        await client.expect("panes")
        return client, record

    async def guest(self, role=wire.EDITOR):
        _, url = await self.host.invite_create(["p1"], role)
        client = client_mod.Client(self.base)
        await client.knock(url, name="alice", platform="Chrome")
        await client.expect("panes")
        return client

    def requests(self) -> list[dict]:
        return [message for message in self.to_gui if message.get("t") == "board_request"]

    async def settle(self, count: int = 1, timeout: float = 5.0) -> dict:
        deadline = time.monotonic() + timeout
        while len(self.requests()) < count:
            if time.monotonic() > deadline:
                raise AssertionError(f"the GUI was sent {len(self.requests())} board_request(s), "
                                     f"not {count}: {[m.get('t') for m in self.to_gui]}")
            await asyncio.sleep(0.02)
        return self.requests()[count - 1]

    def audit_lines(self) -> list[dict]:
        return [json.loads(line) for path in sorted(self.directory.glob("audit-*.jsonl"))
                for line in path.read_text().splitlines()]


async def drain(client, seconds: float = 0.5) -> list[dict]:
    """Everything that reaches a client's socket in the next moment."""
    got = []
    deadline = time.monotonic() + seconds
    while True:
        left = deadline - time.monotonic()
        if left <= 0:
            return got
        try:
            got.append(await asyncio.wait_for(client.inbox.get(), left))
        except asyncio.TimeoutError:
            return got


async def next_board_event(client, timeout: float = 5.0) -> dict:
    """The next `board_event`, without `Client.expect`'s habit of raising on an `error` message —
    a refusal here *is* a board_event."""
    deadline = time.monotonic() + timeout
    while True:
        message = await asyncio.wait_for(client.inbox.get(), max(0.01, deadline - time.monotonic()))
        if message.get("t") == "board_event":
            return message
        if message.get("t") == "error":
            raise AssertionError(f"a wire error, not a board_event: {message}")


class RequestPathTests(unittest.TestCase):
    def test_each_allowed_request_reaches_the_gui_with_the_contracts_shape(self):
        async def main():
            async with Harness() as harness:
                phone, record = await harness.device(name="Elliott's iPhone")
                for number, request in enumerate(board_state.EXAMPLE_REQUESTS, start=1):
                    await phone.send({"t": "board_request", "rid": f"r{number}", "request": request})
                    await asyncio.sleep(0.3 if request["type"] not in board_state.READS else 0.02)
                    line = await harness.settle(number)
                    self.assertEqual(line, {"t": "board_request", "rid": number,
                                            "device": record.device_id, "name": "Elliott's iPhone",
                                            "request": request}, request["type"])
                await phone.close()
        run(main())

    def test_a_refused_request_never_reaches_the_gui_and_says_why(self):
        refused = [
            ({"type": "board_delete", "id": "K7Q2"}, "not_permitted"),
            ({"type": "board_folder_hide"}, "not_permitted"),
            ({"type": "set_board", "project": "/home/elliott/other"}, "not_permitted"),
            ({"type": "board_import_apply"}, "not_permitted"),
            ({"type": "board_card_get", "id": "K7Q2", "path": "issues/x.md"}, "not_permitted"),
            ({"type": "board_open", "where": "/home/elliott/other/issues"}, "not_permitted"),
            ({"type": "board_card_get", "id": "../../etc"}, "unknown_type"),
            ({"type": "board_move", "id": "K7Q2", "status": "finished"}, "unknown_type"),
            ({"type": "board_comment", "id": "K7Q2", "text": "x" * 8001}, "unknown_type"),
            ({"type": "board_chat", "text": "hello"}, "unknown_type"),
            ("board_open", "unknown_type"),
        ]

        async def main():
            async with Harness() as harness:
                phone, _ = await harness.device()
                for number, (request, code) in enumerate(refused):
                    await phone.send({"t": "board_request", "rid": number, "request": request})
                    answer = await next_board_event(phone)
                    self.assertEqual(answer["rid"], number)
                    self.assertEqual(answer["event"]["event"], "error", request)
                    self.assertEqual(answer["event"]["code"], code, request)
                    self.assertEqual(answer["event"]["source"], "hub")
                await asyncio.sleep(0.2)
                self.assertEqual(harness.requests(), [])
                # A request with no rid has nothing to be answered under: a plain wire error.
                await phone.send({"t": "board_request", "request": {"type": "board_open"}})
                with self.assertRaises(wire.WireError) as caught:
                    await phone.expect("board_event", timeout=5)
                self.assertEqual(caught.exception.code, "unknown_type")
                self.assertEqual(harness.requests(), [])
                await phone.close()
        run(main())

    def test_view_and_agent_devices_are_refused_and_told_nothing(self):
        async def main():
            async with Harness() as harness:
                owner, _ = await harness.device(wire.FULL, name="iPad")
                self.assertIn("board", owner.welcome["features"])
                for capability in (wire.VIEW, wire.AGENT):
                    lesser, _ = await harness.device(capability, name=capability)
                    self.assertNotIn("board", lesser.welcome["features"], capability)
                    await lesser.send({"t": "board_request", "rid": 1,
                                       "request": {"type": "board_open"}})
                    with self.assertRaises(wire.WireError) as caught:
                        await lesser.expect("board_event", timeout=5)
                    self.assertEqual(caught.exception.code, "not_permitted", capability)
                    harness.host.board_event_from_gui({"t": "board_event", "rid": None,
                                                       "event": board_event()})
                    await owner.expect("board_event")
                    self.assertFalse([m for m in await drain(lesser, 0.5)
                                      if m.get("t") == "board_event"], capability)
                    await lesser.close()
                self.assertEqual(harness.requests(), [])
                await owner.close()
        run(main())

    def test_a_request_refused_at_the_gate_is_in_the_audit_log_without_its_text(self):
        """A guest's, and a `view` device's: neither reaches the Switchboard's own handler, which
        is where `board_refused` used to be written, so the hosted drive found no line for the
        one asker the owner most wants to hear about. Once a minute per channel, not per try."""
        async def main():
            async with Harness() as harness:
                lesser, record = await harness.device(wire.VIEW, name="view")
                guest = await harness.guest(wire.EDITOR)
                await guest.send({"t": "pane_focus", "pane": "p1"})
                for _ in range(3):
                    await lesser.send({"t": "board_request", "rid": 1, "request": {
                        "type": "board_comment", "id": "K7Q2", "text": "the merger closes on Friday"}})
                    await guest.send({"t": "board_request", "rid": 1, "request": {
                        "type": "board_delete", "id": "K7Q2", "reason": "covering my tracks"}})
                for client in (lesser, guest):
                    with self.assertRaises(wire.WireError) as caught:
                        await client.expect("board_event", timeout=5)
                    self.assertEqual(caught.exception.code, "not_permitted")
                await drain(lesser, 0.3)
                await drain(guest, 0.3)
                lines = [line for line in harness.audit_lines() if line["kind"] == "board_refused"]
                self.assertEqual(sorted((l.get("type"), l.get("code"), l.get("device"),
                                         bool(l.get("participant"))) for l in lines),
                                 sorted([("board_comment", "not_permitted", record.device_id, False),
                                         ("board_delete", "not_permitted", None, True)]))
                written = json.dumps(lines)
                for leak in ("merger", "covering", "K7Q2"):
                    self.assertNotIn(leak, written, leak)
                self.assertEqual(harness.requests(), [])
                await lesser.close()
                await guest.close()
        run(main())

    def test_a_guest_is_refused_and_is_never_sent_an_event(self):
        async def main():
            async with Harness() as harness:
                owner, _ = await harness.device()
                for role in (wire.VIEWER, wire.EDITOR):
                    guest = await harness.guest(role)
                    await guest.send({"t": "pane_focus", "pane": "p1"})
                    await guest.send({"t": "board_request", "rid": 1,
                                      "request": {"type": "board_open"}})
                    with self.assertRaises(wire.WireError) as caught:
                        await guest.expect("board_event", timeout=5)
                    self.assertEqual(caught.exception.code, "not_permitted", role)
                    # …and a forged event is not something a client sends at all.
                    await guest.send({"t": "board_event", "rid": None, "event": board_event()})
                    with self.assertRaises(wire.WireError):
                        await guest.expect("board_event", timeout=5)
                    harness.host.board_event_from_gui({"t": "board_event", "rid": None,
                                                       "event": board_event()})
                    await owner.expect("board_event")
                    self.assertFalse([m for m in await drain(guest, 0.5)
                                      if m.get("t") == "board_event"], role)
                    await guest.close()
                self.assertEqual(harness.requests(), [])
                await owner.close()
        run(main())

    def test_a_downgrade_lands_on_the_very_next_event(self):
        async def main():
            async with Harness() as harness:
                phone, record = await harness.device()
                await phone.send({"t": "board_request", "rid": 5, "request": {"type": "board_open"}})
                line = await harness.settle()
                harness.devices.set_capability(record.device_id, wire.AGENT)
                harness.host.board_event_from_gui({"t": "board_event", "rid": line["rid"],
                                                   "event": board_event()})
                harness.host.board_event_from_gui({"t": "board_event", "rid": None,
                                                   "event": board_event()})
                self.assertFalse([m for m in await drain(phone, 0.6) if m.get("t") == "board_event"])
                await phone.close()
        run(main())

    def test_a_desktop_with_no_gui_has_no_switchboard(self):
        class NoGui:
            """The harness's source as a hub with no GUI behind it sees one: no `send`."""

            def __init__(self, source):
                self._source = source

            def __getattr__(self, name):
                if name == "send":
                    raise AttributeError(name)
                return getattr(self._source, name)

        async def main():
            async with Harness() as harness:
                harness.host.source = NoGui(harness.source)
                harness.host.pane_state = False
                phone, _ = await harness.device()
                self.assertNotIn("board", phone.welcome["features"])
                await phone.send({"t": "board_request", "rid": 1, "request": {"type": "board_open"}})
                answer = await next_board_event(phone)
                self.assertEqual(answer["event"]["code"], "not_permitted")
                await phone.close()
        run(main())


class EventPathTests(unittest.TestCase):
    def test_an_answer_goes_to_the_device_that_asked_under_its_own_rid(self):
        async def main():
            async with Harness() as harness:
                phone, _ = await harness.device(name="iPhone")
                tablet, _ = await harness.device(name="iPad")
                await tablet.send({"t": "board_request", "rid": "warm-up",
                                   "request": {"type": "board_refresh"}})
                await harness.settle(1)
                await phone.send({"t": "board_request", "rid": "open-1",
                                  "request": {"type": "board_open"}})
                line = await harness.settle(2)
                self.assertEqual(line["rid"], 2, "the hub's own rid, not the device's")
                harness.host.board_event_from_gui({"t": "board_event", "rid": line["rid"],
                                                   "event": board_event()})
                answer = await phone.expect("board_event")
                self.assertEqual(answer["rid"], "open-1")
                self.assertEqual(answer["event"]["event"], "board")
                text = json.dumps(answer)
                for leak in ("/home", '"path"', '"root"', '"folder"', '"workspace"'):
                    self.assertNotIn(leak, text, leak)
                self.assertFalse([m for m in await drain(tablet, 0.5) if m.get("t") == "board_event"],
                                 "a read's answer is the asker's alone")
                # One request, several events: the batches that follow use the same rid.
                harness.host.board_event_from_gui({"t": "board_event", "rid": line["rid"], "event": {
                    "event": "board_cards", "rev": 7, "cards": [row("SWPH")], "more": False}})
                self.assertEqual((await phone.expect("board_event"))["rid"], "open-1")
                await phone.close()
                await tablet.close()
        run(main())

    def test_a_change_reaches_every_full_device_and_the_asker_once(self):
        async def main():
            async with Harness() as harness:
                phone, _ = await harness.device(name="iPhone")
                tablet, _ = await harness.device(name="iPad")
                await phone.send({"t": "board_request", "rid": 9, "request": {
                    "type": "board_move", "id": "K7Q2", "status": "planned"}})
                line = await harness.settle()
                changed = {"event": "board_changed", "id": "remote-9", "rev": 8,
                           "root": f"{HOME}/issues", "upserts": [row("K7Q2", status="planned")],
                           "removed": [], "problems": []}
                harness.host.board_event_from_gui({"t": "board_event", "rid": line["rid"],
                                                   "event": changed})
                mine = [m for m in await drain(phone, 0.6) if m.get("t") == "board_event"]
                theirs = [m for m in await drain(tablet, 0.6) if m.get("t") == "board_event"]
                self.assertEqual([m["rid"] for m in mine], [9])
                self.assertEqual([m["rid"] for m in theirs], [None])
                self.assertEqual(theirs[0]["event"]["upserts"][0]["status"], "planned")
                # Nobody asked: everybody hears.
                harness.host.board_event_from_gui({"t": "board_event", "rid": None, "event": changed})
                for client in (phone, tablet):
                    self.assertIsNone((await client.expect("board_event"))["rid"])
                await phone.close()
                await tablet.close()
        run(main())

    def test_an_unknown_event_type_is_dropped_and_counted(self):
        async def main():
            async with Harness() as harness:
                phone, _ = await harness.device()
                for event in ({"event": "board_state", "root": HOME, "project": HOME},
                              {"event": "board_state"}, {"event": "board_created", "workspace": HOME},
                              "not an object"):
                    harness.host.board_event_from_gui({"t": "board_event", "rid": None, "event": event})
                self.assertFalse([m for m in await drain(phone, 0.5) if m.get("t") == "board_event"])
                self.assertEqual(dict(harness.host._board_book().dropped),
                                 {"board_state": 2, "board_created": 1, "(not an event)": 1})
                await phone.close()
        run(main())

    def test_an_answer_for_a_device_that_left_reaches_nobody_but_a_change_still_fans_out(self):
        async def main():
            async with Harness() as harness:
                phone, _ = await harness.device(name="iPhone")
                tablet, _ = await harness.device(name="iPad")
                await phone.send({"t": "board_request", "rid": 1, "request": {
                    "type": "board_comment", "id": "K7Q2", "text": "yes"}})
                line = await harness.settle()
                await phone.close()
                await asyncio.sleep(0.5)
                harness.host.board_event_from_gui({"t": "board_event", "rid": line["rid"], "event": {
                    "event": "board_written", "kind": "comment", "card_id": "K7Q2"}})
                harness.host.board_event_from_gui({"t": "board_event", "rid": line["rid"], "event": {
                    "event": "board_thread_appended", "card_id": "K7Q2",
                    "entry": {"entry_id": "e1", "author": "owner", "kind": "note", "text": "yes"}}})
                # A rid the hub never minted is nobody's answer either.
                harness.host.board_event_from_gui({"t": "board_event", "rid": 4242, "event": {
                    "event": "board_card", "card_id": "K7Q2"}})
                seen = [m for m in await drain(tablet, 0.8) if m.get("t") == "board_event"]
                self.assertEqual([(m["rid"], m["event"]["event"]) for m in seen],
                                 [(None, "board_thread_appended")])
                await tablet.close()
        run(main())

    def test_a_gui_that_says_nothing_is_an_error_not_a_spinner(self):
        async def main():
            async with Harness() as harness:
                host_mod.BOARD_ANSWER_TIMEOUT, before = 0.3, host_mod.BOARD_ANSWER_TIMEOUT
                try:
                    phone, _ = await harness.device()
                    await phone.send({"t": "board_request", "rid": 3,
                                      "request": {"type": "board_open"}})
                    answer = await next_board_event(phone)
                    self.assertEqual((answer["rid"], answer["event"]["code"]), (3, "busy"))
                    # One that was answered in time is left alone.
                    await phone.send({"t": "board_request", "rid": 4,
                                      "request": {"type": "board_open"}})
                    line = await harness.settle(2)
                    harness.host.board_event_from_gui({"t": "board_event", "rid": line["rid"],
                                                       "event": board_event()})
                    seen = [m for m in await drain(phone, 0.8) if m.get("t") == "board_event"]
                    self.assertEqual([m["event"]["event"] for m in seen], ["board"])
                    await phone.close()
                finally:
                    host_mod.BOARD_ANSWER_TIMEOUT = before
        run(main())

    def test_the_sidecar_passes_the_line_to_the_hub(self):
        async def main():
            sidecar = gui_host.Sidecar()
            got = []

            class Hub:
                def board_event_from_gui(self, message):
                    got.append(message)

            sidecar.host = Hub()
            await sidecar.handle({"t": "board_event", "rid": None, "event": {"event": "board"}})
            self.assertEqual(len(got), 1)
            sidecar.host = None
            await sidecar.handle({"t": "board_event", "rid": None, "event": {}})   # not started
        run(main())


class LimitAndAuditTests(unittest.TestCase):
    def test_reads_and_writes_have_their_own_bucket_per_device(self):
        book = board_state.Book()
        now = 1000.0
        reads = sum(book.allow("phone", "board_card_get", now) for _ in range(100))
        writes = sum(book.allow("phone", "board_comment", now) for _ in range(100))
        self.assertEqual((reads, writes), (int(board_state.READ_BURST), int(board_state.WRITE_BURST)))
        self.assertTrue(book.allow("tablet", "board_comment", now), "the limit is per device")
        # A second later: ten more reads and two more writes, and no more than that.
        later = now + 1.0
        self.assertEqual(sum(book.allow("phone", "board_open", later) for _ in range(100)), 10)
        self.assertEqual(sum(book.allow("phone", "board_move", later) for _ in range(100)), 2)
        self.assertEqual((board_state.READ_RATE, board_state.WRITE_RATE), (10.0, 2.0))
        # Idle for an hour is a full bucket, not an hour's worth of tokens.
        self.assertEqual(sum(book.allow("phone", "board_ask", later + 3600) for _ in range(100)),
                         int(board_state.WRITE_BURST))

    def test_the_hub_limits_writes_and_nothing_past_the_limit_reaches_the_gui(self):
        async def main():
            async with Harness() as harness:
                phone, _ = await harness.device()
                for number in range(12):
                    await phone.send({"t": "board_request", "rid": number, "request": {
                        "type": "board_comment", "id": "K7Q2", "text": f"comment {number}"}})
                limited = 0
                for _ in range(12 - int(board_state.WRITE_BURST)):
                    answer = await next_board_event(phone)
                    if answer["event"].get("code") == "rate_limited":
                        limited += 1
                await asyncio.sleep(0.2)
                passed = len(harness.requests())
                self.assertGreaterEqual(limited, 6)
                self.assertLessEqual(passed, int(board_state.WRITE_BURST) + 2)
                self.assertEqual(passed + limited, 12)
                await phone.close()
        run(main())

    def test_the_coarse_wire_limit_never_bites_before_the_buckets(self):
        count, window = host_mod.LIMITS["board_request"]
        self.assertGreaterEqual(count / window, board_state.READ_RATE + board_state.WRITE_RATE)

    def test_the_audit_names_the_type_the_card_and_the_device_and_never_the_text(self):
        async def main():
            async with Harness() as harness:
                phone, record = await harness.device()
                await phone.send({"t": "board_request", "rid": 1, "request": {
                    "type": "board_comment", "id": "K7Q2", "text": "the merger closes on Friday"}})
                await phone.send({"t": "board_request", "rid": 2, "request": {
                    "type": "board_create", "title": "a secret project", "request": "hire nobody"}})
                await phone.send({"t": "board_request", "rid": 3, "request": {
                    "type": "board_delete", "id": "K7Q2", "reason": "covering my tracks"}})
                await phone.send({"t": "board_request", "rid": 4, "request": {
                    "type": "x" * 500, "text": "whatever this is"}})
                await harness.settle(2)
                for _ in range(2):
                    await next_board_event(phone)
                lines = [line for line in harness.audit_lines() if line["kind"].startswith("board_")]
                self.assertEqual([(l["kind"], l.get("type"), l.get("card"), l["device"]) for l in lines], [
                    ("board_request", "board_comment", "K7Q2", record.device_id),
                    ("board_request", "board_create", None, record.device_id),
                    ("board_refused", "board_delete", None, record.device_id),
                    ("board_refused", "unknown", None, record.device_id)])
                written = json.dumps(lines)
                for leak in ("merger", "secret project", "hire nobody", "covering", "whatever", "xxxx"):
                    self.assertNotIn(leak, written, leak)
                await phone.close()
        run(main())


# ---- a card that starts waiting on the owner ---------------------------------------------------------------

class CardWaitingTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.devices = identity_mod.DeviceStore(Path(self.temporary.name))
        _, public = noise.generate_keypair()
        self.device = self.devices.pair(public, "iPhone", "Safari", wire.FULL)
        self.wants(*notify.KINDS)
        self.fired: list[dict] = []
        self.notifier = notify.Notifier(self.devices, send=None, spawn=self.fired.append)
        self.notifier.deliver = lambda body: body          # the body itself, not a coroutine
        # The notifier's clock, and only the notifier's: `notify.time` is the `time` module, and
        # freezing `time.monotonic` itself would stop every asyncio timer in the process.
        self.clock = 5000.0
        self.real_time = notify.time
        notify.time = SimpleNamespace(monotonic=lambda: self.clock)

    def tearDown(self):
        notify.time = self.real_time
        self.temporary.cleanup()

    def wants(self, *kinds):
        self.devices.set_push(self.device.device_id, {"endpoint": "https://push.example.com/x",
                                                      "p256dh": "", "auth": "", "key": "",
                                                      "kinds": list(kinds)})

    def feed(self, event):
        self.notifier.on_board(board_state.clean_event(event))

    def changed(self, card, waiting_on, **more):
        self.feed({"event": "board_changed", "rev": 9, "root": f"{HOME}/issues",
                   "upserts": [row(card, waiting_on=waiting_on)], "removed": [], **more})

    def test_nothing_on_first_sight_of_a_board(self):
        self.feed(board_event())                     # M3XJ is already waiting on the owner
        self.feed(board_event())                     # …and a second phone opening it changes nothing
        self.assertEqual(self.fired, [])

    def test_it_fires_once_on_the_change_with_a_body_the_hub_wrote(self):
        self.feed(board_event())
        self.changed("K7Q2", "owner")
        self.assertEqual(self.fired, [{"v": 1, "kind": "card_waiting", "pane": "", "card": "K7Q2",
                                       "title": "A card is waiting on you", "body": "#K7Q2"}])
        self.clock += 600
        self.changed("K7Q2", "owner")                # still waiting: not a change
        self.feed(board_event(cards=[row("K7Q2", waiting_on="owner")]))
        self.feed(card_event("K7Q2", front={"waiting_on": "owner"}))
        # The board tools' own `board_changed` names the card without its row: nothing to read.
        self.feed({"event": "board_changed", "upserts": ["K7Q2"], "removed": [], "write_id": "w-1"})
        self.assertEqual(len(self.fired), 1)

    def test_a_known_card_that_changed_between_two_snapshots_fires_and_a_new_one_does_not(self):
        self.feed(board_event())
        self.feed(board_event(cards=[row("K7Q2", waiting_on="owner"), row("M3XJ", waiting_on="owner"),
                                     row("ZZZZ", waiting_on="owner")]))
        self.assertEqual([body["card"] for body in self.fired], ["K7Q2"])

    def test_the_body_never_carries_the_title_or_the_text(self):
        self.feed(board_event())
        self.feed({"event": "board_changed", "root": f"{HOME}/issues", "upserts": [
            row("K7Q2", waiting_on="owner", title="Fire the contractor before Friday")]})
        self.assertEqual(len(self.fired), 1)
        text = json.dumps(self.fired)
        for leak in ("Fire", "contractor", "Friday", "issues", "/home"):
            self.assertNotIn(leak, text, leak)

    def test_a_card_read_on_its_own_counts_as_a_change_too(self):
        self.feed(board_event())
        self.feed(card_event("K7Q2", front={"id": "K7Q2", "waiting_on": "owner"}))
        self.assertEqual([body["card"] for body in self.fired], ["K7Q2"])

    def test_a_card_seen_before_its_board_is_first_sight_not_a_change(self):
        self.changed("K7Q2", "owner")                # no board yet: nothing to compare with
        self.feed(card_event("SWPH", front={"waiting_on": "owner"}))
        self.assertEqual(self.fired, [])
        self.feed(board_event(more=True))            # the first batch of several is not the board
        self.changed("3XZV", "owner")
        self.assertEqual(self.fired, [])
        self.feed({"event": "board_cards", "root": f"{HOME}/issues", "cards": [row("FR1C")],
                   "more": False})
        self.clock += 60
        self.changed("PH0N", "owner")                # the board is known now: a new card, waiting
        self.assertEqual([body["card"] for body in self.fired], ["PH0N"])

    def test_another_projects_board_is_first_sight_again(self):
        self.feed(board_event())
        self.feed(board_event(root="/srv/other/issues", project="/srv/other",
                              cards=[row("K7Q2", waiting_on="owner"), row("ZZZZ", waiting_on="owner")]))
        self.assertEqual(self.fired, [], "K7Q2 on another board is another card")

    def test_presence(self):
        self.feed(board_event())
        self.notifier.window_active(True)
        self.changed("K7Q2", "owner")
        self.assertEqual(self.fired, [], "you are looking at the desktop")
        self.notifier.window_active(False)
        self.changed("K7Q2", "agent")
        self.clock += 1
        self.changed("K7Q2", "owner")
        self.assertEqual(len(self.fired), 1)

    def test_the_per_card_cooldown_and_the_gap_between_cards(self):
        self.feed(board_event(cards=[row("K7Q2"), row("SWPH"), row("PH0N")]))
        self.changed("K7Q2", "owner")
        self.changed("SWPH", "owner")                # a cleanup moving many cards rings once
        self.assertEqual([body["card"] for body in self.fired], ["K7Q2"])
        self.clock += notify.CARD_GAP + 1
        self.changed("K7Q2", "agent")
        self.changed("K7Q2", "owner")                # the same card again inside its minute
        self.assertEqual(len(self.fired), 1)
        self.changed("PH0N", "owner")                # another card, past the gap
        self.assertEqual([body["card"] for body in self.fired], ["K7Q2", "PH0N"])
        self.clock += notify.CARD_COOLDOWN + 1
        self.changed("K7Q2", "agent")
        self.changed("K7Q2", "owner")
        self.assertEqual([body["card"] for body in self.fired], ["K7Q2", "PH0N", "K7Q2"])

    def test_a_device_that_switched_needs_me_off_is_not_rung_and_spends_no_cooldown(self):
        self.wants("agent_finished", "failed", "plan")
        self.feed(board_event())
        self.changed("K7Q2", "owner")
        self.assertEqual(self.fired, [])
        self.wants("card_waiting")
        self.changed("K7Q2", "agent")
        self.changed("K7Q2", "owner")
        self.assertEqual(len(self.fired), 1)

    def test_a_list_from_before_the_kind_existed_hears_it_with_waiting_input(self):
        self.assertIn("card_waiting", notify.kinds_of({"kinds": ["waiting_input", "password"]}))
        self.assertNotIn("card_waiting", notify.kinds_of({"kinds": ["agent_finished", "plan"]}))
        self.assertEqual(notify.kinds_of({}), list(notify.KINDS))
        self.wants("waiting_input", "password")
        self.feed(board_event())
        self.changed("K7Q2", "owner")
        self.assertEqual(len(self.fired), 1)

    def test_a_removed_card_is_forgotten(self):
        self.feed(board_event())
        self.feed({"event": "board_changed", "root": f"{HOME}/issues", "upserts": [],
                   "removed": ["M3XJ"]})
        key = board_state.board_key(f"{HOME}/issues")
        self.assertNotIn("M3XJ", self.notifier._boards[key]["cards"])

    def test_the_hub_feeds_the_notifier_the_cleaned_event_with_nobody_connected(self):
        async def main():
            async with Harness() as harness:
                seen = []
                harness.host.notifier.on_board = seen.append
                harness.host.board_event_from_gui({"t": "board_event", "rid": None,
                                                   "event": board_event()})
                harness.host.board_event_from_gui({"t": "board_event", "rid": None,
                                                   "event": {"event": "board_state", "root": HOME}})
                self.assertEqual([event["event"] for event in seen], ["board"])
                self.assertNotIn("/home", json.dumps(seen))
        run(main())


class WireTests(unittest.TestCase):
    def test_the_two_types_are_classified(self):
        self.assertEqual(wire.CLIENT_TYPES["board_request"], wire.FULL)
        self.assertIn("board_request", wire.GUEST_NEVER)
        self.assertNotIn("board_request", wire.GUEST_TYPES)
        self.assertIn("board_event", wire.SERVER_TYPES)
        self.assertFalse(wire.may_send_to_guest("board_event"))
        self.assertIn("board_event", wire.NEVER_FROM_CLIENT)
        self.assertIn("_on_board_request", host_mod.Host.__dict__)

    def test_no_board_request_is_a_wire_type_of_its_own(self):
        for kind in list(board_state.REQUESTS) + list(board_state.NEVER):
            self.assertNotIn(kind, wire.CLIENT_TYPES, kind)


if __name__ == "__main__":
    unittest.main()
