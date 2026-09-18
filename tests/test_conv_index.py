"""Conversation index and the conversation-list protocol commands (protocol section 14).

Everything runs against temporary directories: no test may touch the real index, the keyring or
the network. `relay_home` points XDG_DATA_HOME at a temporary directory for the whole test.
"""
import json
import os
import sqlite3
import sys
import tempfile
import time
import unittest
from pathlib import Path
from unittest import mock

from relay_core import conv_index
from relay_core.agent import Agent
from relay_core.conv_index import ConversationIndex
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

    def downgrade(self, path):
        """Turn the database back into a v2 one: drop the v3 columns and the header entries."""
        db = sqlite3.connect(str(path))
        for name, _kind in conv_index.V3_COLUMNS:
            db.execute(f"ALTER TABLE conversations DROP COLUMN {name}")
        db.execute("DELETE FROM entries WHERE kind IN ('title', 'summary')")
        db.execute("INSERT OR REPLACE INTO meta(key, value) VALUES('schema_version', '2')")
        db.commit()
        db.close()

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


if __name__ == "__main__":
    unittest.main()
