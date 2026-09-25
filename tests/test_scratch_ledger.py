"""Tests for the scratch ledger and the #DVV2 agent hooks.

Every test sandboxes all five env knobs (RELAY_LEDGER, RELAY_SCRATCH_HOME, RELAY_TOOLS_HOME,
RELAY_STATE_HOME, RELAY_SCRATCH_ROOTS) plus RELAY_CCACHE_DIR, so nothing reads or writes the
real ~/.local/state/relay ledger and no gc ever sees a real root: the narrowed walk stays
inside the sandbox, and the one test that needs un-narrowed gc (ledger transitions are only
written then) points TMPDIR itself at the sandbox and passes force_idle, which the #DVV2
floor exists for. Never run gc --apply on the default roots from here.
"""
import os
import shutil
import sys
import tempfile
import time
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "backend"))

from relay_core import scratch  # noqa: E402

ENV = ("RELAY_LEDGER", "RELAY_SCRATCH_HOME", "RELAY_TOOLS_HOME", "RELAY_STATE_HOME",
       "RELAY_SCRATCH_ROOTS", "RELAY_CCACHE_DIR", "TMPDIR")


class Sandbox(unittest.TestCase):
    """A temp sandbox with every scratch root inside it."""

    def setUp(self):
        self.base = Path(tempfile.mkdtemp(prefix="dvv2-test-"))
        self.env = {name: os.environ.get(name) for name in ENV}
        self.scratch_home = self.base / "scratch"
        self.tools_home = self.base / "tools"
        self.state_home = self.base / "state"
        self.walk = self.base / "walk"
        self.ledger_file = self.base / "ledger.jsonl"
        for path in (self.scratch_home, self.tools_home, self.state_home, self.walk):
            path.mkdir(parents=True, exist_ok=True)
        os.environ.update(RELAY_LEDGER=str(self.ledger_file),
                          RELAY_SCRATCH_HOME=str(self.scratch_home),
                          RELAY_TOOLS_HOME=str(self.tools_home),
                          RELAY_STATE_HOME=str(self.state_home),
                          RELAY_SCRATCH_ROOTS=str(self.walk),
                          RELAY_CCACHE_DIR=str(self.base / "ccache-absent"))

    def tearDown(self):
        for name, value in self.env.items():
            if value is None:
                os.environ.pop(name, None)
            else:
                os.environ[name] = value
        shutil.rmtree(self.base, ignore_errors=True)

    def ledger(self):
        return scratch.ScratchLedger()

    def write(self, path: Path, size: int = 512) -> Path:
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(b"x" * size)
        return path


class RowTests(Sandbox):
    def test_row_schema_fields(self):
        row = scratch.Row(id="sc00001", path="/nowhere", cls="keep", purpose="venv",
                          created_by={"session": "s1", "pane": "p1", "model": "m1", "card": "DVV2"},
                          created_at="2026-02-14T10:00:00Z", lifetime="until-promoted",
                          state="live", size=10, freed=0, note="")
        record = row.to_record()
        for key in ("id", "path", "class", "purpose", "created_by", "created_at",
                    "lifetime", "state", "size", "freed", "at", "released_at", "note"):
            self.assertIn(key, record)
        self.assertEqual(record["class"], "keep")
        self.assertNotIn("cls", record)
        self.assertEqual(scratch.Row.from_record(record), row)

    def test_ledger_is_append_only_last_record_wins(self):
        row = scratch.new_dir("scratch", "one", session="s1")
        scratch.release(row.id)
        lines = self.ledger_file.read_text().strip().splitlines()
        self.assertEqual(len(lines), 2)  # the transition was appended, not rewritten
        import json
        records = [json.loads(line) for line in lines]
        self.assertEqual([r["id"] for r in records], [row.id, row.id])
        self.assertEqual(records[0]["state"], "live")
        self.assertEqual(self.ledger().records()[row.id].state, "reclaimed")


