# SPDX-License-Identifier: GPL-3.0-or-later
"""Full-text index of conversations and Relay-run terminal commands (protocol section 14).

An SQLite FTS5 database beside the session files:

    $XDG_DATA_HOME/relay/index.db      0600, in the 0700 relay/ directory

It is a **cache**, never the source of truth: every row can be rebuilt from the session JSON
under `relay/sessions/<workspace-digest>/`, so a corrupt or outdated database is deleted and
recreated instead of migrated. `SCHEMA_VERSION` is stored in `meta`; a mismatch wipes the tables.

Two kinds of conversation live in the same tables, told apart by `conversations.source`:

* `agent`   — one row per saved session; entries are user prompts, assistant replies, tool calls
              and capped tool output, with the turn number they belong to.
* `terminal`— one synthetic row per workspace (`term-<digest>`), holding the commands Relay itself
              ran in that workspace, their exit status and, where the engine captured it, their
              output. Commands typed straight into the terminal in native mode never reach Relay,
              so they are not indexed (see the issue for what is and is not captured).

The index holds message text, so it stays on this machine: same 0700 directory as the sessions,
never synced, and deleting a conversation deletes its rows.
"""
from __future__ import annotations

import hashlib
import json
import os
import re
import sqlite3
import time
from pathlib import Path

SCHEMA_VERSION = 1
MAX_TEXT = 4000              # per-entry cap for replies and tool output
MAX_PROMPT = 8000            # per-entry cap for user prompts
MAX_MATCHES_PER_ITEM = 5     # matching turns returned per conversation
MAX_LIMIT = 200
MAX_ENTRIES_PER_SESSION = 20000
MAX_COMMANDS = 500           # terminal commands accepted in one `terminal_history` message
TERMINAL_ID = re.compile(r"^term-[0-9a-f]{16}$")
_WORD = re.compile(r"[^\W_]+", re.UNICODE)
# Context blocks Relay adds to a user message (agent.CONTEXT_OPEN); not something the user typed.
RELAY_CONTEXT = "[Relay context: added by Relay, not typed by the user]"

SCHEMA = """
CREATE TABLE IF NOT EXISTS meta(key TEXT PRIMARY KEY, value TEXT);
CREATE TABLE IF NOT EXISTS conversations(
    session_id   TEXT PRIMARY KEY,
    source       TEXT NOT NULL DEFAULT 'agent',
    workspace    TEXT NOT NULL DEFAULT '',
    project      TEXT NOT NULL DEFAULT '',
    title        TEXT NOT NULL DEFAULT '',
    custom_title TEXT,
    model        TEXT NOT NULL DEFAULT '',
    preset       TEXT NOT NULL DEFAULT '',
    created      REAL,
    updated      REAL,
    turns        INTEGER NOT NULL DEFAULT 0,
    open_requests INTEGER NOT NULL DEFAULT 0,
    session_dir  TEXT NOT NULL DEFAULT '',
    pinned       INTEGER NOT NULL DEFAULT 0
);
CREATE TABLE IF NOT EXISTS entries(
    id         INTEGER PRIMARY KEY,
    session_id TEXT NOT NULL,
    turn       INTEGER NOT NULL DEFAULT 0,
    seq        INTEGER NOT NULL DEFAULT 0,
    kind       TEXT NOT NULL,
    time       REAL,
    status     INTEGER,
    text       TEXT NOT NULL
);
CREATE INDEX IF NOT EXISTS entries_by_session ON entries(session_id, seq);
CREATE VIRTUAL TABLE IF NOT EXISTS entries_fts USING fts5(
    text, content='entries', content_rowid='id', tokenize='unicode61 remove_diacritics 2');
CREATE TRIGGER IF NOT EXISTS entries_ai AFTER INSERT ON entries BEGIN
    INSERT INTO entries_fts(rowid, text) VALUES (new.id, new.text);
END;
CREATE TRIGGER IF NOT EXISTS entries_ad AFTER DELETE ON entries BEGIN
    INSERT INTO entries_fts(entries_fts, rowid, text) VALUES ('delete', old.id, old.text);
END;
CREATE TRIGGER IF NOT EXISTS entries_au AFTER UPDATE ON entries BEGIN
    INSERT INTO entries_fts(entries_fts, rowid, text) VALUES ('delete', old.id, old.text);
    INSERT INTO entries_fts(rowid, text) VALUES (new.id, new.text);
END;
"""

