# SPDX-License-Identifier: GPL-3.0-or-later
"""Full-text index of conversations and Relay-run terminal commands (protocol section 14).

An SQLite FTS5 database beside the session files:

    $XDG_DATA_HOME/relay/index.db      0600, in the 0700 relay/ directory

It is a **cache**, never the source of truth: every agent row can be rebuilt from the session JSON
under `relay/sessions/<workspace-digest>/`, so a corrupt database is deleted and recreated.
`SCHEMA_VERSION` is stored in `meta`. Versions 1 and 2 are migrated in place (columns added),
because the terminal-history rows have no file to be rebuilt from; any other mismatch wipes the
tables. A row written by an older schema keeps `indexed_version` behind, and the next
`reconcile()` re-reads it once — that is how a new column is backfilled.
User-set titles and pins of saved sessions live in `<id>.meta.json` (since v2), and a summary made
while the session was not loaded joins them there (since v3); the index only mirrors them.
`reconcile()` brings the rows back in line with the files on disk.

Since v3 a conversation also carries what the session-manager pane shows without opening it: its
summary, first and last prompt, the files it wrote, its branch, its mode and whether it was left
unfinished. Its title and summary are indexed as entries of their own, so they are searchable and
outrank body text, and a query may carry operators (`project:`, `file:`, `has:`, `-word`, …) that
are parsed out before anything reaches FTS5.

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

Two more kinds are **guests** — agent CLIs the user runs in a pane, whose transcripts Relay reads
but never writes (`GUEST_SOURCES`, protocol 26.7):

* `claude`  — one row per claude session (`~/.claude/projects/<cwd-slug>/<session-id>.jsonl`).
* `codex`   — one row per codex rollout (`~/.codex/sessions/**/rollout-*.jsonl`), named from the
              codex threads database.

`backend/relay_core/guest_sessions.py` parses the guests' files and calls `update_guest()`; the
rows are read-only for everyone else, and a rebuild cannot recreate them from the session files,
so `delete_session()` on a guest drops index rows only. Guests are left out of a search unless
`sources` names them, the same way subagent threads are (the sessions pane asks for them).

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
# v3 (2026-09-18): summary/first_prompt/last_prompt/files/branch/unfinished/mode columns, title and
# summary indexed as entries, query operators.
SCHEMA_VERSION = 3
MAX_TEXT = 4000              # per-entry cap for replies and tool output
MAX_PROMPT = 8000            # per-entry cap for user prompts
MAX_SUMMARY = 4000           # per-conversation cap for a summary
MAX_PREVIEW = 300            # cap for first_prompt / last_prompt
MAX_FILES = 50               # touched paths kept per conversation
MAX_TODOS = 10               # todos kept per conversation for the overview
MAX_MATCHES_PER_ITEM = 5     # matching turns returned per conversation (default; up to 20)
MAX_LIMIT = 200
MAX_FACET_VALUES = 30
MAX_ENTRIES_PER_SESSION = 20000
MAX_COMMANDS = 500           # terminal commands accepted in one `terminal_history` message
OVERVIEW_TURNS = 3
OVERVIEW_TEXT = 400
OVERVIEW_FILES = 12
ITEM_FILES = 8               # touched paths carried by a list item
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
    summary      TEXT NOT NULL DEFAULT '',
    first_prompt TEXT NOT NULL DEFAULT '',
    last_prompt  TEXT NOT NULL DEFAULT '',
    files        TEXT NOT NULL DEFAULT '[]',
    files_count  INTEGER NOT NULL DEFAULT 0,
    has_edits    INTEGER NOT NULL DEFAULT 0,
    branch       TEXT NOT NULL DEFAULT '',
    unfinished   INTEGER NOT NULL DEFAULT 0,
    mode         TEXT NOT NULL DEFAULT '',
    todos        TEXT NOT NULL DEFAULT '[]',
    indexed_version INTEGER NOT NULL DEFAULT 0,
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

KINDS = ("title", "summary", "prompt", "reply", "tool_call", "tool_output", "command", "command_output")
# Kinds that describe the conversation rather than sit inside it: they are searchable and rank
# above body text, but the preview lists the messages, not these.
HEADER_KINDS = ("title", "summary")
_HEADER_SQL = ", ".join(f"'{kind}'" for kind in HEADER_KINDS)
SOURCES = ("agent", "terminal", "subagent", "claude", "codex")
# The guest sources (protocol 26.7): their rows are written by guest_sessions.py from the guests'
# own transcripts, never from Relay's session files, so a rebuild leaves them to `guest_reconcile`.
GUEST_SOURCES = ("claude", "codex")
SORTS = ("recent", "oldest", "longest", "relevance")
# `relevance` tiers a conversation by the best kind it matched, then by how many entries matched,
# then by recency: a title hit outranks a summary hit outranks a prompt outranks a reply outranks
# tool or terminal output, however many of the weaker ones there are.
RANK_WEIGHTS = {"title": 6, "summary": 5, "prompt": 4, "command": 4, "reply": 3,
                "tool_call": 2, "tool_output": 1, "command_output": 1}
# The same weights as SQL. Built from our own literals, never from user input.
_KIND_CASE = ("CASE e.kind " + " ".join(f"WHEN '{kind}' THEN {weight}" for kind, weight in RANK_WEIGHTS.items())
              + " ELSE 0 END")
# Columns added by v2; a v1 database gets them with ALTER TABLE instead of being wiped.
V2_COLUMNS = (("owner_session", "TEXT"), ("parent_thread", "TEXT"), ("agent_id", "TEXT NOT NULL DEFAULT ''"),
              ("agent_type", "TEXT NOT NULL DEFAULT ''"), ("spawn_turn", "INTEGER"),
              ("status", "TEXT NOT NULL DEFAULT ''"), ("models", "TEXT NOT NULL DEFAULT '[]'"),
              ("tokens", "INTEGER NOT NULL DEFAULT 0"), ("cost", "REAL"), ("file_mtime", "REAL"))
# Columns added by v3, the same way. A row indexed before v3 has `indexed_version` 0 and is
# re-indexed by the next reconcile(), which is what backfills the new columns.
V3_COLUMNS = (("summary", "TEXT NOT NULL DEFAULT ''"), ("first_prompt", "TEXT NOT NULL DEFAULT ''"),
              ("last_prompt", "TEXT NOT NULL DEFAULT ''"), ("files", "TEXT NOT NULL DEFAULT '[]'"),
              ("files_count", "INTEGER NOT NULL DEFAULT 0"), ("has_edits", "INTEGER NOT NULL DEFAULT 0"),
              ("branch", "TEXT NOT NULL DEFAULT ''"), ("unfinished", "INTEGER NOT NULL DEFAULT 0"),
              ("mode", "TEXT NOT NULL DEFAULT ''"), ("todos", "TEXT NOT NULL DEFAULT '[]'"),
              ("indexed_version", "INTEGER NOT NULL DEFAULT 0"))
MAX_MATCHES_LIMIT = 20
THREAD_KIND = "relay_subagent_thread"

# ----- query operators (v3) -------------------------------------------------------------------
# `key:value` pairs parsed out of the query before anything reaches FTS5. An unknown key is not an
# error: the whole token stays free text, so a path or a URL typed into the box still searches.
OPERATOR_KEYS = ("project", "file", "model", "branch", "before", "after", "has", "is", "in")
OPERATOR_VALUES = {"has": ("tasks", "edits", "summary"), "is": ("pinned", "unfinished"),
                   "in": ("terminal", "agent")}
# Todo states that still want doing; a conversation with one of them is unfinished.
OPEN_TODO_STATES = ("pending", "in_progress", "blocked")


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


# ----- query operators -------------------------------------------------------------------------

_OPERATOR_KEY = re.compile(r"([A-Za-z][A-Za-z0-9_]*):")


def like_value(value: str) -> str:
    r"""A user string as the body of a `LIKE ? ESCAPE '\'` pattern: its own wildcards are literal."""
    escaped = str(value or "").replace("\\", r"\\").replace("%", r"\%").replace("_", r"\_")
    return f"%{escaped.lower()}%"


def parse_date(value: str, now: float | None = None) -> float | None:
    """Epoch seconds for a `before:`/`after:` value: `YYYY-MM-DD`, `today`, `yesterday` or `7d`.

    A calendar date means local midnight at its start, so `after:2026-09-18` includes that whole
    day and `before:2026-09-18` stops where it begins. `Nd` is N days ago from now. None when the
    value is not a date at all — the caller reports it as ignored rather than failing the search.
    """
    text = str(value or "").strip().lower()
    now = time.time() if now is None else float(now)

    def midnight(stamp: float) -> float:
        parts = time.localtime(stamp)
        return time.mktime((parts.tm_year, parts.tm_mon, parts.tm_mday, 0, 0, 0, 0, 0, -1))

    if text == "today":
        return midnight(now)
    if text == "yesterday":
        return midnight(now - 86400)
    days = re.fullmatch(r"(\d{1,5})d", text)
    if days:
        return now - int(days.group(1)) * 86400.0
    stamp = re.fullmatch(r"(\d{4})-(\d{1,2})-(\d{1,2})", text)
    if stamp:
        year, month, day = (int(part) for part in stamp.groups())
        if not (1 <= month <= 12 and 1 <= day <= 31):
            return None
        try:
            return time.mktime((year, month, day, 0, 0, 0, 0, 0, -1))
        except (OverflowError, ValueError):
            return None
    return None


def _scan_tokens(query: str) -> list[dict]:
    """Split a query into `{negated, key, value, raw}` tokens, honouring quotes.

    A leading `-` negates; `key:` is taken only when it is a bare word followed by a colon, and a
    value may be quoted (`file:"my file.py"`). Nothing here interprets the value.
    """
    text = str(query or "")
    out: list[dict] = []
    index, size = 0, len(text)
    while index < size:
        if text[index].isspace():
            index += 1
            continue
        start = index
        negated = False
        if text[index] == "-" and index + 1 < size and not text[index + 1].isspace():
            negated = True
            index += 1
        key = None
        match = _OPERATOR_KEY.match(text, index)
        if match:
            key = match.group(1).lower()
            index = match.end()
        if index < size and text[index] == '"':
            close = text.find('"', index + 1)
            if close < 0:
                value, index = text[index + 1:], size
            else:
                value, index = text[index + 1:close], close + 1
        else:
            stop = index
            while stop < size and not text[stop].isspace():
                stop += 1
            value, index = text[index:stop], stop
        out.append({"negated": negated, "key": key, "value": value, "raw": text[start:index]})
    return out


def parse_query(query: str, now: float | None = None) -> dict:
    """`{text, operators, ignored}` for a query string.

    `text` is what is left for FTS5 once the operators are taken out. `operators` is what the GUI
    draws as chips: `{key, value, negated?}`, where the key `text` means an excluded word or
    phrase (`-pelican`, `-"a phrase"`). `ignored` names tokens that look like an operator but are
    not usable (a known key with an unknown value, an unreadable date); they filter nothing. An
    unknown key is not ignored — the whole token goes back into `text` as the user typed it.
    """
    operators: list[dict] = []
    ignored: list[str] = []
    text_parts: list[str] = []
    for token in _scan_tokens(query):
        key, value, raw = token["key"], token["value"], token["raw"]
        if key in OPERATOR_KEYS and value:
            allowed = OPERATOR_VALUES.get(key)
            if allowed is not None and value.lower() not in allowed:
                ignored.append(raw)
                continue
            if key in ("before", "after") and parse_date(value, now) is None:
                ignored.append(raw)
                continue
            item = {"key": key, "value": value.lower() if allowed else value}
            if token["negated"]:
                item["negated"] = True
            operators.append(item)
            continue
        if key in OPERATOR_KEYS and not value:
            ignored.append(raw)
            continue
        if token["negated"] and (key is None or key not in OPERATOR_KEYS):
            # `-word` / `-"a phrase"` — and `-notakey:value`, whose whole token is excluded text.
            excluded = raw[1:]
            if _WORD.search(excluded):
                operators.append({"key": "text", "value": excluded.strip('"'), "negated": True})
            else:
                ignored.append(raw)
            continue
        text_parts.append(raw)
    return {"text": " ".join(text_parts), "operators": operators, "ignored": ignored}


def negated_parts(operators: list[dict]) -> list[str]:
    """The escaped FTS5 expression of every excluded word or phrase, one per operator."""
    parts: list[str] = []
    for operator in operators:
        if operator.get("key") != "text" or not operator.get("negated"):
            continue
        words = _WORD.findall(operator.get("value") or "")
        if not words:
            continue
        parts.append('"' + " ".join(words) + '"' if len(words) > 1 else '"' + words[0] + '"*')
    return parts


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


def _one_line(text: str, cap: int) -> str:
    """A prompt as one line for a list row: whitespace collapsed, control characters out, capped."""
    return " ".join(_clean(str(text or ""), cap * 4).split())[:cap]


def _checkpoint_items(data: dict) -> list[dict]:
    items = (data.get("checkpoints") or {}).get("items") or []
    return [item for item in items if isinstance(item, dict)]


def session_files(data: dict) -> list[str]:
    """Distinct paths the session wrote, most recently written first.

    A checkpoint records a file before the agent's first write of the turn and stamps the new
    digest (`after`) once the write lands, so `after` is what says the file really changed —
    the same test `CheckpointStore.listing()` uses.
    """
    out: list[str] = []
    seen: set[str] = set()
    for item in reversed(_checkpoint_items(data)):
        files = item.get("files")
        if not isinstance(files, dict):
            continue
        for path, record in files.items():
            if not isinstance(path, str) or not path or path in seen:
                continue
            if isinstance(record, dict) and not record.get("after"):
                continue
            seen.add(path)
            out.append(_clean(path, 1024))
    return out


def session_todos(data: dict) -> list[dict]:
    """`[{text, status}]` for the overview: the ones still open first, then the rest, capped."""
    items = (data.get("todos") or {}).get("items") or []
    rows = [{"text": _one_line(item.get("text"), 200), "status": str(item.get("status") or "")}
            for item in items if isinstance(item, dict) and item.get("text")]
    rows.sort(key=lambda row: 0 if row["status"] in OPEN_TODO_STATES else 1)
    return rows[:MAX_TODOS]


def session_unfinished(data: dict) -> bool:
    """Whether the session looks like it was left mid-flight. True when any of:

    * the last checkpoint has no `ended` stamp, in a session whose checkpoints carry one at all
      (a session saved before that field existed never counts as unfinished on its own);
    * the last message is a user or tool message — nothing answered it;
    * a todo is still pending, in progress or blocked.
    """
    items = _checkpoint_items(data)
    if items and any(item.get("ended") is not None for item in items) and items[-1].get("ended") is None:
        return True
    messages = [m for m in (data.get("messages") or []) if isinstance(m, dict)]
    if messages and messages[-1].get("role") in ("user", "tool"):
        return True
    return any(isinstance(item, dict) and item.get("status") in OPEN_TODO_STATES
               for item in ((data.get("todos") or {}).get("items") or []))


def _derived(data: dict) -> dict:
    """The v3 per-conversation columns read straight out of a session's JSON."""
    prompts = [_one_line(item.get("prompt") or item.get("prompt_preview"), MAX_PREVIEW)
               for item in _checkpoint_items(data)]
    prompts = [p for p in prompts if p]
    files = session_files(data)
    todos = session_todos(data)
    return {"summary": _clean(str(data.get("summary") or ""), MAX_SUMMARY).strip(),
            "first_prompt": prompts[0] if prompts else "",
            "last_prompt": prompts[-1] if prompts else "",
            "files": json.dumps(files[:MAX_FILES]), "files_count": len(files),
            "has_edits": 1 if files else 0,
            "branch": _one_line(data.get("branch"), 200),
            "unfinished": 1 if session_unfinished(data) else 0,
            "mode": _one_line(data.get("mode"), 40),
            "todos": json.dumps(todos, ensure_ascii=False)}