class NewDirTests(Sandbox):
    def test_new_dir_creates_dir_and_row(self):
        row = scratch.new_dir("scratch", "build tree", session="s1", pane="p1",
                              model="m1", card="DVV2")
        self.assertTrue(Path(row.path).is_dir())
        self.assertTrue(Path(row.path).is_relative_to(self.scratch_home / "relay" / "scratch" / "s1"))
        stored = self.ledger().find(row.id)
        self.assertEqual(stored.cls, "scratch")
        self.assertEqual(stored.purpose, "build tree")
        self.assertEqual(stored.state, "live")
        self.assertEqual(stored.lifetime, "session")  # the scratch default
        self.assertEqual(stored.created_by,
                         {"session": "s1", "pane": "p1", "model": "m1", "card": "DVV2"})

    def test_new_dir_keeps_and_installs_live_where_they_belong(self):
        project = self.base / "proj"
        project.mkdir()
        keep = scratch.new_dir("keep", "model weights", session="s1", project=project)
        install = scratch.new_dir("install", "ripgrep build")
        self.assertTrue(Path(keep.path).is_relative_to(project / ".relay" / "work"))
        self.assertTrue(Path(install.path).is_relative_to(self.tools_home / "relay" / "tools"))
        self.assertEqual(self.ledger().find(keep.id).lifetime, "until-promoted")
        self.assertEqual(self.ledger().find(install.id).lifetime, "user")

    def test_new_dir_rejects_unknown_class(self):
        with self.assertRaises(ValueError):
            scratch.new_dir("tmp", "no such class", session="s1")


class ReleaseTests(Sandbox):
    def test_release_scratch_reclaims_with_freed(self):
        row = scratch.new_dir("scratch", "build", session="s1")
        self.write(Path(row.path) / "artifact.bin", 4096)
        ended = scratch.release(row.id)
        self.assertEqual(ended.state, "reclaimed")
        self.assertGreaterEqual(ended.freed, 4096)
        self.assertFalse(Path(row.path).exists())
        self.assertEqual(self.ledger().find(row.path).state, "reclaimed")

    def test_release_keep_without_flags_raises(self):
        row = scratch.new_dir("keep", "weights", session="s1", project=self.base / "proj")
        with self.assertRaises(ValueError) as raised:
            scratch.release(row.id)
        self.assertIn("keep must be promoted", str(raised.exception))
        self.assertTrue(Path(row.path).exists())       # nothing was deleted
        self.assertEqual(self.ledger().find(row.id).state, "live")

    def test_release_keep_promote_moves_tree_and_marks_promoted(self):
        project = self.base / "proj"
        project.mkdir()
        row = scratch.new_dir("keep", "fine-tune", session="s1", project=project)
        self.write(Path(row.path) / "model.safetensors", 8192)
        dest = project / "models" / "fine-tune"
        ended = scratch.release(row.id, promote_to=dest)
        self.assertEqual(ended.state, "promoted")
        self.assertFalse(Path(row.path).exists())
        self.assertTrue((dest / "model.safetensors").exists())
        self.assertIn(str(dest), ended.note)

    def test_release_keep_drop(self):
        row = scratch.new_dir("keep", "throwaway keep", session="s1",
                              project=self.base / "proj")
        self.write(Path(row.path) / "part.bin", 1000)
        ended = scratch.release(row.id, drop=True)
        self.assertEqual(ended.state, "reclaimed")
        self.assertGreaterEqual(ended.freed, 1000)
        self.assertFalse(Path(row.path).exists())

    def test_release_install_is_refused(self):
        row = scratch.new_dir("install", "tool build")
        with self.assertRaises(ValueError) as raised:
            scratch.release(row.id)
        self.assertIn("install rows stay", str(raised.exception))
        self.assertTrue(Path(row.path).exists())

    def test_release_by_path_and_unknown_ref(self):
        row = scratch.new_dir("scratch", "by path", session="s1")
        self.assertEqual(scratch.release(row.path).state, "reclaimed")
        with self.assertRaises(ValueError):
            scratch.release("no-such-ref")


class EndSessionTests(Sandbox):
    def test_end_session_reclaims_session_scratch_leaves_keep(self):
        project = self.base / "proj"
        project.mkdir()
        mine = scratch.new_dir("scratch", "mine", session="s1")
        keep = scratch.new_dir("keep", "keep me", session="s1", project=project)
        other = scratch.new_dir("scratch", "another session", session="s2")
        scratch.end_session("s1")
        self.assertFalse(Path(mine.path).exists())
        self.assertEqual(self.ledger().find(mine.id).state, "reclaimed")
        self.assertTrue(Path(keep.path).exists())          # keep is never skipped to
        self.assertEqual(self.ledger().find(keep.id).state, "live")
        self.assertTrue(Path(other.path).exists())         # another session's stays
        self.assertEqual(self.ledger().find(other.id).state, "live")


