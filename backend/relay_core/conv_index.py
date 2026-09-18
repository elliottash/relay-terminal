# SPDX-License-Identifier: GPL-3.0-or-later
"""Full-text index of conversations and Relay-run terminal commands (protocol section 14).

An SQLite FTS5 database beside the session files:

    $XDG_DATA_HOME/relay/index.db      0600, in the 0700 relay/ directory

It is a **cache**, never the source of truth: every agent row can be rebuilt from the session JSON
under `relay/sessions/<workspace-digest>/`, so a corrupt database is deleted and recreated.
`SCHEMA_VERSION` is stored in `meta`. Version 1 is migrated in place (columns added), because the
terminal-history rows have no file to be rebuilt from; any other mismatch wipes the tables.
User-set titles and pins of saved sessions live in `<id>.meta.json` (since v2); the index only
mirrors them. `reconcile()` brings the rows back in line with the files on disk.

Three kinds of conversation live in the same tables, told apart by `conversations.source`:

* `agent`   — one row per saved session; entries are user prompts, assistant replies, tool calls
              and capped tool output, with the turn number they belong to.
* `subagent`— one row per subagent thread (`<session>.threads/<id>.json`), with `owner_session`
              (the session that started it), `parent_thread` (the thread that started it, when a
              thread did), `spawn_turn`, `agent_id`, `agent_type` and `status`. Searches leave
              these rows out unless they are asked for (`include_threads`), since v2 (card #R6J0).
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
import tempfile
import time
from pathlib import Path

# v2 (2026-09-18, cards #Y63Z/#R6J0): subagent threads, owner/parent links, models and usage totals.
SCHEMA_VERSION = 2
MAX_TEXT = 4000              # per-entry cap for replies and tool output
MAX_PROMPT = 8000            # per-entry cap for user prompts
MAX_MATCHES_PER_ITEM = 5     # matching turns returned per conversation (default; up to 20)
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
    pinned       INTEGER NOT NULL DEFAULT 0,
    owner_session TEXT,
    parent_thread TEXT,
    agent_id     TEXT NOT NULL DEFAULT '',
    agent_type   TEXT NOT NULL DEFAULT '',
    spawn_turn   INTEGER,
    status       TEXT NOT NULL DEFAULT '',
    models       TEXT NOT NULL DEFAULT '[]',
    tokens       INTEGER NOT NULL DEFAULT 0,
    cost         REAL,
    file_mtime   REAL
);
CREATE INDEX IF NOT EXISTS conversations_by_owner ON conversations(owner_session);
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
SOURCES = ("agent", "terminal", "subagent")
SORTS = ("recent", "oldest", "longest", "relevance")
# Columns added by v2; a v1 database gets them with ALTER TABLE instead of being wiped.
V2_COLUMNS = (("owner_session", "TEXT"), ("parent_thread", "TEXT"), ("agent_id", "TEXT NOT NULL DEFAULT ''"),
              ("agent_type", "TEXT NOT NULL DEFAULT ''"), ("spawn_turn", "INTEGER"),
              ("status", "TEXT NOT NULL DEFAULT ''"), ("models", "TEXT NOT NULL DEFAULT '[]'"),
              ("tokens", "INTEGER NOT NULL DEFAULT 0"), ("cost", "REAL"), ("file_mtime", "REAL"))
MAX_MATCHES_LIMIT = 20
THREAD_KIND = "relay_subagent_thread"


# ----- paths ---------------------------------------------------------------------------------

def relay_data_dir() -> Path:
    base = os.environ.get("XDG_DATA_HOME") or str(Path.home() / ".local" / "share")
    return Path(base) / "relay"


def default_index_path() -> Path:
    return relay_data_dir() / "index.db"


def sessions_root() -> Path:
    return relay_data_dir() / "sessions"


def normalize_workspace(workspace: str | Path | None) -> str:
    """The one spelling of a workspace path: the session directory digest, the index's `workspace`
    column and its "this project" filter all use it, so a symlinked or `~` workspace still finds
    its own conversations."""
    if not workspace:
        return ""
    try:
        return str(Path(workspace).expanduser().resolve())
    except (OSError, RuntimeError):
        return str(Path(workspace).expanduser())


def workspace_digest(workspace: str | Path) -> str:
    return hashlib.sha256(normalize_workspace(workspace).encode("utf-8")).hexdigest()[:16]


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


def fts_parts(query: str) -> list[str]:
    """The FTS5 expressions of a query, one per word (prefix) or quoted phrase, all escaped."""
    parts: list[str] = []
    for quoted, bare in re.findall(r'"([^"]*)"|(\S+)', query or ""):
        if quoted:
            words = _WORD.findall(quoted)
            if words:
                parts.append('"' + " ".join(words) + '"')
        else:
            for word in _WORD.findall(bare):
                parts.append('"' + word + '"*')
    return parts


def fts_query(query: str) -> str:
    """An FTS5 MATCH expression: quoted phrases stay phrases, bare words become prefix matches.

    Everything the user types is escaped, so no input can reach FTS5 as an operator.
    """
    return " AND ".join(fts_parts(query))


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


def thread_entries(data: dict) -> list[dict]:
    """Index rows for one subagent thread. Its "turns" are its runs: the task, then each message
    that resumed it, so a row's turn says which run of the thread it belongs to."""
    rows: list[dict] = []
    turn = 0
    updated = data.get("updated")
    for message in data.get("messages") or []:
        if not isinstance(message, dict) or len(rows) >= MAX_ENTRIES_PER_SESSION:
            continue
        role, content = message.get("role"), str(message.get("content") or "").strip()
        if role == "user":
            if content.startswith(RELAY_CONTEXT):
                continue
            turn += 1
            kind = "prompt"
        elif role == "assistant":
            kind = "reply"
        elif role == "tool":
            kind = "tool_output"
        else:
            continue
        if content:
            rows.append({"turn": max(turn, 1), "seq": len(rows) + 1, "kind": kind, "time": updated,
                         "text": content[:MAX_PROMPT if kind == "prompt" else MAX_TEXT]})
        for call in message.get("tool_calls") or [] if role == "assistant" else []:
            if not isinstance(call, dict):
                continue
            function = call.get("function") or {}
            arguments = function.get("arguments")
            if not isinstance(arguments, str):
                arguments = json.dumps(arguments, ensure_ascii=False) if arguments else ""
            rows.append({"turn": max(turn, 1), "seq": len(rows) + 1, "kind": "tool_call", "time": updated,
                         "text": f"{function.get('name') or 'tool'} {arguments}"[:MAX_TEXT]})
    return rows


