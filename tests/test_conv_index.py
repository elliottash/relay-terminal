"""Conversation index and the conversation-list protocol commands (protocol section 14).

Everything runs against temporary directories: no test may touch the real index, the keyring or
the network. `relay_home` points XDG_DATA_HOME at a temporary directory for the whole test.
"""
import json
import os
import sqlite3
import sys
import tempfile
import threading
import time
import unittest
from pathlib import Path
from unittest import mock

from relay_core import conv_index
from relay_core.agent import Agent
from relay_core.conv_index import ConversationIndex, MAX_CODES
from relay_core.provider import ProviderConfig
from relay_core.queue import TurnSupervisor
from relay_core.session_protocol import SessionCommands
from relay_core.sessions import SessionStore

sys.path.insert(0, str(Path(__file__).parent))
from test_queue import Recorder  # noqa: E402
from test_sessions import ScriptedProvider, text  # noqa: E402


def session(session_id="a" * 32, *, workspace="/tmp/alpha", title="Search spike", turns=2,
            updated=2000.0, model="glm-5", open_requests=0, extra_turn=None, summary=None,
            branch=None, files=None, todos=None, ended=True, mode="build"):
    messages = [
        {"role": "user", "content": "how do I search the scrollback"},
        {"role": "assistant", "content": "Use the pelican finder",
         "tool_calls": [{"id": "c1", "type": "function",
                         "function": {"name": "run_command", "arguments": '{"command": "rg needle"}'}}]},
        {"role": "tool", "tool_call_id": "c1", "content": "needle found in scrollback.txt"},
    ]
    written = {path: {"before": None, "after": "f" * 64} for path in (files or [])}
    items = [{"turn": 1, "prompt": "how do I search the scrollback", "prompt_preview": "how do I",
              "time": 1000.0, "locations": {"0": 1}, "files": written}]
    if extra_turn:
        items.append({"turn": 2, "prompt": extra_turn, "prompt_preview": extra_turn[:20],
                      "time": 1500.0, "locations": {"0": len(messages) + 1}, "files": {}})
        messages += [{"role": "user", "content": extra_turn},
                     {"role": "assistant", "content": "answered " + extra_turn}]
    if ended:
        for item in items:
            item["ended"] = item["time"] + 10
    data = {"version": 1, "kind": "relay_session", "id": session_id, "title": title,
            "created": 900.0, "updated": updated, "workspace": workspace, "model": model,
            "preset": "glm", "effort": "high", "mode": mode, "turns": turns, "epoch": 0,
            "messages": messages, "snapshots": {}, "checkpoints": {"items": items},
            "requests": {"items": []}, "todos": {"items": list(todos or [])}, "plan_path": None,
            "open_requests": open_requests}
    if summary is not None:
        data["summary"] = summary
        data["summary_turn"] = turns
    if branch is not None:
        data["branch"] = branch
    return data


class HelperTests(unittest.TestCase):
    def test_fts_query_escapes_operators_and_prefixes_words(self):
        self.assertEqual(conv_index.fts_query('pelican'), '"pelican"*')
        self.assertEqual(conv_index.fts_query('"exact phrase" more'), '"exact phrase" AND "more"*')
        # FTS5 operators and punctuation never reach the engine as syntax.
        self.assertEqual(conv_index.fts_query('a OR b'), '"a"* AND "OR"* AND "b"*')
        self.assertEqual(conv_index.fts_query('NEAR("x" "y")'), '"NEAR"* AND "x"* AND "y"')
        self.assertEqual(conv_index.fts_query('   '), '')
        self.assertEqual(conv_index.fts_query('"unterminated'), '"unterminated"*')

    def test_query_terms(self):
        self.assertEqual(conv_index.query_terms('"two words" third'), ['two words', 'third'])
        self.assertEqual(conv_index.query_terms(''), [])

    def test_match_line_picks_the_line_and_merges_ranges(self):
        line, ranges = conv_index.match_line("first line\nsecond has needle and needle\nthird", ["needle"])
        self.assertEqual(line, "second has needle and needle")
        self.assertEqual(ranges, [[11, 6], [22, 6]])
        line, ranges = conv_index.match_line("alpha beta", ["alpha", "alp"])
        self.assertEqual(ranges, [[0, 5]])
        self.assertEqual(conv_index.match_line("nothing here", ["zzz"]), ("nothing here", []))

    def test_match_line_windows_long_lines_around_the_match(self):
        line, ranges = conv_index.match_line("x" * 400 + " needle " + "y" * 400, ["needle"])
        self.assertLessEqual(len(line), 210)
        self.assertEqual(line[ranges[0][0]:ranges[0][0] + ranges[0][1]], "needle")

    def test_turn_marks_map_messages_to_turns(self):
        data = session(extra_turn="what about pelicans")
        marks = conv_index.turn_marks(data)
        self.assertEqual(marks, [(1, 1), (4, 2)])
        self.assertEqual(conv_index.turn_for(marks, 1), 1)
        self.assertEqual(conv_index.turn_for(marks, 3), 1)
        self.assertEqual(conv_index.turn_for(marks, 9), 2)

    def test_session_entries_cover_every_kind_without_duplicating_prompts(self):
        rows = conv_index.session_entries(session())
        self.assertEqual([r["kind"] for r in rows], ["prompt", "reply", "tool_call", "tool_output"])
        self.assertEqual(rows[0]["turn"], 1)
        self.assertIn("run_command", rows[2]["text"])

    def test_relay_context_blocks_are_not_indexed_as_prompts(self):
        data = session()
        data["messages"].append({"role": "user", "content": conv_index.RELAY_CONTEXT + "\nthe pane runs bash"})
        kinds = [(r["kind"], r["text"][:20]) for r in conv_index.session_entries(data)]
        self.assertNotIn("the pane runs bash", " ".join(text for _, text in kinds))

    def test_parse_query_splits_operators_from_free_text(self):
        parsed = conv_index.parse_query('pelican project:relay model:glm has:edits is:pinned')
        self.assertEqual(parsed["text"], "pelican")
        self.assertEqual(parsed["operators"],
                         [{"key": "project", "value": "relay"}, {"key": "model", "value": "glm"},
                          {"key": "has", "value": "edits"}, {"key": "is", "value": "pinned"}])
        self.assertEqual(parsed["ignored"], [])

    def test_parse_query_reads_quoted_operator_values(self):
        parsed = conv_index.parse_query('file:"my file.py" branch:"feature/one"')
        self.assertEqual([op["value"] for op in parsed["operators"]], ["my file.py", "feature/one"])
        self.assertEqual(parsed["text"], "")

    def test_parse_query_keeps_unknown_keys_as_plain_text(self):
        parsed = conv_index.parse_query('https://example.com/x colour:teal needle')
        self.assertEqual(parsed["operators"], [])
        self.assertEqual(parsed["ignored"], [])
        self.assertIn("colour:teal", parsed["text"])
        self.assertIn("https://example.com/x", parsed["text"])

    def test_parse_query_reports_unusable_operator_values_as_ignored(self):
        parsed = conv_index.parse_query('has:wombat is: before:not-a-date after:2026-13-40')
        self.assertEqual(parsed["operators"], [])
        self.assertEqual(parsed["ignored"], ["has:wombat", "is:", "before:not-a-date", "after:2026-13-40"])

    def test_parse_query_negation(self):
        parsed = conv_index.parse_query('needle -pelican -"two words" -model:kimi')
        self.assertEqual(parsed["text"], "needle")
        self.assertEqual(parsed["operators"],
                         [{"key": "text", "value": "pelican", "negated": True},
                          {"key": "text", "value": "two words", "negated": True},
                          {"key": "model", "value": "kimi", "negated": True}])
        self.assertEqual(conv_index.negated_parts(parsed["operators"]), ['"pelican"*', '"two words"'])

    def test_parse_date_forms(self):
        now = time.mktime((2026, 9, 18, 15, 30, 0, 0, 0, -1))
        self.assertEqual(conv_index.parse_date("today", now), time.mktime((2026, 9, 18, 0, 0, 0, 0, 0, -1)))
        self.assertEqual(conv_index.parse_date("yesterday", now), time.mktime((2026, 9, 17, 0, 0, 0, 0, 0, -1)))
        self.assertEqual(conv_index.parse_date("2026-09-01", now), time.mktime((2026, 9, 1, 0, 0, 0, 0, 0, -1)))
        self.assertEqual(conv_index.parse_date("7d", now), now - 7 * 86400)
        for bad in ("", "tomorrow", "2026-13-01", "2026-09-99", "d", "12"):
            self.assertIsNone(conv_index.parse_date(bad, now), bad)

    def test_like_value_escapes_its_own_wildcards(self):
        self.assertEqual(conv_index.like_value("100%_x"), r"%100\%\_x%")

    def test_unfinished_reads_checkpoints_messages_and_todos(self):
        done = session(extra_turn="and then")           # ends with an assistant reply
        self.assertFalse(conv_index.session_unfinished(done))
        # The fixture's last message is tool output nobody answered: an interrupted turn.
        self.assertTrue(conv_index.session_unfinished(session()))
        stopped = session(extra_turn="and then")        # turn 1 closed, turn 2 never did
        stopped["checkpoints"]["items"][-1].pop("ended")
        self.assertTrue(conv_index.session_unfinished(stopped))
        waiting = session(extra_turn="and then")
        waiting["messages"].append({"role": "user", "content": "and then?"})
        self.assertTrue(conv_index.session_unfinished(waiting))
        self.assertTrue(conv_index.session_unfinished(
            session(extra_turn="and then", todos=[{"id": "t1", "text": "ship it", "status": "pending"}])))
        self.assertFalse(conv_index.session_unfinished(
            session(extra_turn="and then", todos=[{"id": "t1", "text": "ship it", "status": "completed"}])))
        # A session from before checkpoints carried `ended` is not unfinished just for that.
        old = session(extra_turn="and then", ended=False)
        for item in old["checkpoints"]["items"]:
            item.pop("ended", None)
        self.assertFalse(conv_index.session_unfinished(old))

    def test_turn_left_open_reads_only_the_checkpoint_stamps(self):
        # The pure cut-off predicate behind `state_loaded {turn_open}` (#SXF1): the last
        # checkpoint lacks `ended`, in a session that carries the field at all.
        self.assertFalse(conv_index.turn_left_open(session(extra_turn="and then")))   # every turn ended
        stopped = session(extra_turn="and then")        # turn 1 closed, turn 2 never did
        stopped["checkpoints"]["items"][-1].pop("ended")
        self.assertTrue(conv_index.turn_left_open(stopped))
        # A user message nothing answered is the *messages* arm of session_unfinished, not this.
        waiting = session(extra_turn="and then")
        waiting["messages"].append({"role": "user", "content": "and then?"})
        self.assertFalse(conv_index.turn_left_open(waiting))
        # A session from before `ended` existed never counts as cut off on its own.
        old = session(extra_turn="and then", ended=False)
        for item in old["checkpoints"]["items"]:
            item.pop("ended", None)
        self.assertFalse(conv_index.turn_left_open(old))

    def test_session_files_lists_written_paths_newest_first(self):
        data = session(files=["/tmp/alpha/a.py", "/tmp/alpha/b.py"], extra_turn="more")
        data["checkpoints"]["items"][1]["files"] = {"/tmp/alpha/c.py": {"before": None, "after": "x"},
                                                    "/tmp/alpha/skipped.py": {"before": None, "after": None}}
        self.assertEqual(conv_index.session_files(data)[0], "/tmp/alpha/c.py")
        self.assertNotIn("/tmp/alpha/skipped.py", conv_index.session_files(data))
        self.assertEqual(len(conv_index.session_files(data)), 3)

    def test_terminal_id_is_stable_and_recognised(self):
        first = conv_index.terminal_id("/tmp/alpha")
        self.assertEqual(first, conv_index.terminal_id("/tmp/alpha"))
        self.assertNotEqual(first, conv_index.terminal_id("/tmp/beta"))
        self.assertTrue(conv_index.TERMINAL_ID.match(first))

    def test_workspace_digest_matches_the_gui_project_key(self):
        """The GUI derives the same key in C++ (`relay::projects::keyFor()`, src/Projects.cpp), and
        a project's conversations are found by it, so the two implementations have to agree
        character for character. One literal pins them: it is absolute, it does not exist and
        nothing above it is a symlink, so neither side's canonicalisation can wander. The same pair
        is asserted in tests/projects_test.cpp — change one and the other fails."""
        self.assertEqual(conv_index.workspace_digest("/nonexistent/relay-projects-key-test"),
                         "9cfae240c229914d")
        # And the canonicalisation the C++ side mirrors: a trailing slash, a `.` and a `..` are not
        # a different project.
        for spelling in ("/nonexistent/relay-projects-key-test/",
                         "/nonexistent/./relay-projects-key-test",
                         "/nonexistent/x/../relay-projects-key-test"):
            self.assertEqual(conv_index.workspace_digest(spelling), "9cfae240c229914d", spelling)
        self.assertEqual(len(conv_index.workspace_digest("/nonexistent/relay-projects-key-test")), 16)


class IndexTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.index = ConversationIndex(self.root / "index.db")
        self.addCleanup(self.index.close)

    def test_build_and_search_finds_prompts_replies_and_tool_output(self):
        self.index.update_session(session(), self.root)
        for query, kind in (("scrollback", "prompt"), ("pelican", "reply"),
                            ("rg", "tool_call"), ("scrollback.txt", "tool_output")):
            result = self.index.search(query, scope="project", workspace="/tmp/alpha")
            self.assertEqual(len(result["items"]), 1, query)
            self.assertIn(kind, [m["kind"] for m in result["items"][0]["matches"]], query)

    def test_database_file_is_private(self):
        self.assertEqual(os.stat(self.root / "index.db").st_mode & 0o777, 0o600)

    def test_card_codes_are_collected_and_returned(self):
        # (#G2C7) The #card codes a conversation mentions ride every item, so the Sessions pane
        # can filter by a code and draw its chips without reading the conversation.
        data = session()
        data["messages"].append({"role": "user", "content": "this is #G2C7 and #pv7w work"})
        data["title"] = "sorting for #MDSG"
        self.index.update_session(data, self.root)
        for result in (self.index.search("", scope="all"),
                       self.index.search("", scope="all", meta_only=True),
                       self.index.search("G2C7", scope="all")):
            self.assertEqual(result["items"][0]["codes"], ["G2C7", "MDSG", "PV7W"], result)
        # A paste of an index cannot balloon the set.
        flooded = session()
        flooded["messages"] = [{"role": "user",
                                "content": " ".join(f"#A{i:03d}" for i in range(400))}]
        self.index.update_session(flooded, self.root)
        codes = [item["codes"] for item in self.index.search("", scope="all")["items"]
                 if item["session_id"] == flooded["id"]][0]
        self.assertEqual(len(codes), MAX_CODES)

    def test_meta_only_lists_every_conversation_once(self):
        # (#G2C7) The instant filter's light listing: every conversation under the same filters
        # the pane uses, no text pass, no matches, codes included.
        self.index.update_session(session(), self.root)
        other = session()
        other["id"] = "f" * 32
        other["title"] = "another one"
        self.index.update_session(other, self.root)
        meta = self.index.search("", scope="all", meta_only=True)
        self.assertEqual(len(meta["items"]), 2)
        self.assertEqual([item["matches"] if "matches" in item else [] for item in meta["items"]],
                         [[], []])
        self.assertEqual(meta["total"], 2)
        # A source filter still applies to the meta listing.
        threads = self.index.search("", scope="all", meta_only=True, sources=["agent"])
        self.assertEqual(len(threads["items"]), 2)

    def test_v8_database_gains_its_codes_in_place(self):
        # (#G2C7) A v8 index opens as v9 without being discarded: the codes are backfilled from
        # the entries it already holds, so the terminal history (which has no file to rebuild
        # from) survives the upgrade.
        data = session()
        data["messages"].append({"role": "user", "content": "see #G2C7"})
        self.index.update_session(data, self.root)
        self.index.close()
        connection = sqlite3.connect(self.root / "index.db")
        connection.execute("ALTER TABLE conversations DROP COLUMN codes")
        connection.execute("UPDATE meta SET value='8' WHERE key='schema_version'")
        connection.commit()
        connection.close()
        index = ConversationIndex(self.root / "index.db")
        self.addCleanup(index.close)
        self.assertEqual(index.migrated_from, 8)
        self.assertEqual(index.search("", scope="all")["items"][0]["codes"], ["G2C7"])

    def test_prefix_and_phrase_queries(self):
        self.index.update_session(session(), self.root)
        self.assertEqual(len(self.index.search("pelic", scope="all")["items"]), 1)      # prefix
        self.assertEqual(len(self.index.search('"pelican finder"', scope="all")["items"]), 1)
        self.assertEqual(len(self.index.search('"finder pelican"', scope="all")["items"]), 0)
        # Several words are an AND over the whole conversation (since v2, card #R6J0): "pelican" is
        # in a reply and "scrollback" in the prompt, and the conversation still matches.
        self.assertEqual(len(self.index.search("pelican finder", scope="all")["items"]), 1)
        self.assertEqual(len(self.index.search("pelican scrollback", scope="all")["items"]), 1)
        self.assertEqual(len(self.index.search("pelican zebra", scope="all")["items"]), 0)

    def test_highlight_ranges_point_at_the_match(self):
        self.index.update_session(session(), self.root)
        match = self.index.search("pelican", scope="all")["items"][0]["matches"][0]
        start, length = match["ranges"][0]
        self.assertEqual(match["line"][start:start + length].lower(), "pelican")

    def test_incremental_update_adds_turns_without_duplicating_rows(self):
        self.index.update_session(session(), self.root)
        before = self.index.stats()["entries"]
        self.index.update_session(session(extra_turn="what about pelicans in Brazil", turns=3,
                                          updated=3000.0), self.root)
        after = self.index.stats()
        self.assertGreater(after["entries"], before)
        self.assertEqual(after["conversations"], 1)
        result = self.index.search("scrollback", scope="all")
        # The turn-1 prompt is still indexed exactly once (checkpoint and message are one row).
        self.assertEqual(sum(1 for m in result["items"][0]["matches"] if m["kind"] == "prompt"), 1)
        self.assertEqual(self.index.search("Brazil", scope="all")["items"][0]["matches"][0]["turn"], 2)

    def test_scope_and_filters(self):
        self.index.update_session(session(), self.root)
        self.index.update_session(session("b" * 32, workspace="/tmp/beta", title="Other project",
                                          model="kimi-k2", open_requests=2, updated=5000.0), self.root)
        self.assertEqual(len(self.index.search("scrollback", scope="project", workspace="/tmp/alpha")["items"]), 1)
        self.assertEqual(len(self.index.search("scrollback", scope="all")["items"]), 2)
        self.assertEqual(len(self.index.search("scrollback", scope="all", model="kimi-k2")["items"]), 1)
        self.assertEqual(len(self.index.search("scrollback", scope="all", has_open=True)["items"]), 1)
        self.assertEqual(len(self.index.search("scrollback", scope="all", since=4000.0)["items"]), 1)
        self.assertEqual(len(self.index.search("scrollback", scope="all", until=100.0)["items"]), 0)
        # Newest first, pinned above everything.
        self.assertEqual([i["session_id"] for i in self.index.search("", scope="all")["items"]],
                         ["b" * 32, "a" * 32])
        self.index.set_pinned("a" * 32, True)
        self.assertEqual([i["session_id"] for i in self.index.search("", scope="all")["items"]],
                         ["a" * 32, "b" * 32])
        self.assertRaises(ValueError, self.index.search, "x", scope="everything")

    def test_project_filter_takes_a_folder_and_everything_below_it(self):
        """The Sessions pane's "Project" chooser (#916B): a project is its folder and every
        workspace under it; "no project" is everything under none of the known ones."""
        self.index.update_session(session(), self.root)                                    # /tmp/alpha
        self.index.update_session(session("b" * 32, workspace="/tmp/alpha/sub", title="Sub",
                                          updated=3000.0), self.root)
        self.index.update_session(session("c" * 32, workspace="/tmp/alphabet", title="Not alpha",
                                          updated=4000.0), self.root)
        self.index.update_session(session("d" * 32, workspace="/tmp/beta", title="Beta",
                                          updated=5000.0), self.root)
        ids = lambda result: sorted(i["session_id"][0] for i in result["items"])
        under = self.index.search("", scope="project", workspace="/tmp/beta", project="/tmp/alpha")
        self.assertEqual(ids(under), ["a", "b"])            # the folder and below it, not /tmp/alphabet
        self.assertEqual(under["scope"], "all")             # naming a project overrides the scope
        self.assertEqual(ids(self.index.search("", scope="all", project="/tmp/alpha/")), ["a", "b"])
        outside = self.index.search("", scope="all", outside_projects=["/tmp/alpha", "/tmp/beta"])
        self.assertEqual(ids(outside), ["c"])
        self.assertEqual(ids(self.index.search("", scope="all", outside_projects=["/tmp/alpha"])), ["c", "d"])
        # Wildcards in a folder name are literal, and the total counts what the filter selects.
        self.assertEqual(ids(self.index.search("", scope="all", project="/tmp/al%")), [])
        self.assertEqual(under["total"], 2)

    def test_rename_and_pin_survive_reindexing(self):
        self.index.update_session(session(), self.root)
        self.index.rename("a" * 32, "  My   renamed thread  ")
        self.index.set_pinned("a" * 32, True)
        self.index.update_session(session(updated=9000.0), self.root)
        item = self.index.search("", scope="all")["items"][0]
        self.assertEqual(item["title"], "My renamed thread")
        self.assertEqual(item["generated_title"], "Search spike")
        self.assertEqual(item["pinned"], 1)
        self.index.rename("a" * 32, "")
        self.assertEqual(self.index.search("", scope="all")["items"][0]["title"], "Search spike")

    def test_terminal_history_rows_are_searchable_and_labelled(self):
        rows = self.index.record_commands("/tmp/alpha", [
            {"command": "git status --short", "exit_status": 0, "output": "M src/main.cpp", "time": 100.0},
            {"command": "cmake --build build", "exit_status": 2, "time": 200.0}])
        self.assertEqual(rows, 3)
        result = self.index.search("cmake", scope="project", workspace="/tmp/alpha")
        item = result["items"][0]
        self.assertEqual(item["source"], "terminal")
        self.assertEqual(item["matches"][0]["kind"], "command")
        self.assertEqual(item["turns"], 2)
        self.assertEqual(self.index.search("main.cpp", scope="all")["items"][0]["matches"][0]["kind"],
                         "command_output")
        # Appending keeps the earlier commands and continues the numbering.
        self.index.record_commands("/tmp/alpha", [{"command": "ls -la", "exit_status": 0, "time": 300.0}])
        conversation = self.index.conversation(conv_index.terminal_id("/tmp/alpha"))
        self.assertEqual([e["turn"] for e in conversation["items"]], [1, 1, 2, 3])
        self.assertEqual(conversation["items"][2]["exit_status"], 2)
        self.assertEqual(self.index.record_commands("/tmp/alpha", []), 0)
        self.assertRaises(ValueError, self.index.record_commands, "/tmp/alpha", "no")
        self.assertRaises(ValueError, self.index.record_commands, "/tmp/alpha",
                          [{"command": "x"}] * (conv_index.MAX_COMMANDS + 1))

    def test_sources_filter_separates_terminal_from_agent(self):
        self.index.update_session(session(), self.root)
        self.index.record_commands("/tmp/alpha", [{"command": "grep scrollback README.md", "exit_status": 0}])
        everything = self.index.search("scrollback", scope="all")
        self.assertEqual({i["source"] for i in everything["items"]}, {"agent", "terminal"})
        only_agent = self.index.search("scrollback", scope="all", sources=["agent"])
        self.assertEqual([i["source"] for i in only_agent["items"]], ["agent"])

    def test_recorded_token_sort(self):
        for sid, amount in (("a", 30), ("b", 10), ("c", 20)):
            data = session(sid * 32)
            data["usage"] = {"prompt_tokens": amount, "total_tokens": amount}
            self.index.update_session(data, self.root)
        ids = lambda sort: [i["session_id"][0] for i in self.index.search("", scope="all", sort=sort)["items"]]
        self.assertEqual(ids("tokens_desc"), ["a", "c", "b"])
        self.assertEqual(ids("tokens"), ["b", "c", "a"])

    def test_shortest_requests_title_and_model_sorts(self):
        """Column sorts use stored counts and shown text, with newest-first ties."""
        self.index.update_session(session("a" * 32, title="capybara", turns=9, updated=1000.0,
                                          model="zeta", summary="Zebra recap", open_requests=2), self.root)
        self.index.update_session(session("b" * 32, title="Capybara", turns=1, updated=2000.0,
                                          model="alpha", summary="alpha recap", open_requests=0), self.root)
        self.index.update_session(session("c" * 32, title="wombat", turns=4, updated=3000.0,
                                          model="m", summary="Middle recap", open_requests=1), self.root)
        ids = lambda result: [i["session_id"][0] for i in result["items"]]  # noqa: E731
        self.assertEqual(ids(self.index.search("", scope="all", sort="shortest")), ["b", "c", "a"])
        self.assertEqual(ids(self.index.search("", scope="all", sort="requests_desc")), ["a", "c", "b"])
        self.assertEqual(ids(self.index.search("", scope="all", sort="requests")), ["b", "c", "a"])
        # Case-insensitive over the shown title, ties newest first, and a rename re-keys the row.
        self.assertEqual(ids(self.index.search("", scope="all", sort="title")), ["b", "a", "c"])
        self.assertEqual(ids(self.index.search("", scope="all", sort="title_desc")), ["c", "b", "a"])
        self.index.rename("c" * 32, "aardvark")
        self.assertEqual(ids(self.index.search("", scope="all", sort="title")), ["c", "b", "a"])
        # The Model column's own text: a terminal row sorts as "terminal", not its (empty) model.
        self.assertEqual(ids(self.index.search("", scope="all", sort="model")), ["b", "c", "a"])
        self.assertEqual(ids(self.index.search("", scope="all", sort="model_desc")), ["a", "c", "b"])
        self.assertEqual(ids(self.index.search("", scope="all", sort="summary")), ["b", "c", "a"])
        self.assertEqual(ids(self.index.search("", scope="all", sort="summary_desc")), ["a", "c", "b"])
        self.index.set_summary("c" * 32, "aardvark recap")
        self.assertEqual(ids(self.index.search("", scope="all", sort="summary")), ["c", "b", "a"])
        self.index.record_commands("/tmp/alpha", [{"command": "git status", "exit_status": 0}])
        terminal = conv_index.terminal_id("/tmp/alpha")
        self.assertEqual([i["session_id"] for i in self.index.search("", scope="all", sort="requests")["items"]],
                         ["b" * 32, "c" * 32, "a" * 32, terminal])
        order = [i["session_id"] for i in self.index.search("", scope="all", sort="model")["items"]]
        self.assertEqual(order, ["b" * 32, "c" * 32, terminal, "a" * 32])
        # Pinned is ahead of every order, so an alphabetical sort lists the pinned row first.
        self.index.set_pinned("a" * 32, True)
        order = [i["session_id"] for i in self.index.search("", scope="all", sort="title_desc")["items"]]
        self.assertEqual(order[0], "a" * 32)

    def test_conversation_preview_highlights_and_filters_by_turn(self):
        self.index.update_session(session(extra_turn="what about pelicans in Brazil"), self.root)
        whole = self.index.conversation("a" * 32, query="Brazil")
        self.assertEqual(whole["match_count"], 2)
        self.assertTrue(any("ranges" in item for item in whole["items"]))
        # The preview reads in conversation order, not in the order the rows were written.
        self.assertEqual([item["turn"] for item in whole["items"]], sorted(item["turn"] for item in whole["items"]))
        one = self.index.conversation("a" * 32, turn=1)
        self.assertEqual({item["turn"] for item in one["items"]}, {1})
        self.assertRaises(ValueError, self.index.conversation, "c" * 32)

    def test_conversation_limit_keeps_the_newest_entries(self):
        self.index.update_session(session(extra_turn="what about pelicans in Brazil"), self.root)
        full = self.index.conversation("a" * 32)["items"]
        self.assertGreater(len(full), 3)
        # The limit bites off the oldest entries, never the newest: a restored pane has to end
        # where the conversation ended (#KDB4), and the preview to show its latest turns.
        capped = self.index.conversation("a" * 32, limit=3)["items"]
        self.assertEqual(capped, full[-3:])
        # What is kept still reads in conversation order.
        self.assertEqual([item["turn"] for item in capped], sorted(item["turn"] for item in capped))

    def test_delete_removes_rows_files_and_blobs(self):
        store = SessionStore(self.root / "sessions", index=self.index)
        data = session()
        store.save(data)
        blobs = store.blob_dir(data["id"])
        blobs.mkdir(parents=True)
        (blobs / "deadbeef").write_bytes(b"pre-image")
        self.index.update_session(data, store.directory)
        self.assertEqual(len(self.index.search("scrollback", scope="all")["items"]), 1)
        removed = self.index.delete_session(data["id"])
        self.assertGreaterEqual(removed["files"], 3)
        self.assertFalse((store.directory / f"{data['id']}.json").exists())
        self.assertFalse(blobs.exists())
        self.assertEqual(self.index.search("scrollback", scope="all")["items"], [])
        self.assertEqual(self.index.stats()["entries"], 0)

    def test_store_delete_removes_index_rows(self):
        store = SessionStore(self.root / "sessions", index=self.index)
        data = session()
        store.save(data)
        self.index.update_session(data, store.directory)
        store.delete(data["id"])
        self.assertEqual(self.index.search("scrollback", scope="all")["items"], [])
        self.assertRaises(ValueError, store.delete, data["id"])

    def test_rebuild_from_session_files(self):
        sessions = self.root / "sessions" / "digest"
        sessions.mkdir(parents=True)
        for index, name in enumerate(("a" * 32, "b" * 32)):
            data = session(name, updated=1000.0 + index)
            (sessions / f"{name}.json").write_text(json.dumps(data), encoding="utf-8")
            (sessions / f"{name}.meta.json").write_text(json.dumps({"id": name}), encoding="utf-8")
        (sessions / "broken.json").write_text("{not json", encoding="utf-8")
        report = self.index.rebuild(self.root / "sessions")
        self.assertEqual(report["sessions"], 2)
        self.assertGreater(report["entries"], 0)
        self.assertEqual(len(self.index.search("scrollback", scope="all")["items"]), 2)
        # A rebuild is idempotent and does not double the rows.
        again = self.index.rebuild(self.root / "sessions")
        self.assertEqual(again["entries"], report["entries"])

    def test_rebuild_keeps_terminal_history(self):
        self.index.record_commands("/tmp/alpha", [{"command": "make check", "exit_status": 0}])
        self.index.rebuild(self.root / "nothing-here")
        self.assertEqual(len(self.index.search("make", scope="all")["items"]), 1)

    def test_corrupt_database_is_discarded_and_recreated(self):
        self.index.update_session(session(), self.root)
        self.index.close()
        path = self.root / "index.db"
        path.write_bytes(b"this is not an SQLite database, not even close" * 10)
        for suffix in ("-wal", "-shm"):
            Path(str(path) + suffix).unlink(missing_ok=True)
        index = ConversationIndex(path)
        self.addCleanup(index.close)
        self.assertTrue(index.recovered)
        self.assertEqual(index.stats()["entries"], 0)
        index.update_session(session(), self.root)          # still usable afterwards
        self.assertEqual(len(index.search("scrollback", scope="all")["items"]), 1)

    def test_corrupt_database_found_while_querying_is_recreated(self):
        self.index.update_session(session(), self.root)

        class Malformed:
            """A connection that reports corruption once, the way SQLite does mid-session."""
            def __init__(self, real):
                self._real = real
                self.fired = False

            def execute(self, *args, **kwargs):
                if not self.fired:
                    self.fired = True
                    raise sqlite3.DatabaseError("database disk image is malformed")
                return self._real.execute(*args, **kwargs)

            def __getattr__(self, name):
                return getattr(self._real, name)

        self.index._db = Malformed(self.index._db)
        stats = self.index.stats()
        self.assertEqual(stats["entries"], 0)   # started over from an empty database
        self.index.update_session(session(), self.root)
        self.assertEqual(len(self.index.search("scrollback", scope="all")["items"]), 1)

    def test_schema_version_change_wipes_the_cache(self):
        self.index.update_session(session(), self.root)
        self.index.close()
        with mock.patch.object(conv_index, "SCHEMA_VERSION", conv_index.SCHEMA_VERSION + 1):
            index = ConversationIndex(self.root / "index.db")
            self.addCleanup(index.close)
            self.assertTrue(index.recovered)
            self.assertEqual(index.stats()["entries"], 0)

    def test_index_off_by_environment(self):
        with mock.patch.dict(os.environ, {"RELAY_INDEX": "off"}):
            self.assertFalse(conv_index.enabled())

    def test_long_output_is_capped(self):
        data = session()
        data["messages"][2]["content"] = "needle " + "x" * 50000
        self.index.update_session(data, self.root)
        stored = self.index.conversation("a" * 32)["items"]
        self.assertTrue(all(len(item["text"]) <= conv_index.MAX_PROMPT for item in stored))