class GcTests(Sandbox):
    def test_gc_writes_reclaimed_transitions_and_keeps_keeps(self):
        # Ledger transitions are only written in un-narrowed mode, so this test unsets
        # RELAY_SCRATCH_ROOTS and points TMPDIR itself at the sandbox: default_roots() then
        # is [sandbox scratch home, sandbox/claude-<uid>] and no real root is touched.
        # idle 0 needs force_idle — the #DVV2 floor — and it is safe exactly because every
        # root is in the sandbox.
        os.environ.pop("RELAY_SCRATCH_ROOTS", None)
        old_tempdir = tempfile.tempdir
        tempfile.tempdir = str(self.base)   # gettempdir(), and so default_roots(), is ours
        try:
            project = self.base / "proj"
            project.mkdir()
            gone = scratch.new_dir("scratch", "stale build", session="old")
            self.write(Path(gone.path) / "big.bin", 8192)
            keep = scratch.new_dir("keep", "must survive", session="old", project=project)
            self.write(Path(keep.path) / "model.bin", 4096)
            count, _ = scratch.gc(idle_hours=0, apply=True, force_idle=True,
                                  include_loose=False, log=lambda *a, **k: None)
            self.assertGreaterEqual(count, 1)
            self.assertFalse(Path(gone.path).exists())
            self.assertEqual(self.ledger().find(gone.id).state, "reclaimed")
            self.assertGreaterEqual(self.ledger().find(gone.id).freed, 8192)
            self.assertIn("reclaimed by gc", self.ledger().find(gone.id).note)
            self.assertTrue(Path(keep.path).exists())       # keep is never removable
            self.assertEqual(self.ledger().find(keep.id).state, "live")
        finally:
            tempfile.tempdir = old_tempdir


class ReportTests(Sandbox):
    def test_ledger_summary_groups_by_class_and_session(self):
        project = self.base / "proj"
        project.mkdir()
        scratch.new_dir("scratch", "a", session="s1")
        scratch.new_dir("scratch", "b", session="s1")
        scratch.new_dir("scratch", "c", session="s2")
        keep = scratch.new_dir("keep", "k", session="s1", project=project)
        summary = scratch.ledger_summary(self.ledger())
        self.assertEqual(summary["by_class"]["scratch"]["rows"], 3)
        self.assertEqual(summary["by_class"]["keep"]["rows"], 1)
        self.assertEqual(summary["by_session"]["s1"]["rows"], 3)
        self.assertEqual(summary["by_session"]["s2"]["rows"], 1)
        self.assertEqual(summary["live"], 4)
        self.assertEqual([r["id"] for r in summary["unpromoted_keep"]], [keep.id])
        self.assertEqual(summary["orphans"], [])

    def test_adopt_lists_orphans_deletes_nothing_and_dedupes(self):
        home = self.base / "home"
        (home / "relay-qa").mkdir(parents=True)
        loose = self.walk / "relay-old-build"
        loose.mkdir()
        self.write(loose / "part.bin", 256)
        rows = scratch.adopt(ledger=self.ledger(), home=home)   # dry run is the default
        paths = [row.path for row in rows]
        self.assertIn(str(loose), paths)
        self.assertIn(str(home / "relay-qa"), paths)
        self.assertFalse(self.ledger_file.exists())             # nothing written, nothing deleted
        self.assertTrue(loose.is_dir())
        rows = scratch.adopt(apply=True, ledger=self.ledger(), home=home)
        self.assertTrue(self.ledger_file.exists())
        for row in rows:
            self.assertEqual(self.ledger().find(row.id).state, "orphaned")
        again = scratch.adopt(ledger=self.ledger(), home=home)   # dedupes on a second pass
        self.assertEqual([r.path for r in again], [])
        self.assertTrue(loose.is_dir())                          # still nothing deleted

    def test_unledgered_created_since_sees_new_and_skips_ledgered(self):
        tmp = self.base / "tmp"
        home = self.base / "home"
        tmp.mkdir()
        home.mkdir()
        since = time.time() - 5
        ledgered = scratch.new_dir("scratch", "ledgered tmp entry", session="s1",
                                   path=tmp / "ledgered-build")
        stray_tmp = self.write(tmp / "stray-build", 64)
        stray_home = self.write(home / "relay-side-project", 64)
        old_tmp = self.write(tmp / "old-entry", 64)
        os.utime(old_tmp, (since - 3600, since - 3600))
        found = scratch.unledgered_created_since(since, ledger=self.ledger(),
                                                 home=home, tmp=tmp)
        self.assertIn(str(stray_tmp), found)
        self.assertIn(str(stray_home), found)
        self.assertNotIn(ledgered.path, found)      # the ledger row covers it
        self.assertNotIn(str(old_tmp), found)       # older than the turn
        self.assertNotIn(str(tmp / "ledgered-build" / "child"), found)  # top level only