KINDS = ("prompt", "reply", "tool_call", "tool_output", "command", "command_output")


# ----- paths ---------------------------------------------------------------------------------

def relay_data_dir() -> Path:
    base = os.environ.get("XDG_DATA_HOME") or str(Path.home() / ".local" / "share")
    return Path(base) / "relay"


def default_index_path() -> Path:
    return relay_data_dir() / "index.db"


def sessions_root() -> Path:
    return relay_data_dir() / "sessions"


def workspace_digest(workspace: str | Path) -> str:
    return hashlib.sha256(str(Path(workspace).expanduser()).encode("utf-8")).hexdigest()[:16]


def terminal_id(workspace: str | Path) -> str:
    """The synthetic conversation id holding a workspace's Relay-run terminal commands."""
    return "term-" + workspace_digest(workspace)


def project_name(workspace: str) -> str:
    name = Path(workspace).name if workspace else ""
    return name or workspace or "(no workspace)"


def enabled() -> bool:
    return os.environ.get("RELAY_INDEX", "").lower() not in ("0", "off", "no", "false")


# ----- pure helpers (also exercised by tests) ------------------------------------------------

def query_terms(query: str) -> list[str]:
    """Bare words and quoted phrases of a user query, in order. Punctuation is dropped."""
    out: list[str] = []
    for quoted, bare in re.findall(r'"([^"]*)"|(\S+)', query or ""):
        text = (quoted or bare).strip()
        if not text:
            continue
        if quoted:
            words = _WORD.findall(text)
            if words:
                out.append(" ".join(words))
        else:
            words = _WORD.findall(text)
            out.extend(words)
    return out


def fts_query(query: str) -> str:
    """An FTS5 MATCH expression: quoted phrases stay phrases, bare words become prefix matches.

    Everything the user types is escaped, so no input can reach FTS5 as an operator.
    """
    parts: list[str] = []
    for quoted, bare in re.findall(r'"([^"]*)"|(\S+)', query or ""):
        if quoted:
            words = _WORD.findall(quoted)
            if words:
                parts.append('"' + " ".join(words) + '"')
        else:
            for word in _WORD.findall(bare):
                parts.append('"' + word + '"*')
    return " AND ".join(parts)


def match_line(text: str, terms: list[str]) -> tuple[str, list[list[int]]]:
    """The first line of `text` that contains a term, with [start, length] ranges to highlight.

    Ranges are in UTF-16-agnostic character offsets of the returned line, sorted and merged.
    """
    lines = (text or "").splitlines() or [""]
    lowered = [line.lower() for line in lines]
    needles = [t.lower() for t in terms if t]
    best_index, best_hits = 0, -1
    for index, line in enumerate(lowered):
        hits = sum(1 for needle in needles if needle in line)
        if hits > best_hits:
            best_index, best_hits = index, hits
        if hits == len(needles) and hits:
            break
    line = lines[best_index]
    low = lowered[best_index]
    ranges: list[list[int]] = []
    for needle in needles:
        start = low.find(needle)
        while start >= 0:
            ranges.append([start, len(needle)])
            start = low.find(needle, start + max(1, len(needle)))
    ranges.sort()
    merged: list[list[int]] = []
    for start, length in ranges:
        if merged and start <= merged[-1][0] + merged[-1][1]:
            merged[-1][1] = max(merged[-1][1], start + length - merged[-1][0])
        else:
            merged.append([start, length])
    # Keep lines short enough for a list row, moving the window onto the first match.
    if len(line) > 220:
        offset = max(0, (merged[0][0] if merged else 0) - 40)
        line = ("…" if offset else "") + line[offset:offset + 200]
        shift = offset - (1 if offset else 0)
        merged = [[start - shift, length] for start, length in merged
                  if 0 <= start - shift < len(line)]
    return line, merged