class OperatorSearchTests(unittest.TestCase):
    """The query operators of protocol 14.2, end to end against a real index."""

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.index = ConversationIndex(self.root / "index.db")
        self.addCleanup(self.index.close)
        self.index.update_session(session(
            "a" * 32, workspace="/tmp/alpha", model="glm-5", branch="main", updated=2000.0,
            files=["/tmp/alpha/src/Pane.h"], extra_turn="and then", summary="Indexing the scrollback"),
            self.root)
        self.index.update_session(session(
            "b" * 32, workspace="/tmp/beta-project", title="Other project", model="kimi-k2",
            branch="feature/search", updated=5000.0, open_requests=2, extra_turn="and then",
            mode="plan"), self.root)

    def ids(self, query, **kwargs):
        return [item["session_id"] for item in self.index.search(query, scope="all", **kwargs)["items"]]

    def test_project_operator_matches_name_or_path_and_implies_scope_all(self):
        # scope="project" for another workspace, yet project: finds it: naming a project is asking
        # about that project.
        result = self.index.search("project:beta", scope="project", workspace="/tmp/alpha")
        self.assertEqual([i["session_id"] for i in result["items"]], ["b" * 32])
        self.assertEqual(result["scope"], "all")
        self.assertEqual(self.ids("project:BETA-PROJECT"), ["b" * 32])
        self.assertEqual(self.ids("project:/tmp/alpha"), ["a" * 32])
        self.assertEqual(self.ids("project:nothing"), [])

    def test_file_model_branch_operators(self):
        self.assertEqual(self.ids("file:Pane.h"), ["a" * 32])
        self.assertEqual(self.ids("file:pane"), ["a" * 32])
        self.assertEqual(self.ids("file:nowhere.c"), [])
        self.assertEqual(self.ids("model:kimi"), ["b" * 32])
        self.assertEqual(self.ids("branch:feature"), ["b" * 32])
        self.assertEqual(self.ids("branch:main"), ["a" * 32])

    def test_date_operators(self):
        now = 5000.0 + 86400 * 3
        self.assertEqual(self.ids("after:1d", now=now), [])
        self.assertEqual(self.ids("after:7d", now=now), ["b" * 32, "a" * 32])
        self.assertEqual(self.ids("before:3d", now=now), ["a" * 32])   # b was updated exactly then
        self.assertEqual(self.ids("before:2d", now=now), ["b" * 32, "a" * 32])
        # A bad date filters nothing and is reported instead of raising.
        result = self.index.search("before:yesteryear", scope="all")
        self.assertEqual(result["parsed"]["ignored"], ["before:yesteryear"])
        self.assertEqual(len(result["items"]), 2)

    def test_has_is_and_in_operators(self):
        self.assertEqual(self.ids("has:tasks"), ["b" * 32])
        self.assertEqual(self.ids("has:edits"), ["a" * 32])
        self.assertEqual(self.ids("has:summary"), ["a" * 32])
        self.index.set_pinned("b" * 32, True)
        self.assertEqual(self.ids("is:pinned"), ["b" * 32])
        self.index.update_session(session("c" * 32, workspace="/tmp/alpha", updated=100.0), self.root)
        self.assertEqual(self.ids("is:unfinished"), ["c" * 32])
        self.index.record_commands("/tmp/alpha", [{"command": "make check", "exit_status": 0, "time": 9000.0}])
        self.assertEqual(self.ids("in:terminal"), [conv_index.terminal_id("/tmp/alpha")])
        self.assertNotIn(conv_index.terminal_id("/tmp/alpha"), self.ids("in:agent"))

    def test_negation_spans_the_whole_conversation(self):
        # "pelican" is in a reply of both; "kimi" only in b's model, so use real text instead.
        self.assertEqual(self.ids("scrollback"), ["b" * 32, "a" * 32])
        self.assertEqual(self.ids('scrollback -"Other project"'), ["a" * 32])
        # The excluded word sits in a different message from the matched one, and still excludes.
        self.assertEqual(self.ids("scrollback -Brazil"), ["b" * 32, "a" * 32])
        self.index.update_session(session("d" * 32, workspace="/tmp/alpha", updated=10.0,
                                          extra_turn="what about Brazil"), self.root)
        self.assertIn("d" * 32, self.ids("scrollback"))
        self.assertNotIn("d" * 32, self.ids("scrollback -Brazil"))

    def test_operators_combine_with_free_text_and_explicit_filters(self):
        self.assertEqual(self.ids("pelican project:alpha has:edits"), ["a" * 32])
        self.assertEqual(self.ids("pelican project:alpha has:tasks"), [])
        self.assertEqual(self.ids("pelican", file="Pane.h"), ["a" * 32])
        self.assertEqual(self.ids("pelican", branch="feature"), ["b" * 32])
        self.assertEqual(self.ids("", has_edits=True), ["a" * 32])
        self.assertEqual(self.ids("", has_edits=False), ["b" * 32])
        self.assertEqual(self.ids("", has_summary=True), ["a" * 32])
        self.index.set_pinned("a" * 32, True)
        self.assertEqual(self.ids("", pinned=True), ["a" * 32])
        self.assertEqual(self.ids("", unfinished=True), [])

    def test_negated_operators(self):
        self.assertEqual(self.ids("-model:kimi"), ["a" * 32])
        self.assertEqual(self.ids("-has:edits"), ["b" * 32])
        self.assertEqual(self.ids("-project:alpha"), ["b" * 32])

    def test_every_user_string_stays_escaped(self):
        for attempt in ('file:"x\' OR 1=1"', 'file:"100%"', "branch:_", 'project:"a\\"; DROP TABLE entries;--"',
                        '-"', '-', 'model:"*"', 'has:tasks OR is:pinned', 'file:x" AND "y'):
            with self.subTest(attempt):
                result = self.index.search(attempt, scope="all")
                self.assertIsInstance(result["items"], list)
        # The tables are still there and still answer.
        self.assertEqual(len(self.ids("scrollback")), 2)
        self.assertEqual(self.index.stats()["conversations"], 2)

    def test_parsed_reports_what_the_gui_should_draw(self):
        parsed = self.index.search('pelican project:alpha -zebra has:wombat', scope="all")["parsed"]
        self.assertEqual(parsed["text"], "pelican")
        self.assertEqual(parsed["operators"], [{"key": "project", "value": "alpha"},
                                               {"key": "text", "value": "zebra", "negated": True}])
        self.assertEqual(parsed["ignored"], ["has:wombat"])

    def test_facets_list_the_values_of_the_current_scope(self):
        facets = self.index.search("", scope="all")["facets"]
        self.assertEqual(facets["models"], ["kimi-k2", "glm-5"])          # most recent first
        self.assertEqual(facets["branches"], ["feature/search", "main"])
        self.assertEqual(facets["projects"], ["beta-project", "alpha"])
        scoped = self.index.search("", scope="project", workspace="/tmp/alpha")["facets"]
        self.assertEqual(scoped["models"], ["glm-5"])
        self.assertEqual(scoped["projects"], ["alpha"])

    def test_the_model_filter_and_its_menu_work_on_names_not_ids(self):
        """One model, one filter entry (card #MDL1, rule 1).

        History records the id the API took, and one model has several: `k3` on the Kimi Coding
        Plan and `kimi-k3` on the Kimi platform, `openai/gpt-6-sol` on OpenRouter and
        `gpt-6-sol` first-party. Those stay on disk exactly as written — nothing is migrated —
        and the menu folds them into one line per name, which selects every row of that model.
        """
        self.index.update_session(session("e" * 32, workspace="/tmp/alpha", model="k3",
                                          updated=6000.0), self.root)
        self.index.update_session(session("f" * 32, workspace="/tmp/alpha", model="kimi-k3",
                                          updated=6100.0), self.root)
        self.index.update_session(session("g" * 32, workspace="/tmp/alpha",
                                          model="openai/gpt-6-sol", updated=6200.0), self.root)
        models = self.index.search("", scope="all")["facets"]["models"]
        self.assertEqual(models.count("kimi-k3"), 1)      # `k3` and `kimi-k3` are one entry
        self.assertNotIn("k3", models)
        self.assertIn("gpt-6-sol", models)              # the vendor prefix is not a name
        self.assertNotIn("openai/gpt-6-sol", models)
        # Picking that one entry selects both rows, newest first.
        self.assertEqual(self.ids("", model="kimi-k3"), ["f" * 32, "e" * 32])
        self.assertEqual(self.ids("", model="gpt-6-sol"), ["g" * 32])
        # A raw id still filters: a query saved before this, and an id no build can name.
        self.assertEqual(self.ids("", model="k3"), ["e" * 32])
        # And so does the `model:` operator, on the name as well as on the id.
        self.assertEqual(self.ids("model:kimi-k3"), ["f" * 32, "e" * 32])
        self.assertEqual(self.ids("model:gpt-6-sol"), ["g" * 32])

    def test_item_carries_the_overview_columns(self):
        item = [i for i in self.index.search("", scope="all")["items"] if i["session_id"] == "a" * 32][0]
        self.assertEqual(item["summary"], "Indexing the scrollback")
        self.assertEqual(item["first_prompt"], "how do I search the scrollback")
        self.assertEqual(item["last_prompt"], "and then")
        self.assertEqual(item["files"], ["/tmp/alpha/src/Pane.h"])
        self.assertEqual(item["files_count"], 1)
        self.assertTrue(item["has_edits"])
        self.assertEqual(item["branch"], "main")
        self.assertFalse(item["unfinished"])
        self.assertEqual(item["mode"], "build")
        self.assertEqual(item["snippet"], "Indexing the scrollback")


class RankingTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.index = ConversationIndex(self.root / "index.db")
        self.addCleanup(self.index.close)

    def build(self, session_id, *, title="Plain", summary="", prompt="nothing", reply="nothing",
              tool="nothing", updated=1000.0):
        data = session(session_id, title=title, updated=updated, extra_turn="and then")
        data["messages"] = [{"role": "user", "content": prompt},
                            {"role": "assistant", "content": reply,
                             "tool_calls": [{"id": "c1", "type": "function",
                                             "function": {"name": "run_command", "arguments": "{}"}}]},
                            {"role": "tool", "tool_call_id": "c1", "content": tool},
                            {"role": "assistant", "content": "done"}]
        data["checkpoints"]["items"] = [{"turn": 1, "prompt": prompt, "prompt_preview": prompt[:20],
                                         "time": 10.0, "ended": 20.0, "locations": {"0": 1}, "files": {}}]
        if summary:
            data["summary"] = summary
        self.index.update_session(data, self.root)

    def test_relevance_puts_title_over_summary_over_prompt_over_reply_over_tool_output(self):
        self.build("1" * 32, title="wombat plan")
        self.build("2" * 32, summary="a wombat was involved")
        self.build("3" * 32, prompt="tell me about the wombat")
        self.build("4" * 32, reply="the wombat is fine")
        self.build("5" * 32, tool="wombat wombat wombat wombat wombat")
        order = [i["session_id"] for i in self.index.search("wombat", scope="all", sort="relevance")["items"]]
        self.assertEqual(order, ["1" * 32, "2" * 32, "3" * 32, "4" * 32, "5" * 32])
        # Many weak hits never outrank one strong one.
        self.assertLess(order.index("3" * 32), order.index("5" * 32))

    def test_relevance_breaks_ties_by_recency(self):
        self.build("1" * 32, prompt="wombat here", updated=100.0)
        self.build("2" * 32, prompt="wombat here", updated=900.0)
        order = [i["session_id"] for i in self.index.search("wombat", scope="all", sort="relevance")["items"]]
        self.assertEqual(order, ["2" * 32, "1" * 32])

    def test_active_session_filter_keeps_ranked_header_and_body_matches(self):
        self.build("1" * 32, title="wombat plan")
        self.build("2" * 32, summary="wombat recap")
        self.build("3" * 32, reply="wombat in the transcript")
        self.build("4" * 32, title="wombat closed")
        active = ["1" * 32, "2" * 32, "3" * 32]
        result = self.index.search("wombat", scope="all", sort="relevance", session_ids=active)
        self.assertEqual([item["session_id"] for item in result["items"]], active)
        self.assertEqual([item["best_match_kind"] for item in result["items"]],
                         ["title", "summary", "reply"])
        self.assertEqual(self.index.search("wombat", scope="all", session_ids=[])["items"], [])

    def test_a_title_or_summary_hit_is_a_match_line_of_its_own_kind(self):
        self.build("1" * 32, title="wombat plan", summary="the wombat summary")
        matches = self.index.search("wombat", scope="all")["items"][0]["matches"]
        self.assertEqual([m["kind"] for m in matches[:2]], ["title", "summary"])
        self.assertEqual(matches[0]["line"], "wombat plan")
        self.assertEqual(matches[0]["turn"], 0)

    def test_set_summary_updates_the_column_and_the_searchable_entry(self):
        self.build("1" * 32, title="Plain")
        self.assertEqual(self.index.search("capybara", scope="all")["items"], [])
        self.index.set_summary("1" * 32, "a capybara appeared")
        item = self.index.search("capybara", scope="all")["items"][0]
        self.assertEqual(item["summary"], "a capybara appeared")
        self.assertEqual(item["matches"][0]["kind"], "summary")
        self.assertEqual(len(self.index.search("", scope="all", has_summary=True)["items"]), 1)
        # An autosave that carries no summary of its own keeps the one that was set.
        self.build("1" * 32, title="Plain", updated=4000.0)
        self.assertEqual(self.index.search("capybara", scope="all")["items"][0]["summary"], "a capybara appeared")
        self.index.set_summary("1" * 32, "")
        self.assertEqual(self.index.search("capybara", scope="all")["items"], [])
        # An id nobody indexed is a no-op, not an orphan entry.
        self.index.set_summary("9" * 32, "nothing to attach this to")
        self.assertEqual(self.index.search("attach", scope="all")["items"], [])

    def test_a_rename_is_searchable_under_the_new_name(self):
        self.build("1" * 32, title="Plain")
        self.index.rename("1" * 32, "wombat rescue")
        self.assertEqual(self.index.search("wombat", scope="all")["items"][0]["session_id"], "1" * 32)
        self.index.rename("1" * 32, "")
        self.assertEqual(self.index.search("wombat", scope="all")["items"], [])


class OverviewTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.index = ConversationIndex(self.root / "index.db")
        self.addCleanup(self.index.close)

    def test_conversation_overview_carries_the_inline_preview(self):
        data = session("a" * 32, summary="What this was about", branch="main",
                       files=["/tmp/alpha/one.py", "/tmp/alpha/two.py"],
                       todos=[{"id": "t1", "text": "done thing", "status": "completed"},
                              {"id": "t2", "text": "open thing", "status": "pending"}],
                       extra_turn="and then")
        self.index.update_session(data, self.root)
        overview = self.index.conversation("a" * 32)["overview"]
        self.assertEqual(overview["summary"], "What this was about")
        self.assertEqual(overview["first_prompt"], "how do I search the scrollback")
        self.assertEqual([t["turn"] for t in overview["last_turns"]], [1, 2])
        self.assertEqual(overview["last_turns"][0]["prompt"], "how do I search the scrollback")
        self.assertEqual(overview["last_turns"][0]["reply"], "Use the pelican finder")
        self.assertEqual(sorted(overview["files"]), ["/tmp/alpha/one.py", "/tmp/alpha/two.py"])
        self.assertEqual(overview["files_count"], 2)
        self.assertEqual([t["text"] for t in overview["todos"]], ["open thing", "done thing"])
        self.assertEqual(overview["branch"], "main")
        self.assertTrue(overview["unfinished"])          # an open todo

    def test_overview_keeps_only_the_last_three_turns_and_caps_their_text(self):
        data = session("a" * 32)
        data["checkpoints"]["items"] = []
        data["messages"] = []
        for turn in range(1, 6):
            data["checkpoints"]["items"].append(
                {"turn": turn, "prompt": f"prompt {turn} " + "x" * 800, "prompt_preview": "p",
                 "time": float(turn), "ended": float(turn) + 1, "locations": {"0": len(data["messages"]) + 1},
                 "files": {}})
            data["messages"] += [{"role": "user", "content": f"prompt {turn}"},
                                 {"role": "assistant", "content": f"reply {turn} " + "y" * 800}]
        self.index.update_session(data, self.root)
        overview = self.index.conversation("a" * 32)["overview"]
        self.assertEqual([t["turn"] for t in overview["last_turns"]], [3, 4, 5])
        self.assertLessEqual(len(overview["last_turns"][0]["prompt"]), conv_index.OVERVIEW_TEXT)
        self.assertLessEqual(len(overview["last_turns"][0]["reply"]), conv_index.OVERVIEW_TEXT)
        self.assertLessEqual(len(self.index.conversation("a" * 32)["first_prompt"]), conv_index.MAX_PREVIEW)

    def test_conversation_still_returns_everything_it_did_and_hides_the_header_entries(self):
        self.index.update_session(session("a" * 32, summary="a summary", extra_turn="what about Brazil"),
                                  self.root)
        whole = self.index.conversation("a" * 32, query="Brazil")
        self.assertNotIn("title", [item["kind"] for item in whole["items"]])
        self.assertNotIn("summary", [item["kind"] for item in whole["items"]])
        self.assertEqual(whole["match_count"], 2)
        self.assertEqual(whole["title"], "Search spike")
        self.assertEqual(whole["turns"], 2)
        self.assertEqual({item["turn"] for item in self.index.conversation("a" * 32, turn=1)["items"]}, {1})

    def test_terminal_history_has_an_overview_too(self):
        self.index.record_commands("/tmp/alpha", [{"command": "make check", "exit_status": 0, "time": 10.0}])
        overview = self.index.conversation(conv_index.terminal_id("/tmp/alpha"))["overview"]
        self.assertEqual(overview["last_turns"], [{"turn": 1, "prompt": "make check", "reply": ""}])
        self.assertEqual(overview["files"], [])
        self.assertFalse(overview["unfinished"])


class MigrationTests(unittest.TestCase):
    """A database written by the previous schema is migrated in place and backfilled once."""

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.sessions = self.root / "sessions" / "digest"
        self.sessions.mkdir(parents=True)

    def write(self, data):
        (self.sessions / f"{data['id']}.json").write_text(json.dumps(data), encoding="utf-8")
        (self.sessions / f"{data['id']}.meta.json").write_text(
            json.dumps({"id": data["id"], "updated": data["updated"]}), encoding="utf-8")

    def downgrade(self, path, version=2):
        """Turn the database back into an older one: drop the columns that version did not have
        (and, below v3, the header entries it did not write)."""
        db = sqlite3.connect(str(path))
        dropped = conv_index.V8_COLUMNS
        if version < 7:
            dropped += conv_index.V7_COLUMNS
        if version < 5:
            dropped += conv_index.V5_COLUMNS
        if version < 4:
            dropped += conv_index.V4_COLUMNS
        if version < 3:
            dropped += conv_index.V3_COLUMNS
        for name, _kind in dropped:
            db.execute(f"ALTER TABLE conversations DROP COLUMN {name}")
        if version < 3:
            db.execute("DELETE FROM entries WHERE kind IN ('title', 'summary')")
        db.execute("INSERT OR REPLACE INTO meta(key, value) VALUES('schema_version', ?)", (str(version),))
        db.commit()
        db.close()

    def test_a_v3_index_gains_the_guest_columns_and_tables_in_place(self):
        """v4 adds `raw_cwd` and the guest tables. A wipe would take the terminal history with it
        — there is no file to rebuild that from — so it is an ALTER, like every version before."""
        self.write(session("a" * 32, workspace="/tmp/alpha", branch="main"))
        index = ConversationIndex(self.root / "index.db")
        index.rebuild(self.root / "sessions")
        index.record_commands("/tmp/alpha", [{"command": "rg needle", "status": 0}])
        index.close()
        self.downgrade(self.root / "index.db", version=3)

        index = ConversationIndex(self.root / "index.db")
        self.addCleanup(index.close)
        self.assertEqual(index.migrated_from, 3)
        self.assertFalse(index.recovered, "migrated, not wiped")
        items = {item["session_id"]: item for item in index.search("", scope="all")["items"]}
        self.assertIn("term-" + conv_index.workspace_digest("/tmp/alpha"), items)
        self.assertEqual("", items["a" * 32]["raw_cwd"])
        self.assertEqual("main", items["a" * 32]["branch"], "the v3 columns are still there")
        self.assertEqual(set(), index.forgotten())

    def test_a_v4_index_gains_the_fingerprint_without_a_re_index(self):
        """v5 adds `entry_count`/`entry_digest` (#TZWF). Nothing has to be re-read to fill them:
        each conversation writes its own at its next save. (The one backfill pass below is v6's,
        for the sidecar rows — v5 on its own asks for none, which is why the fingerprint is there
        before any reconcile has run.)"""
        self.write(session("a" * 32, workspace="/tmp/alpha", branch="main"))
        index = ConversationIndex(self.root / "index.db")
        index.rebuild(self.root / "sessions")
        index.record_commands("/tmp/alpha", [{"command": "rg needle", "status": 0}])
        index.close()
        self.downgrade(self.root / "index.db", version=4)

        index = ConversationIndex(self.root / "index.db")
        self.addCleanup(index.close)
        self.assertEqual(index.migrated_from, 4)
        self.assertFalse(index.recovered, "migrated, not wiped")
        items = {item["session_id"]: item for item in index.search("", scope="all")["items"]}
        self.assertIn("term-" + conv_index.workspace_digest("/tmp/alpha"), items)
        self.assertEqual("main", items["a" * 32]["branch"], "the v3 columns are still there")
        fingerprint = lambda: index._db.execute(
            "SELECT entry_count, entry_digest FROM conversations WHERE session_id=?",
            ("a" * 32,)).fetchone()
        self.assertEqual((0, ""), tuple(fingerprint()), "added empty by the migration")
        # The first save of each conversation fills it, with no reconcile and no file re-read.
        index.update_session(session("a" * 32, workspace="/tmp/alpha", branch="main"), self.sessions)
        row = fingerprint()
        self.assertGreater(row["entry_count"], 0)
        self.assertTrue(row["entry_digest"])

    def test_a_v6_index_backfills_the_link_to_its_guest_session(self):
        guest_id = "ea11ece1-7ec2-4597-8639-32fb1f43f073"
        data = session("a" * 32)
        data.update(guest="codex", guest_session=guest_id)
        self.write(data)
        index = ConversationIndex(self.root / "index.db")
        index.reconcile(self.root / "sessions")
        index.close()
        self.downgrade(self.root / "index.db", version=6)

        index = ConversationIndex(self.root / "index.db")
        self.addCleanup(index.close)
        self.assertEqual(6, index.migrated_from)
        self.assertFalse(index.recovered)
        self.assertEqual(0, index._db.execute(
            "SELECT indexed_version FROM conversations WHERE session_id=?", (data["id"],)).fetchone()[0])
        self.assertEqual(1, index.reconcile(self.root / "sessions")["backfilled"])
        row = index._db.execute(
            "SELECT guest_source, guest_session FROM conversations WHERE session_id=?", (data["id"],)).fetchone()
        self.assertEqual(("codex", guest_id), tuple(row))

    def test_a_v7_index_keeps_guest_rows_and_adds_provenance_column(self):
        path = self.root / "index.db"
        index = ConversationIndex(path)
        index.update_guest({"source": "codex", "id": "codex-old", "title": "old guest",
                            "workspace": "/tmp/alpha", "created": 1000.0, "mtime": 1000.0,
                            "message_count": 1, "entries": []})
        index.close()
        self.downgrade(path, version=7)

        index = ConversationIndex(path)
        self.addCleanup(index.close)
        index.search("", scope="all", sources=["codex"])
        self.assertEqual(7, index.migrated_from)
        row = index._db.execute("SELECT source, relay_launched FROM conversations"
                                " WHERE session_id='codex-old'").fetchone()
        self.assertEqual(("codex", 0), tuple(row))
        index.update_guest({"source": "codex", "id": "codex-old", "title": "old guest",
                            "workspace": "/tmp/alpha", "created": 1000.0, "mtime": 1000.0,
                            "message_count": 1, "entries": [], "relay_launched": True})
        self.assertEqual(1, index._db.execute("SELECT relay_launched FROM conversations"
                                              " WHERE session_id='codex-old'").fetchone()[0])

    def test_a_v2_index_is_migrated_and_reconcile_backfills_the_new_columns(self):
        data = session("a" * 32, workspace="/tmp/alpha", branch="main", summary="the summary",
                       files=["/tmp/alpha/src/Pane.h"], extra_turn="and then")
        self.write(data)
        index = ConversationIndex(self.root / "index.db")
        index.rebuild(self.root / "sessions")
        index.close()
        self.downgrade(self.root / "index.db")

        index = ConversationIndex(self.root / "index.db")
        self.addCleanup(index.close)
        self.assertEqual(index.migrated_from, 2)
        self.assertFalse(index.recovered)                    # migrated, not wiped
        item = index.search("", scope="all")["items"][0]
        self.assertEqual(item["branch"], "")                 # not backfilled yet
        report = index.reconcile(self.root / "sessions")
        self.assertEqual(report["backfilled"], 1)
        item = index.search("", scope="all")["items"][0]
        self.assertEqual(item["branch"], "main")
        self.assertEqual(item["summary"], "the summary")
        self.assertEqual(item["files"], ["/tmp/alpha/src/Pane.h"])
        # …and only once: a second reconcile finds nothing to do.
        self.assertEqual(index.reconcile(self.root / "sessions")["backfilled"], 0)
        self.assertEqual(index.reconcile(self.root / "sessions")["refreshed"], 0)

    def test_a_v1_index_still_migrates_all_the_way(self):
        self.write(session("a" * 32, workspace="/tmp/alpha", branch="main"))
        index = ConversationIndex(self.root / "index.db")
        index.rebuild(self.root / "sessions")
        index.close()
        self.downgrade(self.root / "index.db")
        db = sqlite3.connect(str(self.root / "index.db"))
        db.execute("DROP INDEX IF EXISTS conversations_by_owner")
        for name, _kind in conv_index.V2_COLUMNS:
            db.execute(f"ALTER TABLE conversations DROP COLUMN {name}")
        db.execute("UPDATE conversations SET custom_title='Kept name', pinned=1")
        db.execute("INSERT OR REPLACE INTO meta(key, value) VALUES('schema_version', '1')")
        db.commit()
        db.close()

        index = ConversationIndex(self.root / "index.db")
        self.addCleanup(index.close)
        self.assertEqual(index.migrated_from, 1)
        self.assertFalse(index.recovered)
        # The v1 step moved the user's title and pin into the session's meta file…
        user = conv_index.read_user_fields(self.sessions, "a" * 32)
        self.assertEqual(user["custom_title"], "Kept name")
        self.assertTrue(user["pinned"])
        # …and the v3 step backfills on the next reconcile, keeping them.
        index.reconcile(self.root / "sessions")
        item = index.search("", scope="all")["items"][0]
        self.assertEqual(item["title"], "Kept name")
        self.assertEqual(item["branch"], "main")

    def test_a_summary_in_the_meta_file_is_read_when_the_session_has_none(self):
        data = session("a" * 32, workspace="/tmp/alpha")
        self.write(data)
        conv_index.write_user_fields(self.sessions, "a" * 32, summary="summarised while closed")
        index = ConversationIndex(self.root / "index.db")
        self.addCleanup(index.close)
        index.rebuild(self.root / "sessions")
        self.assertEqual(index.search("", scope="all")["items"][0]["summary"], "summarised while closed")
        # The session's own summary is the newer one and wins.
        self.write(session("a" * 32, workspace="/tmp/alpha", summary="written by the session",
                           updated=3000.0))
        index.reconcile(self.root / "sessions")
        self.assertEqual(index.search("", scope="all")["items"][0]["summary"], "written by the session")