class ToolDeclarationTests(Sandbox):
    def test_tools_declared_and_prepared(self):
        from relay_core import tools
        names = {t["function"]["name"] for t in tools.TOOLS}
        self.assertIn("scratch_dir", names)
        self.assertIn("scratch_release", names)
        executor = tools.ToolExecutor(str(self.base), lambda *a, **k: None, lambda: None)
        prepared = executor.prepare("scratch_dir", {"purpose": "build tree", "card": "DVV2"})
        self.assertEqual(prepared.arguments["class"], "scratch")
        self.assertEqual(prepared.arguments["purpose"], "build tree")
        prepared = executor.prepare("scratch_dir",
                                    {"class": "keep", "purpose": "venv", "lifetime": "until-promoted"})
        self.assertEqual(prepared.arguments["lifetime"], "until-promoted")
        prepared = executor.prepare("scratch_release",
                                    {"ref": "sc12345", "promote_to": "models/x", "drop": True})
        self.assertEqual(prepared.arguments["drop"], True)
        for bad in ({"class": "tmp", "purpose": "x"},
                    {"purpose": "two\nlines"},
                    {"purpose": "x", "lifetime": "forever"}):
            with self.assertRaises(ValueError):
                executor.prepare("scratch_dir", bad)


class WriteGuardTests(Sandbox):
    """agent.scratch_write_refusal is the guard _execute raises for write_file/edit_file."""

    def setUp(self):
        super().setUp()
        # Point the "system temp dir" at an empty dir of ours so /tmp here is only what the
        # test names, and paths elsewhere are judged by the other rules alone.
        self.sys_temp = self.base / "sys-temp"
        self.sys_temp.mkdir()
        self.old_tempdir = tempfile.tempdir
        tempfile.tempdir = str(self.sys_temp)
        self.project = self.base / "proj"
        self.project.mkdir()
        from relay_core import agent
        self.agent = agent

    def tearDown(self):
        tempfile.tempdir = self.old_tempdir
        super().tearDown()

    def test_temp_dir_write_refused_with_replacement_call(self):
        refusal = self.agent.scratch_write_refusal(self.sys_temp / "x.txt", self.project)
        self.assertIsNotNone(refusal)
        self.assertIn("scratch_dir", refusal)
        self.assertIn('relay-scratch new --class scratch --purpose', refusal)
        self.assertIn("temp", refusal.lower())

    def test_hardcoded_real_tmp_refused_even_when_tmpdir_is_repointed(self):
        # TMPDIR may point at this pane's own scratch root; a hardcoded /tmp path must still
        # be refused — scratch.system_tmp() is captured before any session repointed it.
        real_tmp = scratch.system_tmp()
        if Path(tempfile.gettempdir()).resolve() == real_tmp.resolve():
            self.skipTest("gettempdir() already is the system temp dir")
        refusal = self.agent.scratch_write_refusal(real_tmp / "agent-draft.txt", self.project)
        self.assertIsNotNone(refusal)
        self.assertIn("scratch_dir", refusal)

    def test_workspace_and_relay_work_allowed(self):
        self.assertIsNone(self.agent.scratch_write_refusal(self.project / "notes.txt", self.project))
        self.assertIsNone(
            self.agent.scratch_write_refusal(self.project / ".relay" / "work" / "model.bin", self.project))

    def test_new_top_level_home_entry_refused(self):
        home = self.base / "home"
        home.mkdir()
        old_home = self.agent.Path.home
        self.agent.Path.home = lambda: home
        try:
            refusal = self.agent.scratch_write_refusal(home / "agent-made-dir", self.project)
            self.assertIsNotNone(refusal)
            self.assertIn("scratch_dir", refusal)
        finally:
            self.agent.Path.home = old_home

    def test_deliverable_outside_workspace_refused(self):
        docs = self.base / "elsewhere" / "docs"
        docs.mkdir(parents=True)
        refusal = self.agent.scratch_write_refusal(docs / "report-out.md", self.project)
        self.assertIsNotNone(refusal)
        self.assertIn("deliverable", refusal)
        self.assertIn("workspace", refusal)
        for name in ("reply-msg.txt", "evidence-3.txt", "message-draft.md"):
            refusal = self.agent.scratch_write_refusal(docs / name, self.project)
            self.assertIsNotNone(refusal, name)
        self.assertIsNone(self.agent.scratch_write_refusal(docs / "data.json", self.project))
        self.assertIsNone(self.agent.scratch_write_refusal(self.project / "docs" / "report.md", self.project))

    def test_session_scratch_root_allowed(self):
        root = scratch.session_root("s1") / "tmp"
        root.mkdir(parents=True)
        self.assertIsNone(self.agent.scratch_write_refusal(root / "somefile.bin", self.project))