def turn_marks(data: dict) -> list[tuple[int, int]]:
    """(message index, turn number) for the session's current compaction epoch, oldest first."""
    epoch = str(data.get("epoch", 0) or 0)
    marks: list[tuple[int, int]] = []
    items = (data.get("checkpoints") or {}).get("items") or []
    for item in items:
        if not isinstance(item, dict):
            continue
        location = (item.get("locations") or {}).get(epoch)
        turn = item.get("turn")
        if isinstance(location, int) and isinstance(turn, int):
            marks.append((location, turn))
    marks.sort()
    return marks


def turn_for(marks: list[tuple[int, int]], index: int) -> int:
    turn = 0
    for location, number in marks:
        if location <= index:
            turn = number
        else:
            break
    return turn


def session_entries(data: dict) -> list[dict]:
    """Index rows for one saved session: {turn, seq, kind, time, text}.

    User prompts come from the checkpoints (they survive compaction, which rewrites `messages`);
    replies, tool calls and capped tool output come from the messages themselves.
    """
    rows: list[dict] = []
    seen: set[tuple[int, str, str]] = set()
    seq = 0

    def add(turn: int, kind: str, text: str, when) -> None:
        nonlocal seq
        text = (text or "").strip()
        if not text:
            return
        key = (turn, kind, hashlib.sha256(text[:512].encode("utf-8")).hexdigest())
        if key in seen:
            return
        seen.add(key)
        seq += 1
        rows.append({"turn": turn, "seq": seq, "kind": kind, "time": when,
                     "text": text[:MAX_PROMPT if kind == "prompt" else MAX_TEXT]})

    for item in (data.get("checkpoints") or {}).get("items") or []:
        if isinstance(item, dict) and isinstance(item.get("turn"), int):
            add(item["turn"], "prompt", str(item.get("prompt") or ""), item.get("time"))

    marks = turn_marks(data)
    updated = data.get("updated")
    messages = data.get("messages") or []
    for offset, message in enumerate(messages):
        if not isinstance(message, dict) or len(rows) >= MAX_ENTRIES_PER_SESSION:
            continue
        # session_data() stores messages without the system message, which sat at index 0.
        turn = turn_for(marks, offset + 1)
        role = message.get("role")
        content = message.get("content")
        if role == "user":
            # Relay's own context blocks are machine-written and repeat what the pane already
            # knows; the user's prompt is indexed from the checkpoint instead.
            if str(content or "").lstrip().startswith(RELAY_CONTEXT):
                continue
            add(turn, "prompt", str(content or ""), updated)
        elif role == "assistant":
            add(turn, "reply", str(content or ""), updated)
            for call in message.get("tool_calls") or []:
                if not isinstance(call, dict):
                    continue
                function = call.get("function") or {}
                arguments = function.get("arguments")
                if not isinstance(arguments, str):
                    arguments = json.dumps(arguments, ensure_ascii=False) if arguments else ""
                add(turn, "tool_call", f"{function.get('name') or 'tool'} {arguments}", updated)
        elif role == "tool":
            add(turn, "tool_output", str(content or ""), updated)
    return rows


def _clean(text: str, cap: int) -> str:
    """Printable text for the index: control characters out, length capped."""
    text = "".join(ch for ch in (text or "") if ch == "\n" or ch == "\t" or ch >= " ")
    return text[:cap]


# ----- the index -----------------------------------------------------------------------------

