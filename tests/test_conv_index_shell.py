"""Session search over the pane text journals (card #HEY7, step 4): the `shell` source.

Journals are written by hand as JSONL segments (the format is specified in src/TextJournal.h) into
a temporary XDG_DATA_HOME; nothing here touches the real index or the real journals.
"""

import json
import os
import shutil
import sqlite3
import sys
import zlib
from pathlib import Path
from unittest import mock

import pytest

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "backend"))
sys.path.insert(0, str(ROOT / "tests"))

from relay_core import conv_index, textjournal as tj  # noqa: E402
from relay_core.conv_index import ConversationIndex  # noqa: E402

JOURNAL = "abcdef12-0000"
# Only ever in shell output: the word the searches look for, and a line the index must not hold.
WORD = "zanzibarquux"
LINE = f"frobnicated {WORD} widgets: 42 done"


@pytest.fixture
def home(tmp_path):
    with mock.patch.dict(os.environ, {"XDG_DATA_HOME": str(tmp_path)}):
        yield tmp_path


def _journal(home: Path, journal: str, segments: dict[str, list[dict]], cwd: str = "/tmp/ws") -> Path:
    directory = home / "relay" / "text" / journal
    directory.mkdir(parents=True, exist_ok=True)
    (directory / "meta.json").write_text(json.dumps({"v": 1, "id": journal, "cwd": cwd,
                                                     "created": "2026-09-25T10:00:00Z",
                                                     "updated": "2026-09-25T10:05:00Z", "lines": 0}))
    for name, records in segments.items():
        raw = "".join(json.dumps(record) + "\n" for record in records).encode()
        (directory / name).write_bytes(zlib.compress(raw) if name.endswith(".z") else raw)
    return directory


def _prompt(text: str) -> dict:
    return {"t": text, "m": tj.MARK_PROMPT_START | tj.MARK_USER_SHELL}


def _records() -> list[dict]:
    return [{"at": "2026-09-25T10:00:00Z"},
            _prompt("user@host:~/ws$ make"), {"t": "cc -o x x.c"}, {"t": LINE},
            _prompt("user@host:~/ws$ ls"), {"t": "a.txt  b.txt"}]


def _index(home: Path) -> ConversationIndex:
    index = ConversationIndex(conv_index.default_index_path())
    assert index.journal_root == tj.text_root()
    return index


def _shell(result: dict) -> list[dict]:
    return [item for item in result["items"] if item["source"] == "shell"]


def test_shell_text_is_found_only_when_asked(home):
    _journal(home, JOURNAL, {"seg-000001.rtj.z": _records()[:4], "seg-000002.rtj": _records()[4:]})
    index = _index(home)
    try:
        report = index.reconcile()
        assert report["journals"] == 1
        assert _shell(index.search(WORD, scope="all")) == []
        assert "shell_total" not in index.search(WORD, scope="all")

        found = index.search(WORD, scope="all", include_shell=True)
        [item] = _shell(found)
        assert found["shell_total"] == 1
        assert item["journal_id"] == JOURNAL and item["session_id"] == "shell-" + JOURNAL
        assert item["command"] == 0 and item["cwd"] == "/tmp/ws" and item["turns"] == 2
        assert item["title"] == "user@host:~/ws$ make"
        assert item["time"] == conv_index.iso_seconds("2026-09-25T10:00:00Z")
        [match] = item["matches"]
        assert match["kind"] == "shell" and match["command"] == 0
        assert match["line"] == LINE and item["snippet"] == LINE
        assert [match["line"][s:s + n] for s, n in match["ranges"]] == [WORD]

        # `has:shell` asks for one query; `-has:shell` refuses it even with the flag on.
        assert [i["journal_id"] for i in _shell(index.search(f"{WORD} has:shell", scope="all"))] == [JOURNAL]
        assert _shell(index.search(f"{WORD} -has:shell", scope="all", include_shell=True)) == []
        # The scope is the journal's cwd; a conversation-only filter excludes every journal.
        assert _shell(index.search(WORD, workspace="/tmp/elsewhere", include_shell=True)) == []
        assert len(_shell(index.search(WORD, workspace="/tmp/ws", include_shell=True))) == 1
        assert _shell(index.search(f"{WORD} is:pinned", scope="all", include_shell=True)) == []
        # Words are an AND inside one command.
        assert _shell(index.search(f"{WORD} b.txt", scope="all", include_shell=True)) == []
        assert len(_shell(index.search(f"{WORD} widgets", scope="all", include_shell=True))) == 1
        assert _shell(index.search(f"{WORD} -widgets", scope="all", include_shell=True)) == []
    finally:
        index.close()


def test_the_index_holds_no_copy_of_the_journal_text(home):
    _journal(home, JOURNAL, {"seg-000001.rtj": _records()})
    index = _index(home)
    try:
        index.reconcile()
        assert _shell(index.search(WORD, scope="all", include_shell=True))
        db = sqlite3.connect(str(index.path))
        tables = [row[0] for row in db.execute("SELECT name FROM sqlite_master WHERE type='table'")]
        # No table but the FTS index's own term store holds even the word; no column holds a line.
        fts_shadow = {"shell_fts_data", "shell_fts_idx"}
        for table in tables:
            for row in db.execute(f"SELECT * FROM '{table}'"):
                blob = b"".join(value if isinstance(value, bytes) else str(value).encode()
                                for value in row)
                assert LINE.encode() not in blob, table
                assert "cc -o x x.c".encode() not in blob, table
                if table not in fts_shadow:
                    assert WORD.encode() not in blob, table
        assert "shell_fts_content" not in tables
        db.close()
    finally:
        index.close()
    data = b"".join(Path(str(index.path) + suffix).read_bytes()
                    for suffix in ("", "-wal") if Path(str(index.path) + suffix).exists())
    assert LINE.encode() not in data
    assert b"a.txt  b.txt" not in data