def header_entries(title: str, summary: str) -> list[dict]:
    """The searchable `title` and `summary` entries of a conversation (turn 0, before its text)."""
    rows = []
    for kind, text in (("title", title), ("summary", summary)):
        text = (text or "").strip()
        if text:
            rows.append({"turn": 0, "seq": 0, "kind": kind, "time": None,
                         "text": text[:MAX_SUMMARY]})
    return rows


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
        if version in (1, 2) and version < SCHEMA_VERSION:
            try:
                self._migrate(db, version)
                self.migrated_from = version
                version = SCHEMA_VERSION
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
    def _migrate(db, version: int) -> None:
        """v1/v2 -> v3 in place, because terminal-history rows have no file to be rebuilt from.

        v1 -> v2 adds the thread columns and moves user titles and pins to the session files, where
        they are safe from a cache wipe. v2 -> v3 adds the overview columns; they stay empty until
        the next `reconcile()`, which re-reads every row whose `indexed_version` is behind.
        """
        columns = {row[1] for row in db.execute("PRAGMA table_info(conversations)").fetchall()}
        for name, kind in V2_COLUMNS + V3_COLUMNS:
            if name not in columns:
                db.execute(f"ALTER TABLE conversations ADD COLUMN {name} {kind}")
        if version < 2:
            rows = db.execute("SELECT session_id, session_dir, custom_title, pinned FROM conversations"
                              " WHERE source='agent' AND (custom_title IS NOT NULL OR pinned != 0)").fetchall()
            for row in rows:
                if row["session_dir"]:
                    write_user_fields(Path(row["session_dir"]), row["session_id"],
                                      custom_title=row["custom_title"], pinned=bool(row["pinned"]))
        db.execute("UPDATE conversations SET indexed_version=0 WHERE source IN ('agent', 'subagent')")
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

        derived = _derived(data)

        def work(db):
            keep = db.execute("SELECT custom_title, pinned, summary FROM conversations WHERE session_id=?",
                              (session_id,)).fetchone()
            # The session files hold user titles and pins (since v2); an older caller that does not
            # pass them keeps what the row had.
            previous_summary = (keep["summary"] if keep else "") or ""
            if "custom_title" in data or "pinned" in data:
                keep = {"custom_title": data.get("custom_title") or None, "pinned": 1 if data.get("pinned") else 0}
            # A summary set while the session was not loaded (set_summary, or the meta file) is not
            # in this autosave's JSON: keep it rather than lose it to a save that never had one.
            summary = derived["summary"] or previous_summary
            db.execute("DELETE FROM entries WHERE session_id=?", (session_id,))
            tokens, cost = _usage_tokens(data)
            db.execute(
                "INSERT OR REPLACE INTO conversations(session_id, source, workspace, project, title, custom_title,"
                " model, preset, created, updated, turns, open_requests, session_dir, pinned, models, tokens, cost,"
                " summary, first_prompt, last_prompt, files, files_count, has_edits, branch, unfinished, mode,"
                " todos, indexed_version)"
                " VALUES(?,'agent',?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)",
                (session_id, workspace, project_name(workspace), title,
                 keep["custom_title"] if keep else None,
                 str(data.get("model") or ""), str(data.get("preset") or ""),
                 data.get("created"), data.get("updated") or time.time(),
                 int(data.get("turns") or 0), int(data.get("open_requests") or 0),
                 str(session_dir or ""), int(keep["pinned"]) if keep else 0, _models(data), tokens, cost,
                 summary, derived["first_prompt"], derived["last_prompt"], derived["files"],
                 derived["files_count"], derived["has_edits"], derived["branch"], derived["unfinished"],
                 derived["mode"], derived["todos"], SCHEMA_VERSION))
            written = header_entries((keep["custom_title"] if keep else None) or title, summary) + rows
            db.executemany(
                "INSERT INTO entries(session_id, turn, seq, kind, time, text) VALUES(?,?,?,?,?,?)",
                [(session_id, row["turn"], row["seq"], row["kind"], row["time"], row["text"]) for row in written])
            db.commit()
            return len(rows)
        return self._run(work)

    def set_summary(self, session_id: str, summary: str) -> None:
        """Store (or clear) a conversation's summary and its searchable entry, without re-reading
        the session file. This is what the summariser calls for a session nobody has loaded."""
        summary = _clean(str(summary or ""), MAX_SUMMARY).strip()

        def work(db):
            changed = db.execute("UPDATE conversations SET summary=? WHERE session_id=?",
                                 (summary, session_id)).rowcount
            if not changed:
                return                      # no such conversation: leave no orphan entry behind
            db.execute("DELETE FROM entries WHERE session_id=? AND kind='summary'", (session_id,))
            if summary:
                db.execute("INSERT INTO entries(session_id, turn, seq, kind, time, text)"
                           " VALUES(?,0,0,'summary',NULL,?)", (session_id, summary))
            db.commit()
        self._run(work)

    def update_guest(self, data: dict) -> int:
        """Index (or re-index) one guest session (protocol 26.7) from a parsed record.

        `data` is what `guest_sessions.parse_*` produce: `{source, id, file, title, title_kind,
        workspace, created, mtime, message_count, entries}`. The rows are the guests' own
        transcripts mirrored — `file_mtime` is the transcript's mtime, which is what makes the
        next reconcile cheap — and `session_dir` stays empty, because the guests' directories are
        not Relay's to write. A user's pin (or rename) survives a re-index, as it does for the
        other sources.

        `custom_title` and `pinned` in `data` override what the row holds, **one key at a time**:
        a caller that renames a session says only `custom_title` and the pin it did not mention
        stays on, and a caller that pins says only `pinned` and keeps the name the user gave.
        """
        source = str(data.get("source") or "")
        session_id = str(data.get("id") or "")
        if source not in GUEST_SOURCES:
            raise ValueError(f"A guest source must be one of {', '.join(GUEST_SOURCES)}.")
        if not session_id:
            raise ValueError("A guest session has no id.")
        workspace = normalize_workspace(str(data.get("workspace") or ""))
        title = _one_line(data.get("title"), 200)
        rows = [row for row in data.get("entries") or [] if isinstance(row, dict) and row.get("text")]
        mtime = data.get("mtime")
        mtime = float(mtime) if isinstance(mtime, (int, float)) and not isinstance(mtime, bool) else None

        def work(db):
            stored = db.execute("SELECT custom_title, pinned FROM conversations WHERE session_id=?",
                                (session_id,)).fetchone()
            # Merge per key: whichever of the two `data` does not mention keeps the stored value,
            # so a rename does not clear the pin and a pin does not clear the rename.
            custom_title = stored["custom_title"] if stored else None
            pinned = int(stored["pinned"] or 0) if stored else 0
            if "custom_title" in data:
                custom_title = data.get("custom_title") or None
            if "pinned" in data:
                pinned = 1 if data.get("pinned") else 0
            db.execute("DELETE FROM entries WHERE session_id=?", (session_id,))
            db.execute(
                "INSERT OR REPLACE INTO conversations(session_id, source, workspace, project, title, custom_title,"
                " model, preset, created, updated, turns, open_requests, session_dir, pinned, file_mtime,"
                " indexed_version)"
                " VALUES(?,?,?,?,?,?, '', '', ?, ?, ?, 0, '', ?, ?, ?)",
                (session_id, source, workspace, project_name(workspace), title, custom_title,
                 data.get("created") or mtime, mtime or time.time(),
                 max(0, int(data.get("message_count") or 0)),
                 pinned, mtime, SCHEMA_VERSION))
            written = header_entries(custom_title or title, "") + rows
            db.executemany(
                "INSERT INTO entries(session_id, turn, seq, kind, time, text) VALUES(?,?,?,?,?,?)",
                [(session_id, int(row.get("turn") or 0), int(row.get("seq") or 0),
                  str(row.get("kind") or "reply"), row.get("time"),
                  _clean(str(row.get("text") or ""), MAX_PROMPT)) for row in written])
            db.commit()
            return len(rows)
        return self._run(work)

    def guest_file_stamps(self, sources=GUEST_SOURCES) -> dict[str, tuple[str, float, float | None]]:
        """`{session_id: (source, updated, file_mtime)}` of the guest rows, for `reconcile()`:
        what is indexed and how fresh, without reading a transcript."""
        wanted = [source for source in sources if source in GUEST_SOURCES]
        if not wanted:
            return {}
        placeholders = ",".join("?" * len(wanted))
        return self._run(lambda db: {
            row["session_id"]: (row["source"], row["updated"] or 0, row["file_mtime"])
            for row in db.execute("SELECT session_id, source, updated, file_mtime FROM conversations"
                                  f" WHERE source IN ({placeholders})", wanted).fetchall()})

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
                " parent_thread, agent_id, agent_type, spawn_turn, status, models, tokens, cost, file_mtime,"
                " indexed_version)"
                " VALUES(?,'subagent',?,?,?,?,?,'',?,?,?,0,?,?,?,?,?,?,?,?,?,?,?,?,?)",
                (thread_id, workspace, project_name(workspace), title, keep["custom_title"] if keep else None,
                 str(data.get("model") or ""), data.get("created"), data.get("updated") or time.time(),
                 max(1, runs), str(session_dir or ""), int(keep["pinned"]) if keep else 0, owner, parent,
                 str(data.get("agent_id") or ""), str(data.get("type") or ""), spawn_turn,
                 str(data.get("status") or ""), _models(data), tokens, cost, file_mtime, SCHEMA_VERSION))
            written = header_entries((keep["custom_title"] if keep else None) or title, "") + rows
            db.executemany(
                "INSERT INTO entries(session_id, turn, seq, kind, time, text) VALUES(?,?,?,?,?,?)",
                [(thread_id, row["turn"], row["seq"], row["kind"], row["time"], row["text"]) for row in written])
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
        merged = {**data, **read_user_fields(path.parent, str(data["id"]))}
        # The session's own summary is the newer one; the meta file only carries one for a session
        # summarised while nobody had it open.
        if data.get("summary"):
            merged["summary"] = data["summary"]
        self.update_session(merged, path.parent)
        return True

    def reconcile(self, root: str | Path | None = None) -> dict:
        """Bring the rows in line with the files: index sessions and threads that are missing or
        newer on disk than in the index, and drop rows whose file is gone. Cheap when nothing
        changed (one stat per file and one small meta read per session), so it runs whenever a
        worker first opens the index; `rebuild()` is still there for a full refresh."""
        started = time.time()
        directory = Path(root) if root else sessions_root()
        known = self._run(lambda db: {row["session_id"]: (row["source"], row["updated"] or 0, row["session_dir"],
                                                          row["file_mtime"], row["indexed_version"] or 0)
                                      for row in db.execute("SELECT session_id, source, updated, session_dir,"
                                                            " file_mtime, indexed_version"
                                                            " FROM conversations WHERE source IN"
                                                            " ('agent', 'subagent')").fetchall()})
        seen: set[str] = set()
        added = refreshed = backfilled = 0
        if directory.is_dir():
            for path in directory.glob("*/*.json"):
                name = path.name
                if name.endswith(".meta.json") or name.startswith("."):
                    continue
                session_id = name[:-5]
                seen.add(session_id)
                row = known.get(session_id)
                stamp = _meta_updated(path)
                # A row written before the current schema has empty new columns whatever its
                # mtime says, so it is read again once and then left alone.
                stale = row is not None and int(row[4]) < SCHEMA_VERSION
                if row is not None and not stale and stamp is not None and stamp <= float(row[1]) + 1e-6 \
                        and row[2] == str(path.parent):
                    continue
                if self.index_session_file(path):
                    added += row is None
                    refreshed += row is not None
                    backfilled += bool(stale)
            for path in directory.glob("*/*.threads/*.json"):
                thread_id = path.name[:-5]
                seen.add(thread_id)
                row = known.get(thread_id)
                try:
                    mtime = path.stat().st_mtime
                except OSError:
                    continue
                stale = row is not None and int(row[4]) < SCHEMA_VERSION
                if row is not None and not stale and row[3] is not None and abs(mtime - float(row[3])) < 1e-6:
                    continue
                if self.index_thread_file(path):
                    added += row is None
                    refreshed += row is not None
                    backfilled += bool(stale)
        gone = [sid for sid, (_source, _updated, folder, _mtime, _version) in known.items()
                if sid not in seen and (not folder or Path(folder).parent == directory or
                                        Path(folder).parent.parent == directory)]

        def drop(db):
            for sid in gone:
                db.execute("DELETE FROM entries WHERE session_id=?", (sid,))
                db.execute("DELETE FROM conversations WHERE session_id=?", (sid,))
            db.commit()
        if gone:
            self._run(drop)
        return {"added": added, "refreshed": refreshed, "removed": len(gone), "backfilled": backfilled,
                "ms": int((time.time() - started) * 1000)}

    def rebuild(self, root: str | Path | None = None) -> dict:
        """Drop every agent conversation and rebuild it from the session JSON files.

        Terminal history has no file to rebuild from, so its rows are kept; so are the guest
        rows (protocol 26.7), which have no session file either — `guest_sessions.reconcile()`
        keeps those in line with the guests' own transcripts.
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
                " model, preset, created, updated, turns, open_requests, session_dir, pinned, indexed_version)"
                " VALUES(?,'terminal',?,?,?,(SELECT custom_title FROM conversations WHERE session_id=?),"
                " '', '', ?, ?, ?, 0, '', COALESCE((SELECT pinned FROM conversations WHERE session_id=?), 0),"
                f" {SCHEMA_VERSION})",
                (session_id, workspace, project_name(workspace),
                 "Terminal · " + project_name(workspace), session_id, created, newest, commands, session_id))
            db.executemany(
                "INSERT INTO entries(session_id, turn, seq, kind, time, status, text) VALUES(?,?,?,?,?,?,?)", payload)
            db.commit()
            return len(payload)
        return self._run(work)

    def delete_session(self, session_id: str, *, remove_files: bool = True) -> dict:
        """Remove a conversation's index rows and, for agent sessions, its files and blobs.

        A guest session (protocol 26.7) has no Relay file to delete: its rows go and the
        transcript the guest owns stays, so `remove_files` never touches it.
        """
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
            # The searchable title entry follows the name the list shows, so a renamed conversation
            # is found under its new name straight away.
            row = db.execute("SELECT title FROM conversations WHERE session_id=?", (session_id,)).fetchone()
            shown = title or (row["title"] if row else "")
            db.execute("DELETE FROM entries WHERE session_id=? AND kind='title'", (session_id,))
            if shown:
                db.execute("INSERT INTO entries(session_id, turn, seq, kind, time, text)"
                           " VALUES(?,0,0,'title',NULL,?)", (session_id, shown[:MAX_SUMMARY]))
            db.commit()
        self._run(work)

    def set_pinned(self, session_id: str, pinned: bool) -> None:
        def work(db):
            db.execute("UPDATE conversations SET pinned=? WHERE session_id=?", (1 if pinned else 0, session_id))
            db.commit()
        self._run(work)

    # ----- reading ---------------------------------------------------------------------
    def _filters(self, *, scope: str, workspace: str | None, model: str | None, has_open: bool,
                 since, until, kinds, has_edits=None, unfinished=None, pinned=None, file=None,
                 branch=None, has_summary=None, operators=(), now=None) -> tuple[str, list]:
        """The WHERE clause of the explicit filter arguments and the query's operators.

        Everything the user typed arrives as a bound parameter; LIKE patterns have their own
        wildcards escaped, so no input reaches SQLite as syntax.
        """
        where, params = [], []

        def add(sql: str, *values, negated: bool = False) -> None:
            where.append("NOT (" + sql + ")" if negated else sql)
            params.extend(values)

        if scope == "project":
            add("c.workspace = ?", normalize_workspace(str(workspace or "")))
        if model:
            add("c.model = ?", model)
        if has_open:
            add("c.open_requests > 0")
        if isinstance(since, (int, float)):
            add("COALESCE(c.updated, 0) >= ?", float(since))
        if isinstance(until, (int, float)):
            add("COALESCE(c.updated, 0) <= ?", float(until))
        if kinds:
            add("c.source IN (%s)" % ",".join("?" * len(kinds)), *kinds)
        for flag, column in ((has_edits, "c.has_edits"), (unfinished, "c.unfinished"), (pinned, "c.pinned")):
            if flag is not None:
                add(f"{column} = ?", 1 if flag else 0)
        if has_summary is not None:
            add("c.summary != ''" if has_summary else "c.summary = ''")
        if file:
            add(r"lower(c.files) LIKE ? ESCAPE '\'", like_value(file))
        if branch:
            add(r"lower(c.branch) LIKE ? ESCAPE '\'", like_value(branch))
        for operator in operators or ():
            key, value = operator.get("key"), operator.get("value") or ""
            negated = bool(operator.get("negated"))
            if key == "project":
                add(r"(lower(c.project) LIKE ? ESCAPE '\' OR lower(c.workspace) LIKE ? ESCAPE '\')",
                    like_value(value), like_value(value), negated=negated)
            elif key in ("file", "model", "branch"):
                add(r"lower(c.%s) LIKE ? ESCAPE '\'" % ("files" if key == "file" else key),
                    like_value(value), negated=negated)
            elif key == "after":
                add("COALESCE(c.updated, 0) >= ?", parse_date(value, now), negated=negated)
            elif key == "before":
                add("COALESCE(c.updated, 0) < ?", parse_date(value, now), negated=negated)
            elif key == "has":
                add({"tasks": "c.open_requests > 0", "edits": "c.has_edits = 1",
                     "summary": "c.summary != ''"}[value], negated=negated)
            elif key == "is":
                add({"pinned": "c.pinned = 1", "unfinished": "c.unfinished = 1"}[value], negated=negated)
            elif key == "in":
                add("c.source = ?", value, negated=negated)
        return (" AND ".join(where), params)

    def _facets(self, db, clause: str, params: list) -> dict:
        """Distinct `models`, `branches` and `projects` of the filtered rows, most recent first.

        The GUI fills its filter menus from this instead of asking a second time; the query itself
        is not applied, so the menus do not empty out as the user types.
        """
        out = {}
        for name, column in (("models", "model"), ("branches", "branch"), ("projects", "project")):
            sql = (f"SELECT c.{column} AS value, MAX(COALESCE(c.updated, 0)) AS last FROM conversations c"
                   f" WHERE c.{column} IS NOT NULL AND c.{column} != ''")
            if clause:
                sql += " AND " + clause
            sql += f" GROUP BY c.{column} ORDER BY last DESC LIMIT {MAX_FACET_VALUES}"
            out[name] = [row["value"] for row in db.execute(sql, params).fetchall()]
        return out

    def search(self, query: str = "", *, scope: str = "project", workspace: str | None = None,
               model: str | None = None, has_open: bool = False, since=None, until=None,
               sources=None, limit: int = 50, include_threads: bool = False, sort: str = "recent",
               offset: int = 0, matches_per_item: int = MAX_MATCHES_PER_ITEM,
               has_edits: bool | None = None, unfinished: bool | None = None,
               pinned: bool | None = None, file: str | None = None, branch: str | None = None,
               has_summary: bool | None = None, now: float | None = None) -> dict:
        """Conversations matching `query`, each with its matching turns.

        The query may carry operators (`project:`, `file:`, `model:`, `branch:`, `before:`,
        `after:`, `has:`, `is:`, `in:`, and `-word` to exclude); `parse_query` takes them out and
        what is left is the free text. They stack with the explicit filter arguments, which are the
        same conditions by another route. The result's `parsed` says what was understood.

        Words are an AND over the whole conversation, not over one message: a conversation matches
        when every word or phrase occurs somewhere in it, and an excluded word must occur nowhere
        in it. `sort` is "recent" (default), "oldest", "longest" (most turns) or "relevance" (the
        best kind matched first — title, then summary, then prompt, then reply, then tool and
        terminal output — then the number of matching entries, then recency); pinned rows come
        first in every order. `offset` pages through the list; `next_offset` is set when there is
        more. Subagent threads are rows too, but only with `include_threads` (or "subagent" named
        in `sources`): the list is about sessions, and threads are what the checkbox adds."""
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
        parsed = parse_query(query, now)
        operators = parsed["operators"]
        # Naming a project is asking about that project, wherever the pane happens to be.
        if any(op.get("key") == "project" for op in operators):
            scope = "all"
        kinds = [s for s in (sources or []) if s in SOURCES]
        if not kinds:
            kinds = ["agent", "terminal"]
        if include_threads and "subagent" not in kinds:
            kinds.append("subagent")
        clause, params = self._filters(
            scope=scope, workspace=workspace, model=model, has_open=has_open, since=since, until=until,
            kinds=kinds, has_edits=has_edits, unfinished=unfinished, pinned=pinned, file=file,
            branch=branch, has_summary=has_summary, operators=operators, now=now)
        started = time.time()
        parts = fts_parts(parsed["text"])
        terms = query_terms(parsed["text"])
        excluded = negated_parts(operators)
        # An excluded word must be nowhere in the conversation, so it is a clause, not an FTS term.
        full_clause, full_params = clause, list(params)
        for part in excluded:
            full_clause = (full_clause + " AND " if full_clause else "") + (
                "c.session_id NOT IN (SELECT e2.session_id FROM entries_fts JOIN entries e2"
                " ON e2.id = entries_fts.rowid WHERE entries_fts MATCH ?)")
            full_params.append(part)
        order = {"recent": "COALESCE(c.updated, 0) DESC", "oldest": "COALESCE(c.updated, 0) ASC",
                 "longest": "c.turns DESC, COALESCE(c.updated, 0) DESC",
                 "relevance": "best DESC, hits DESC, COALESCE(c.updated, 0) DESC"}[sort]

        def work(db):
            if parts:
                any_part = " OR ".join(parts)
                # Every part somewhere in the conversation: one set of conversations per part,
                # intersected. Then count the entries that match any part, in SQLite.
                every = " INTERSECT ".join(
                    "SELECT e.session_id FROM entries_fts JOIN entries e ON e.id = entries_fts.rowid"
                    " WHERE entries_fts MATCH ?" for _ in parts)
                sql = (f"SELECT e.session_id AS sid, count(*) AS hits, MAX({_KIND_CASE}) AS best, c.*"
                       " FROM entries_fts"
                       " JOIN entries e ON e.id = entries_fts.rowid"
                       " JOIN conversations c ON c.session_id = e.session_id"
                       f" WHERE entries_fts MATCH ? AND e.session_id IN ({every})")
                args: list = [any_part, *parts]
                if full_clause:
                    sql += " AND " + full_clause
                    args += full_params
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
                        f" ORDER BY {_KIND_CASE} DESC, bm25(entries_fts) LIMIT ?",
                        [any_part, *by_id, len(by_id) * per_item * 4]).fetchall()
                    for line_row in lines:
                        item = by_id[line_row["session_id"]]
                        if len(item["matches"]) >= per_item:
                            continue
                        line, ranges = match_line(line_row["text"], terms)
                        item["matches"].append({"turn": line_row["turn"], "kind": line_row["kind"],
                                                "line": line, "ranges": ranges, "time": line_row["time"]})
            else:
                sql = "SELECT c.*, 0 AS hits, 0 AS best FROM conversations c"
                args = []
                if full_clause:
                    sql += " WHERE " + full_clause
                    args += full_params
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
                    item["snippet"] = item["summary"][:200] or item["first_prompt"][:200]
                if not item["snippet"]:
                    first = db.execute(
                        f"SELECT text FROM entries WHERE session_id=? AND kind NOT IN ({_HEADER_SQL})"
                        " ORDER BY seq LIMIT 1", (item["session_id"],)).fetchone()
                    item["snippet"] = " ".join((first["text"] if first else "").split())[:200]
                # Title and summary matches carry turn 0, so they lead the list.
                item["matches"].sort(key=lambda m: m["turn"])
            total = db.execute("SELECT count(*) FROM conversations" + (" c WHERE " + clause if clause else ""),
                               params).fetchone()[0]
            return out, total, more, self._facets(db, clause, params)
        items, total, more, facets = self._run(work)
        result = {"items": items, "total": total, "query": query, "sort": sort, "offset": offset,
                  "scope": scope, "parsed": parsed, "facets": facets,
                  "elapsed_ms": int((time.time() - started) * 1000)}
        if more:
            result["next_offset"] = offset + len(items)
        return result

    @staticmethod
    def _overview(db, row) -> dict:
        """What the pane shows before the transcript: the summary, the first prompt, the last few
        turns, the files touched, the open todos, the branch and whether it was left unfinished.

        It is read from the conversation's own row and a handful of its entries, so opening the
        preview never reads the session JSON back off disk."""
        session_id = row["session_id"]
        last_turns: list[dict] = []
        top = db.execute(f"SELECT MAX(turn) FROM entries WHERE session_id=? AND kind NOT IN ({_HEADER_SQL})",
                         (session_id,)).fetchone()[0]
        if top:
            first_wanted = max(1, int(top) - OVERVIEW_TURNS + 1)
            rows = db.execute(
                "SELECT turn, kind, text FROM entries WHERE session_id=? AND turn >= ?"
                " AND kind IN ('prompt', 'reply', 'command', 'command_output') ORDER BY turn, seq",
                (session_id, first_wanted)).fetchall()
            by_turn: dict[int, dict] = {}
            for entry in rows:
                slot = by_turn.setdefault(entry["turn"], {"turn": entry["turn"], "prompt": "", "reply": ""})
                field = "prompt" if entry["kind"] in ("prompt", "command") else "reply"
                if not slot[field]:
                    slot[field] = _one_line(entry["text"], OVERVIEW_TEXT)
            last_turns = [by_turn[key] for key in sorted(by_turn)][-OVERVIEW_TURNS:]
        todos = []
        for todo in _json_list_of_dicts(row["todos"]):
            todos.append({"text": str(todo.get("text") or ""), "status": str(todo.get("status") or "")})
        return {"summary": row["summary"] or "", "first_prompt": row["first_prompt"] or "",
                "last_turns": last_turns, "files": _json_list(row["files"])[:OVERVIEW_FILES],
                "files_count": row["files_count"] or 0, "todos": todos[:MAX_TODOS],
                "branch": row["branch"] or "", "unfinished": bool(row["unfinished"])}

    def conversation(self, session_id: str, turn: int | None = None, query: str = "",
                     limit: int = 400) -> dict:
        """One conversation for the preview: its header, its `overview` and its entries (optionally
        one turn). The searchable `title` and `summary` entries are not messages, so they are not
        listed; the overview carries the summary instead."""
        limit = max(1, min(int(limit or 400), 2000))
        terms = query_terms(query)

        def work(db):
            row = db.execute("SELECT * FROM conversations WHERE session_id=?", (session_id,)).fetchone()
            if row is None:
                raise ValueError("No indexed conversation with that id.")
            sql = ("SELECT turn, kind, time, status, text FROM entries"
                   f" WHERE session_id=? AND kind NOT IN ({_HEADER_SQL})")
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
            header["overview"] = self._overview(db, row)
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
            # v3: what the list row and the inline preview show without opening the session.
            "summary": row["summary"] or "", "first_prompt": row["first_prompt"] or "",
            "last_prompt": row["last_prompt"] or "",
            "files": _json_list(row["files"])[:ITEM_FILES], "files_count": row["files_count"] or 0,
            "has_edits": bool(row["has_edits"]), "branch": row["branch"] or "",
            "unfinished": bool(row["unfinished"]), "mode": row["mode"] or "",
            "snippet": "", "matches": [], "match_count": 0}


def _json_list(text) -> list:
    try:
        value = json.loads(text or "[]")
    except ValueError:
        return []
    return [v for v in value if isinstance(v, str)] if isinstance(value, list) else []


def _json_list_of_dicts(text) -> list[dict]:
    try:
        value = json.loads(text or "[]")
    except ValueError:
        return []
    return [v for v in value if isinstance(v, dict)] if isinstance(value, list) else []


# ----- user fields in the session files (since v2) --------------------------------------------

def _meta_path(directory: Path, session_id: str) -> Path:
    return Path(directory) / f"{session_id}.meta.json"


def read_user_fields(directory: str | Path, session_id: str) -> dict:
    """{custom_title, pinned, summary} from `<id>.meta.json`; empty when the file has none of them.

    `summary` is there for a session summarised while it was not loaded: the session JSON has no
    summary of its own then, so the meta file is what a rebuild reads it back from. A summary in
    the session JSON is the newer one and wins (`update_session` merges in that order).
    """
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
    if isinstance(meta.get("summary"), str) and meta["summary"].strip():
        out["summary"] = meta["summary"]
    if out:
        out.setdefault("custom_title", None)
        out.setdefault("pinned", False)
    return out


def write_user_fields(directory: Path, session_id: str, **fields) -> bool:
    """Merge custom_title / pinned / summary into `<id>.meta.json` (0600, atomic). False when there
    is no meta file to hold them (the session is gone)."""
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
    if "summary" in fields:
        summary = _clean(str(fields["summary"] or ""), MAX_SUMMARY).strip()
        if summary:
            meta["summary"] = summary
        else:
            meta.pop("summary", None)
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