class ConversationIndex:
    """A connection to index.db. Safe to use from several threads and several worker processes."""

    def __init__(self, path: str | Path | None = None, *, rebuild_on_reset: bool = False):
        self.path = Path(path) if path else default_index_path()
        self._db: sqlite3.Connection | None = None
        self.recovered = False       # a corrupt or outdated database was discarded
        self._open()
        if self.recovered and rebuild_on_reset:
            try:
                self.rebuild()
            except (OSError, sqlite3.Error):
                pass

    # ----- connection ------------------------------------------------------------------
    def _open(self) -> None:
        try:
            self._connect()
        except (sqlite3.DatabaseError, OSError):
            self._discard()
            self._connect()

    def _connect(self) -> None:
        self.path.parent.mkdir(parents=True, exist_ok=True, mode=0o700)
        fresh = not self.path.exists()
        db = sqlite3.connect(str(self.path), check_same_thread=False, timeout=10)
        db.row_factory = sqlite3.Row
        db.execute("PRAGMA busy_timeout=10000")
        db.execute("PRAGMA journal_mode=WAL")
        db.execute("PRAGMA synchronous=NORMAL")
        if fresh:
            os.chmod(self.path, 0o600)
        version = None
        try:
            row = db.execute("SELECT value FROM meta WHERE key='schema_version'").fetchone()
            version = int(row[0]) if row else None
        except sqlite3.DatabaseError:
            version = None
        if version is not None and version != SCHEMA_VERSION:
            db.close()
            self._discard()
            self.recovered = True
            db = sqlite3.connect(str(self.path), check_same_thread=False, timeout=10)
            db.row_factory = sqlite3.Row
            db.execute("PRAGMA busy_timeout=10000")
            db.execute("PRAGMA journal_mode=WAL")
            os.chmod(self.path, 0o600)
        db.executescript(SCHEMA)
        db.execute("INSERT OR REPLACE INTO meta(key, value) VALUES('schema_version', ?)", (str(SCHEMA_VERSION),))
        db.commit()
        try:
            os.chmod(self.path, 0o600)
        except OSError:
            pass
        self._db = db

    def _discard(self) -> None:
        self.recovered = True
        for suffix in ("", "-wal", "-shm", "-journal"):
            try:
                os.unlink(str(self.path) + suffix)
            except OSError:
                pass

    def _reset(self) -> None:
        """Throw the database away after corruption and start an empty one."""
        try:
            if self._db is not None:
                self._db.close()
        except sqlite3.Error:
            pass
        self._db = None
        self._discard()
        self._connect()

    def close(self) -> None:
        if self._db is not None:
            try:
                self._db.close()
            finally:
                self._db = None

    def _run(self, work):
        """Run `work(db)`, recreating the database once if SQLite reports corruption."""
        try:
            return work(self._db)
        except sqlite3.DatabaseError as exc:
            if "malformed" not in str(exc) and "not a database" not in str(exc) and "corrupt" not in str(exc):
                raise
            self._reset()
            return work(self._db)

    # ----- writing ---------------------------------------------------------------------
    def update_session(self, data: dict, session_dir: str | Path | None = None) -> int:
        """Index (or re-index) one saved session. Returns the number of entries written."""
        session_id = str(data.get("id") or "")
        if not session_id:
            raise ValueError("Session has no id.")
        workspace = str(data.get("workspace") or "")
        rows = session_entries(data)
        title = str(data.get("title") or "")
        if not title:
            first = next((row["text"] for row in rows if row["kind"] == "prompt"), "")
            title = " ".join(first.split())[:80]

        def work(db):
            keep = db.execute("SELECT custom_title, pinned FROM conversations WHERE session_id=?", (session_id,)).fetchone()
            db.execute("DELETE FROM entries WHERE session_id=?", (session_id,))
            db.execute(
                "INSERT OR REPLACE INTO conversations(session_id, source, workspace, project, title, custom_title,"
                " model, preset, created, updated, turns, open_requests, session_dir, pinned)"
                " VALUES(?,'agent',?,?,?,?,?,?,?,?,?,?,?,?)",
                (session_id, workspace, project_name(workspace), title,
                 keep["custom_title"] if keep else None,
                 str(data.get("model") or ""), str(data.get("preset") or ""),
                 data.get("created"), data.get("updated") or time.time(),
                 int(data.get("turns") or 0), int(data.get("open_requests") or 0),
                 str(session_dir or ""), int(keep["pinned"]) if keep else 0))
            db.executemany(
                "INSERT INTO entries(session_id, turn, seq, kind, time, text) VALUES(?,?,?,?,?,?)",
                [(session_id, row["turn"], row["seq"], row["kind"], row["time"], row["text"]) for row in rows])
            db.commit()
            return len(rows)
        return self._run(work)

    def index_session_file(self, path: str | Path) -> bool:
        path = Path(path)
        try:
            with open(path, encoding="utf-8") as handle:
                data = json.load(handle)
        except (OSError, ValueError):
            return False
        if not isinstance(data, dict) or data.get("kind") != "relay_session" or not data.get("id"):
            return False
        self.update_session(data, path.parent)
        return True

    def rebuild(self, root: str | Path | None = None) -> dict:
        """Drop every agent conversation and rebuild it from the session JSON files.

        Terminal history has no file to rebuild from, so its rows are kept.
        """
        started = time.time()
        directory = Path(root) if root else sessions_root()

        def clear(db):
            db.execute("DELETE FROM entries WHERE session_id IN (SELECT session_id FROM conversations WHERE source='agent')")
            db.execute("DELETE FROM conversations WHERE source='agent'")
            db.commit()
        self._run(clear)
        sessions = entries = 0
        if directory.is_dir():
            for path in sorted(directory.glob("*/*.json")):
                if path.name.endswith(".meta.json"):
                    continue
                if self.index_session_file(path):
                    sessions += 1
        entries = self._run(lambda db: db.execute("SELECT count(*) FROM entries").fetchone()[0])
        self._run(lambda db: (db.execute("INSERT OR REPLACE INTO meta(key, value) VALUES('rebuilt', ?)",
                                         (str(time.time()),)), db.commit())[0])
        return {"sessions": sessions, "entries": entries, "ms": int((time.time() - started) * 1000)}

    def record_commands(self, workspace: str, items: list[dict]) -> int:
        """Append Relay-run terminal commands for a workspace. Returns the number of rows added."""
        if not isinstance(items, list):
            raise ValueError("terminal_history items must be a list.")
        if len(items) > MAX_COMMANDS:
            raise ValueError(f"At most {MAX_COMMANDS} commands per message.")
        workspace = str(workspace or "")
        session_id = terminal_id(workspace)
        rows: list[tuple] = []
        newest = 0.0
        for item in items:
            if not isinstance(item, dict):
                raise ValueError("Each terminal history item must be an object.")
            command = _clean(str(item.get("command") or ""), 4000).strip()
            if not command:
                continue
            when = item.get("time")
            when = float(when) if isinstance(when, (int, float)) else time.time()
            newest = max(newest, when)
            status = item.get("exit_status")
            status = int(status) if isinstance(status, int) else None
            rows.append(("command", when, status, command))
            output = _clean(str(item.get("output") or ""), MAX_TEXT).strip()
            if output:
                rows.append(("command_output", when, status, output))
        if not rows:
            return 0

        def work(db):
            existing = db.execute("SELECT turns, created FROM conversations WHERE session_id=?", (session_id,)).fetchone()
            base = int(existing["turns"]) if existing else 0
            created = existing["created"] if existing and existing["created"] else newest
            top = db.execute("SELECT COALESCE(MAX(seq), 0) FROM entries WHERE session_id=?", (session_id,)).fetchone()[0]
            commands = base
            payload = []
            for kind, when, status, text in rows:
                if kind == "command":
                    commands += 1
                top += 1
                payload.append((session_id, commands, top, kind, when, status, text))
            db.execute(
                "INSERT OR REPLACE INTO conversations(session_id, source, workspace, project, title, custom_title,"
                " model, preset, created, updated, turns, open_requests, session_dir, pinned)"
                " VALUES(?,'terminal',?,?,?,(SELECT custom_title FROM conversations WHERE session_id=?),"
                " '', '', ?, ?, ?, 0, '', COALESCE((SELECT pinned FROM conversations WHERE session_id=?), 0))",
                (session_id, workspace, project_name(workspace),
                 "Terminal · " + project_name(workspace), session_id, created, newest, commands, session_id))
            db.executemany(
                "INSERT INTO entries(session_id, turn, seq, kind, time, status, text) VALUES(?,?,?,?,?,?,?)", payload)
            db.commit()
            return len(payload)
        return self._run(work)

    def delete_session(self, session_id: str, *, remove_files: bool = True) -> dict:
        """Remove a conversation's index rows and, for agent sessions, its files and blobs."""
        def work(db):
            row = db.execute("SELECT source, session_dir FROM conversations WHERE session_id=?", (session_id,)).fetchone()
            db.execute("DELETE FROM entries WHERE session_id=?", (session_id,))
            db.execute("DELETE FROM conversations WHERE session_id=?", (session_id,))
            db.commit()
            return dict(row) if row else None
        row = self._run(work)
        removed = {"session_id": session_id, "files": 0, "indexed": row is not None}
        if not remove_files or row is None or row["source"] != "agent" or not row["session_dir"]:
            return removed
        directory = Path(row["session_dir"])
        for name in (f"{session_id}.json", f"{session_id}.meta.json"):
            try:
                (directory / name).unlink()
                removed["files"] += 1
            except OSError:
                pass
        blobs = directory / f"{session_id}.blobs"
        if blobs.is_dir():
            for child in blobs.iterdir():
                try:
                    child.unlink()
                except OSError:
                    pass
            try:
                blobs.rmdir()
                removed["files"] += 1
            except OSError:
                pass
        return removed

    def rename(self, session_id: str, title: str) -> None:
        title = " ".join(str(title or "").split())[:200]

        def work(db):
            db.execute("UPDATE conversations SET custom_title=? WHERE session_id=?", (title or None, session_id))
            db.commit()
        self._run(work)

    def set_pinned(self, session_id: str, pinned: bool) -> None:
        def work(db):
            db.execute("UPDATE conversations SET pinned=? WHERE session_id=?", (1 if pinned else 0, session_id))
            db.commit()
        self._run(work)

    # ----- reading ---------------------------------------------------------------------
    def _filters(self, scope: str, workspace: str | None, model: str | None, has_open: bool,
                 since, until, kinds) -> tuple[str, list]:
        where, params = [], []
        if scope == "project":
            where.append("c.workspace = ?")
            params.append(str(workspace or ""))
        if model:
            where.append("c.model = ?")
            params.append(model)
        if has_open:
            where.append("c.open_requests > 0")
        if isinstance(since, (int, float)):
            where.append("COALESCE(c.updated, 0) >= ?")
            params.append(float(since))
        if isinstance(until, (int, float)):
            where.append("COALESCE(c.updated, 0) <= ?")
            params.append(float(until))
        if kinds:
            where.append("c.source IN (%s)" % ",".join("?" * len(kinds)))
            params.extend(kinds)
        return (" AND ".join(where), params)

    def search(self, query: str = "", *, scope: str = "project", workspace: str | None = None,
               model: str | None = None, has_open: bool = False, since=None, until=None,
               sources=None, limit: int = 50) -> dict:
        """Conversations matching `query`, newest first, each with its matching turns."""
        if scope not in ("project", "all"):
            raise ValueError('scope must be "project" or "all".')
        limit = max(1, min(int(limit or 50), MAX_LIMIT))
        query = str(query or "")
        if len(query) > 500:
            raise ValueError("Search query is too long.")
        kinds = [s for s in (sources or []) if s in ("agent", "terminal")]
        clause, params = self._filters(scope, workspace, model, has_open, since, until, kinds)
        started = time.time()
        match = fts_query(query)
        terms = query_terms(query)

        def work(db):
            if match:
                # Two steps: count the matches per conversation in SQLite (no Python per row), then
                # fetch only the few best-ranked lines of the conversations that made the page.
                sql = ("SELECT e.session_id AS sid, count(*) AS hits, c.* FROM entries_fts"
                       " JOIN entries e ON e.id = entries_fts.rowid"
                       " JOIN conversations c ON c.session_id = e.session_id"
                       " WHERE entries_fts MATCH ?")
                args: list = [match]
                if clause:
                    sql += " AND " + clause
                    args += params
                sql += " GROUP BY e.session_id ORDER BY c.pinned DESC, COALESCE(c.updated, 0) DESC LIMIT ?"
                args.append(limit)
                try:
                    rows = db.execute(sql, args).fetchall()
                except sqlite3.OperationalError as exc:
                    raise ValueError(f"Search query is not valid ({exc}).") from exc
                out = []
                for row in rows:
                    item = _item(row)
                    item["match_count"] = row["hits"]
                    out.append(item)
                by_id = {item["session_id"]: item for item in out}
                if by_id:
                    placeholders = ",".join("?" * len(by_id))
                    lines = db.execute(
                        "SELECT e.session_id, e.turn, e.kind, e.time, e.text FROM entries_fts"
                        " JOIN entries e ON e.id = entries_fts.rowid"
                        f" WHERE entries_fts MATCH ? AND e.session_id IN ({placeholders})"
                        " ORDER BY bm25(entries_fts) LIMIT ?",
                        [match, *by_id, len(by_id) * MAX_MATCHES_PER_ITEM * 4]).fetchall()
                    for line_row in lines:
                        item = by_id[line_row["session_id"]]
                        if len(item["matches"]) >= MAX_MATCHES_PER_ITEM:
                            continue
                        line, ranges = match_line(line_row["text"], terms)
                        item["matches"].append({"turn": line_row["turn"], "kind": line_row["kind"],
                                                "line": line, "ranges": ranges, "time": line_row["time"]})
            else:
                sql = "SELECT c.* FROM conversations c"
                args = []
                if clause:
                    sql += " WHERE " + clause
                    args += params
                sql += " ORDER BY c.pinned DESC, COALESCE(c.updated, 0) DESC LIMIT ?"
                args.append(limit)
                out = [_item(row) for row in db.execute(sql, args).fetchall()]
            for item in out:
                if not item["snippet"]:
                    first = db.execute(
                        "SELECT text FROM entries WHERE session_id=? ORDER BY seq LIMIT 1", (item["session_id"],)).fetchone()
                    item["snippet"] = " ".join((first["text"] if first else "").split())[:200]
                item["matches"].sort(key=lambda m: m["turn"])
            total = db.execute("SELECT count(*) FROM conversations" + (" c WHERE " + clause if clause else ""),
                               params).fetchone()[0]
            return out, total
        items, total = self._run(work)
        return {"items": items, "total": total, "query": query,
                "elapsed_ms": int((time.time() - started) * 1000)}

    def conversation(self, session_id: str, turn: int | None = None, query: str = "",
                     limit: int = 400) -> dict:
        """One conversation for the preview: its header plus its entries (optionally one turn)."""
        limit = max(1, min(int(limit or 400), 2000))
        terms = query_terms(query)

        def work(db):
            row = db.execute("SELECT * FROM conversations WHERE session_id=?", (session_id,)).fetchone()
            if row is None:
                raise ValueError("No indexed conversation with that id.")
            sql = "SELECT turn, kind, time, status, text FROM entries WHERE session_id=?"
            args: list = [session_id]
            if isinstance(turn, int):
                sql += " AND turn=?"
                args.append(turn)
            # Checkpoint prompts are written before the messages, so order by turn for the preview.
            sql += " ORDER BY turn, seq LIMIT ?"
            args.append(limit)
            entries = []
            for entry in db.execute(sql, args).fetchall():
                item = {"turn": entry["turn"], "kind": entry["kind"], "time": entry["time"],
                        "text": entry["text"]}
                if entry["status"] is not None:
                    item["exit_status"] = entry["status"]
                if terms:
                    line, ranges = match_line(entry["text"], terms)
                    if ranges:
                        item["ranges"] = ranges
                        item["line"] = line
                entries.append(item)
            header = _item(row)
            header.pop("matches", None)
            header["items"] = entries
            header["match_count"] = sum(1 for e in entries if e.get("ranges"))
            return header
        return self._run(work)

    def stats(self) -> dict:
        def work(db):
            conversations = db.execute("SELECT count(*) FROM conversations").fetchone()[0]
            entries = db.execute("SELECT count(*) FROM entries").fetchone()[0]
            size = 0
            for suffix in ("", "-wal"):
                try:
                    size += os.stat(str(self.path) + suffix).st_size
                except OSError:
                    pass
            return {"conversations": conversations, "entries": entries, "bytes": size,
                    "schema_version": SCHEMA_VERSION, "path": str(self.path)}
        return self._run(work)


def _item(row) -> dict:
    return {"session_id": row["session_id"], "source": row["source"],
            "title": (row["custom_title"] or row["title"] or "Untitled"),
            "generated_title": row["title"] or "",
            "workspace": row["workspace"], "project": row["project"],
            "model": row["model"], "preset": row["preset"],
            "created": row["created"], "updated": row["updated"],
            "turns": row["turns"], "open_requests": row["open_requests"],
            "session_dir": row["session_dir"], "pinned": int(row["pinned"] or 0),
            "snippet": "", "matches": [], "match_count": 0}