class ThreadedAccess(unittest.TestCase):
    """One index, several threads (protocol 26.7).

    The connection is opened `check_same_thread=False` because the worker shares it: the protocol
    thread answers a `conversations` listing while `_guest_refresh` reconciles the guests'
    transcripts on a background thread. sqlite3 does not serialise that for us, and two statements
    interleaved on one connection come back as `InterfaceError: bad parameter or other API misuse`
    — once in 48 runs of the backend suite under load, inside `search()` (card #99T0). Without
    `ConversationIndex._lock` this test fails within a second or two.
    """

    CLAUDE = "cc000000-0000-4000-8000-00000000000c"

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.index = ConversationIndex(Path(self.temp.name) / "index.db")
        self.addCleanup(lambda: self.index.close())

    def guest(self, n: int) -> dict:
        return {"source": "claude", "id": "%s%04d" % (self.CLAUDE[:-4], n), "title": "pane drag %d" % n,
                "workspace": "/tmp/alpha", "raw_cwd": "/tmp/alpha", "created": 1000.0, "mtime": 1000.0 + n,
                "message_count": 2,
                "entries": [{"turn": 1, "seq": 1, "kind": "prompt", "time": 1000.0, "text": "fix the pane drag"},
                            {"turn": 1, "seq": 2, "kind": "reply", "time": 1000.0, "text": "fixed it in Pane.h"}]}

    def test_searching_while_the_guests_are_reconciled_is_not_an_api_misuse(self):
        failures, done = [], threading.Event()

        def writer():
            try:
                for n in range(120):
                    if done.is_set():
                        return
                    self.index.update_guest(self.guest(n))
                    self.index.rename(self.guest(n)["id"], "renamed %d" % n)
                    self.index.set_pinned(self.guest(n)["id"], n % 2 == 0)
            except Exception as error:                      # the race, whichever call loses it
                failures.append("writer: %r" % (error,))
            finally:
                done.set()

        def reader():
            try:
                while not done.is_set():
                    self.index.search("pane", scope="all", sources=list(conv_index.GUEST_SOURCES))
                    self.index.guest_meta("claude")
                    self.index.forgotten()
            except Exception as error:
                failures.append("reader: %r" % (error,))
                done.set()

        threads = [threading.Thread(target=writer, name="writer")]
        threads += [threading.Thread(target=reader, name="reader%d" % i) for i in range(3)]
        for thread in threads:
            thread.start()
        for thread in threads:
            thread.join(60)
            self.assertFalse(thread.is_alive(), "%s did not finish" % thread.name)
        self.assertEqual([], failures)
        # And the writes all landed: a lock that serialised nothing would also lose rows.
        rows = {item["session_id"] for item in
                self.index.search("", scope="all", sources=list(conv_index.GUEST_SOURCES),
                                  limit=200)["items"]}
        self.assertEqual({self.guest(n)["id"] for n in range(120)}, rows)


class GuestRowTests(unittest.TestCase):
    """The guest rows' own machinery in the index (protocol 26.7, GT7X): the meta store that
    outlives the database, the forgotten set and the incremental cursors. `guest_sessions.py`
    owns the parsing; this is what `conv_index` owes it."""

    CLAUDE = "ea11ece1-7ec2-4597-8639-32fb1f43f073"

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.index = ConversationIndex(self.root / "index.db")
        self.addCleanup(lambda: self.index.close())

    def record(self, session_id=None, *, source="claude", title="pane drag", workspace="/tmp/alpha",
               raw_cwd=None, mtime=1000.0, entries=None, **extra):
        rows = entries if entries is not None else [
            {"turn": 1, "seq": 1, "kind": "prompt", "time": mtime, "text": "fix the pane drag"},
            {"turn": 1, "seq": 2, "kind": "reply", "time": mtime, "text": "fixed it in Pane.h"}]
        return {"source": source, "id": session_id or self.CLAUDE, "title": title, "workspace": workspace,
                "raw_cwd": workspace if raw_cwd is None else raw_cwd, "created": mtime, "mtime": mtime,
                "message_count": len(rows), "entries": rows, **extra}

    def item(self, session_id=None):
        rows = {item["session_id"]: item for item in
                self.index.search("", scope="all", sources=list(conv_index.GUEST_SOURCES))["items"]}
        return rows.get(session_id or self.CLAUDE)

    def test_linked_relay_session_appears_once_in_combined_list(self):
        links = [("claude", self.CLAUDE, "a" * 32),
                 ("codex", "01a0ce9a-41c6-75a2-9ffc-1036534eafd3", "b" * 32)]
        for source, guest_id, relay_id in links:
            self.index.update_guest(self.record(guest_id, source=source))
            relay = session(relay_id)
            relay.update(guest=source, guest_session=guest_id)
            self.index.update_session(relay, self.root)
        both = dict(scope="all", sources=["agent", "claude", "codex"])
        combined = self.index.search("", **both)
        self.assertEqual({"a" * 32, "b" * 32},
                         {item["session_id"] for item in combined["items"]})
        self.assertEqual(2, combined["total"])
        for source, guest_id, _relay_id in links:
            self.assertEqual([guest_id], [item["session_id"] for item in
                                          self.index.search("", scope="all", sources=[source])["items"]])
        self.index.delete_session("a" * 32)
        self.assertEqual({self.CLAUDE, "b" * 32},
                         {item["session_id"] for item in self.index.search("", **both)["items"]})

    def test_spawned_guest_is_a_subagent_not_a_top_level_session(self):
        owner, thread = "a" * 32, "b" * 32
        spawned = "01a0c6b5-139f-7493-83a8-4f16a86a22c2"
        independent, independent_codex = self.CLAUDE, "codex-cli"
        self.index.update_session(session(owner))
        self.index.update_thread({"id": thread, "owner_session": owner,
                                  "workspace": "/tmp/alpha", "description": "Research QA methods",
                                  "created": 1000.0, "messages": [{"role": "user", "content": "Research QA"}]})
        self.index.update_guest(self.record(spawned, source="codex", relay_launched=True))
        self.index.update_guest(self.record(independent, source="claude", relay_launched=False))
        self.index.update_guest(self.record(independent_codex, source="codex", relay_launched=False))
        combined = {"scope": "all", "sources": ["agent", "claude", "codex"]}
        ids = lambda **extra: {item["session_id"] for item in
                               self.index.search("", **combined, **extra)["items"]}
        self.assertEqual({owner, independent, independent_codex}, ids())
        self.assertEqual({owner, independent, independent_codex, thread}, ids(include_threads=True))
        self.assertEqual({spawned, independent_codex}, {item["session_id"] for item in
                                                        self.index.search("", scope="all",
                                                                          sources=["codex"])["items"]})

        # A growing guest transcript keeps its provenance when indexed incrementally.
        self.index.update_guest({**self.record(spawned, source="codex", relay_launched=True),
                                 "append": True, "entry_base": 2, "entries": [], "mtime": 2000.0})
        self.assertEqual({owner, independent, independent_codex}, ids())

    # ----- the meta store ------------------------------------------------------------
    def test_a_name_and_a_pin_are_written_beside_the_database(self):
        self.index.update_guest(self.record())
        self.index.rename(self.CLAUDE, "  the pane   drag one ")
        self.index.set_pinned(self.CLAUDE, True)
        self.assertEqual({"custom_title": "the pane drag one", "pinned": True},
                         self.index.guest_meta("claude", self.CLAUDE))
        self.assertEqual(conv_index.GUEST_META_NAME, self.index.store_path.name)
        self.assertEqual(0o600, self.index.store_path.stat().st_mode & 0o777)

    def test_the_store_is_what_a_rebuilt_row_takes_them_from(self):
        self.index.update_guest(self.record())
        self.index.rename(self.CLAUDE, "kept")
        self.index.set_pinned(self.CLAUDE, True)
        self.index.delete_guests()                       # as a wiped database leaves it
        self.index.update_guest(self.record())
        self.assertEqual(("kept", 1), (self.item()["title"], self.item()["pinned"]))

    def test_the_row_wins_while_there_is_one(self):
        """The two are written together, so they agree; where they cannot, the row is the live
        one and the store is only its backup."""
        self.index.update_guest(self.record())
        self.index.set_pinned(self.CLAUDE, True)
        self.index.update_guest({**self.record(), "pinned": False})
        self.assertEqual(0, self.item()["pinned"])

    def test_a_store_that_cannot_be_read_is_an_empty_one(self):
        for text in ("{not json", "[]", '{"claude": 7}', '{"claude": {"x": "not a dict"}}'):
            with self.subTest(text=text):
                self.index.store_path.write_text(text, encoding="utf-8")
                self.assertEqual({}, self.index.guest_meta())
        self.index.store_path.write_text('{"gemini": {"x": {"pinned": true}}}', encoding="utf-8")
        self.assertEqual({}, self.index.guest_meta(), "only the guest sources belong in it")

    def test_only_guest_rows_reach_the_store(self):
        self.index.update_session(session("a" * 32))
        self.index.rename("a" * 32, "an agent session")
        self.index.set_pinned("a" * 32, True)
        self.assertEqual({}, self.index.guest_meta())

    # ----- the forgotten set ---------------------------------------------------------
    def test_deleting_a_guest_row_forgets_it_and_update_guest_refuses_it(self):
        self.index.update_guest(self.record())
        removed = self.index.delete_session(self.CLAUDE, remove_files=False)
        self.assertEqual({"session_id": self.CLAUDE, "files": 0, "indexed": True, "forgotten": True},
                         removed)
        self.assertEqual({("claude", self.CLAUDE)}, self.index.forgotten())
        self.assertEqual(0, self.index.update_guest(self.record()))
        self.assertIsNone(self.item())
        self.index.unforget("claude", self.CLAUDE)
        self.assertEqual(2, self.index.update_guest(self.record()))
        self.assertIsNotNone(self.item())

    def test_a_deletion_that_is_not_the_users_does_not_forget(self):
        self.index.update_guest(self.record())
        self.index.delete_session(self.CLAUDE, remove_files=False, forget=False)
        self.assertEqual(set(), self.index.forgotten())
        self.assertEqual(2, self.index.update_guest(self.record()))

    def test_the_forgotten_set_is_seeded_from_the_store_on_a_fresh_database(self):
        self.index.update_guest(self.record())
        self.index.delete_session(self.CLAUDE, remove_files=False)
        self.index.close()
        for suffix in ("", "-wal", "-shm"):
            Path(str(self.root / "index.db") + suffix).unlink(missing_ok=True)
        self.index = ConversationIndex(self.root / "index.db")
        self.assertEqual({("claude", self.CLAUDE)}, self.index.forgotten())
        self.assertEqual(0, self.index.update_guest(self.record()))

    def test_forgetting_ignores_everything_that_is_not_a_guest(self):
        self.index.forget("agent", "a" * 32)
        self.index.forget("claude", "")
        self.assertEqual(set(), self.index.forgotten())
        self.assertEqual(set(), self.index.forgotten(sources=["agent"]))

    # ----- purging and the incremental cursors ---------------------------------------
    def test_delete_guests_drops_the_rows_and_keeps_what_the_user_set(self):
        self.index.update_guest(self.record())
        self.index.update_guest(self.record("codex-1", source="codex"))
        self.index.update_session(session("a" * 32))
        self.index.set_pinned(self.CLAUDE, True)
        self.assertEqual(1, self.index.delete_guests(sources=["codex"]))
        self.assertEqual([self.CLAUDE], sorted(item["session_id"] for item in
                                               self.index.search("", scope="all",
                                                                 sources=["claude", "codex"])["items"]))
        self.assertEqual(1, self.index.delete_guests())
        self.assertEqual(0, self.index.delete_guests())
        self.assertEqual(1, len(self.index.search("", scope="all")["items"]), "Relay's own row stays")
        self.assertTrue(self.index.guest_meta("claude", self.CLAUDE)["pinned"])

    def test_an_append_adds_entries_instead_of_rewriting_them(self):
        self.index.update_guest(self.record(entries=[
            {"turn": 1, "seq": 1, "kind": "prompt", "time": 1.0, "text": "first ask"}]))
        self.index.update_guest({**self.record(entries=[
            {"turn": 2, "seq": 2, "kind": "prompt", "time": 2.0, "text": "second ask"}]),
            "append": True, "entry_base": 1, "message_count": 2, "mtime": 2000.0})
        entries = self.index.conversation(self.CLAUDE)["items"]
        self.assertEqual(["first ask", "second ask"], [entry["text"] for entry in entries])
        self.assertEqual(2, self.item()["turns"])
        self.assertEqual([self.CLAUDE], [item["session_id"] for item in
                                         self.index.search("second", scope="all",
                                                           sources=["claude"])["items"]])

    def test_an_append_without_a_row_writes_nothing_and_drops_the_cursor(self):
        """Entries added to a row that is not there would index a session starting in the middle."""
        self.index.update_guest({**self.record(), "cursor": {"path": "/x.jsonl", "read_to": 10}})
        self.assertEqual({self.CLAUDE: {"path": "/x.jsonl", "read_to": 10}}, self.index.guest_cursors())
        self.index.delete_session(self.CLAUDE, remove_files=False, forget=False)
        self.assertEqual(0, self.index.update_guest({**self.record(), "append": True, "entry_base": 2}))
        self.assertIsNone(self.item())
        self.assertEqual({}, self.index.guest_cursors())

    def test_a_cursor_is_stored_with_the_entries_and_goes_with_the_row(self):
        cursor = {"path": "/tmp/alpha/x.jsonl", "size": 40, "mtime_ns": 7, "read_to": 40,
                  "parser": {"entries": 2}}
        self.index.update_guest({**self.record(), "cursor": cursor})
        self.assertEqual({self.CLAUDE: cursor}, self.index.guest_cursors())
        self.assertEqual({}, self.index.guest_cursors(sources=["codex"]))
        self.index.delete_session(self.CLAUDE, remove_files=False)
        self.assertEqual({}, self.index.guest_cursors())

    def test_the_raw_cwd_is_kept_beside_the_resolved_workspace(self):
        link = self.root / "linked"
        real = self.root / "real"
        real.mkdir()
        link.symlink_to(real, target_is_directory=True)
        self.index.update_guest(self.record(workspace=str(link)))
        item = self.item()
        self.assertEqual((str(real), str(link)), (item["workspace"], item["raw_cwd"]))
        self.assertEqual(str(real), self.index.conversation(self.CLAUDE)["workspace"])


