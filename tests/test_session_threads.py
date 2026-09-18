"""Session and subagent-thread index, and the session info view's data (cards #Y63Z, #R6J0).

Everything runs in temporary directories with XDG_DATA_HOME pointed at one: no test touches the
real sessions, the real index, the keyring or the network.
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

from relay_core import conv_index, sessions
from relay_core.agent import Agent
from relay_core.agents_defs import load_catalog
from relay_core.conv_index import ConversationIndex
from relay_core.queue import TurnSupervisor
from relay_core.session_protocol import SessionCommands
from relay_core.sessions import SessionStore
from relay_core.subagents import SubagentFactory, SubagentManager

sys.path.insert(0, str(Path(__file__).parent))
from test_conv_index import session  # noqa: E402
from test_subagents import CONFIG, Hub, MainProvider, Recorder, SubProvider, call, calls, final  # noqa: E402

OWNER = "a" * 32
THREAD = "b" * 32
NESTED = "c" * 32


def thread(thread_id=THREAD, *, owner=OWNER, parent=None, turn=1, call_id="call-1", workspace="/tmp/alpha",
           description="Find the pelican", messages=None, updated=3000.0, status="done"):
    return {"version": 1, "kind": sessions.THREAD_KIND, "id": thread_id, "agent_id": "a1", "type": "general",
            "description": description, "title": description, "status": status, "owner_session": owner,
            "parent_thread": parent, "spawn_turn": turn, "spawn_call": call_id, "background": False,
            "workspace": workspace, "model": "glm-5", "models": ["glm-5"],
            "usage": {"prompt_tokens": 100, "completion_tokens": 20, "total_tokens": 120, "requests": 1},
            "created": 2500.0, "updated": updated, "runs": 1, "task": "look for the wombat",
            "messages": messages if messages is not None else [
                {"role": "user", "content": "look for the wombat"},
                {"role": "assistant", "content": "", "tool_calls": [
                    {"id": "n1", "type": "function", "function": {"name": "agent", "arguments": "{}"}}]},
                {"role": "tool", "tool_call_id": "n1", "content": "nested report"},
                {"role": "assistant", "content": "The wombat is in the burrow."}]}


class Home(unittest.TestCase):
    """XDG_DATA_HOME in a temporary directory, so SessionStore indexes into a private index.db."""

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.home = Path(self.temp.name)
        patch = mock.patch.dict(os.environ, {"XDG_DATA_HOME": str(self.home), "RELAY_INDEX": "on"})
        patch.start()
        self.addCleanup(patch.stop)
        self.addCleanup(self.temp.cleanup)
        self.dir = conv_index.sessions_root() / "digest"
        self.store = SessionStore(self.dir)

    def index(self):
        index = ConversationIndex()
        self.addCleanup(index.close)
        return index


class UsageTests(unittest.TestCase):
    def test_usage_totals_count_only_what_the_provider_reported(self):
        totals = sessions.empty_usage()
        sessions.add_usage(totals, {"prompt_tokens": 100, "completion_tokens": 20, "total_tokens": 120})
        sessions.add_usage(totals, {"prompt_tokens": 50, "completion_tokens": 5})
        self.assertEqual(totals, {"prompt_tokens": 150, "completion_tokens": 25, "total_tokens": 175, "requests": 2})
        self.assertNotIn("cost", totals)   # never reported, so not a zero
        sessions.add_usage(totals, {"total_tokens": 10, "cost": 0.0021})
        self.assertAlmostEqual(totals["cost"], 0.0021)
        sessions.add_usage(totals, {"prompt_tokens": True, "cost": "free"})   # junk is ignored
        self.assertEqual(totals["prompt_tokens"], 150)
        self.assertEqual(sessions.load_usage({"total_tokens": -5, "requests": "x"}), sessions.empty_usage())

    def test_agent_records_usage_and_models_in_the_session_file(self):
        with tempfile.TemporaryDirectory() as temp:
            from test_sessions import ScriptedProvider
            provider = ScriptedProvider(usage={"prompt_tokens": 40, "completion_tokens": 2, "total_tokens": 42})
            agent = Agent(CONFIG, temp, lambda e: None, provider=provider, session_dir=str(Path(temp) / "s"))
            agent.ask("hello")
            data = json.loads(agent.store.path(agent.session_id).read_text())
            self.assertEqual(data["usage"]["total_tokens"], 42)
            self.assertEqual(data["models"], ["mock"])
            resumed = Agent(CONFIG, temp, lambda e: None, provider=provider, session_dir=str(Path(temp) / "s"))
            resumed.resume(agent.session_id)
            self.assertEqual(resumed.usage_totals["total_tokens"], 42)


class ThreadStoreTests(Home):
    def test_threads_are_saved_beside_their_owner_and_deleted_with_it(self):
        self.store.save(session(OWNER))
        path = self.store.save_thread(thread())
        self.assertEqual(path, self.dir / f"{OWNER}.threads" / f"{THREAD}.json")
        self.assertEqual(os.stat(path).st_mode & 0o777, 0o600)
        self.assertEqual([t["id"] for t in self.store.threads(OWNER)], [THREAD])
        self.assertEqual(self.store.load_thread(THREAD)["owner_session"], OWNER)
        # The session listing is sessions only.
        self.assertEqual([i["id"] for i in self.store.listing()], [OWNER])
        self.store.delete(OWNER)
        self.assertFalse(path.exists())
        self.assertFalse((self.dir / f"{OWNER}.threads").exists())

    def test_index_links_threads_to_their_owner_and_parent(self):
        self.store.save(session(OWNER, title="Owner session"))
        self.store.save_thread(thread())
        self.store.save_thread(thread(NESTED, parent=THREAD, call_id="n1", description="Nested dig",
                                      messages=[{"role": "user", "content": "dig deeper for the wombat"}]))
        index = self.index()
        # Unchecked by default: the list is sessions.
        plain = index.search("", scope="all")
        self.assertEqual([i["session_id"] for i in plain["items"]], [OWNER])
        # Checked: threads are rows, each naming its owner session (and parent thread).
        rows = {i["session_id"]: i for i in index.search("", scope="all", include_threads=True)["items"]}
        self.assertEqual(set(rows), {OWNER, THREAD, NESTED})
        self.assertEqual(rows[THREAD]["source"], "subagent")
        self.assertEqual(rows[THREAD]["owner_session"], OWNER)
        self.assertEqual(rows[THREAD]["owner_title"], "Owner session")
        self.assertEqual(rows[THREAD]["spawn_turn"], 1)
        self.assertEqual(rows[NESTED]["parent_thread"], THREAD)
        self.assertEqual(rows[NESTED]["parent_title"], "Find the pelican")
        self.assertEqual(rows[NESTED]["owner_session"], OWNER)
        # Findable by search, and only when asked for.
        self.assertEqual(index.search("wombat", scope="all")["items"], [])
        found = index.search("wombat", scope="all", include_threads=True)["items"]
        self.assertEqual({i["session_id"] for i in found}, {THREAD, NESTED})
        # Deleting the owner takes its threads' rows with it.
        self.store.delete(OWNER)
        self.assertEqual(index.search("", scope="all", include_threads=True)["items"], [])

    def test_rebuild_and_reconcile_pick_up_threads_and_missing_sessions(self):
        self.store.save(session(OWNER))
        self.store.save_thread(thread())
        index = self.index()
        index.delete_session(OWNER, remove_files=False)
        index.delete_session(THREAD, remove_files=False)
        self.assertEqual(index.search("", scope="all", include_threads=True)["items"], [])
        result = index.reconcile()
        self.assertEqual(result["added"], 2)
        self.assertEqual(len(index.search("", scope="all", include_threads=True)["items"]), 2)
        # Nothing changed: nothing is re-read.
        self.assertEqual(index.reconcile()["added"] + index.reconcile()["refreshed"], 0)
        # A file removed behind the index's back drops its row.
        (self.dir / f"{OWNER}.json").unlink()
        (self.dir / f"{OWNER}.meta.json").unlink()
        self.assertEqual(index.reconcile()["removed"], 1)
        self.assertEqual(index.rebuild()["threads"], 1)

    def test_reconcile_catches_sessions_written_with_the_index_off(self):
        with mock.patch.dict(os.environ, {"RELAY_INDEX": "off"}):
            SessionStore(self.dir).save(session(OWNER))
            SessionStore(self.dir).save(session("d" * 32, updated=5000.0))
        index = self.index()
        self.assertEqual(index.search("", scope="all")["items"], [])
        index.reconcile()
        self.assertEqual(len(index.search("", scope="all")["items"]), 2)

    def test_user_titles_and_pins_live_in_the_session_files(self):
        self.store.save(session(OWNER))
        self.store.set_user_fields(OWNER, custom_title="  Kept   name ", pinned=True)
        meta = json.loads((self.dir / f"{OWNER}.meta.json").read_text())
        self.assertEqual(meta["custom_title"], "Kept name")
        self.assertTrue(meta["pinned"])
        # An autosave from the pane keeps them, and a wiped index gets them back from the files.
        self.store.save(session(OWNER, updated=9000.0))
        index = self.index()
        index.rebuild()
        item = index.search("", scope="all")["items"][0]
        self.assertEqual((item["title"], item["pinned"]), ("Kept name", 1))
        self.store.set_user_fields(OWNER, custom_title="")
        self.assertEqual(index.search("", scope="all")["items"][0]["title"], "Search spike")
        # A thread keeps its own rename across the next save of a running thread.
        self.store.save_thread(thread())
        self.store.set_thread_fields(THREAD, custom_title="Wombat hunt")
        self.store.save_thread(thread(updated=4000.0))
        self.assertEqual(self.store.load_thread(THREAD)["custom_title"], "Wombat hunt")

    def test_v1_index_is_migrated_in_place_keeping_terminal_history(self):
        path = conv_index.default_index_path()
        path.parent.mkdir(parents=True)
        # The session files exist (a v2 index is created by the save); then the index is replaced
        # by a v1 database, as an install from before 2026-09-18 has it.
        self.store.save(session(OWNER))
        ConversationIndex().close()
        for suffix in ("", "-wal", "-shm"):
            Path(str(path) + suffix).unlink(missing_ok=True)
        # Build a v1 database by hand: the v1 schema, a terminal row and a renamed session.
        db = sqlite3.connect(str(path))
        db.executescript("""