def test_an_unchanged_journal_is_not_read_again_and_a_grown_one_only_from_its_tail(home):
    directory = _journal(home, JOURNAL, {"seg-000001.rtj": _records()})
    index = _index(home)
    try:
        first = index.reconcile_journals()
        assert first["indexed"] == 1 and first["commands"] == 2
        with mock.patch.object(tj, "commands", side_effect=AssertionError("re-read")):
            again = index.reconcile_journals()
            assert again["indexed"] == 0 and again["commands"] == 0
            assert index.reconcile()["journals"] == 0
        # Appending rewrites the last command and adds one; the first stays as it was indexed.
        with open(directory / "seg-000001.rtj", "a") as handle:
            handle.write(json.dumps({"t": "c.txt"}) + "\n")
            handle.write(json.dumps(_prompt("user@host:~/ws$ echo tombola")) + "\n")
            handle.write(json.dumps({"t": "tombola"}) + "\n")
        grown = index.reconcile_journals()
        assert grown["indexed"] == 1 and grown["commands"] == 2
        assert len(_shell(index.search("tombola", scope="all", include_shell=True))) == 1
        assert len(_shell(index.search("c.txt", scope="all", include_shell=True))) == 1
        assert len(_shell(index.search(WORD, scope="all", include_shell=True))) == 1
        # Sealing a segment moves the stamp but not the text: one command re-indexed.
        tj.seal_file(directory / "seg-000001.rtj")
        sealed = index.reconcile_journals()
        assert sealed["indexed"] == 1 and sealed["commands"] == 1
        assert len(_shell(index.search(WORD, scope="all", include_shell=True))) == 1
        rows = sqlite3.connect(str(index.path)).execute("SELECT count(*) FROM shell_commands").fetchone()[0]
        assert rows == 3
    finally:
        index.close()


def test_a_deleted_journal_loses_its_hits(home):
    directory = _journal(home, JOURNAL, {"seg-000001.rtj": _records()})
    _journal(home, "fedcba98-0000", {"seg-000001.rtj": [_prompt("$ true"), {"t": "keepsake"}]})
    index = _index(home)
    try:
        index.reconcile()
        assert len(_shell(index.search(WORD, scope="all", include_shell=True))) == 1
        shutil.rmtree(directory)
        assert index.reconcile_journals()["removed"] == 1
        assert _shell(index.search(WORD, scope="all", include_shell=True)) == []
        assert len(_shell(index.search("keepsake", scope="all", include_shell=True))) == 1
        db = sqlite3.connect(str(index.path))
        assert db.execute("SELECT count(*) FROM shell_commands WHERE journal=?", (JOURNAL,)).fetchone()[0] == 0
        if index.shell_delete:
            # The FTS rows went too, not only the map to them.
            assert db.execute("SELECT count(*) FROM shell_fts WHERE shell_fts MATCH ?",
                              (f'"{WORD}"',)).fetchone()[0] == 0
    finally:
        index.close()


def test_the_orphaning_fallback_compacts(home, monkeypatch):
    """Without contentless_delete (SQLite < 3.43), replaced rows are orphaned and then compacted."""
    monkeypatch.setattr(conv_index, "SHELL_ORPHANS_COMPACT", 0)
    directory = _journal(home, JOURNAL, {"seg-000001.rtj": _records()})
    path = conv_index.default_index_path()
    path.parent.mkdir(parents=True, exist_ok=True)
    made = sqlite3.connect(str(path))      # the table as an older SQLite would have made it
    made.execute("CREATE VIRTUAL TABLE shell_fts USING fts5(text, content='')")
    made.commit()
    made.close()
    index = _index(home)
    try:
        assert index.shell_delete is False
        index.reconcile_journals()
        assert len(_shell(index.search(WORD, scope="all", include_shell=True))) == 1
        shutil.rmtree(directory)
        index.reconcile_journals()
        db = sqlite3.connect(str(index.path))
        assert int(db.execute("SELECT value FROM meta WHERE key='shell_orphans'").fetchone()[0]) == 2
        assert _shell(index.search(WORD, scope="all", include_shell=True)) == []
        index.reconcile_journals()          # orphans now outnumber the live rows: emptied
        assert db.execute("SELECT value FROM meta WHERE key='shell_orphans'").fetchone() is None
        assert db.execute("SELECT count(*) FROM shell_fts WHERE shell_fts MATCH ?",
                          (f'"{WORD}"',)).fetchone()[0] == 0
    finally:
        index.close()


def test_the_conversations_message_carries_include_shell(home):
    from relay_core.queue import TurnSupervisor
    from relay_core.session_protocol import SessionCommands
    from test_queue import Recorder

    _journal(home, JOURNAL, {"seg-000001.rtj": _records()})
    rec = Recorder()
    sup = TurnSupervisor(rec)
    cmds = SessionCommands(sup, rec)
    try:
        def ask(**fields):
            cmds.handle("conversations", {"id": "q", "query": WORD, "scope": "all", **fields})
            return [e for e in rec.events if e.get("event") in ("conversations", "error")][-1]

        assert _shell(ask()) == []
        assert [i["journal_id"] for i in _shell(ask(include_shell=True))] == [JOURNAL]
        with pytest.raises(ValueError, match="include_shell"):
            cmds.handle("conversations", {"query": WORD, "include_shell": "yes"})
    finally:
        sup.shutdown()
        if cmds._index:
            cmds._index.close()