def rewind_record(n=1, *, turn=2, prompt="what happened to the marmoset", at=500.0, messages=None):
    """One line of `<id>.rewound.jsonl` as the worker writes it (protocol 5, card #0TJ9)."""
    return {"n": n, "at": at, "turn": turn, "restore": "conversation", "epoch": 0, "prompt": prompt,
            "messages": messages if messages is not None else [
                {"role": "user", "content": prompt},
                {"role": "assistant", "content": "the marmoset was renamed",
                 "tool_calls": [{"id": "r1", "type": "function",
                                 "function": {"name": "run_command",
                                              "arguments": '{"command": "rg marmoset"}'}}]},
                {"role": "tool", "tool_call_id": "r1", "content": "marmoset in three files"}],
            "restored_files": [], "conflicts": []}


class SidecarTests(unittest.TestCase):
    """The saved terminal text and the rewound turns (protocol 14.1, v6, card #0TJ9).

    The GUI writes the three sidecar files; the index only reads them, so everything here writes
    them by hand, exactly as the contract in the protocol spells them.
    """

    SID = "a" * 32

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.sessions = self.root / "sessions"
        self.digest = self.sessions / "digest"
        self.digest.mkdir(parents=True)
        self.index = ConversationIndex(self.root / "index.db")
        self.addCleanup(self.index.close)

    # ----- writing the files the GUI writes -------------------------------------------
    def write_session(self, data=None, **kwargs):
        data = data or session(self.SID, **kwargs)
        (self.digest / f"{data['id']}.json").write_text(json.dumps(data), encoding="utf-8")
        # The meta file is what a reconcile reads to decide the session JSON has not moved; without
        # one it re-reads every session every pass, and the sidecar assertions would say nothing.
        (self.digest / f"{data['id']}.meta.json").write_text(
            json.dumps({"id": data["id"], "updated": data["updated"]}), encoding="utf-8")
        return data

    def scrollback(self, text, session_id=None, *, rewound=None):
        name = f"{session_id or self.SID}"
        if rewound is not None:
            name += f".rewound-{rewound}"
        (self.digest / f"{name}.scrollback.txt").write_text(text, encoding="utf-8")

    def rewound(self, *records, session_id=None, trailing=""):
        text = "".join(json.dumps(record) + "\n" for record in records) + trailing
        (self.digest / f"{session_id or self.SID}.rewound.jsonl").write_text(text, encoding="utf-8")

    def guest_scrollback(self, text, session_id, source="claude"):
        folder = conv_index.guest_text_dir(source, self.sessions)
        folder.mkdir(parents=True, exist_ok=True)
        (folder / f"{session_id}.scrollback.txt").write_text(text, encoding="utf-8")

    def kinds(self, query):
        """The distinct `(session, kind)` pairs a query matched, sorted."""
        result = self.index.search(query, scope="all")
        return sorted({(item["session_id"], match["kind"]) for item in result["items"]
                       for match in item["matches"]})

    # ----- both kinds are indexed and found ------------------------------------------
    def test_saved_terminal_text_and_a_rewind_are_indexed_and_found(self):
        self.write_session()
        self.scrollback("$ ls\nquokka.txt  wombat.txt\n$ cat quokka.txt\nthe quokka file\n")
        self.rewound(rewind_record())
        self.scrollback("the capybara scrolled past here\n", rewound=1)
        self.index.reconcile(self.sessions)

        self.assertEqual([(self.SID, "terminal_text")], self.kinds("quokka"))
        self.assertEqual([(self.SID, "rewound")], self.kinds("marmoset"))
        self.assertEqual([(self.SID, "rewound")], self.kinds("capybara"))
        # A rewound row carries the turn it was dropped from, and the tool call is rendered the
        # way the session's own messages are.
        rewound = [entry for entry in self.index.conversation(self.SID)["items"]
                   if entry["kind"] == "rewound"]
        self.assertEqual({2}, {entry["turn"] for entry in rewound})
        self.assertIn('run_command {"command": "rg marmoset"}', [entry["text"] for entry in rewound])

    def test_saved_terminal_text_is_chunked_so_a_snippet_is_local(self):
        self.write_session()
        lines = [f"line {number}" for number in range(conv_index.TERMINAL_TEXT_LINES * 3)]
        lines[-1] = "the wallaby appears at the end"
        self.scrollback("\n".join(lines) + "\n")
        self.index.reconcile(self.sessions)
        rows = self.index._db.execute(
            "SELECT text FROM entries WHERE session_id=? AND kind='terminal_text' ORDER BY seq",
            (self.SID,)).fetchall()
        self.assertEqual(3, len(rows))
        self.assertTrue(all(len(row["text"].splitlines()) <= conv_index.TERMINAL_TEXT_LINES
                            for row in rows))
        match = self.index.search("wallaby", scope="all")["items"][0]["matches"][0]
        self.assertEqual("the wallaby appears at the end", match["line"])

    def test_the_sidecar_kinds_rank_below_message_text(self):
        """A word the user typed beats the same word scrolling past in the terminal, or in a turn
        that was thrown away, however many times it is in either."""
        self.write_session(session(self.SID, extra_turn="tell me about the wallaby"))
        self.write_session(session("b" * 32, workspace="/tmp/alpha", title="Rewound one"))
        self.write_session(session("c" * 32, workspace="/tmp/alpha", title="Terminal one"))
        self.rewound(rewind_record(messages=[{"role": "user", "content": "wallaby " * 20}]),
                     session_id="b" * 32)
        self.scrollback("wallaby " * 200, session_id="c" * 32)
        self.index.reconcile(self.sessions)
        order = [item["session_id"] for item in
                 self.index.search("wallaby", scope="all", sort="relevance")["items"]]
        self.assertEqual([self.SID, "b" * 32, "c" * 32], order)

    def test_the_sidecars_are_not_messages(self):
        """Not a turn, not the first or last prompt, not the overview, not the list snippet."""
        self.write_session()
        self.scrollback("\n".join(f"scrolled line {n}" for n in range(200)))
        self.rewound(rewind_record(turn=9))
        self.index.reconcile(self.sessions)
        item = self.index.search("", scope="all")["items"][0]
        self.assertEqual(2, item["turns"])
        self.assertEqual("how do I search the scrollback", item["first_prompt"])
        self.assertEqual("how do I search the scrollback", item["last_prompt"])
        overview = self.index.conversation(self.SID)["overview"]
        self.assertEqual([1], [turn["turn"] for turn in overview["last_turns"]])
        self.assertNotIn("scrolled line", item["snippet"])
        # The preview lists what a rewind dropped and leaves saved terminal text out of it.
        kinds = {entry["kind"] for entry in self.index.conversation(self.SID)["items"]}
        self.assertIn("rewound", kinds)
        self.assertNotIn("terminal_text", kinds)

    def test_a_hit_in_saved_terminal_text_is_visible_in_the_preview(self):
        self.write_session()
        self.scrollback("$ ls\nquokka.txt\n")
        self.index.reconcile(self.sessions)
        preview = self.index.conversation(self.SID, query="quokka")
        hits = [entry for entry in preview["items"] if entry["kind"] == "terminal_text"]
        self.assertEqual(["quokka.txt"], [entry["line"] for entry in hits])
        self.assertEqual(1, preview["match_count"])
        # Only the chunks the query matched: the default preview is still the conversation.
        self.assertEqual([], [entry for entry in self.index.conversation(self.SID, query="zebra")["items"]
                              if entry["kind"] == "terminal_text"])

    def test_has_rewound_selects_the_conversations_that_kept_one(self):
        self.write_session()
        self.write_session(session("b" * 32, workspace="/tmp/alpha", title="No rewind"))
        self.rewound(rewind_record())
        self.index.reconcile(self.sessions)
        self.assertEqual([self.SID], [item["session_id"] for item in
                                      self.index.search("has:rewound", scope="all")["items"]])
        self.assertEqual(["b" * 32], [item["session_id"] for item in
                                      self.index.search("-has:rewound", scope="all")["items"]])
        self.assertEqual([{"key": "has", "value": "rewound"}],
                         conv_index.parse_query("has:rewound")["operators"])

    def test_a_truncated_last_record_is_tolerated(self):
        """The GUI appends a line per rewind and can be stopped in the middle of one."""
        self.write_session()
        self.rewound(rewind_record(), rewind_record(n=2, turn=3, prompt="the second marmoset ask"),
                     trailing='{"n": 3, "turn": 4, "messages": [{"role": "user", "content": "the thir')
        self.index.reconcile(self.sessions)
        self.assertEqual([(self.SID, "rewound")], self.kinds("second"))
        self.assertEqual([], self.kinds("thir"), "the half-written line is dropped, not indexed")
        self.assertEqual({2, 3}, {entry["turn"] for entry in self.index.conversation(self.SID)["items"]
                                  if entry["kind"] == "rewound"})

    def test_only_the_newest_records_are_kept(self):
        self.write_session()
        self.rewound(*[rewind_record(n=number, turn=number,
                                     messages=[{"role": "user", "content": f"rewind marker{number:02d}"}])
                       for number in range(1, conv_index.MAX_REWOUND_RECORDS + 4)])
        self.index.reconcile(self.sessions)
        rows = self.index._db.execute(
            "SELECT count(*) FROM entries WHERE session_id=? AND kind='rewound'", (self.SID,)).fetchone()[0]
        self.assertEqual(conv_index.MAX_REWOUND_RECORDS, rows)
        self.assertEqual([], self.kinds("marker01"))        # the oldest three fell off the front
        self.assertEqual([(self.SID, "rewound")], self.kinds("marker04"))

    def test_the_worker_writes_records_this_reads(self):
        """`SessionStore.append_rewound` is the other half of the jsonl contract (commit
        73d50015). The parser here is fed by it rather than by a hand-written line, so the two
        cannot drift apart without a test saying so."""
        self.write_session()
        store = SessionStore(self.digest, index=self.index)
        number = store.append_rewound(self.SID, {
            "at": 500.0, "turn": 3, "restore": "conversation", "epoch": 0,
            "prompt": "what about the marmoset", "restored_files": [], "conflicts": [],
            "messages": rewind_record()["messages"]})
        self.assertEqual(1, number)
        self.scrollback("the capybara scrolled past here\n", rewound=number)
        self.index.reconcile(self.sessions)
        rewound = [entry for entry in self.index.conversation(self.SID)["items"]
                   if entry["kind"] == "rewound"]
        self.assertEqual({3}, {entry["turn"] for entry in rewound})
        self.assertIn("the capybara scrolled past here", [entry["text"] for entry in rewound])
        self.assertEqual([(self.SID, "rewound")], self.kinds("marmoset"))

    # ----- reconcile reads a sidecar once ---------------------------------------------
    def test_a_changed_sidecar_is_re_read_and_an_unchanged_one_is_not(self):
        self.write_session()
        self.scrollback("$ ls\nquokka.txt\n")
        reads: list[str] = []
        real = conv_index.read_sidecar_text

        def counted(path):
            text = real(path)
            if text:
                reads.append(Path(path).name)
            return text

        with mock.patch.object(conv_index, "read_sidecar_text", counted):
            first = self.index.reconcile(self.sessions)
            self.assertEqual(["a" * 32 + ".scrollback.txt"], reads)
            self.assertEqual(1, first["sidecars"])
            # Nothing moved: the stamp matches, so the file is not opened again.
            reads.clear()
            again = self.index.reconcile(self.sessions)
            self.assertEqual(([], 0), (reads, again["sidecars"]))
            # The GUI writes the scrollback at quit, after the last autosave: the session JSON has
            # not moved, and the sidecar is still picked up.
            self.scrollback("$ ls\nquokka.txt\n$ cat quokka.txt\nthe quokka file\n")
            third = self.index.reconcile(self.sessions)
            self.assertEqual(1, third["sidecars"])
            self.assertEqual(0, third["refreshed"], "the session file itself was not re-read")
        self.assertEqual([(self.SID, "terminal_text")], self.kinds("quokka file"))

    def test_an_autosave_leaves_the_saved_terminal_text_alone(self):
        """The incremental write (#TZWF) rewrites what the session holds; the sidecars are not
        part of it, and counting them would have made every save a full rewrite."""
        self.write_session()
        self.scrollback("$ ls\nquokka.txt\n")
        self.index.reconcile(self.sessions)
        self.index.update_session(session(self.SID, extra_turn="and then", turns=3, updated=9000.0),
                                  self.digest)
        self.assertEqual([(self.SID, "terminal_text")], self.kinds("quokka"))
        self.index.update_session(session(self.SID, updated=9500.0), self.digest)   # a full rewrite
        self.assertEqual([(self.SID, "terminal_text")], self.kinds("quokka"))

    def test_a_rebuild_writes_the_sidecar_rows_again(self):
        self.write_session()
        self.scrollback("$ ls\nquokka.txt\n")
        self.index.reconcile(self.sessions)
        self.index.rebuild(self.sessions)
        self.assertEqual([(self.SID, "terminal_text")], self.kinds("quokka"))

    def test_a_v5_index_backfills_the_sidecar_rows_on_the_next_reconcile(self):
        """v6 adds no column, so the migration is the v3 one: the rows are marked behind and the
        next reconcile reads each session's sidecars once, keeping the terminal history."""
        self.write_session()
        self.scrollback("$ ls\nquokka.txt\n")
        self.index.reconcile(self.sessions)
        self.index.record_commands("/tmp/alpha", [{"command": "rg numbat", "exit_status": 0}])
        self.index.close()
        db = sqlite3.connect(str(self.root / "index.db"))
        db.execute("DELETE FROM entries WHERE kind IN ('terminal_text', 'rewound')")
        db.execute("DROP TABLE session_sidecars")
        db.execute("INSERT OR REPLACE INTO meta(key, value) VALUES('schema_version', '5')")
        db.commit()
        db.close()

        self.index = ConversationIndex(self.root / "index.db")
        self.assertEqual(5, self.index.migrated_from)
        self.assertFalse(self.index.recovered, "migrated, not wiped")
        self.assertEqual([], self.kinds("quokka"))
        report = self.index.reconcile(self.sessions)
        self.assertEqual((1, 1), (report["backfilled"], report["sidecars"]))
        self.assertEqual([(self.SID, "terminal_text")], self.kinds("quokka"))
        self.assertEqual(1, len(self.index.search("numbat", scope="all")["items"]),
                         "the terminal history has no file to be rebuilt from and is kept")
        self.assertEqual(0, self.index.reconcile(self.sessions)["backfilled"])

    # ----- delete ----------------------------------------------------------------------
    def test_delete_removes_the_sidecars(self):
        data = self.write_session()
        self.scrollback("$ ls\nquokka.txt\n")
        self.rewound(rewind_record())
        self.scrollback("the capybara scrolled past here\n", rewound=1)
        self.index.reconcile(self.sessions)
        removed = self.index.delete_session(data["id"])
        self.assertEqual(5, removed["files"])       # the json, the meta file and the three sidecars
        self.assertEqual([], sorted(path.name for path in self.digest.iterdir()))
        self.assertEqual(0, self.index.stats()["entries"])
        self.assertEqual({}, self.index.sidecar_stamps())

    # ----- guests ------------------------------------------------------------------------
    def test_the_guests_folder_is_not_a_workspace_digest(self):
        """`sessions/guests/` is Relay's own store for the guests' saved text. A walk that took it
        for a workspace would go looking for session files under `guests/claude/`."""
        self.write_session()
        self.guest_scrollback("nothing here\n", "g" * 32)
        (self.sessions / "guests" / "claude" / f"{'g' * 32}.json").write_text(
            json.dumps(session("g" * 32)), encoding="utf-8")
        self.assertEqual([self.digest], conv_index.session_folders(self.sessions))
        self.index.reconcile(self.sessions)
        self.assertEqual([self.SID], [item["session_id"] for item in
                                      self.index.search("", scope="all")["items"]])
        self.assertEqual(1, self.index.rebuild(self.sessions)["sessions"])

    def test_a_guest_scrollback_is_indexed_under_its_guest_row_and_deleted_with_it(self):
        guest = "ea11ece1-7ec2-4597-8639-32fb1f43f073"
        self.index.update_guest({"source": "claude", "id": guest, "title": "pane drag",
                                 "workspace": "/tmp/alpha", "created": 1.0, "mtime": 1.0,
                                 "message_count": 1,
                                 "entries": [{"turn": 1, "seq": 1, "kind": "prompt", "time": 1.0,
                                              "text": "fix the pane drag"}]})
        self.guest_scrollback("$ ls\nquokka.txt\n", guest)
        report = self.index.reconcile(self.sessions)
        self.assertEqual(1, report["sidecars"])
        self.assertEqual([(guest, "terminal_text")],
                         [(item["session_id"], match["kind"]) for item in
                          self.index.search("quokka", scope="all", sources=["claude"])["items"]
                          for match in item["matches"]])
        # Re-parsing the transcript rewrites the guest's own rows and leaves Relay's sidecar be.
        self.index.update_guest({"source": "claude", "id": guest, "title": "pane drag",
                                 "workspace": "/tmp/alpha", "mtime": 2.0, "message_count": 1,
                                 "entries": [{"turn": 1, "seq": 1, "kind": "prompt", "time": 2.0,
                                              "text": "fix the pane drag again"}]})
        self.assertEqual(1, len(self.index.search("quokka", scope="all", sources=["claude"])["items"]))
        # The transcript is the guest's, but that scrollback is Relay's, so a delete takes it.
        removed = self.index.delete_session(guest)
        self.assertEqual(1, removed["files"])
        self.assertFalse((conv_index.guest_text_dir("claude", self.sessions)
                          / f"{guest}.scrollback.txt").exists())

    def test_a_scrollback_with_no_conversation_row_leaves_no_orphan(self):
        self.guest_scrollback("$ ls\nquokka.txt\n", "h" * 32)
        self.index.reconcile(self.sessions)
        self.assertEqual(0, self.index.stats()["entries"])
        self.assertEqual({}, self.index.sidecar_stamps(), "and it is looked at again next pass")


class StoreIntegrationTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.home = Path(self.temp.name)
        patcher = mock.patch.dict(os.environ, {"XDG_DATA_HOME": str(self.home)})
        patcher.start()
        self.addCleanup(patcher.stop)

    def test_autosave_indexes_sessions_in_the_relay_data_directory(self):
        directory = conv_index.sessions_root() / "digest"
        store = SessionStore(directory)
        store.save(session())
        index = ConversationIndex()
        self.addCleanup(index.close)
        self.assertEqual(len(index.search("scrollback", scope="all")["items"]), 1)
        self.assertEqual(index.path, self.home / "relay" / "index.db")

    def test_sessions_outside_the_data_directory_are_not_indexed(self):
        store = SessionStore(self.home / "elsewhere")
        store.save(session())
        self.assertIsNone(store.index())
        self.assertFalse((self.home / "relay" / "index.db").exists())

    def test_agent_autosave_feeds_the_index(self):
        recorder = Recorder()
        supervisor = TurnSupervisor(recorder)
        self.addCleanup(supervisor.shutdown)
        workspace = self.home / "ws"
        workspace.mkdir()
        agent = Agent(ProviderConfig("http://127.0.0.1:1/v1", "m", ""), str(workspace), supervisor.agent_emit,
                      provider=ScriptedProvider([text("the aardvark is in the index")]),
                      session_dir=str(conv_index.sessions_root() / "digest"))
        supervisor.set_agent(agent)
        supervisor.submit("tell me about aardvarks", "now")
        recorder.wait(lambda e: e["event"] == "agent_finished")
        index = ConversationIndex()
        self.addCleanup(index.close)
        found = index.search("aardvark", scope="project", workspace=str(workspace))
        self.assertEqual(len(found["items"]), 1)
        # The generated title comes from the first prompt, so it is a match of its own (v3).
        self.assertEqual({m["kind"] for m in found["items"][0]["matches"]}, {"title", "prompt", "reply"})


class ProtocolTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.home = Path(self.temp.name)
        patcher = mock.patch.dict(os.environ, {"XDG_DATA_HOME": str(self.home)})
        patcher.start()
        self.addCleanup(patcher.stop)
        self.workspace = self.home / "ws"
        self.workspace.mkdir()
        self.rec = Recorder()
        self.sup = TurnSupervisor(self.rec)
        self.addCleanup(self.sup.shutdown)
        self.cmds = SessionCommands(self.sup, self.rec)
        self.addCleanup(lambda: self.cmds._index and self.cmds._index.close())

    def seed(self, session_id="a" * 32, **kwargs):
        store = SessionStore(conv_index.sessions_root() / "digest")
        data = session(session_id, workspace=str(self.workspace), **kwargs)
        store.save(data)
        return data

    def last(self, name):
        return [event for event in self.rec.events if event.get("event") == name][-1]

    def test_conversations_without_a_configured_agent(self):
        self.seed()
        self.cmds.handle("conversations", {"id": "q1", "query": "pelican", "scope": "all"})
        event = self.last("conversations")
        self.assertEqual(event["id"], "q1")
        self.assertEqual(len(event["items"]), 1)
        self.assertEqual(event["items"][0]["matches"][0]["kind"], "reply")
        self.assertIn("elapsed_ms", event)

    def test_scope_project_uses_the_requested_workspace(self):
        self.seed()
        self.cmds.handle("conversations", {"query": "", "scope": "project", "workspace": str(self.workspace)})
        self.assertEqual(len(self.last("conversations")["items"]), 1)
        self.cmds.handle("conversations", {"query": "", "scope": "project", "workspace": "/tmp/elsewhere"})
        self.assertEqual(self.last("conversations")["items"], [])

    def test_terminal_history_then_search_reports_the_kind(self):
        self.cmds.handle("terminal_history", {"id": "t1", "workspace": str(self.workspace),
                                              "items": [{"command": "rg --hidden capybara",
                                                         "exit_status": 1, "output": "no matches"}]})
        self.assertEqual(self.last("terminal_history_indexed")["rows"], 2)
        self.cmds.handle("conversations", {"query": "capybara", "scope": "all"})
        item = self.last("conversations")["items"][0]
        self.assertEqual(item["source"], "terminal")
        self.assertEqual(item["matches"][0]["kind"], "command")
        self.cmds.handle("conversation_get", {"session_id": item["session_id"], "query": "capybara"})
        preview = self.last("conversation")
        self.assertEqual(preview["items"][0]["exit_status"], 1)

    def test_conversation_get_and_rename_and_pin(self):
        data = self.seed()
        self.cmds.handle("conversation_get", {"session_id": data["id"], "turn": 1})
        self.assertEqual({item["turn"] for item in self.last("conversation")["items"]}, {1})
        self.cmds.handle("conversation_rename", {"session_id": data["id"], "title": "Renamed"})
        self.assertEqual(self.last("conversation_renamed")["title"], "Renamed")
        self.cmds.handle("conversation_pin", {"session_id": data["id"], "pinned": True})
        self.assertTrue(self.last("conversation_pinned")["pinned"])
        self.cmds.handle("conversations", {"query": "", "scope": "all"})
        self.assertEqual(self.last("conversations")["items"][0]["title"], "Renamed")
        self.assertRaises(ValueError, self.cmds.handle, "conversation_pin",
                          {"session_id": data["id"], "pinned": "yes"})
        self.assertRaises(ValueError, self.cmds.handle, "conversation_get", {"session_id": "not-an-id"})

    def test_conversation_delete_removes_files_and_rows(self):
        data = self.seed()
        self.cmds.handle("conversations", {"query": "scrollback", "scope": "all"})
        self.assertEqual(len(self.last("conversations")["items"]), 1)
        self.cmds.handle("conversation_delete", {"id": "d1", "session_id": data["id"]})
        self.assertEqual(self.last("conversation_deleted")["session_id"], data["id"])
        self.assertFalse((conv_index.sessions_root() / "digest" / f"{data['id']}.json").exists())
        self.cmds.handle("conversations", {"query": "scrollback", "scope": "all"})
        self.assertEqual(self.last("conversations")["items"], [])

    def test_terminal_conversation_delete_keeps_files_alone(self):
        self.cmds.handle("terminal_history", {"workspace": str(self.workspace),
                                              "items": [{"command": "echo wombat", "exit_status": 0}]})
        self.cmds.handle("conversation_delete", {"session_id": conv_index.terminal_id(str(self.workspace))})
        self.cmds.handle("conversations", {"query": "wombat", "scope": "all"})
        self.assertEqual(self.last("conversations")["items"], [])

    def test_index_rebuild(self):
        self.seed()
        self.cmds.index().delete_session(session()["id"], remove_files=False)
        self.cmds.handle("index_rebuild", {"id": "r1"})
        deadline = time.monotonic() + 5
        while time.monotonic() < deadline:
            if [e for e in self.rec.events if e.get("event") == "index_rebuilt"]:
                break
            time.sleep(0.02)
        event = self.last("index_rebuilt")
        self.assertEqual(event["sessions"], 1)
        self.assertEqual(event["schema_version"], conv_index.SCHEMA_VERSION)

    def test_conversations_passes_the_new_filters_and_reports_parsed_and_facets(self):
        self.seed(summary="A wombat summary", branch="main", files=["/ws/src/Pane.h"],
                  extra_turn="and then")
        self.seed(session_id="b" * 32, title="Second", model="kimi-k2", branch="feature/x",
                  updated=6000.0, extra_turn="and then")
        self.cmds.handle("conversations", {"id": "q1", "query": "wombat", "scope": "all"})
        event = self.last("conversations")
        self.assertEqual(event["items"][0]["session_id"], "a" * 32)
        self.assertEqual(event["items"][0]["matches"][0]["kind"], "summary")
        self.assertEqual(event["parsed"], {"text": "wombat", "operators": [], "ignored": []})
        self.assertEqual(event["facets"]["branches"], ["feature/x", "main"])
        self.assertEqual(event["facets"]["models"], ["kimi-k2", "glm-5"])
        for request, expected in (
                ({"has_edits": True}, ["a" * 32]),
                ({"has_edits": False}, ["b" * 32]),
                ({"has_summary": True}, ["a" * 32]),
                ({"branch": "feature"}, ["b" * 32]),
                ({"file": "Pane.h"}, ["a" * 32]),
                ({"unfinished": True}, []),
                ({"pinned": True}, []),
                ({"query": "file:Pane.h"}, ["a" * 32]),
                ({"query": "is:unfinished"}, [])):
            with self.subTest(request):
                self.cmds.handle("conversations", {"query": "", "scope": "all", **request})
                self.assertEqual([i["session_id"] for i in self.last("conversations")["items"]], expected)
        for bad in ({"has_edits": "yes"}, {"unfinished": 1}, {"pinned": "no"}, {"file": 3}, {"branch": []}):
            self.assertRaises(ValueError, self.cmds.handle, "conversations", {"query": "", **bad})

    def test_project_and_outside_projects_ride_the_request(self):
        """The `project` / `outside_projects` fields of 14.3 (#916B), and what is refused."""
        self.seed(extra_turn="and then")
        elsewhere = self.home / "elsewhere"
        elsewhere.mkdir()
        self.seed(session_id="b" * 32, title="Elsewhere", updated=6000.0, extra_turn="and then")
        store = SessionStore(conv_index.sessions_root() / "digest2")
        store.save(session("c" * 32, workspace=str(elsewhere), title="Loose", updated=7000.0))
        self.cmds.handle("conversations", {"query": "", "scope": "project", "project": str(self.workspace)})
        event = self.last("conversations")
        self.assertEqual(event["scope"], "all")
        self.assertEqual(sorted(i["session_id"] for i in event["items"]), ["a" * 32, "b" * 32])
        self.cmds.handle("conversations", {"query": "", "scope": "project",
                                           "outside_projects": [str(self.workspace)]})
        self.assertEqual([i["session_id"] for i in self.last("conversations")["items"]], ["c" * 32])
        for bad in ({"project": 3}, {"outside_projects": "x"}, {"outside_projects": [1]}, {"outside_projects": [""]}):
            self.assertRaises(ValueError, self.cmds.handle, "conversations", {"query": "", **bad})

    def test_project_operator_reports_the_scope_it_switched_to(self):
        self.seed(extra_turn="and then")
        self.cmds.handle("conversations", {"query": "project:ws", "scope": "project",
                                           "workspace": "/tmp/elsewhere"})
        event = self.last("conversations")
        self.assertEqual(event["scope"], "all")
        self.assertEqual(len(event["items"]), 1)

    def test_conversation_get_carries_the_overview(self):
        data = self.seed(summary="What this was about", branch="main", files=["/ws/one.py"],
                         todos=[{"id": "t1", "text": "open thing", "status": "pending"}])
        self.cmds.handle("conversation_get", {"session_id": data["id"]})
        overview = self.last("conversation")["overview"]
        self.assertEqual(overview["summary"], "What this was about")
        self.assertEqual(overview["files"], ["/ws/one.py"])
        self.assertEqual(overview["todos"], [{"text": "open thing", "status": "pending"}])
        self.assertEqual(overview["branch"], "main")
        self.assertTrue(overview["unfinished"])

    def test_index_disabled_reports_an_error(self):
        with mock.patch.dict(os.environ, {"RELAY_INDEX": "off"}):
            commands = SessionCommands(self.sup, self.rec)
            self.assertRaises(ValueError, commands.handle, "conversations", {"query": "x"})


