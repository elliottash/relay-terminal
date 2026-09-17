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
            updated=2000.0, model="glm-5", open_requests=0, extra_turn=None):
    messages = [
        {"role": "user", "content": "how do I search the scrollback"},
        {"role": "assistant", "content": "Use the pelican finder",
         "tool_calls": [{"id": "c1", "type": "function",
                         "function": {"name": "run_command", "arguments": '{"command": "rg needle"}'}}]},
        {"role": "tool", "tool_call_id": "c1", "content": "needle found in scrollback.txt"},
    ]
    items = [{"turn": 1, "prompt": "how do I search the scrollback", "prompt_preview": "how do I",
              "time": 1000.0, "locations": {"0": 1}, "files": {}}]
    if extra_turn:
        items.append({"turn": 2, "prompt": extra_turn, "prompt_preview": extra_turn[:20],
                      "time": 1500.0, "locations": {"0": len(messages) + 1}, "files": {}})
        messages += [{"role": "user", "content": extra_turn},
                     {"role": "assistant", "content": "answered " + extra_turn}]
    return {"version": 1, "kind": "relay_session", "id": session_id, "title": title,
            "created": 900.0, "updated": updated, "workspace": workspace, "model": model,
            "preset": "glm", "effort": "high", "mode": "build", "turns": turns, "epoch": 0,
            "messages": messages, "snapshots": {}, "checkpoints": {"items": items},
            "requests": {"items": []}, "todos": {"items": []}, "plan_path": None,
            "open_requests": open_requests}


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
        # Several words are an AND *within one message or command*, like grep over a turn.
        self.assertEqual(len(self.index.search("pelican finder", scope="all")["items"]), 1)
        self.assertEqual(len(self.index.search("pelican scrollback", scope="all")["items"]), 0)
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
        self.assertEqual({m["kind"] for m in found["items"][0]["matches"]}, {"prompt", "reply"})


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

    def seed(self, **kwargs):
        store = SessionStore(conv_index.sessions_root() / "digest")
        data = session(workspace=str(self.workspace), **kwargs)
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

    def test_index_disabled_reports_an_error(self):
        with mock.patch.dict(os.environ, {"RELAY_INDEX": "off"}):
            commands = SessionCommands(self.sup, self.rec)
            self.assertRaises(ValueError, commands.handle, "conversations", {"query": "x"})


if __name__ == "__main__":
    unittest.main()