def _usage_tokens(data: dict) -> tuple[int, float | None]:
    usage = data.get("usage") if isinstance(data.get("usage"), dict) else {}
    tokens = usage.get("total_tokens")
    cost = usage.get("cost")
    return (tokens if isinstance(tokens, int) and not isinstance(tokens, bool) and tokens >= 0 else 0,
            float(cost) if isinstance(cost, (int, float)) and not isinstance(cost, bool) else None)


def _models(data: dict) -> str:
    models = [m for m in (data.get("models") or []) if isinstance(m, str)][:50]
    if not models and data.get("model"):
        models = [str(data.get("model"))]
    return json.dumps(models)


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
        self.migrated_from = None
        if version == 1 and SCHEMA_VERSION == 2:
            try:
                self._migrate_v1(db)
                version = SCHEMA_VERSION
                self.migrated_from = 1
            except sqlite3.DatabaseError:
                pass
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

    @staticmethod
    def _migrate_v1(db) -> None:
        """v1 -> v2 in place: add the thread columns and move user titles and pins to the session
        files, where they are safe from a cache wipe. Terminal-history rows are kept as they are."""
        columns = {row[1] for row in db.execute("PRAGMA table_info(conversations)").fetchall()}
        for name, kind in V2_COLUMNS:
            if name not in columns:
                db.execute(f"ALTER TABLE conversations ADD COLUMN {name} {kind}")
        rows = db.execute("SELECT session_id, session_dir, custom_title, pinned FROM conversations"
                          " WHERE source='agent' AND (custom_title IS NOT NULL OR pinned != 0)").fetchall()
        for row in rows:
            if row["session_dir"]:
                write_user_fields(Path(row["session_dir"]), row["session_id"],
                                  custom_title=row["custom_title"], pinned=bool(row["pinned"]))
        db.execute("INSERT OR REPLACE INTO meta(key, value) VALUES('schema_version', ?)", (str(SCHEMA_VERSION),))
        db.commit()

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
        workspace = normalize_workspace(str(data.get("workspace") or ""))
        rows = session_entries(data)
        title = str(data.get("title") or "")
        if not title:
            first = next((row["text"] for row in rows if row["kind"] == "prompt"), "")
            title = " ".join(first.split())[:80]

        def work(db):
            keep = db.execute("SELECT custom_title, pinned FROM conversations WHERE session_id=?", (session_id,)).fetchone()
            # The session files hold user titles and pins (since v2); an older caller that does not
            # pass them keeps what the row had.
            if "custom_title" in data or "pinned" in data:
                keep = {"custom_title": data.get("custom_title") or None, "pinned": 1 if data.get("pinned") else 0}
            db.execute("DELETE FROM entries WHERE session_id=?", (session_id,))
            tokens, cost = _usage_tokens(data)
            db.execute(
                "INSERT OR REPLACE INTO conversations(session_id, source, workspace, project, title, custom_title,"
                " model, preset, created, updated, turns, open_requests, session_dir, pinned, models, tokens, cost)"
                " VALUES(?,'agent',?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)",
                (session_id, workspace, project_name(workspace), title,
                 keep["custom_title"] if keep else None,
                 str(data.get("model") or ""), str(data.get("preset") or ""),
                 data.get("created"), data.get("updated") or time.time(),
                 int(data.get("turns") or 0), int(data.get("open_requests") or 0),
                 str(session_dir or ""), int(keep["pinned"]) if keep else 0, _models(data), tokens, cost))
            db.executemany(
                "INSERT INTO entries(session_id, turn, seq, kind, time, text) VALUES(?,?,?,?,?,?)",
                [(session_id, row["turn"], row["seq"], row["kind"], row["time"], row["text"]) for row in rows])
            db.commit()
            return len(rows)
        return self._run(work)

    def update_thread(self, data: dict, session_dir: str | Path | None = None, file_mtime: float | None = None) -> int:
        """Index (or re-index) one subagent thread. Returns the number of entries written."""
        thread_id = str(data.get("id") or "")
        owner = str(data.get("owner_session") or "")
        if not thread_id or not owner:
            raise ValueError("A thread needs an id and an owner session.")
        workspace = normalize_workspace(str(data.get("workspace") or ""))
        rows = thread_entries(data)
        title = " ".join(str(data.get("description") or data.get("title") or "").split())[:200] or "Subagent"
        parent = data.get("parent_thread") if isinstance(data.get("parent_thread"), str) else None
        spawn_turn = data.get("spawn_turn") if type(data.get("spawn_turn")) is int else None
        tokens, cost = _usage_tokens(data)
        runs = data.get("runs") if type(data.get("runs")) is int else 1

        def work(db):
            keep = db.execute("SELECT custom_title, pinned FROM conversations WHERE session_id=?", (thread_id,)).fetchone()
            if "custom_title" in data or "pinned" in data:
                keep = {"custom_title": data.get("custom_title") or None, "pinned": 1 if data.get("pinned") else 0}
            db.execute("DELETE FROM entries WHERE session_id=?", (thread_id,))
            db.execute(
                "INSERT OR REPLACE INTO conversations(session_id, source, workspace, project, title, custom_title,"
                " model, preset, created, updated, turns, open_requests, session_dir, pinned, owner_session,"
                " parent_thread, agent_id, agent_type, spawn_turn, status, models, tokens, cost, file_mtime)"
                " VALUES(?,'subagent',?,?,?,?,?,'',?,?,?,0,?,?,?,?,?,?,?,?,?,?,?,?)",
                (thread_id, workspace, project_name(workspace), title, keep["custom_title"] if keep else None,
                 str(data.get("model") or ""), data.get("created"), data.get("updated") or time.time(),
                 max(1, runs), str(session_dir or ""), int(keep["pinned"]) if keep else 0, owner, parent,
                 str(data.get("agent_id") or ""), str(data.get("type") or ""), spawn_turn,
                 str(data.get("status") or ""), _models(data), tokens, cost, file_mtime))
            db.executemany(
                "INSERT INTO entries(session_id, turn, seq, kind, time, text) VALUES(?,?,?,?,?,?)",
                [(thread_id, row["turn"], row["seq"], row["kind"], row["time"], row["text"]) for row in rows])
            db.commit()
            return len(rows)
        return self._run(work)

    def index_thread_file(self, path: str | Path) -> bool:
        path = Path(path)
        try:
            with open(path, encoding="utf-8") as handle:
                data = json.load(handle)
        except (OSError, ValueError):
            return False
        if not isinstance(data, dict) or data.get("kind") != THREAD_KIND or not data.get("id"):
            return False
        mtime = path.stat().st_mtime
        data.setdefault("updated", mtime)
        # The folder is <owner>.threads inside the session directory.
        self.update_thread(data, path.parent.parent, mtime)
        return True

    def index_session_file(self, path: str | Path) -> bool:
        path = Path(path)
        try:
            with open(path, encoding="utf-8") as handle:
                data = json.load(handle)
        except (OSError, ValueError):
            return False
        if not isinstance(data, dict) or data.get("kind") != "relay_session" or not data.get("id"):
            return False
        self.update_session({**data, **read_user_fields(path.parent, str(data["id"]))}, path.parent)
        return True

    def reconcile(self, root: str | Path | None = None) -> dict:
        """Bring the rows in line with the files: index sessions and threads that are missing or
        newer on disk than in the index, and drop rows whose file is gone. Cheap when nothing
        changed (one stat per file and one small meta read per session), so it runs whenever a
        worker first opens the index; `rebuild()` is still there for a full refresh."""
        started = time.time()
        directory = Path(root) if root else sessions_root()
        known = self._run(lambda db: {row["session_id"]: (row["source"], row["updated"] or 0, row["session_dir"],
                                                          row["file_mtime"])
                                      for row in db.execute("SELECT session_id, source, updated, session_dir, file_mtime"
                                                            " FROM conversations WHERE source IN"
                                                            " ('agent', 'subagent')").fetchall()})
        seen: set[str] = set()
        added = refreshed = 0
        if directory.is_dir():
            for path in directory.glob("*/*.json"):
                name = path.name
                if name.endswith(".meta.json") or name.startswith("."):
                    continue
                session_id = name[:-5]
                seen.add(session_id)
                row = known.get(session_id)
                stamp = _meta_updated(path)
                if row is not None and stamp is not None and stamp <= float(row[1]) + 1e-6 \
                        and row[2] == str(path.parent):
                    continue
                if self.index_session_file(path):
                    added += row is None
                    refreshed += row is not None
            for path in directory.glob("*/*.threads/*.json"):
                thread_id = path.name[:-5]
                seen.add(thread_id)
                row = known.get(thread_id)
                try:
                    mtime = path.stat().st_mtime
                except OSError:
                    continue
                if row is not None and row[3] is not None and abs(mtime - float(row[3])) < 1e-6:
                    continue
                if self.index_thread_file(path):
                    added += row is None
                    refreshed += row is not None
        gone = [sid for sid, (_source, _updated, folder, _mtime) in known.items()
                if sid not in seen and (not folder or Path(folder).parent == directory or
                                        Path(folder).parent.parent == directory)]

        def drop(db):
            for sid in gone:
                db.execute("DELETE FROM entries WHERE session_id=?", (sid,))
                db.execute("DELETE FROM conversations WHERE session_id=?", (sid,))
            db.commit()
        if gone:
            self._run(drop)
        return {"added": added, "refreshed": refreshed, "removed": len(gone),
                "ms": int((time.time() - started) * 1000)}

    def rebuild(self, root: str | Path | None = None) -> dict:
        """Drop every agent conversation and rebuild it from the session JSON files.

        Terminal history has no file to rebuild from, so its rows are kept.
        """
        started = time.time()
        directory = Path(root) if root else sessions_root()

        def clear(db):
            db.execute("DELETE FROM entries WHERE session_id IN"
                       " (SELECT session_id FROM conversations WHERE source IN ('agent', 'subagent'))")
            db.execute("DELETE FROM conversations WHERE source IN ('agent', 'subagent')")
            db.commit()
        self._run(clear)
        sessions = entries = threads = 0
        if directory.is_dir():
            for path in sorted(directory.glob("*/*.json")):
                if path.name.endswith(".meta.json"):
                    continue
                if self.index_session_file(path):
                    sessions += 1
            for path in sorted(directory.glob("*/*.threads/*.json")):
                if self.index_thread_file(path):
                    threads += 1
        entries = self._run(lambda db: db.execute("SELECT count(*) FROM entries").fetchone()[0])
        self._run(lambda db: (db.execute("INSERT OR REPLACE INTO meta(key, value) VALUES('rebuilt', ?)",
                                         (str(time.time()),)), db.commit())[0])
        return {"sessions": sessions, "threads": threads, "entries": entries,
                "ms": int((time.time() - started) * 1000)}

    def record_commands(self, workspace: str, items: list[dict]) -> int:
        """Append Relay-run terminal commands for a workspace. Returns the number of rows added."""
        if not isinstance(items, list):
            raise ValueError("terminal_history items must be a list.")
        if len(items) > MAX_COMMANDS:
            raise ValueError(f"At most {MAX_COMMANDS} commands per message.")
        workspace = normalize_workspace(str(workspace or ""))
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
            # A session's subagent threads go with it (their files sit in its .threads folder).
            db.execute("DELETE FROM entries WHERE session_id IN"
                       " (SELECT session_id FROM conversations WHERE owner_session=?)", (session_id,))
            db.execute("DELETE FROM conversations WHERE owner_session=?", (session_id,))
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
        for folder in (directory / f"{session_id}.blobs", directory / f"{session_id}.threads"):
            if folder.is_dir():
                for child in folder.iterdir():
                    try:
                        child.unlink()
                    except OSError:
                        pass
                try:
                    folder.rmdir()
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
            params.append(normalize_workspace(str(workspace or "")))
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
               sources=None, limit: int = 50, include_threads: bool = False, sort: str = "recent",
               offset: int = 0, matches_per_item: int = MAX_MATCHES_PER_ITEM) -> dict:
        """Conversations matching `query`, each with its matching turns.

        Words are an AND over the whole conversation, not over one message: a conversation matches
        when every word or phrase occurs somewhere in it. `sort` is "recent" (default), "oldest",
        "longest" (most turns) or "relevance" (most matching entries); pinned rows come first in
        every order. `offset` pages through the list; `next_offset` is set when there is more.
        Subagent threads are rows too, but only with `include_threads` (or "subagent" named in
        `sources`): the list is about sessions, and threads are what the checkbox adds."""
        if scope not in ("project", "all"):
            raise ValueError('scope must be "project" or "all".')
        if sort not in SORTS:
            raise ValueError("sort must be one of " + ", ".join(SORTS) + ".")
        limit = max(1, min(int(limit or 50), MAX_LIMIT))
        offset = max(0, int(offset or 0))
        per_item = max(1, min(int(matches_per_item or MAX_MATCHES_PER_ITEM), MAX_MATCHES_LIMIT))
        query = str(query or "")
        if len(query) > 500:
            raise ValueError("Search query is too long.")
        kinds = [s for s in (sources or []) if s in SOURCES]
        if not kinds:
            kinds = ["agent", "terminal"]
        if include_threads and "subagent" not in kinds:
            kinds.append("subagent")
        clause, params = self._filters(scope, workspace, model, has_open, since, until, kinds)
        started = time.time()
        parts = fts_parts(query)
        terms = query_terms(query)
        order = {"recent": "COALESCE(c.updated, 0) DESC", "oldest": "COALESCE(c.updated, 0) ASC",
                 "longest": "c.turns DESC, COALESCE(c.updated, 0) DESC",
                 "relevance": "hits DESC, COALESCE(c.updated, 0) DESC"}[sort]

        def work(db):
            if parts:
                any_part = " OR ".join(parts)
                # Every part somewhere in the conversation: one set of conversations per part,
                # intersected. Then count the entries that match any part, in SQLite.
                every = " INTERSECT ".join(
                    "SELECT e.session_id FROM entries_fts JOIN entries e ON e.id = entries_fts.rowid"
                    " WHERE entries_fts MATCH ?" for _ in parts)
                sql = ("SELECT e.session_id AS sid, count(*) AS hits, c.* FROM entries_fts"
                       " JOIN entries e ON e.id = entries_fts.rowid"
                       " JOIN conversations c ON c.session_id = e.session_id"
                       f" WHERE entries_fts MATCH ? AND e.session_id IN ({every})")
                args: list = [any_part, *parts]
                if clause:
                    sql += " AND " + clause
                    args += params
                sql += f" GROUP BY e.session_id ORDER BY c.pinned DESC, {order} LIMIT ? OFFSET ?"
                args += [limit + 1, offset]
                try:
                    rows = db.execute(sql, args).fetchall()
                except sqlite3.OperationalError as exc:
                    raise ValueError(f"Search query is not valid ({exc}).") from exc
                more = len(rows) > limit
                out = []
                for row in rows[:limit]:
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
                        [any_part, *by_id, len(by_id) * per_item * 4]).fetchall()
                    for line_row in lines:
                        item = by_id[line_row["session_id"]]
                        if len(item["matches"]) >= per_item:
                            continue
                        line, ranges = match_line(line_row["text"], terms)
                        item["matches"].append({"turn": line_row["turn"], "kind": line_row["kind"],
                                                "line": line, "ranges": ranges, "time": line_row["time"]})
            else:
                sql = "SELECT c.*, 0 AS hits FROM conversations c"
                args = []
                if clause:
                    sql += " WHERE " + clause
                    args += params
                sql += f" ORDER BY c.pinned DESC, {order} LIMIT ? OFFSET ?"
                args += [limit + 1, offset]
                rows = db.execute(sql, args).fetchall()
                more = len(rows) > limit
                out = [_item(row) for row in rows[:limit]]
            owners = {item["owner_session"] for item in out if item.get("owner_session")}
            parents = {item["parent_thread"] for item in out if item.get("parent_thread")}
            if owners or parents:
                wanted = list(owners | parents)
                titles = {row["session_id"]: (row["custom_title"] or row["title"] or "Untitled")
                          for row in db.execute(
                              "SELECT session_id, title, custom_title FROM conversations WHERE session_id IN (%s)"
                              % ",".join("?" * len(wanted)), wanted).fetchall()}
                for item in out:
                    if item.get("owner_session"):
                        item["owner_title"] = titles.get(item["owner_session"], "")
                    if item.get("parent_thread"):
                        item["parent_title"] = titles.get(item["parent_thread"], "")
            for item in out:
                if not item["snippet"]:
                    first = db.execute(
                        "SELECT text FROM entries WHERE session_id=? ORDER BY seq LIMIT 1", (item["session_id"],)).fetchone()
                    item["snippet"] = " ".join((first["text"] if first else "").split())[:200]
                item["matches"].sort(key=lambda m: m["turn"])
            total = db.execute("SELECT count(*) FROM conversations" + (" c WHERE " + clause if clause else ""),
                               params).fetchone()[0]
            return out, total, more
        items, total, more = self._run(work)
        result = {"items": items, "total": total, "query": query, "sort": sort, "offset": offset,
                  "elapsed_ms": int((time.time() - started) * 1000)}
        if more:
            result["next_offset"] = offset + len(items)
        return result

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
            if header.get("owner_session"):
                owner = db.execute("SELECT title, custom_title FROM conversations WHERE session_id=?",
                                   (header["owner_session"],)).fetchone()
                header["owner_title"] = (owner["custom_title"] or owner["title"]) if owner else ""
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
            "owner_session": row["owner_session"], "parent_thread": row["parent_thread"],
            "agent_id": row["agent_id"], "agent_type": row["agent_type"], "spawn_turn": row["spawn_turn"],
            "status": row["status"], "models": _json_list(row["models"]), "tokens": row["tokens"] or 0,
            "cost": row["cost"],
            "snippet": "", "matches": [], "match_count": 0}