def conversation(turns: int, session_id="g" * 32, *, updated=2000.0, reword=None, epoch=0) -> dict:
    """A session of `turns` question/answer turns, as the autosave writes it.

    `reword` is `{turn: text}` for a turn whose prompt changed where it stands — an edit inside
    the conversation rather than at its end.
    """
    messages: list[dict] = []
    items: list[dict] = []
    for turn in range(1, turns + 1):
        prompt = (reword or {}).get(turn, f"question {turn} about capybaras")
        items.append({"turn": turn, "prompt": prompt, "prompt_preview": prompt[:20],
                      "time": 1000.0 + turn, "locations": {str(epoch): len(messages) + 1},
                      "files": {}, "ended": 1005.0 + turn})
        messages += [{"role": "user", "content": prompt},
                     {"role": "assistant", "content": f"answer {turn} about wombats"}]
    return {"version": 1, "kind": "relay_session", "id": session_id, "title": "Growing",
            "created": 900.0, "updated": updated, "workspace": "/tmp/alpha", "model": "glm-5",
            "preset": "glm", "effort": "high", "mode": "build", "turns": turns, "epoch": epoch,
            "messages": messages, "snapshots": {}, "checkpoints": {"items": items},
            "requests": {"items": []}, "todos": {"items": []}, "plan_path": None,
            "open_requests": 0}


class IncrementalIndexTests(unittest.TestCase):
    """An autosave writes the turns that were added, not the whole conversation again (#TZWF).

    Every save used to `DELETE FROM entries WHERE session_id=?` and insert every row back — 46 ms
    on the owner's largest session, on the turn's own thread, and the churn that left a third of
    his index file as free pages. What decides is the rolling digest of the rows already indexed
    (`entry_digests`), never the caller: anything but a pure extension falls back to the rewrite.
    """

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.index = ConversationIndex(self.root / "index.db")
        self.addCleanup(self.index.close)

    def entries(self, session_id="g" * 32):
        return self.index._db.execute(
            "SELECT id, turn, seq, kind, time, text FROM entries WHERE session_id=? ORDER BY id",
            (session_id,)).fetchall()

    def contents(self, index=None, session_id="g" * 32):
        """The rows as a rebuild would have to reproduce them: everything but the row id."""
        db = (index or self.index)._db
        return sorted(tuple(row)[1:] for row in db.execute(
            "SELECT id, turn, seq, kind, time, text FROM entries WHERE session_id=?", (session_id,)))

    def fts_is_consistent(self):
        # FTS5's own check that its index matches the content table, which is `entries`.
        self.index._db.execute("INSERT INTO entries_fts(entries_fts) VALUES('integrity-check')")

    def changes(self, data):
        """Rows the save touched, FTS shadow rows included — what the work is proportional to."""
        before = self.index._db.total_changes
        self.index.update_session(data, self.root)
        return self.index._db.total_changes - before

    def forget_the_fingerprint(self):
        """What a v4 row looks like after the migration: the next save rewrites everything."""
        self.index._db.execute("UPDATE conversations SET entry_count=0, entry_digest=''")
        self.index._db.commit()

    def test_a_grown_conversation_writes_only_its_new_turns(self):
        self.index.update_session(conversation(20), self.root)
        before = {row["id"]: tuple(row) for row in self.entries()}
        self.assertEqual(len([r for r in before.values() if r[3] in ("prompt", "reply")]), 40)
        grown = conversation(21, updated=3000.0)
        incremental = self.changes(grown)
        after = {row["id"]: tuple(row) for row in self.entries()}
        # The 40 rows that were already there are the same rows: same ids, same seq, same text —
        # and the same `time`, which a body row now takes from the save that indexed it rather
        # than from the latest save of the conversation.
        for row_id, row in before.items():
            if row[3] in ("prompt", "reply"):
                self.assertEqual(after.get(row_id), row)
        self.assertEqual(len([r for r in after.values() if r[3] in ("prompt", "reply")]), 42)
        # …and the whole rewrite it replaces, on the same data, costs several times as much.
        self.forget_the_fingerprint()
        rewrite = self.changes(grown)
        self.assertLess(incremental * 5, rewrite, (incremental, rewrite))
        self.fts_is_consistent()
        self.assertEqual(len(self.index.search("question 21", scope="all")["items"]), 1)
        self.assertEqual(len(self.index.search("wombats", scope="all")["items"]), 1)

    def test_a_rewind_falls_back_and_leaves_what_a_rebuild_from_scratch_writes(self):
        self.index.update_session(conversation(20), self.root)
        rewound = conversation(8, updated=5000.0)
        self.index.update_session(rewound, self.root)
        fresh = ConversationIndex(self.root / "fresh.db")
        self.addCleanup(fresh.close)
        fresh.update_session(rewound, self.root)
        self.assertEqual(self.contents(), self.contents(fresh))
        self.fts_is_consistent()
        self.assertEqual(self.index.search("question 20", scope="all")["items"], [])
        self.assertEqual(len(self.index.search("question 8", scope="all")["items"]), 1)

    def test_a_prompt_edited_where_it_stands_is_not_taken_for_an_appended_turn(self):
        self.index.update_session(conversation(12), self.root)
        edited = conversation(12, updated=5000.0, reword={5: "question 5 about narwhals"})
        self.index.update_session(edited, self.root)
        fresh = ConversationIndex(self.root / "fresh.db")
        self.addCleanup(fresh.close)
        fresh.update_session(edited, self.root)
        self.assertEqual(self.contents(), self.contents(fresh))
        self.assertEqual(len(self.index.search("narwhals", scope="all")["items"]), 1)
        self.assertNotIn("question 5 about capybaras", [row["text"] for row in self.entries()])
        self.fts_is_consistent()

    def test_a_compaction_that_renumbers_the_turns_falls_back(self):
        self.index.update_session(conversation(14), self.root)
        compacted = conversation(14, updated=5000.0, epoch=1)
        for item in compacted["checkpoints"]["items"]:         # the epoch moved the messages
            item["locations"]["1"] = max(1, item["locations"]["1"] - 6)
        compacted["messages"] = compacted["messages"][6:]
        self.index.update_session(compacted, self.root)
        fresh = ConversationIndex(self.root / "fresh.db")
        self.addCleanup(fresh.close)
        fresh.update_session(compacted, self.root)
        self.assertEqual(self.contents(), self.contents(fresh))
        self.fts_is_consistent()

    def test_entries_lost_under_a_row_that_stayed_are_written_again(self):
        # The digest says "a pure extension"; the table says the rows are not there. The table wins.
        self.index.update_session(conversation(10), self.root)
        self.index._db.execute("DELETE FROM entries WHERE session_id=? AND turn > 4", ("g" * 32,))
        self.index._db.commit()
        self.index.update_session(conversation(11, updated=5000.0), self.root)
        fresh = ConversationIndex(self.root / "fresh.db")
        self.addCleanup(fresh.close)
        fresh.update_session(conversation(11, updated=5000.0), self.root)
        self.assertEqual(self.contents(), self.contents(fresh))
        self.fts_is_consistent()

    def test_a_row_indexed_before_the_fingerprint_existed_re_indexes_itself_once(self):
        self.index.update_session(conversation(30), self.root)
        self.forget_the_fingerprint()
        rewritten = self.changes(conversation(31, updated=5000.0))       # everything, once
        incremental = self.changes(conversation(32, updated=6000.0))     # then only the new turn
        self.assertLess(incremental * 5, rewritten, (incremental, rewritten))
        self.fts_is_consistent()

    def test_the_digest_covers_a_row_s_identity_and_text_but_not_its_time(self):
        rows = conv_index.session_entries(conversation(4))
        prefix, whole = conv_index.entry_digests(rows, len(rows))
        self.assertEqual(prefix, whole)
        self.assertEqual(conv_index.entry_digests(rows, 2)[0], conv_index.entry_digests(rows[:2], 2)[1])
        moved = [{**row, "time": (row["time"] or 0) + 99} for row in rows]
        self.assertEqual(conv_index.entry_digests(moved, len(rows))[1], whole)
        changed = [{**row} for row in rows]
        changed[1]["text"] += "!"
        self.assertNotEqual(conv_index.entry_digests(changed, len(rows))[1], whole)
        # Growing the conversation leaves the digest of what is already indexed alone.
        self.assertEqual(conv_index.entry_digests(conv_index.session_entries(conversation(9)),
                                                  len(rows))[0], whole)
        # A conversation that shrank has no prefix to match.
        self.assertEqual(conv_index.entry_digests(rows, len(rows) + 1)[0], "")

    def test_a_rebuild_gives_back_the_free_pages_when_there_are_enough_of_them(self):
        for n in range(30):
            self.index.update_session(conversation(20, session_id=f"{n:032d}"), self.root)
        # No session files to rebuild from, so the rebuild drops the rows and frees their pages.
        self.assertEqual(self.index.rebuild(self.root / "sessions")["reclaimed"], 0,
                         "a few megabytes of freelist are not worth rewriting the file")
        pages = self.index._db.execute("PRAGMA page_count").fetchone()[0]
        self.assertGreater(self.index._db.execute("PRAGMA freelist_count").fetchone()[0], 0)
        with mock.patch.object(conv_index, "VACUUM_MIN_BYTES", 0), \
             mock.patch.object(conv_index, "VACUUM_FREE_IN", 1000):
            report = self.index.rebuild(self.root / "sessions")
        self.assertGreater(report["reclaimed"], 0)
        self.assertEqual(self.index._db.execute("PRAGMA freelist_count").fetchone()[0], 0)
        self.assertLess(self.index._db.execute("PRAGMA page_count").fetchone()[0], pages)
        self.fts_is_consistent()

    def test_a_subagent_thread_is_written_the_same_way(self):
        thread = {"kind": conv_index.THREAD_KIND, "id": "t" * 32, "owner_session": "g" * 32,
                  "workspace": "/tmp/alpha", "description": "Find the pelican", "updated": 2000.0,
                  "messages": [{"role": "user", "content": "task one"},
                               {"role": "assistant", "content": "did one"}]}
        for extra in range(9):
            thread["messages"] += [{"role": "user", "content": f"more {extra}"},
                                   {"role": "assistant", "content": f"done {extra}"}]
        self.index.update_thread(thread, self.root)
        before = {row["id"]: tuple(row) for row in self.entries("t" * 32)}
        grown = {**thread, "updated": 3000.0,
                 "messages": thread["messages"] + [{"role": "user", "content": "task two"},
                                                   {"role": "assistant", "content": "did two"}]}
        mark = self.index._db.total_changes
        self.index.update_thread(grown, self.root)
        incremental = self.index._db.total_changes - mark
        after = {row["id"]: tuple(row) for row in self.entries("t" * 32)}
        for row_id, row in before.items():
            if row[3] != "title":
                self.assertEqual(after.get(row_id), row)
        self.forget_the_fingerprint()
        mark = self.index._db.total_changes
        self.index.update_thread(grown, self.root)
        self.assertLess(incremental * 3, self.index._db.total_changes - mark)
        self.fts_is_consistent()


if __name__ == "__main__":
    unittest.main()