class SweepTests(Sandbox):
    def test_note_lists_entries_and_asks_only(self):
        from relay_core import agent
        note = agent.scratch_sweep_note(["/tmp/relay-stray", "/home/u/agent-made"])
        self.assertIn("/tmp/relay-stray", note)
        self.assertIn("/home/u/agent-made", note)
        self.assertIn("scratch_dir", note)
        self.assertIn("relay-scratch adopt", note)
        self.assertIn("Nothing is deleted", note)
        self.assertIsNone(agent.scratch_sweep_note([]))

    def test_sweep_is_bound_to_once_per_turn(self):
        from relay_core import agent
        entries = ["/tmp/relay-stray"]
        first = agent.scratch_sweep_turn(entries, swept=False)   # the turn continues once
        self.assertIsNotNone(first)
        second = agent.scratch_sweep_turn(entries, swept=True)   # the second pass closes it
        self.assertIsNone(second)


class TmpdirHookTests(Sandbox):
    def test_session_tmpdir_points_inside_session_root_and_is_ledgered(self):
        from relay_core import agent
        root = agent.scratch_tmpdir("sess-1", pane="p1", model="m1")
        self.assertEqual(root, scratch.session_root("sess-1") / "tmp")
        self.assertTrue(root.is_dir())
        self.assertEqual(os.environ.get("TMPDIR"), str(root))
        row = self.ledger().find(str(root))
        self.assertIsNotNone(row)
        self.assertEqual(row.cls, "scratch")
        self.assertEqual(row.purpose, "TMPDIR for session sess-1")
        self.assertEqual(row.lifetime, "days:7")   # the pane outlives its conversations
        self.assertEqual(row.created_by["session"], "sess-1")
        self.assertEqual(row.created_by["model"], "m1")
        agent.scratch_tmpdir("sess-1", pane="p1", model="m1")    # restart: reuse, not duplicate
        rows = [r for r in self.ledger().rows() if r.path == str(root)]
        self.assertEqual(len(rows), 1)

    def test_tmpdir_keys_on_pane_token_when_worker_knows_it(self):
        # startWorker exports RELAY_SESSION_TOKEN per process (#DVV2): the pane's shell, its
        # guest CLIs and the worker's run_command children share one root and one row.
        from relay_core import agent
        os.environ["RELAY_SESSION_TOKEN"] = "tok-42"
        try:
            root = agent.scratch_tmpdir("sess-9")
            self.assertEqual(root, scratch.session_root("tok-42") / "tmp")
            row = self.ledger().find(str(root))
            self.assertIsNotNone(row)
            self.assertEqual(row.created_by["session"], "tok-42")
            self.assertEqual(row.purpose, "TMPDIR for pane tok-42")
            # a conversation reset reclaims conversation scratch, never the pane's tmpdir row
            scratch.end_session("sess-9")
            self.assertEqual(self.ledger().find(str(root)).state, "live")
        finally:
            os.environ.pop("RELAY_SESSION_TOKEN", None)


if __name__ == "__main__":
    unittest.main()