def _json_list(text) -> list:
    try:
        value = json.loads(text or "[]")
    except ValueError:
        return []
    return [v for v in value if isinstance(v, str)] if isinstance(value, list) else []


# ----- user fields in the session files (since v2) --------------------------------------------

def _meta_path(directory: Path, session_id: str) -> Path:
    return Path(directory) / f"{session_id}.meta.json"


def read_user_fields(directory: str | Path, session_id: str) -> dict:
    """{custom_title, pinned} from `<id>.meta.json`; empty when the file has neither."""
    try:
        with open(_meta_path(Path(directory), session_id), encoding="utf-8") as handle:
            meta = json.load(handle)
    except (OSError, ValueError):
        return {}
    if not isinstance(meta, dict):
        return {}
    out = {}
    if isinstance(meta.get("custom_title"), str) and meta["custom_title"].strip():
        out["custom_title"] = meta["custom_title"]
    if meta.get("pinned"):
        out["pinned"] = True
    if out:
        out.setdefault("custom_title", None)
        out.setdefault("pinned", False)
    return out


def write_user_fields(directory: Path, session_id: str, **fields) -> bool:
    """Merge custom_title / pinned into `<id>.meta.json` (0600, atomic). False when there is no
    meta file to hold them (the session is gone)."""
    path = _meta_path(Path(directory), session_id)
    try:
        with open(path, encoding="utf-8") as handle:
            meta = json.load(handle)
    except (OSError, ValueError):
        return False
    if not isinstance(meta, dict):
        return False
    if "custom_title" in fields:
        title = " ".join(str(fields["custom_title"] or "").split())[:200]
        if title:
            meta["custom_title"] = title
        else:
            meta.pop("custom_title", None)
    if "pinned" in fields:
        if fields["pinned"]:
            meta["pinned"] = True
        else:
            meta.pop("pinned", None)
    fd, temp = tempfile.mkstemp(dir=path.parent, prefix=".session-")
    try:
        with os.fdopen(fd, "w", encoding="utf-8") as out:
            os.fchmod(out.fileno(), 0o600)
            json.dump(meta, out, ensure_ascii=False)
        os.replace(temp, path)
    finally:
        if os.path.exists(temp):
            os.unlink(temp)
    return True


def _meta_updated(path: Path) -> float | None:
    """`updated` of a session from its small meta file (cheaper than the session JSON)."""
    try:
        with open(path.with_name(path.name[:-5] + ".meta.json"), encoding="utf-8") as handle:
            meta = json.load(handle)
        value = meta.get("updated") if isinstance(meta, dict) else None
        return float(value) if isinstance(value, (int, float)) else None
    except (OSError, ValueError):
        return None