CREATE TABLE meta(key TEXT PRIMARY KEY, value TEXT);
CREATE TABLE conversations(session_id TEXT PRIMARY KEY, source TEXT NOT NULL DEFAULT 'agent',
  workspace TEXT NOT NULL DEFAULT '', project TEXT NOT NULL DEFAULT '', title TEXT NOT NULL DEFAULT '',
  custom_title TEXT, model TEXT NOT NULL DEFAULT '', preset TEXT NOT NULL DEFAULT '', created REAL, updated REAL,
  turns INTEGER NOT NULL DEFAULT 0, open_requests INTEGER NOT NULL DEFAULT 0, session_dir TEXT NOT NULL DEFAULT '',
  pinned INTEGER NOT NULL DEFAULT 0);
CREATE TABLE entries(id INTEGER PRIMARY KEY, session_id TEXT NOT NULL, turn INTEGER NOT NULL DEFAULT 0,
  seq INTEGER NOT NULL DEFAULT 0, kind TEXT NOT NULL, time REAL, status INTEGER, text TEXT NOT NULL);
CREATE VIRTUAL TABLE entries_fts USING fts5(text, content='entries', content_rowid='id');
CREATE TRIGGER entries_ai AFTER INSERT ON entries BEGIN INSERT INTO entries_fts(rowid, text) VALUES (new.id, new.text); END;
INSERT INTO meta VALUES('schema_version', '1');
""")
        db.execute("INSERT INTO conversations(session_id, source, workspace, title, session_dir, custom_title, pinned)"
                   " VALUES(?, 'agent', '/tmp/alpha', 'Search spike', ?, 'Old name', 1)", (OWNER, str(self.dir)))
        db.execute("INSERT INTO conversations(session_id, source, workspace, title, turns) VALUES"
                   " ('term-0123456789abcdef', 'terminal', '/tmp/alpha', 'Terminal · alpha', 1)")
        db.execute("INSERT INTO entries(session_id, turn, seq, kind, text) VALUES"
                   " ('term-0123456789abcdef', 1, 1, 'command', 'echo capybara')")
        db.commit()
        db.close()
        index = self.index()
        self.assertEqual(index.migrated_from, 1)
        self.assertFalse(index.recovered)
        self.assertEqual(index.search("capybara", scope="all")["items"][0]["source"], "terminal")
        meta = json.loads((self.dir / f"{OWNER}.meta.json").read_text())
        self.assertEqual((meta["custom_title"], meta["pinned"]), ("Old name", True))

    def test_workspace_spelling_is_one_function(self):
        real = self.home / "real-ws"
        real.mkdir()
        link = self.home / "link-ws"
        link.symlink_to(real)
        self.assertEqual(sessions.default_session_dir(link), sessions.default_session_dir(real))
        self.store.save(session(OWNER, workspace=str(real)))
        index = self.index()
        self.assertEqual(len(index.search("", scope="project", workspace=str(link))["items"]), 1)
        self.assertEqual(conv_index.terminal_id(link), conv_index.terminal_id(real))

    def test_sort_and_paging(self):
        self.store.save(session(OWNER, updated=1000.0, turns=9))
        self.store.save(session("d" * 32, updated=5000.0, turns=1))
        self.store.save(session("e" * 32, updated=3000.0, turns=4))
        index = self.index()
        ids = lambda result: [i["session_id"][0] for i in result["items"]]  # noqa: E731
        self.assertEqual(ids(index.search("", scope="all")), ["d", "e", "a"])
        self.assertEqual(ids(index.search("", scope="all", sort="oldest")), ["a", "e", "d"])
        self.assertEqual(ids(index.search("", scope="all", sort="longest")), ["a", "e", "d"])
        first = index.search("", scope="all", limit=2)
        self.assertEqual((ids(first), first["next_offset"]), (["d", "e"], 2))
        second = index.search("", scope="all", limit=2, offset=2)
        self.assertEqual(ids(second), ["a"])
        self.assertNotIn("next_offset", second)
        self.assertRaises(ValueError, index.search, "", scope="all", sort="alphabetical")


class LiveThreadTests(Home):
    """A real main agent with a session directory starts a real subagent: the thread file appears
    beside the owner session with its turn, call and status, and session_info places it."""

    def setUp(self):
        super().setUp()
        self.workspace = self.home / "ws"
        self.workspace.mkdir()
        self.rec = Recorder()
        self.hub = Hub()
        self.manager = SubagentManager(self.rec)
        factory = SubagentFactory(CONFIG, str(self.workspace), provider_factory=lambda config: SubProvider(self.hub))
        self.manager.configure(load_catalog(self.workspace, []), factory)
        self.addCleanup(self.manager.shutdown)

    def test_a_subagent_is_a_thread_of_its_owner_session(self):
        spawn = call("agent", {"description": "scan the tree", "prompt": "tool please", "subagent_type": "general"},
                     "spawn-1")
        provider = MainProvider([calls(spawn), final("done with it")])
        turns = TurnSupervisor(self.rec)
        self.addCleanup(turns.shutdown)
        agent = Agent(CONFIG, str(self.workspace), turns.agent_emit, provider=provider, session_dir=str(self.dir))
        self.manager.attach(agent)
        self.manager.turns = turns
        turns.set_agent(agent)
        turns.submit("look around", "now", "q1")
        self.rec.wait(lambda e: e.get("event") == "done")
        started = self.rec.of("subagent_started")[0]
        thread_id = started["thread_id"]
        self.assertRegex(thread_id, r"^[0-9a-f]{32}$")
        saved = self.store.load_thread(thread_id)
        self.assertEqual(saved["owner_session"], agent.session_id)
        self.assertEqual(saved["spawn_turn"], 1)
        self.assertEqual(saved["spawn_call"], "spawn-1")
        self.assertEqual(saved["status"], "done")
        self.assertIsNone(saved["parent_thread"])
        self.assertTrue(any(m.get("role") == "assistant" for m in saved["messages"]))
        self.assertGreaterEqual(saved["usage"]["total_tokens"], 120)   # the fake reports 120 per answer

        commands = SessionCommands(turns, self.rec, subagents=self.manager)
        commands.handle("session_info", {"id": "i1"})
        info = self.rec.of("session_info")[-1]
        self.assertEqual(info["kind"], "session")
        self.assertTrue(info["live"])
        self.assertEqual(info["session_id"], agent.session_id)
        self.assertEqual(info["file"], str(self.store.path(agent.session_id)))
        self.assertEqual(info["history"][0]["turn"], 1)
        self.assertEqual(info["history"][0]["prompt"], "look around")
        self.assertEqual([t["id"] for t in info["history"][0]["threads"]], [thread_id])
        self.assertIn("window", info["context"])
        self.assertEqual(info["models"], ["mock"])

        commands.handle("session_info", {"id": "i2", "thread_id": thread_id})
        info = self.rec.of("session_info")[-1]
        self.assertEqual(info["kind"], "thread")
        self.assertEqual(info["owner_session"], agent.session_id)
        self.assertTrue(info["owner_exists"])
        self.assertEqual(info["history"][0]["role"], "user")
        self.assertEqual(info["history"][-1]["role"], "assistant")


class SessionInfoTests(Home):
    def commands(self):
        sup = TurnSupervisor(Recorder())
        self.addCleanup(sup.shutdown)
        self.rec = Recorder()
        return SessionCommands(sup, self.rec)

    def test_saved_session_history_places_threads_and_nests_children(self):
        data = session(OWNER, title="Owner session", extra_turn="second question")
        self.store.save(data)
        self.store.save_thread(thread())
        self.store.save_thread(thread(NESTED, parent=THREAD, call_id="n1", description="Nested dig",
                                      messages=[{"role": "user", "content": "dig"}]))
        self.store.save_thread(thread("f" * 32, turn=None, description="Turn unknown"))
        cmds = self.commands()
        cmds.handle("session_info", {"session_id": OWNER, "session_dir": str(self.dir)})
        info = self.rec.of("session_info")[-1]
        self.assertFalse(info["live"])
        self.assertEqual([t["turn"] for t in info["history"]], [1, 2])
        first = info["history"][0]["threads"]
        self.assertEqual([t["id"] for t in first], [THREAD])
        self.assertEqual([c["id"] for c in first[0]["children"]], [NESTED])
        self.assertEqual(info["history"][1]["threads"], [])
        self.assertEqual([t["id"] for t in info["unplaced_threads"]], ["f" * 32])
        self.assertEqual(info["thread_count"], 3)

        cmds.handle("session_info", {"thread_id": NESTED, "session_dir": str(self.dir)})
        nested = self.rec.of("session_info")[-1]
        self.assertEqual((nested["owner_session"], nested["owner_title"]), (OWNER, "Owner session"))
        self.assertEqual((nested["parent_thread"], nested["parent_title"]), (THREAD, "Find the pelican"))
        cmds.handle("session_info", {"thread_id": THREAD, "session_dir": str(self.dir), "owner_session": OWNER})
        parent = self.rec.of("session_info")[-1]
        # The nested thread sits right after the tool call that started it.
        placed = [item for item in parent["history"] if item.get("threads")]
        self.assertEqual(placed[0]["tool_calls"][0]["id"], "n1")
        self.assertEqual(placed[0]["threads"][0]["id"], NESTED)
        self.assertRaises(ValueError, cmds.handle, "session_info",
                          {"thread_id": "9" * 32, "session_dir": str(self.dir)})
        self.assertRaises(ValueError, cmds.handle, "session_info", {"thread_id": "../x", "session_dir": str(self.dir)})

    def test_conversations_protocol_threads_checkbox_rename_and_delete(self):
        self.store.save(session(OWNER))
        self.store.save_thread(thread())
        cmds = self.commands()
        cmds.handle("conversations", {"query": "", "scope": "all"})
        self.assertEqual([i["session_id"] for i in self.rec.of("conversations")[-1]["items"]], [OWNER])
        cmds.handle("conversations", {"query": "wombat", "scope": "all", "include_threads": True})
        row = self.rec.of("conversations")[-1]["items"][0]
        self.assertEqual((row["session_id"], row["owner_session"]), (THREAD, OWNER))
        self.assertRaises(ValueError, cmds.handle, "conversations", {"include_threads": "yes"})
        cmds.handle("conversation_rename", {"session_id": OWNER, "title": "Named here"})
        self.assertEqual(json.loads((self.dir / f"{OWNER}.meta.json").read_text())["custom_title"], "Named here")
        cmds.handle("conversation_pin", {"session_id": THREAD, "pinned": True})
        self.assertTrue(self.store.load_thread(THREAD)["pinned"])
        cmds.handle("conversation_delete", {"session_id": THREAD})
        self.assertFalse((self.dir / f"{OWNER}.threads" / f"{THREAD}.json").exists())
        self.assertTrue((self.dir / f"{OWNER}.json").exists())


if __name__ == "__main__":
    unittest.main()
