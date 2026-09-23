# SPDX-License-Identifier: AGPL-3.0-or-later
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

A guest row has no file of Relay's beside it, so since v4 three things live outside the tables
that would otherwise be lost with them:

* `guest-meta.json` beside the database (`GUEST_META_NAME`, 0600) holds the pin, the custom title
  and the forgotten flag the user set on a guest row, `{source: {id: {...}}}`. The database is a
  cache that a schema bump or a corruption throws away; a guest row cannot be rebuilt from a file
  of Relay's, so without this the pin and the name the user gave it went with it.
* `guest_forgotten` is the same forgotten set as a table, seeded from that file whenever the
  database is (re)created, so `update_guest()` can skip a deleted session without a file read.
* `guest_files` is where each transcript has been read to — `(path, size, mtime_ns, read_to)` plus
  the parser's own state — so a reconcile parses only the bytes a guest appended instead of the
  whole file (the largest transcript on the machine this was written on is 99 MB).

Since v6 a conversation also carries the text that is not in its message list (card #0TJ9): the
terminal text the pane showed, and what a rewind undid. The GUI writes three sidecar files beside
`<session_dir>/<id>.json` and this module only ever reads them —

* `<id>.scrollback.txt`          the pane's saved terminal text, plain UTF-8, capped at 5 000
                                 lines / 512 KiB; indexed in ~40-line chunks as `terminal_text`.
* `<id>.rewound.jsonl`           one JSON record per rewind, newest 20 kept, holding the dropped
                                 messages verbatim; indexed as `rewound`, at the record's turn.
* `<id>.rewound-<n>.scrollback.txt`  the terminal text rewind `<n>` undid; `rewound` as well.

A guest has no Relay session directory, so its one sidecar lives in Relay's own tree instead:
`relay/sessions/guests/<source>/<id>.scrollback.txt` (protocol 26.7 keeps Relay out of `~/.claude`
and `~/.codex`; `guest-meta.json` is the same precedent). `sessions/guests/` is therefore **not** a
workspace-digest directory, and nothing that walks `sessions/*/` may take it for one.

Both kinds rank below message text and are left out of the message counts, the first and last
prompt and the overview: they are what the conversation had around it, not what it said. The GUI
writes them when a pane's session changes and at quit, which is after the last autosave, so a
sidecar moves while the session JSON does not: `reconcile()` stats them in the same directory
listing the session files already cost, and `session_sidecars` records what it read, so an
unchanged one is never read again.

The index holds message text, so it stays on this machine: same 0700 directory as the sessions,
never synced, and deleting a conversation deletes its rows.
"""
from __future__ import annotations

import hashlib
import json
import os
from .filelock import chmod_fd
import re
import sqlite3
import tempfile
import threading
import time
from pathlib import Path

from .presets import model_name as _model_name

# v2 (2026-09-18, cards #Y63Z/#R6J0): subagent threads, owner/parent links, models and usage totals.
# v3 (2026-09-18): summary/first_prompt/last_prompt/files/branch/unfinished/mode columns, title and
# summary indexed as entries, query operators.
# v4 (2026-09-19, GT7X): `raw_cwd` (the working directory a guest transcript names, as written —
# what `claude -r` has to be run in), the `guest_forgotten` and `guest_files` tables, and the
# `guest-meta.json` store beside the database. v1/v2/v3 all migrate in place.
# v5 (2026-09-20, #TZWF): `entry_count` and `entry_digest` — what an autosave compares itself
# against to write only the turns that were added instead of the whole conversation again. v4
# migrates in place and the two columns fill themselves at the next save of each conversation.
# v6 (2026-09-20, #0TJ9): the `terminal_text` and `rewound` entry kinds and the `session_sidecars`
# table that records what the files they come from looked like when they were read. No new column;
# v1–v5 migrate in place and the rows are backfilled by the next `reconcile()`, as v3's were.
# v7 (2026-09-23, #D2PX): link Relay sessions to the guest transcripts they wrap.
# v8 (2026-09-23, #K9QA): mark native guest transcripts launched by Relay, including subagents.
SCHEMA_VERSION = 8
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
# When a rebuild is worth following with a VACUUM: one page in this many must be free, and there
# must be at least this many bytes of them. Below either, rewriting a 100 MB file to give back a
# few pages is not a trade (#TZWF).
VACUUM_FREE_IN = 10
VACUUM_MIN_BYTES = 16 * 1024 * 1024
OVERVIEW_TURNS = 3
OVERVIEW_TEXT = 400
OVERVIEW_FILES = 12
ITEM_FILES = 8               # touched paths carried by a list item
TERMINAL_ID = re.compile(r"^term-[0-9a-f]{16}$")
_WORD = re.compile(r"[^\W_]+", re.UNICODE)
# ----- the sidecars beside a session file (v6, #0TJ9) -----------------------------------------
# The GUI writes all three; this module only reads them. See the module docstring for the shape.
SCROLLBACK_SUFFIX = ".scrollback.txt"       # <id>.scrollback.txt, and <id>.rewound-<n>.scrollback.txt
REWOUND_SUFFIX = ".rewound.jsonl"           # <id>.rewound.jsonl
GUESTS_DIRNAME = "guests"                   # sessions/guests/<source>/<id>.scrollback.txt
_REWOUND_TEXT = re.compile(r"^(?P<id>.+)\.rewound-(?P<n>\d+)\.scrollback\.txt$")
MAX_SCROLLBACK_LINES = 5000                 # the caps the GUI writes under, applied again here
MAX_SCROLLBACK_BYTES = 512 * 1024           # because a file in that directory is not ours to trust
MAX_REWOUND_BYTES = 4 * 1024 * 1024         # a rewind record holds the dropped messages verbatim
MAX_REWOUND_RECORDS = 20                    # newest kept, as the GUI keeps them
TERMINAL_TEXT_LINES = 40                    # lines per `terminal_text` entry
# Sidecar entries share the `entries` table with the conversation's messages. Their `seq` starts
# past anything a session can hold, so inside a turn they sort after the messages, never among them.
SIDECAR_SEQ = 1_000_000
# Context blocks Relay adds to a user message (agent.CONTEXT_OPEN); not something the user typed.
RELAY_CONTEXT = "[Relay context: added by Relay, not typed by the user]"

SCHEMA = """
CREATE TABLE IF NOT EXISTS meta(key TEXT PRIMARY KEY, value TEXT);
CREATE TABLE IF NOT EXISTS conversations(
    session_id   TEXT PRIMARY KEY,
    source       TEXT NOT NULL DEFAULT 'agent',
    workspace    TEXT NOT NULL DEFAULT '',
    raw_cwd      TEXT NOT NULL DEFAULT '',
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
    file_mtime   REAL,
    entry_count  INTEGER NOT NULL DEFAULT 0,
    entry_digest TEXT NOT NULL DEFAULT '',
    guest_source TEXT NOT NULL DEFAULT '',
    guest_session TEXT NOT NULL DEFAULT '',
    relay_launched INTEGER NOT NULL DEFAULT 0
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
CREATE TABLE IF NOT EXISTS guest_forgotten(
    source     TEXT NOT NULL,
    session_id TEXT NOT NULL,
    at         REAL,
    PRIMARY KEY(source, session_id)
);
CREATE TABLE IF NOT EXISTS guest_files(
    session_id TEXT PRIMARY KEY,
    source     TEXT NOT NULL DEFAULT '',
    path       TEXT NOT NULL DEFAULT '',
    size       INTEGER NOT NULL DEFAULT 0,
    mtime_ns   INTEGER NOT NULL DEFAULT 0,
    read_to    INTEGER NOT NULL DEFAULT 0,
    state      TEXT NOT NULL DEFAULT '{}'
);
CREATE TABLE IF NOT EXISTS session_sidecars(
    session_id TEXT PRIMARY KEY,
    stamp      TEXT NOT NULL DEFAULT ''
);
"""

KINDS = ("title", "summary", "prompt", "reply", "tool_call", "tool_output", "command",
         "command_output", "terminal_text", "rewound")
# Kinds that describe the conversation rather than sit inside it: they are searchable and rank
# above body text, but the preview lists the messages, not these.
HEADER_KINDS = ("title", "summary")
_HEADER_SQL = ", ".join(f"'{kind}'" for kind in HEADER_KINDS)
# The other way round (v6, #0TJ9): what was *around* the conversation rather than in it — the
# terminal text the pane showed (`terminal_text`, so named because the `terminal` source is
# something else: the commands Relay ran) and what a rewind undid (`rewound`). They are searchable
# and rank below every kind of message text, and they come from the sidecars, not from the session
# JSON, so every write that rewrites a conversation's own rows leaves them where they are.
SIDECAR_KINDS = ("terminal_text", "rewound")
_SIDECAR_SQL = ", ".join(f"'{kind}'" for kind in SIDECAR_KINDS)
# Neither a header nor a message: what "the conversation's messages" means in SQL. Turn counts,
# the overview and the list snippet all use it, so a 512 KiB scrollback cannot become a snippet or
# push the last turn past the end of the conversation.
_NOT_BODY_SQL = ", ".join(f"'{kind}'" for kind in HEADER_KINDS + SIDECAR_KINDS)
# What the preview leaves out by default. `rewound` is listed — it is conversation, just dropped —
# and saved terminal text is not, unless the query matched inside it (see `conversation`).
_UNLISTED_SQL = ", ".join(f"'{kind}'" for kind in HEADER_KINDS + ("terminal_text",))
SOURCES = ("agent", "terminal", "subagent", "claude", "codex")
# The guest sources (protocol 26.7): their rows are written by guest_sessions.py from the guests'
# own transcripts, never from Relay's session files, so a rebuild leaves them to `guest_reconcile`.
GUEST_SOURCES = ("claude", "codex")
SORTS = ("recent", "oldest", "longest", "shortest", "requests_desc", "requests", "title", "title_desc", "model", "model_desc",
         "summary", "summary_desc", "relevance")
# The keys the alphabetical sorts order by. `title` keys on the name the GUI shows — a custom
# rename over the stored title — and `model` on what the Sessions pane's Model column displays
# (`addSessionRow` in src/Conversations.cpp): the terminal rows say "terminal", the guest sources
# their label, everything else its model. The CASE is written to match, so the two never drift
# apart silently; both fall back to recency when the key ties.
_TITLE_KEY = "COALESCE(NULLIF(c.custom_title, ''), c.title, '') COLLATE NOCASE"
_MODEL_KEY = ("CASE c.source WHEN 'terminal' THEN 'terminal' WHEN 'claude' THEN 'claude code'"
              " WHEN 'codex' THEN 'codex' ELSE relay_model_name(c.model) END COLLATE NOCASE")
_SUMMARY_KEY = "COALESCE(c.summary, '') COLLATE NOCASE"


def _sql_model_name(value):
    """`relay_model_name(<a stored model id>)`: the one name that model has (card #MDL1, rule 1).

    History records the id the API took — "k3", "openai/gpt-6-sol", "MiniMax-M3" — and those
    stay on disk exactly as they were written. This is the name a person reads, and registering it
    on the connection is what lets the Model filter and the Model sort work on names without a
    second table: the menu lists each name once, so `k3` and `kimi-k3` are one entry that selects
    both, and `openai/gpt-6-sol` sorts beside `gpt-6-sol` rather than under "o".
    """
    if not isinstance(value, str) or not value:
        return value
    return _model_name(None, value) or value


def _register_functions(db: sqlite3.Connection) -> None:
    """`relay_model_name` on this connection, for the Model filter, menu and sort."""
    try:
        db.create_function("relay_model_name", 1, _sql_model_name, deterministic=True)
    except TypeError:      # an sqlite too old for `deterministic`
        db.create_function("relay_model_name", 1, _sql_model_name)
# `relevance` tiers a conversation by the best kind it matched, then by how many entries matched,
# then by recency: a title hit outranks a summary hit outranks a prompt outranks a reply outranks
# tool or terminal output, however many of the weaker ones there are.
# Below all of it sit the two sidecar kinds (v6, #0TJ9): a word the user said is a better answer
# to a search than the same word scrolling past in the terminal, or in a turn that was thrown away.
RANK_WEIGHTS = {"title": 8, "summary": 7, "prompt": 6, "command": 6, "reply": 5,
                "tool_call": 4, "tool_output": 3, "command_output": 3,
                "rewound": 2, "terminal_text": 1}
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
# Columns added by v4 (GT7X). `raw_cwd` is filled by the next guest reconcile; an agent row never
# has one (its `workspace` is already the directory Relay runs it in).
V4_COLUMNS = (("raw_cwd", "TEXT NOT NULL DEFAULT ''"),)
# Columns added by v5 (#TZWF): how many entry rows of this conversation are indexed and the rolling
# digest of them (`entry_digests`). A row migrated from v4 has `''` for the digest, which says
# "unknown" and costs it one full re-index at its next save — no reconcile is needed for these.
V5_COLUMNS = (("entry_count", "INTEGER NOT NULL DEFAULT 0"), ("entry_digest", "TEXT NOT NULL DEFAULT ''"))
V7_COLUMNS = (("guest_source", "TEXT NOT NULL DEFAULT ''"),
              ("guest_session", "TEXT NOT NULL DEFAULT ''"))
V8_COLUMNS = (("relay_launched", "INTEGER NOT NULL DEFAULT 0"),)
# v6 (#0TJ9) adds no column: its rows are entries, and `session_sidecars` is a table the schema
# creates on its own. What it does ask for is the v3 backfill — every agent and subagent row is
# marked behind, so the next reconcile reads each conversation's sidecars once.
MAX_MATCHES_LIMIT = 20
THREAD_KIND = "relay_subagent_thread"
# The guest rows' `<id>.meta.json` (see the module docstring): one file beside the database for
# every guest row, because there is nowhere else to put a guest session's pin.
GUEST_META_NAME = "guest-meta.json"

# ----- query operators (v3) -------------------------------------------------------------------
# `key:value` pairs parsed out of the query before anything reaches FTS5. An unknown key is not an
# error: the whole token stays free text, so a path or a URL typed into the box still searches.
OPERATOR_KEYS = ("project", "file", "model", "branch", "before", "after", "has", "is", "in")
OPERATOR_VALUES = {"has": ("tasks", "edits", "summary", "rewound"), "is": ("pinned", "unfinished"),
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


def under_pattern(folder: str) -> str:
    r"""`<folder>/%` as a `LIKE ? ESCAPE '\'` pattern: every path below `folder`, wildcards literal.

    Case is kept: the `workspace` column holds `normalize_workspace()` spellings and a project
    folder arrives the same way, so this is a prefix test on the canonical path, not a search.
    """
    escaped = str(folder or "").replace("\\", r"\\").replace("%", r"\%").replace("_", r"\_")
    return escaped.rstrip("/") + "/%"


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
    """Index rows for one saved session: {turn, seq, kind, time, text}, in conversation order.

    User prompts come from the checkpoints (they survive compaction, which rewrites `messages`);
    replies, tool calls and capped tool output come from the messages themselves. They are read in
    that order and then sorted back into the order they happened — turn by turn, and inside a turn
    the order the messages are in.

    That ordering is what makes a turn added to a conversation an *append* (#TZWF): the rows of
    every earlier turn keep the `seq` they were given, so `update_session` can write the new
    turn's rows and leave the rest where they are. Reading the checkpoints first and numbering as
    it went put every prompt in the index before every reply, and one new turn then renumbered
    half the conversation.
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
    # Back into conversation order (see the docstring). The sort is stable and keyed on the number
    # each row was given as it was read, so the messages of a turn keep the order they are in and
    # the turn's prompt — read first, from the checkpoint — stays in front of them.
    rows.sort(key=lambda row: (row["turn"], row["seq"]))
    for number, row in enumerate(rows, 1):
        row["seq"] = number
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


def turn_left_open(data: dict) -> bool:
    """Whether the session's last turn was cut off: the last checkpoint has no `ended` stamp,
    in a session whose checkpoints carry one at all — a session saved before that field existed
    never counts as cut off on its own. This is the pure checkpoint predicate; the worker adds
    it to `state_loaded` as `turn_open` so a restored pane knows it may continue the turn.
    """
    items = _checkpoint_items(data)
    return bool(items) and any(item.get("ended") is not None for item in items) and items[-1].get("ended") is None


def session_unfinished(data: dict) -> bool:
    """Whether the session looks like it was left mid-flight. True when any of:

    * the last checkpoint has no `ended` stamp (`turn_left_open`);
    * the last message is a user or tool message — nothing answered it;
    * a todo is still pending, in progress or blocked.
    """
    if turn_left_open(data):
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


def entry_digests(rows: list[dict], indexed: int) -> tuple[str, str]:
    """`(digest of the first `indexed` rows, digest of all of them)`, in one pass over `rows`.

    This is what tells a save that only added turns from one that rewrote the conversation
    (#TZWF). It is a chain — each row's digest covers every row before it — so a rewind, a fork,
    a compaction that renumbers the turns, an edited prompt, anything at all inside the part that
    is already indexed changes the prefix and the save falls back to the full rewrite. It covers a
    row's identity and its text but deliberately not its `time`, which `session_entries` fills
    from the session's `updated` for every reply row and so moves at every save.

    The prefix is `""` when `indexed` is past the end of `rows` (the conversation shrank), which
    matches no stored digest.
    """
    running = hashlib.sha256()
    prefix = ""
    for index, row in enumerate(rows):
        if index == indexed:
            prefix = running.hexdigest()
        running.update(f"{row['turn']}\0{row['seq']}\0{row['kind']}\0".encode("utf-8"))
        running.update(str(row["text"]).encode("utf-8", "surrogatepass"))
        running.update(b"\0")
    whole = running.hexdigest()
    return (whole if indexed == len(rows) else prefix), whole


def header_entries(title: str, summary: str) -> list[dict]:
    """The searchable `title` and `summary` entries of a conversation (turn 0, before its text)."""
    rows = []
    for kind, text in (("title", title), ("summary", summary)):
        text = (text or "").strip()
        if text:
            rows.append({"turn": 0, "seq": 0, "kind": kind, "time": None,
                         "text": text[:MAX_SUMMARY]})
    return rows


# ----- the sidecars (v6, #0TJ9) ---------------------------------------------------------------
#
# Everything below reads the three files the GUI writes beside a session (module docstring). None
# of it raises on what it finds there: a sidecar is a convenience the index mirrors, and a file
# that is half-written, truncated or not text at all costs the entries it would have held and
# nothing else.

def read_sidecar_text(path: str | Path) -> str:
    """A saved-scrollback file as text, capped the way the GUI caps it. "" when there is none.

    The caps are applied again here because the file is in a directory Relay writes but does not
    own the contents of: a `<id>.scrollback.txt` left by an older build, or by hand, must not be
    able to put a gigabyte into the index.
    """
    try:
        with open(path, "rb") as handle:
            raw = handle.read(MAX_SCROLLBACK_BYTES)
    except OSError:
        return ""
    return "\n".join(raw.decode("utf-8", "replace").splitlines()[:MAX_SCROLLBACK_LINES])


def text_chunks(text: str, lines: int = TERMINAL_TEXT_LINES) -> list[str]:
    """Saved terminal text split into runs of `lines` lines, blank runs dropped.

    One row for a whole 512 KiB file would make a search hit in it useless: `match_line` would have
    five thousand lines to choose a snippet from and the preview would print the session's entire
    history to show one of them. Forty lines is about a screen, so a hit reads like the place it
    came from.
    """
    rows = (text or "").splitlines()
    out = []
    for start in range(0, len(rows), max(1, lines)):
        chunk = "\n".join(rows[start:start + max(1, lines)]).strip()
        if chunk:
            out.append(chunk)
    return out


def rewound_records(path: str | Path) -> list[dict]:
    """The rewind records of `<id>.rewound.jsonl`, oldest first, the newest `MAX_REWOUND_RECORDS`.

    One JSON object per line (`{n, at, turn, restore, epoch, prompt, messages, restored_files,
    conflicts}`). The GUI appends a line per rewind and can be stopped in the middle of one, so a
    line that does not parse is dropped rather than failing the file — which is also what a read
    that hit the byte cap mid-line leaves behind.
    """
    try:
        with open(path, "rb") as handle:
            raw = handle.read(MAX_REWOUND_BYTES)
    except OSError:
        return []
    records = []
    for line in raw.decode("utf-8", "replace").splitlines():
        line = line.strip()
        if not line:
            continue
        try:
            record = json.loads(line)
        except ValueError:
            continue
        if isinstance(record, dict):
            records.append(record)
    return records[-MAX_REWOUND_RECORDS:]


def dropped_message_texts(messages) -> list[str]:
    """The text of each message a rewind dropped, rendered as `session_entries` renders the
    session's own: the prompt, the reply, `name {arguments}` for each tool call, the tool output.
    Relay's own context blocks are left out here too — they are not something the user typed."""
    out: list[str] = []
    for message in messages or []:
        if not isinstance(message, dict):
            continue
        role = message.get("role")
        content = str(message.get("content") or "")
        if role == "user":
            if content.lstrip().startswith(RELAY_CONTEXT):
                continue
            out.append(content)
        elif role == "assistant":
            out.append(content)
            for call in message.get("tool_calls") or []:
                if not isinstance(call, dict):
                    continue
                function = call.get("function") or {}
                arguments = function.get("arguments")
                if not isinstance(arguments, str):
                    arguments = json.dumps(arguments, ensure_ascii=False) if arguments else ""
                out.append(f"{function.get('name') or 'tool'} {arguments}")
        elif role == "tool":
            out.append(content)
    return out


def terminal_text_entries(text: str) -> list[dict]:
    """Saved terminal text as `terminal_text` rows: a guest session's whole sidecar, and the first
    part of a Relay session's (`sidecar_entries`). Turn 0 — it belongs to no turn."""
    rows: list[dict] = []
    for offset, chunk in enumerate(text_chunks(text), 1):
        chunk = _clean(chunk, MAX_TEXT).strip()
        if chunk:
            rows.append({"turn": 0, "seq": SIDECAR_SEQ + offset, "kind": "terminal_text",
                         "time": None, "text": chunk})
    return rows


def sidecar_entries(directory: str | Path, session_id: str) -> list[dict]:
    """The `terminal_text` and `rewound` rows of one Relay session's sidecars.

    A rewind record's rows carry its `turn`, so the preview shows what was dropped where it was
    dropped from, and its `<id>.rewound-<n>.scrollback.txt` — the terminal text that went with it —
    is chunked under the same kind and the same turn.
    """
    directory = Path(directory)
    rows = terminal_text_entries(read_sidecar_text(directory / f"{session_id}{SCROLLBACK_SUFFIX}"))
    seq = rows[-1]["seq"] if rows else SIDECAR_SEQ

    def add(turn: int, kind: str, text: str, when) -> None:
        nonlocal seq
        text = _clean(str(text or ""), MAX_TEXT).strip()
        if not text:
            return
        seq += 1
        rows.append({"turn": turn, "seq": seq, "kind": kind, "time": when, "text": text})

    for record in rewound_records(directory / f"{session_id}{REWOUND_SUFFIX}"):
        turn = record.get("turn")
        turn = turn if isinstance(turn, int) and not isinstance(turn, bool) and turn >= 0 else 0
        when = record.get("at") if isinstance(record.get("at"), (int, float)) else None
        for text in dropped_message_texts(record.get("messages")):
            add(turn, "rewound", text, when)
        number = record.get("n")
        if isinstance(number, int) and not isinstance(number, bool):
            path = directory / f"{session_id}.rewound-{number}{SCROLLBACK_SUFFIX}"
            for chunk in text_chunks(read_sidecar_text(path)):
                add(turn, "rewound", chunk, when)
    return rows


def guest_text_dir(source: str, root: str | Path | None = None) -> Path:
    """`sessions/guests/<source>/`, where a guest session's saved terminal text lives.

    A guest has no session directory of Relay's to sit the file beside, and protocol 26.7 keeps
    Relay out of `~/.claude` and `~/.codex`, so its own sidecar for a guest goes here instead.
    """
    return (Path(root) if root else sessions_root()) / GUESTS_DIRNAME / str(source)


def _entry_stamp(entry) -> str | None:
    """`name:size:mtime_ns` for one directory entry — what says a sidecar has to be read again."""
    try:
        stat = entry.stat()
    except OSError:
        return None
    return f"{entry.name}:{stat.st_size}:{stat.st_mtime_ns}"


def _sidecar_owner(name: str) -> str | None:
    """The session id a sidecar file name belongs to, or None when it is not one of ours."""
    match = _REWOUND_TEXT.match(name)
    if match:
        return match.group("id")
    for suffix in (SCROLLBACK_SUFFIX, REWOUND_SUFFIX):
        if name.endswith(suffix) and len(name) > len(suffix):
            return name[:-len(suffix)]
    return None


def session_folders(directory: str | Path) -> list[Path]:
    """The workspace-digest folders under the sessions root.

    `sessions/guests/` is not one of them: it is Relay's own store for the guests' saved terminal
    text, and a walk that took it for a workspace digest would go looking for session files under
    `guests/claude/`.
    """
    out: list[Path] = []
    try:
        entries = list(os.scandir(directory))
    except OSError:
        return out
    for entry in entries:
        if entry.name == GUESTS_DIRNAME or entry.name.startswith("."):
            continue
        try:
            if entry.is_dir():
                out.append(Path(entry.path))
        except OSError:
            continue
    return out


def scan_session_folder(folder: str | Path) -> tuple[list[Path], dict[str, str], list[Path]]:
    """One listing of a session folder: `(session files, {session id: sidecar stamp}, .threads/)`.

    `os.scandir` costs what the `glob` of the session files alone cost, and it hands back the
    sidecars in the same pass. That is the point: the GUI writes a scrollback at quit, after the
    last autosave, so noticing it means stat'ing the sidecars on every reconcile — and this runs on
    the sessions pane's hot path (#MDSG), where a second listing of every session directory is not
    free. A session with no sidecars is not stat'ed at all: it simply has no stamp.
    """
    sessions: list[Path] = []
    threads: list[Path] = []
    parts: dict[str, list[str]] = {}
    try:
        entries = list(os.scandir(folder))
    except OSError:
        return sessions, {}, threads
    for entry in entries:
        name = entry.name
        if name.startswith("."):
            continue
        if name.endswith(".threads"):
            try:
                if entry.is_dir():
                    threads.append(Path(entry.path))
            except OSError:
                pass
            continue
        if name.endswith(".json"):
            if not name.endswith(".meta.json"):
                sessions.append(Path(entry.path))
            continue
        owner = _sidecar_owner(name)
        stamp = _entry_stamp(entry) if owner else None
        if stamp is not None:
            parts.setdefault(owner, []).append(stamp)
    return sessions, {sid: "\x1f".join(sorted(items)) for sid, items in parts.items()}, threads


def guest_text_files(root: str | Path) -> list[tuple[str, Path, str]]:
    """`(session_id, path, stamp)` for every guest scrollback under `sessions/guests/<source>/`.

    One listing per guest source per pass — two directories, whatever the number of sessions.
    """
    out: list[tuple[str, Path, str]] = []
    base = Path(root) / GUESTS_DIRNAME
    for source in GUEST_SOURCES:
        try:
            entries = list(os.scandir(base / source))
        except OSError:
            continue
        for entry in entries:
            name = entry.name
            if not name.endswith(SCROLLBACK_SUFFIX) or len(name) <= len(SCROLLBACK_SUFFIX):
                continue
            stamp = _entry_stamp(entry)
            if stamp is not None:
                out.append((name[:-len(SCROLLBACK_SUFFIX)], Path(entry.path), stamp))
    return out


# ----- the index -----------------------------------------------------------------------------

class ConversationIndex:
    """A connection to index.db, shared by the threads of one worker and by several workers.

    **One connection, one lock.** The connection is opened `check_same_thread=False` because the
    worker's own threads share it: the protocol thread answers a listing while `_guest_refresh`
    reconciles the guests' transcripts on a background thread (protocol 26.7), and both go through
    this object. sqlite3 will not serialise them for us at that setting, and two statements
    interleaved on one connection surface as `InterfaceError: bad parameter or other API misuse`
    from whichever call lost — once in 48 runs of the backend suite under load, in `search()`
    beneath a `conversations` event (found by the load run behind card #99T0). Every use of the
    connection therefore goes through `_run`, which holds `_lock`, and so do the open, the reset
    and the close. It is an `RLock` because a `work(db)` callback may call back in — a corrupt
    database reconnects and re-seeds from inside `_run` — and re-entering must not deadlock. The
    guest meta store is a file two *processes* may write, so its read and its rewrite take the
    same lock: that keeps this worker's own threads from clobbering each other, and the file is
    replaced atomically for the other processes.
    """

    def __init__(self, path: str | Path | None = None, *, rebuild_on_reset: bool = False):
        self.path = Path(path) if path else default_index_path()
        # Before `_open()`: everything below serialises on it.
        self._lock = threading.RLock()
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
        if version in (1, 2, 3, 4, 5, 6, 7) and version < SCHEMA_VERSION:
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
        _register_functions(db)
        db.executescript(SCHEMA)
        db.execute("INSERT OR REPLACE INTO meta(key, value) VALUES('schema_version', ?)", (str(SCHEMA_VERSION),))
        db.commit()
        try:
            os.chmod(self.path, 0o600)
        except OSError:
            pass
        self._db = db
        # The forgotten guest sessions outlive the database (see the module docstring): whatever the
        # store holds is put back into the table every time the tables are (re)created, so a wipe
        # cannot make a session the user deleted walk back into the listing at the next reconcile.
        self._seed_forgotten(db)

    def _seed_forgotten(self, db) -> None:
        rows = [(source, session_id, entry.get("forgotten_at"))
                for source, sessions in self._guest_store().items()
                for session_id, entry in sessions.items() if entry.get("forgotten")]
        if not rows:
            return
        try:
            db.executemany("INSERT OR IGNORE INTO guest_forgotten(source, session_id, at)"
                           " VALUES(?,?,?)", rows)
            db.commit()
        except sqlite3.DatabaseError:
            pass

    @staticmethod
    def _migrate(db, version: int) -> None:
        """v1..v7 -> v8 in place, because terminal-history rows have no file to be rebuilt
        from — and, since v4, neither have the guest rows.

        v1 -> v2 adds the thread columns and moves user titles and pins to the session files, where
        they are safe from a cache wipe. v2 -> v3 adds the overview columns; they stay empty until
        the next `reconcile()`, which re-reads every row whose `indexed_version` is behind. v3 -> v4
        adds `raw_cwd`, which the next guest reconcile fills the same way. v4 -> v5 adds the
        incremental-index fingerprint, which every conversation fills itself at its next save, so
        it is the one migration that does not ask for a re-read. v5 -> v6 adds no column at all:
        `session_sidecars` is created with the schema, empty, and the rows the sidecars hold are
        backfilled by the next reconcile the way v3's columns were. v6 -> v7 adds the Relay
        session's linked guest source and id, also backfilled from its file on reconcile. v7 -> v8
        adds native guest provenance, filled by the next guest reconcile.
        """
        columns = {row[1] for row in db.execute("PRAGMA table_info(conversations)").fetchall()}
        for name, kind in V2_COLUMNS + V3_COLUMNS + V4_COLUMNS + V5_COLUMNS + V7_COLUMNS + V8_COLUMNS:
            if name not in columns:
                db.execute(f"ALTER TABLE conversations ADD COLUMN {name} {kind}")
        if version < 2:
            rows = db.execute("SELECT session_id, session_dir, custom_title, pinned FROM conversations"
                              " WHERE source='agent' AND (custom_title IS NOT NULL OR pinned != 0)").fetchall()
            for row in rows:
                if row["session_dir"]:
                    write_user_fields(Path(row["session_dir"]), row["session_id"],
                                      custom_title=row["custom_title"], pinned=bool(row["pinned"]))
        if version < 7:
            # The columns v2, v3 and v4 added are backfilled by re-reading the session files, and
            # so are v6's sidecar rows and v7's guest links. (v5's fingerprint was the one that
            # needed no re-read.)
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
        with self._lock:
            self._close_locked()

    def _close_locked(self) -> None:
        if self._db is not None:
            try:
                self._db.close()
            finally:
                self._db = None

    def _run(self, work):
        """Run `work(db)` under the connection's lock, recreating the database once if SQLite
        reports corruption. The lock is what makes one connection safe for the worker's threads
        (see the class docstring); it is re-entrant, so a `work` that reaches back in is fine."""
        with self._lock:
            try:
                return work(self._db)
            except sqlite3.DatabaseError as exc:
                if "malformed" not in str(exc) and "not a database" not in str(exc) and "corrupt" not in str(exc):
                    raise
                self._reset()
                return work(self._db)

    # ----- the guest meta store (v4) ----------------------------------------------------
    #
    # Relay's own sessions keep the user's title and pin in `<id>.meta.json` beside the session,
    # so throwing the cache away costs nothing but the time to read them back. A guest row has no
    # such file — Relay may not write into `~/.claude` or `~/.codex` (protocol 26.7) — and until
    # v4 its pin, its name and the fact that the user had deleted it lived only in the database
    # that `_connect()` discards on a schema change. This is the one file that holds them instead.

    @property
    def store_path(self) -> Path:
        """`guest-meta.json` beside the index database."""
        return self.path.with_name(GUEST_META_NAME)

    @property
    def sessions_dir(self) -> Path:
        """`sessions/` beside the index database — the same directory `sessions_root()` names for
        the real one, and the temporary one in a test that pointed the index elsewhere."""
        return self.path.with_name("sessions")

    def _guest_store(self) -> dict:
        """The store as `{source: {id: {...}}}`, re-read when the file changed under us.

        Another worker process writes the same file, so the cache is keyed by its (mtime_ns, size)
        rather than trusted for the life of the connection.
        """
        with self._lock:
            return self._guest_store_locked()

    def _guest_store_locked(self) -> dict:
        cache = getattr(self, "_store_cache", None)
        try:
            stat = self.store_path.stat()
            stamp = (stat.st_mtime_ns, stat.st_size)
        except OSError:
            stamp = None
        if cache is not None and cache[0] == stamp:
            return cache[1]
        data = read_guest_meta(self.store_path)
        self._store_cache = (stamp, data)
        return data

    def guest_meta(self, source: str | None = None, session_id: str | None = None) -> dict:
        """The store, or one source's part of it, or one guest row's `{custom_title, pinned,
        forgotten}` — whatever is set. Always a fresh dict: the caller may not edit the cache."""
        data = self._guest_store()
        if source is None:
            return {name: {key: dict(value) for key, value in rows.items()} for name, rows in data.items()}
        rows = data.get(str(source)) or {}
        if session_id is None:
            return {key: dict(value) for key, value in rows.items()}
        return dict(rows.get(str(session_id)) or {})

    def _write_guest_meta(self, source: str, session_id: str, **fields) -> None:
        """Merge `fields` into one guest row's entry and rewrite the store (0600, atomic).

        A key set to its empty value (no custom title, unpinned, not forgotten) is dropped rather
        than written as `false`, so an entry with nothing left in it goes away and the file stays
        the size of what the user actually set.
        """
        if source not in GUEST_SOURCES or not session_id:
            return
        # Read, change and rewrite under the lock: two of this worker's threads merging into the
        # same file would otherwise each write what they read, and the later write would win.
        with self._lock:
            self._write_guest_meta_locked(source, session_id, **fields)

    def _write_guest_meta_locked(self, source: str, session_id: str, **fields) -> None:
        data = self._guest_store_locked()
        data = {name: {key: dict(value) for key, value in rows.items()} for name, rows in data.items()}
        entry = dict(data.get(source, {}).get(session_id) or {})
        if "custom_title" in fields:
            title = " ".join(str(fields["custom_title"] or "").split())[:200]
            entry["custom_title"] = title
            if not title:
                entry.pop("custom_title", None)
        if "pinned" in fields:
            entry["pinned"] = True
            if not fields["pinned"]:
                entry.pop("pinned", None)
        if "forgotten" in fields:
            if fields["forgotten"]:
                entry["forgotten"] = True
                entry.setdefault("forgotten_at", time.time())
            else:
                entry.pop("forgotten", None)
                entry.pop("forgotten_at", None)
        rows = data.setdefault(source, {})
        if entry:
            rows[session_id] = entry
        else:
            rows.pop(session_id, None)
        if not rows:
            data.pop(source, None)
        write_guest_meta(self.store_path, data)
        self._store_cache = None                 # the next read stats the file we just wrote

    def forget(self, source: str, session_id: str) -> None:
        """Remember that the user deleted this guest session, so the next reconcile leaves it out.

        The transcript is the guest's and stays where it is (protocol 26.7): "delete" here means
        "stop indexing and stop listing it". Without this record the row came straight back at the
        next reconcile, which reads the same file and finds the same session — the delete looked
        like it had failed. It stays forgotten until `unforget()`, even if the user resumes the
        session through Relay again: the alternative is a row the user deleted reappearing because
        something touched the file, and the "show forgotten" listing is how it comes back.
        """
        if source not in GUEST_SOURCES or not session_id:
            return

        def work(db):
            db.execute("INSERT OR REPLACE INTO guest_forgotten(source, session_id, at) VALUES(?,?,?)",
                       (source, session_id, time.time()))
            db.commit()
        self._run(work)
        self._write_guest_meta(source, session_id, forgotten=True)

    def unforget(self, source: str, session_id: str) -> None:
        """Undo `forget()`: the next reconcile indexes the session again if its file is still there."""
        if source not in GUEST_SOURCES or not session_id:
            return

        def work(db):
            db.execute("DELETE FROM guest_forgotten WHERE source=? AND session_id=?", (source, session_id))
            db.commit()
        self._run(work)
        self._write_guest_meta(source, session_id, forgotten=False)

    def forgotten(self, sources=GUEST_SOURCES) -> set[tuple[str, str]]:
        """`{(source, id)}` the user deleted. Read from the table, which `_connect()` seeds from
        the store, so it is one query however the database got here."""
        wanted = [source for source in sources if source in GUEST_SOURCES]
        if not wanted:
            return set()
        placeholders = ",".join("?" * len(wanted))
        return self._run(lambda db: {
            (row["source"], row["session_id"]) for row in
            db.execute(f"SELECT source, session_id FROM guest_forgotten WHERE source IN ({placeholders})",
                       wanted).fetchall()})

    def delete_guests(self, sources=GUEST_SOURCES) -> int:
        """Drop every guest row of these sources from the index — and only from the index.

        This is what "do not index my other agents' sessions" turns off (`guest_sessions.purge`):
        the guests' own transcripts are untouched, and so are the store and the forgotten set, so
        turning the setting back on restores the pins and the names with the rows.
        """
        wanted = [source for source in sources if source in GUEST_SOURCES]
        if not wanted:
            return 0
        placeholders = ",".join("?" * len(wanted))

        def work(db):
            rows = db.execute(f"SELECT session_id FROM conversations WHERE source IN ({placeholders})",
                              wanted).fetchall()
            if not rows:
                return 0
            ids = [row["session_id"] for row in rows]
            for start in range(0, len(ids), 500):
                chunk = ids[start:start + 500]
                marks = ",".join("?" * len(chunk))
                db.execute(f"DELETE FROM entries WHERE session_id IN ({marks})", chunk)
                db.execute(f"DELETE FROM conversations WHERE session_id IN ({marks})", chunk)
                db.execute(f"DELETE FROM guest_files WHERE session_id IN ({marks})", chunk)
                # The saved terminal text stays on disk, so only its stamp goes: turning the
                # setting back on reads it again with the row (v6).
                db.execute(f"DELETE FROM session_sidecars WHERE session_id IN ({marks})", chunk)
            db.commit()
            return len(ids)
        return self._run(work)

    # ----- writing ---------------------------------------------------------------------
    @staticmethod
    def _entries_to_write(db, session_id: str, rows: list[dict], indexed: int,
                          stored_digest: str) -> tuple[list[dict], str]:
        """Delete the entry rows this save replaces; return the body rows it must insert, and the
        digest to store beside them.

        The common case is an autosave of a conversation that grew by a turn: what is indexed is a
        prefix of what the session now holds, so only that turn's rows are written, along with the
        header rows (the title and the summary, which a save can change). Everything else — a
        rewind, a fork, a compaction that renumbered the turns, a prompt edited in place, or a
        database whose entries went missing under a row that stayed — falls back to deleting the
        conversation's rows and writing them all, and ends with exactly what a rebuild from scratch
        writes. Before #TZWF every save did that: 46 ms on the owner's largest session, on the
        turn's own thread, against ~0.3 ms for the incremental write.

        The caller is never asked whether it appended: `entry_digests` decides from the text.

        The sidecar rows (v6) are neither counted nor deleted here. They come from files the
        autosave knows nothing about, so a save that rewrote them would drop a session's saved
        terminal text on every turn, and counting them would make the row total disagree with
        `entry_count` and fall back to the full rewrite for good.
        """
        prefix, whole = entry_digests(rows, indexed)
        extend = bool(stored_digest) and indexed <= len(rows) and prefix == stored_digest
        if extend:
            # The digest guards the session's text; this guards the table. The header rows are the
            # only others a conversation owns, so the total may exceed the indexed body rows by at
            # most a title and a summary — anything else means the rows are not what is recorded.
            total = db.execute(f"SELECT COUNT(*) FROM entries WHERE session_id=?"
                               f" AND kind NOT IN ({_SIDECAR_SQL})", (session_id,)).fetchone()[0]
            extend = indexed <= total <= indexed + len(HEADER_KINDS)
        if extend:
            db.execute(f"DELETE FROM entries WHERE session_id=? AND kind IN ({_HEADER_SQL})", (session_id,))
            return rows[indexed:], whole
        db.execute(f"DELETE FROM entries WHERE session_id=? AND kind NOT IN ({_SIDECAR_SQL})", (session_id,))
        return rows, whole

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
        guest_source = str(data.get("guest") or "")
        guest_session = str(data.get("guest_session") or "")
        if guest_source not in GUEST_SOURCES or not guest_session:
            guest_source = guest_session = ""

        def work(db):
            # What is indexed is read and rewritten under one write lock (#TZWF): two workers may
            # save the same conversation — the pane that holds it, and any worker reconciling the
            # store — and a check-then-append that read outside the transaction could append what
            # the other one had just appended. Inside a transaction already (a caller that left
            # one open) it is that one's lock, and beginning a second would raise.
            if not db.in_transaction:
                db.execute("BEGIN IMMEDIATE")
            try:
                keep = db.execute("SELECT custom_title, pinned, summary, entry_count, entry_digest"
                                  " FROM conversations WHERE session_id=?", (session_id,)).fetchone()
                # The session files hold user titles and pins (since v2); an older caller that does not
                # pass them keeps what the row had.
                previous_summary = (keep["summary"] if keep else "") or ""
                indexed = int(keep["entry_count"] or 0) if keep else 0
                stored_digest = (keep["entry_digest"] or "") if keep else ""
                if "custom_title" in data or "pinned" in data:
                    keep = {"custom_title": data.get("custom_title") or None, "pinned": 1 if data.get("pinned") else 0}
                # A summary set while the session was not loaded (set_summary, or the meta file) is not
                # in this autosave's JSON: keep it rather than lose it to a save that never had one.
                summary = derived["summary"] or previous_summary
                added, digest = self._entries_to_write(db, session_id, rows, indexed, stored_digest)
                tokens, cost = _usage_tokens(data)
                db.execute(
                    "INSERT OR REPLACE INTO conversations(session_id, source, workspace, project, title, custom_title,"
                    " model, preset, created, updated, turns, open_requests, session_dir, pinned, models, tokens, cost,"
                    " summary, first_prompt, last_prompt, files, files_count, has_edits, branch, unfinished, mode,"
                    " todos, indexed_version, entry_count, entry_digest, guest_source, guest_session)"
                    " VALUES(?,'agent',?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)",
                    (session_id, workspace, project_name(workspace), title,
                     keep["custom_title"] if keep else None,
                     str(data.get("model") or ""), str(data.get("preset") or ""),
                     data.get("created"), data.get("updated") or time.time(),
                     int(data.get("turns") or 0), int(data.get("open_requests") or 0),
                     str(session_dir or ""), int(keep["pinned"]) if keep else 0, _models(data), tokens, cost,
                     summary, derived["first_prompt"], derived["last_prompt"], derived["files"],
                     derived["files_count"], derived["has_edits"], derived["branch"], derived["unfinished"],
                     derived["mode"], derived["todos"], SCHEMA_VERSION, len(rows), digest,
                     guest_source, guest_session))
                written = header_entries((keep["custom_title"] if keep else None) or title, summary) + added
                db.executemany(
                    "INSERT INTO entries(session_id, turn, seq, kind, time, text) VALUES(?,?,?,?,?,?)",
                    [(session_id, row["turn"], row["seq"], row["kind"], row["time"], row["text"]) for row in written])
                db.commit()
            except BaseException:
                db.rollback()       # never leave the write lock held by a half-written save
                raise
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

    def sidecar_stamps(self) -> dict[str, str]:
        """`{session_id: stamp}` — what each conversation's sidecars looked like when they were
        last read, for `reconcile()`. One query, no file reads."""
        return self._run(lambda db: {row["session_id"]: row["stamp"] or "" for row in
                                     db.execute("SELECT session_id, stamp FROM session_sidecars").fetchall()})

    def update_sidecars(self, session_id: str, rows: list[dict], stamp: str = "") -> int:
        """Replace a conversation's `terminal_text` and `rewound` rows and record `stamp` as what
        the files they came from looked like (v6, #0TJ9). Returns the number of rows written.

        They are written on their own, never inside `update_session` or `update_guest`: the GUI
        saves the scrollback when a pane's session changes and at quit, which is *after* the last
        autosave, so the session JSON is not what says they moved. `reconcile()` is what notices,
        by the stamp, and this is what it calls.

        Rows for a conversation the index does not hold would be orphans nothing could ever find or
        delete, so they are dropped — and no stamp is stored for them, so the next pass looks
        again: a guest's scrollback may simply be there before `guest_sessions.reconcile()` has
        indexed the transcript it belongs to.
        """
        def work(db):
            known = db.execute("SELECT 1 FROM conversations WHERE session_id=?", (session_id,)).fetchone()
            db.execute(f"DELETE FROM entries WHERE session_id=? AND kind IN ({_SIDECAR_SQL})", (session_id,))
            if not known:
                db.execute("DELETE FROM session_sidecars WHERE session_id=?", (session_id,))
                db.commit()
                return 0
            db.executemany(
                "INSERT INTO entries(session_id, turn, seq, kind, time, text) VALUES(?,?,?,?,?,?)",
                [(session_id, int(row["turn"]), int(row["seq"]), str(row["kind"]), row["time"],
                  str(row["text"])) for row in rows])
            if stamp:
                db.execute("INSERT OR REPLACE INTO session_sidecars(session_id, stamp) VALUES(?,?)",
                           (session_id, stamp))
            else:
                db.execute("DELETE FROM session_sidecars WHERE session_id=?", (session_id,))
            db.commit()
            return len(rows)
        return self._run(work)

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
        Where there is no row — a first index, or the first one after the database was thrown
        away — the guest meta store is what they come from, which is how a pin and a name survive
        a schema bump (see the module docstring).

        A session the user deleted (`forget()`) is not indexed at all: 0 rows, no row written. The
        transcript is still there and still parses, so without that check the next reconcile put
        the session the user had just deleted straight back into the pane.

        `data["append"]` says the entries are only the ones the guest appended since
        `data["entry_base"]`: the row's existing entries stay where they are and the new ones are
        inserted after them, so a 99 MB transcript that grew by a line costs a line. It needs the
        row those entries belong to; asked to append to a row that is not there, this writes
        nothing and forgets where the transcript was read to, so the next pass reads it whole. `data["cursor"]`
        is where the transcript has been read to; it is stored in the same transaction as the
        entries, because an offset that moved without its entries would skip them forever.
        """
        source = str(data.get("source") or "")
        session_id = str(data.get("id") or "")
        if source not in GUEST_SOURCES:
            raise ValueError(f"A guest source must be one of {', '.join(GUEST_SOURCES)}.")
        if not session_id:
            raise ValueError("A guest session has no id.")
        workspace = normalize_workspace(str(data.get("workspace") or ""))
        raw_cwd = str(data.get("raw_cwd") or "")
        title = _one_line(data.get("title"), 200)
        first_prompt = _one_line(data.get("first_prompt"), MAX_PREVIEW)
        rows = [row for row in data.get("entries") or [] if isinstance(row, dict) and row.get("text")]
        mtime = data.get("mtime")
        mtime = float(mtime) if isinstance(mtime, (int, float)) and not isinstance(mtime, bool) else None
        cursor = data.get("cursor") if isinstance(data.get("cursor"), dict) else None
        saved = self.guest_meta(source, session_id)
        if saved.get("forgotten"):
            return 0

        def work(db):
            stored = db.execute("SELECT custom_title, pinned FROM conversations WHERE session_id=?",
                                (session_id,)).fetchone()
            # Merge per key: whichever of the two `data` does not mention keeps the stored value,
            # so a rename does not clear the pin and a pin does not clear the rename. With no row
            # to keep them in, the store is what the user set the last time there was one.
            custom_title = stored["custom_title"] if stored else (saved.get("custom_title") or None)
            pinned = int(stored["pinned"] or 0) if stored else int(bool(saved.get("pinned")))
            if "custom_title" in data:
                custom_title = data.get("custom_title") or None
            if "pinned" in data:
                pinned = 1 if data.get("pinned") else 0
            appending = bool(data.get("append")) and stored is not None
            if bool(data.get("append")) and stored is None and int(data.get("entry_base") or 0) > 0:
                # Entries to add to a row that is not there: the caller read only the tail of the
                # transcript, so writing it would make a session that starts in the middle. Drop
                # the stale cursor instead — with no row and no cursor, the next reconcile reads
                # the file from the beginning, which is the only way to get this right.
                db.execute("DELETE FROM guest_files WHERE session_id=?", (session_id,))
                db.commit()
                return 0
            if appending:
                db.execute(
                    "UPDATE conversations SET workspace=?, raw_cwd=?, project=?, title=?, custom_title=?,"
                    " updated=?, turns=?, pinned=?, first_prompt=?, relay_launched=?, file_mtime=?, indexed_version=? WHERE session_id=?",
                    (workspace, raw_cwd, project_name(workspace), title, custom_title,
                     mtime or time.time(), max(0, int(data.get("message_count") or 0)),
                     pinned, first_prompt, int(bool(data.get("relay_launched"))), mtime,
                     SCHEMA_VERSION, session_id))
                # The searchable title entry is rewritten only when the name changed; the rest of
                # the entries are left exactly where they are, which is the point of appending.
                shown = custom_title or title
                current = db.execute("SELECT text FROM entries WHERE session_id=? AND kind='title'",
                                     (session_id,)).fetchone()
                written = rows
                if (current["text"] if current else "") != (shown or ""):
                    db.execute("DELETE FROM entries WHERE session_id=? AND kind='title'", (session_id,))
                    written = header_entries(shown, "") + rows
            else:
                # Not the guest's saved terminal text, though: that is Relay's own sidecar, and
                # re-parsing a transcript says nothing about it (v6).
                db.execute(f"DELETE FROM entries WHERE session_id=? AND kind NOT IN ({_SIDECAR_SQL})",
                           (session_id,))
                db.execute(
                    "INSERT OR REPLACE INTO conversations(session_id, source, workspace, raw_cwd, project, title,"
                    " custom_title, model, preset, created, updated, turns, open_requests, session_dir, pinned,"
                    " file_mtime, indexed_version, first_prompt, relay_launched)"
                    " VALUES(?,?,?,?,?,?,?, '', '', ?, ?, ?, 0, '', ?, ?, ?, ?, ?)",
                    (session_id, source, workspace, raw_cwd, project_name(workspace), title, custom_title,
                     data.get("created") or mtime, mtime or time.time(),
                     max(0, int(data.get("message_count") or 0)),
                     pinned, mtime, SCHEMA_VERSION, first_prompt, int(bool(data.get("relay_launched")))))
                written = header_entries(custom_title or title, "") + rows
            db.executemany(
                "INSERT INTO entries(session_id, turn, seq, kind, time, text) VALUES(?,?,?,?,?,?)",
                [(session_id, int(row.get("turn") or 0), int(row.get("seq") or 0),
                  str(row.get("kind") or "reply"), row.get("time"),
                  _clean(str(row.get("text") or ""), MAX_PROMPT)) for row in written])
            if cursor is not None:
                db.execute("INSERT OR REPLACE INTO guest_files(session_id, source, path, size, mtime_ns,"
                           " read_to, state) VALUES(?,?,?,?,?,?,?)",
                           (session_id, source, str(cursor.get("path") or ""),
                            int(cursor.get("size") or 0), int(cursor.get("mtime_ns") or 0),
                            int(cursor.get("read_to") or 0), json.dumps(cursor, ensure_ascii=False)))
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

    def guest_cursors(self, sources=GUEST_SOURCES) -> dict[str, dict]:
        """`{session_id: cursor state}` — where each transcript was read to, for an incremental
        reconcile (`guest_sessions.parse_transcript`). A row with no cursor, or one whose state
        does not fit the file any more, simply costs a full re-parse."""
        wanted = [source for source in sources if source in GUEST_SOURCES]
        if not wanted:
            return {}
        placeholders = ",".join("?" * len(wanted))

        def work(db):
            out: dict[str, dict] = {}
            for row in db.execute(f"SELECT session_id, state FROM guest_files WHERE source IN ({placeholders})",
                                  wanted).fetchall():
                try:
                    state = json.loads(row["state"] or "{}")
                except ValueError:
                    continue
                if isinstance(state, dict):
                    out[row["session_id"]] = state
            return out
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
            if not db.in_transaction:                # as in update_session (#TZWF)
                db.execute("BEGIN IMMEDIATE")
            try:
                keep = db.execute("SELECT custom_title, pinned, entry_count, entry_digest FROM conversations"
                                  " WHERE session_id=?", (thread_id,)).fetchone()
                indexed = int(keep["entry_count"] or 0) if keep else 0
                stored_digest = (keep["entry_digest"] or "") if keep else ""
                if "custom_title" in data or "pinned" in data:
                    keep = {"custom_title": data.get("custom_title") or None, "pinned": 1 if data.get("pinned") else 0}
                added, digest = self._entries_to_write(db, thread_id, rows, indexed, stored_digest)
                db.execute(
                    "INSERT OR REPLACE INTO conversations(session_id, source, workspace, project, title, custom_title,"
                    " model, preset, created, updated, turns, open_requests, session_dir, pinned, owner_session,"
                    " parent_thread, agent_id, agent_type, spawn_turn, status, models, tokens, cost, file_mtime,"
                    " indexed_version, entry_count, entry_digest)"
                    " VALUES(?,'subagent',?,?,?,?,?,'',?,?,?,0,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)",
                    (thread_id, workspace, project_name(workspace), title, keep["custom_title"] if keep else None,
                     str(data.get("model") or ""), data.get("created"), data.get("updated") or time.time(),
                     max(1, runs), str(session_dir or ""), int(keep["pinned"]) if keep else 0, owner, parent,
                     str(data.get("agent_id") or ""), str(data.get("type") or ""), spawn_turn,
                     str(data.get("status") or ""), _models(data), tokens, cost, file_mtime, SCHEMA_VERSION,
                     len(rows), digest))
                written = header_entries((keep["custom_title"] if keep else None) or title, "") + added
                db.executemany(
                    "INSERT INTO entries(session_id, turn, seq, kind, time, text) VALUES(?,?,?,?,?,?)",
                    [(thread_id, row["turn"], row["seq"], row["kind"], row["time"], row["text"]) for row in written])
                db.commit()
            except BaseException:
                db.rollback()
                raise
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
        # A close-time recap may finish after the last session-file save. Prefer whichever
        # successful summary covers more turns when rebuilding the index.
        meta_success = merged.get("summary_success_turn") or merged.get("summary_turn") or 0
        data_success = data.get("summary_success_turn") or data.get("summary_turn") or 0
        if data.get("summary") and data_success >= meta_success:
            merged["summary"] = data["summary"]
        self.update_session(merged, path.parent)
        return True

    def reconcile(self, root: str | Path | None = None) -> dict:
        """Bring the rows in line with the files: index sessions and threads that are missing or
        newer on disk than in the index, index the sidecars that changed, and drop rows whose file
        is gone. Cheap when nothing changed (one listing per session folder, one stat per file and
        one small meta read per session), so it runs whenever a worker first opens the index;
        `rebuild()` is still there for a full refresh.

        Each folder is listed **once** (`scan_session_folder`), which is what the session files
        alone already cost. The sidecars come back from that same listing with their size and
        mtime, and `sidecars` in the report counts the sessions whose stamp had moved: the GUI
        writes a scrollback at quit, after the last autosave, so a sidecar changes while the
        session JSON does not — and an unchanged one is never opened.
        """
        started = time.time()
        directory = Path(root) if root else sessions_root()
        known = self._run(lambda db: {row["session_id"]: (row["source"], row["updated"] or 0, row["session_dir"],
                                                          row["file_mtime"], row["indexed_version"] or 0)
                                      for row in db.execute("SELECT session_id, source, updated, session_dir,"
                                                            " file_mtime, indexed_version"
                                                            " FROM conversations WHERE source IN"
                                                            " ('agent', 'subagent')").fetchall()})
        stored = self.sidecar_stamps()
        seen: set[str] = set()
        added = refreshed = backfilled = sidecars = 0
        if directory.is_dir():
            for folder in session_folders(directory):
                paths, stamps, thread_folders = scan_session_folder(folder)
                for path in paths:
                    session_id = path.name[:-5]
                    seen.add(session_id)
                    row = known.get(session_id)
                    stamp = _meta_updated(path)
                    # A row written before the current schema has empty new columns whatever its
                    # mtime says, so it is read again once and then left alone.
                    stale = row is not None and int(row[4]) < SCHEMA_VERSION
                    fresh = (row is not None and not stale and stamp is not None
                             and stamp <= float(row[1]) + 1e-6 and row[2] == str(folder))
                    if not fresh and self.index_session_file(path):
                        added += row is None
                        refreshed += row is not None
                        backfilled += bool(stale)
                    # The sidecars are the GUI's, not the autosave's: it is their own stamp that
                    # says to read them, and a row that is behind the schema reads them once too.
                    # Most sessions have none — nothing on disk and nothing recorded — and those
                    # cost nothing at all, not even on the one pass that backfills the schema.
                    wanted, was = stamps.get(session_id, ""), stored.get(session_id, "")
                    if (wanted or was) and (stale or wanted != was):
                        self.update_sidecars(session_id, sidecar_entries(folder, session_id), wanted)
                        sidecars += 1
                for thread_folder in thread_folders:
                    for path in sorted(thread_folder.glob("*.json")):
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
            # A guest's saved terminal text sits in Relay's own tree, not beside a transcript, and
            # is stamped the same way. Two listings a pass, whatever the number of guest sessions.
            for session_id, path, stamp in guest_text_files(directory):
                if stamp == stored.get(session_id, ""):
                    continue
                self.update_sidecars(session_id, terminal_text_entries(read_sidecar_text(path)), stamp)
                sidecars += 1
        gone = [sid for sid, (_source, _updated, folder, _mtime, _version) in known.items()
                if sid not in seen and (not folder or Path(folder).parent == directory or
                                        Path(folder).parent.parent == directory)]

        def drop(db):
            for sid in gone:
                db.execute("DELETE FROM entries WHERE session_id=?", (sid,))
                db.execute("DELETE FROM conversations WHERE session_id=?", (sid,))
                db.execute("DELETE FROM session_sidecars WHERE session_id=?", (sid,))
            db.commit()
        if gone:
            self._run(drop)
        return {"added": added, "refreshed": refreshed, "removed": len(gone), "backfilled": backfilled,
                "sidecars": sidecars, "ms": int((time.time() - started) * 1000)}

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
            # The stamps go with them, so nothing claims a sidecar is indexed when its rows are not.
            db.execute("DELETE FROM session_sidecars WHERE session_id IN"
                       " (SELECT session_id FROM conversations WHERE source IN ('agent', 'subagent'))")
            db.execute("DELETE FROM conversations WHERE source IN ('agent', 'subagent')")
            db.commit()
        self._run(clear)
        sessions = entries = threads = 0
        if directory.is_dir():
            # `session_folders` rather than a `*/` glob: `sessions/guests/` holds the guests' saved
            # terminal text, not a workspace's sessions (v6). A guest row survives a rebuild, and
            # so does its sidecar's rows; a Relay session's are written again here with its own.
            for folder in sorted(session_folders(directory)):
                paths, stamps, thread_folders = scan_session_folder(folder)
                for path in sorted(paths):
                    if path.name.endswith(".meta.json") or not self.index_session_file(path):
                        continue
                    sessions += 1
                    session_id = path.name[:-5]
                    self.update_sidecars(session_id, sidecar_entries(folder, session_id),
                                         stamps.get(session_id, ""))
                for thread_folder in sorted(thread_folders):
                    for path in sorted(thread_folder.glob("*.json")):
                        if self.index_thread_file(path):
                            threads += 1
        entries = self._run(lambda db: db.execute("SELECT count(*) FROM entries").fetchone()[0])
        self._run(lambda db: (db.execute("INSERT OR REPLACE INTO meta(key, value) VALUES('rebuilt', ?)",
                                         (str(time.time()),)), db.commit())[0])
        return {"sessions": sessions, "threads": threads, "entries": entries,
                "reclaimed": self._run(self._vacuum_if_fragmented),
                "ms": int((time.time() - started) * 1000)}

    @staticmethod
    def _vacuum_if_fragmented(db) -> int:
        """Give back the free pages, if there are enough of them to be worth it. Bytes reclaimed.

        The delete-and-reinsert that #TZWF replaced left a third of the owner's 119.6 MB index as
        freelist — the incremental write stops that growing, but it does not return what is
        already there, and only a VACUUM can. It rewrites the whole file, so it happens **here**
        and nowhere else: a rebuild is asked for by hand, already takes a second, and
        `index_rebuild` runs it on a background thread (`session_protocol._background`), never on
        a turn. A VACUUM that cannot run — no room for the copy it makes — leaves the database
        exactly as it was, so it is reported as nothing reclaimed rather than failing the rebuild.
        """
        try:
            page_size = db.execute("PRAGMA page_size").fetchone()[0]
            pages = db.execute("PRAGMA page_count").fetchone()[0]
            free = db.execute("PRAGMA freelist_count").fetchone()[0]
            if not pages or free * VACUUM_FREE_IN < pages or free * page_size < VACUUM_MIN_BYTES:
                return 0
            db.commit()                      # VACUUM cannot run inside a transaction
            db.execute("VACUUM")
            return free * page_size
        except (sqlite3.DatabaseError, OSError):
            return 0

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
            host = _clean(str(item.get("host") or ""), 255).strip()
            if host:
                command = f"[{host} · {item.get('cwd') or '?'}] {command}"
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

    def delete_session(self, session_id: str, *, remove_files: bool = True, forget: bool = True) -> dict:
        """Remove a conversation's index rows and, for agent sessions, its files and blobs.

        A guest session (protocol 26.7) has no Relay file to delete: its rows go and the
        transcript the guest owns stays, so `remove_files` never touches it. Because the file
        stays, the row is also written into the forgotten set (`forget()`), or the next reconcile
        reads the same transcript and puts the session the user just deleted back in the list.

        `forget=False` is for the reconcile's own pruning: a row whose transcript is *gone* is
        dropped because there is nothing left to index, not because the user said so, and
        remembering it would keep a session out of the pane for good if its file came back (a
        network home that was late, a restored backup).
        """
        def work(db):
            row = db.execute("SELECT source, session_dir FROM conversations WHERE session_id=?", (session_id,)).fetchone()
            db.execute("DELETE FROM entries WHERE session_id=?", (session_id,))
            db.execute("DELETE FROM conversations WHERE session_id=?", (session_id,))
            db.execute("DELETE FROM guest_files WHERE session_id=?", (session_id,))
            db.execute("DELETE FROM session_sidecars WHERE session_id=?", (session_id,))
            # A session's subagent threads go with it (their files sit in its .threads folder).
            db.execute("DELETE FROM entries WHERE session_id IN"
                       " (SELECT session_id FROM conversations WHERE owner_session=?)", (session_id,))
            db.execute("DELETE FROM conversations WHERE owner_session=?", (session_id,))
            db.commit()
            return dict(row) if row else None
        row = self._run(work)
        removed = {"session_id": session_id, "files": 0, "indexed": row is not None}
        if row is not None and row["source"] in GUEST_SOURCES:
            removed["forgotten"] = bool(forget)
            if forget:
                self.forget(row["source"], session_id)
            # The one file of a guest's that *is* Relay's: the terminal text Relay saved for it
            # (v6). The guest's own transcript is still untouched, as it must be.
            if remove_files:
                try:
                    (guest_text_dir(row["source"], self.sessions_dir)
                     / f"{session_id}{SCROLLBACK_SUFFIX}").unlink()
                    removed["files"] += 1
                except OSError:
                    pass
        if not remove_files or row is None or row["source"] != "agent" or not row["session_dir"]:
            return removed
        directory = Path(row["session_dir"])
        for name in (f"{session_id}.json", f"{session_id}.meta.json",
                     f"{session_id}{SCROLLBACK_SUFFIX}", f"{session_id}{REWOUND_SUFFIX}"):
            try:
                (directory / name).unlink()
                removed["files"] += 1
            except OSError:
                pass
        # The rewind sidecars are numbered, and the record that names them has just gone, so they
        # are found by name rather than read out of the jsonl (v6).
        for path in sorted(directory.glob(f"{session_id}.rewound-*{SCROLLBACK_SUFFIX}")):
            try:
                path.unlink()
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
        """Give a conversation the name the user typed (or clear it with "").

        For a guest row the name also goes into the meta store, which is the only place it can
        survive the database being discarded: there is no `<id>.meta.json` beside a guest
        transcript, and Relay may not make one (protocol 26.7).
        """
        title = " ".join(str(title or "").split())[:200]

        def work(db):
            source = db.execute("SELECT source FROM conversations WHERE session_id=?", (session_id,)).fetchone()
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
            return source["source"] if source else ""
        source = self._run(work)
        if source in GUEST_SOURCES:
            self._write_guest_meta(source, session_id, custom_title=title)

    def set_pinned(self, session_id: str, pinned: bool) -> None:
        """Pin (or unpin) a conversation; a guest row's pin is mirrored into the meta store, for
        the same reason its name is (see `rename`)."""
        def work(db):
            source = db.execute("SELECT source FROM conversations WHERE session_id=?", (session_id,)).fetchone()
            db.execute("UPDATE conversations SET pinned=? WHERE session_id=?", (1 if pinned else 0, session_id))
            db.commit()
            return source["source"] if source else ""
        source = self._run(work)
        if source in GUEST_SOURCES:
            self._write_guest_meta(source, session_id, pinned=bool(pinned))

    # ----- reading ---------------------------------------------------------------------
    def _filters(self, *, scope: str, workspace: str | None, model: str | None, has_open: bool,
                 since, until, kinds, has_edits=None, unfinished=None, pinned=None, file=None,
                 branch=None, has_summary=None, operators=(), now=None, project=None,
                 outside_projects=()) -> tuple[str, list]:
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
        # The Sessions pane's "Project" chooser (card #916B). A row belongs to a project when its
        # workspace *is* that folder or lies below it — a pane opened in a subdirectory of a
        # checkout is that checkout's — so this is an equality-or-prefix test on the canonical
        # path. "No project" is the same test negated for every project Relay knows.
        if project:
            folder = normalize_workspace(str(project))
            add(r"(c.workspace = ? OR c.workspace LIKE ? ESCAPE '\')", folder, under_pattern(folder))
        for known in outside_projects or ():
            folder = normalize_workspace(str(known))
            add(r"(c.workspace = ? OR c.workspace LIKE ? ESCAPE '\')", folder, under_pattern(folder),
                negated=True)
        if model:
            # By name (card #MDL1, rule 1): the menu offers each model once, and picking "kimi-k3"
            # selects the rows recorded as `k3` too. A raw id still filters, for a saved query and
            # for a name this build cannot derive.
            add("(relay_model_name(c.model) = ? OR c.model = ?)", model, model)
        if has_open:
            add("c.open_requests > 0")
        if isinstance(since, (int, float)):
            add("COALESCE(c.updated, 0) >= ?", float(since))
        if isinstance(until, (int, float)):
            add("COALESCE(c.updated, 0) <= ?", float(until))
        if kinds:
            add("c.source IN (%s)" % ",".join("?" * len(kinds)), *kinds)
        # A Relay pane running a guest has two durable transcripts, but is one session in the
        # combined list. A guest-only source filter still exposes its native transcript and
        # resume action. When the Relay session is deleted, its guest row reappears automatically.
        if "agent" in kinds and any(source in kinds for source in GUEST_SOURCES):
            add("NOT (c.source IN ('claude','codex') AND EXISTS ("
                "SELECT 1 FROM conversations owner WHERE owner.source='agent'"
                " AND owner.guest_source=c.source AND owner.guest_session=c.session_id))")
            # Saved subagent threads represent Relay's spawned guests; their native transcripts
            # must not become top-level sessions when the thread checkbox is off.
            add("NOT (c.source IN ('claude','codex') AND c.relay_launched=1)")
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
            elif key == "model":
                # `model:` matches the name as well as the stored id, so `model:kimi-k3` finds the
                # conversations history wrote as `k3` (card #MDL1, rule 1).
                add(r"(lower(c.model) LIKE ? ESCAPE '\' OR lower(relay_model_name(c.model)) LIKE ? ESCAPE '\')",
                    like_value(value), like_value(value), negated=negated)
            elif key in ("file", "branch"):
                add(r"lower(c.%s) LIKE ? ESCAPE '\'" % ("files" if key == "file" else key),
                    like_value(value), negated=negated)
            elif key == "after":
                add("COALESCE(c.updated, 0) >= ?", parse_date(value, now), negated=negated)
            elif key == "before":
                add("COALESCE(c.updated, 0) < ?", parse_date(value, now), negated=negated)
            elif key == "has":
                add({"tasks": "c.open_requests > 0", "edits": "c.has_edits = 1",
                     "summary": "c.summary != ''",
                     # v6: the conversation has a rewind whose dropped turns were kept. There is no
                     # column for it — the rows are the record — so it is an EXISTS on them.
                     "rewound": "EXISTS (SELECT 1 FROM entries er WHERE er.session_id = c.session_id"
                                " AND er.kind = 'rewound')"}[value], negated=negated)
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
            # The models menu lists *names* (card #MDL1, rule 1): one entry per model, so the rows
            # written as `k3` and as `kimi-k3` are one line that selects both, and `_filters`
            # takes the name straight back.
            value_sql = "relay_model_name(c.model)" if column == "model" else f"c.{column}"
            sql = (f"SELECT {value_sql} AS value, MAX(COALESCE(c.updated, 0)) AS last FROM conversations c"
                   f" WHERE c.{column} IS NOT NULL AND c.{column} != ''")
            if clause:
                sql += " AND " + clause
            sql += f" GROUP BY value ORDER BY last DESC LIMIT {MAX_FACET_VALUES}"
            out[name] = [row["value"] for row in db.execute(sql, params).fetchall()]
        return out

    def search(self, query: str = "", *, scope: str = "project", workspace: str | None = None,
               model: str | None = None, has_open: bool = False, since=None, until=None,
               sources=None, limit: int = 50, include_threads: bool = False, sort: str = "recent",
               offset: int = 0, matches_per_item: int = MAX_MATCHES_PER_ITEM,
               has_edits: bool | None = None, unfinished: bool | None = None,
               pinned: bool | None = None, file: str | None = None, branch: str | None = None,
               has_summary: bool | None = None, now: float | None = None,
               project: str | None = None, outside_projects: list[str] | None = None) -> dict:
        """Conversations matching `query`, each with its matching turns.

        The query may carry operators (`project:`, `file:`, `model:`, `branch:`, `before:`,
        `after:`, `has:`, `is:`, `in:`, and `-word` to exclude); `parse_query` takes them out and
        what is left is the free text. They stack with the explicit filter arguments, which are the
        same conditions by another route. The result's `parsed` says what was understood.

        Words are an AND over the whole conversation, not over one message: a conversation matches
        when every word or phrase occurs somewhere in it, and an excluded word must occur nowhere
        in it. `sort` is "recent" (default), "oldest", "longest" (most turns), "shortest" (fewest),
        "requests_desc" (most unfinished requests), "requests" (fewest),
        "title"/"title_desc" (the custom title over the stored one, A→Z or Z→A), "model"/
        "model_desc" (what the GUI's Model column shows, A→Z or Z→A), "summary"/
        "summary_desc" (the saved Recap column, A→Z or Z→A), or "relevance" (the
        best kind matched first — title, then summary, then prompt, then reply, then tool and
        terminal output — then the number of matching entries, then recency); the alphabetical
        orders break ties by recency, pinned rows come
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
        # Naming a project is asking about that project, wherever the pane happens to be. The
        # pane's "Project" chooser (`project` / `outside_projects`, #916B) says so just as plainly.
        if any(op.get("key") == "project" for op in operators) or project or outside_projects:
            scope = "all"
        kinds = [s for s in (sources or []) if s in SOURCES]
        if not kinds:
            kinds = ["agent", "terminal"]
        if include_threads and "subagent" not in kinds:
            kinds.append("subagent")
        clause, params = self._filters(
            scope=scope, workspace=workspace, model=model, has_open=has_open, since=since, until=until,
            kinds=kinds, has_edits=has_edits, unfinished=unfinished, pinned=pinned, file=file,
            branch=branch, has_summary=has_summary, operators=operators, now=now, project=project,
            outside_projects=outside_projects or ())
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
                 "shortest": "c.turns ASC, COALESCE(c.updated, 0) DESC",
                 "requests_desc": "CASE WHEN c.source='agent' THEN 0 ELSE 1 END, c.open_requests DESC, COALESCE(c.updated, 0) DESC",
                 "requests": "CASE WHEN c.source='agent' THEN 0 ELSE 1 END, c.open_requests ASC, COALESCE(c.updated, 0) DESC",
                 "title": _TITLE_KEY + " ASC, COALESCE(c.updated, 0) DESC",
                 "title_desc": _TITLE_KEY + " DESC, COALESCE(c.updated, 0) DESC",
                 "model": _MODEL_KEY + " ASC, COALESCE(c.updated, 0) DESC",
                 "model_desc": _MODEL_KEY + " DESC, COALESCE(c.updated, 0) DESC",
                 "summary": _SUMMARY_KEY + " ASC, COALESCE(c.updated, 0) DESC",
                 "summary_desc": _SUMMARY_KEY + " DESC, COALESCE(c.updated, 0) DESC",
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
                if item["source"] in GUEST_SOURCES:
                    item["snippet"] = item["first_prompt"][:200]
                elif not item["snippet"]:
                    item["snippet"] = item["summary"][:200] or item["first_prompt"][:200]
                if not item["snippet"] and item["source"] not in GUEST_SOURCES:
                    first = db.execute(
                        f"SELECT text FROM entries WHERE session_id=? AND kind NOT IN ({_NOT_BODY_SQL})"
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
        preview never reads the session JSON back off disk. The sidecar kinds are left out of it
        entirely (v6): a rewound turn is not where the conversation got to, and saved terminal text
        is not a turn at all."""
        session_id = row["session_id"]
        last_turns: list[dict] = []
        top = db.execute(f"SELECT MAX(turn) FROM entries WHERE session_id=? AND kind NOT IN ({_NOT_BODY_SQL})",
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
        listed; the overview carries the summary instead.

        `rewound` entries (v6) *are* listed, at the turn they were dropped from and after that
        turn's messages, so the preview shows what a rewind undid where it happened. Saved
        `terminal_text` is not: it is the whole of a pane's history and would bury the conversation
        it belongs to. The exception is a search — with `query`, the chunks the query matched are
        appended, so a hit found in saved terminal text can be read where it was found."""
        limit = max(1, min(int(limit or 400), 2000))
        terms = query_terms(query)

        def work(db):
            row = db.execute("SELECT * FROM conversations WHERE session_id=?", (session_id,)).fetchone()
            if row is None:
                raise ValueError("No indexed conversation with that id.")
            sql = ("SELECT turn, kind, time, status, text FROM entries"
                   f" WHERE session_id=? AND kind NOT IN ({_UNLISTED_SQL})")
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
            if terms and not isinstance(turn, int):
                for entry in db.execute(
                        "SELECT turn, kind, time, text FROM entries WHERE session_id=?"
                        " AND kind='terminal_text' ORDER BY seq LIMIT ?", (session_id, limit)).fetchall():
                    line, ranges = match_line(entry["text"], terms)
                    if not ranges:
                        continue
                    entries.append({"turn": entry["turn"], "kind": entry["kind"], "time": entry["time"],
                                    "text": entry["text"], "line": line, "ranges": ranges})
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
            # v4: the working directory a guest transcript named, as written. `workspace` is that
            # path resolved, which is the one grouping and filters use — and the wrong one to
            # resume a guest in when it reached its own cwd through a symlink (GT7X).
            "raw_cwd": row["raw_cwd"] or "",
            # The id history recorded, and the one name that model has (card #MDL1, rule 1): the
            # Model column, the quick look and the filter menu then all say the same word, and a
            # name the derivation cannot reach — the Kimi Coding Plan's "k3" is "kimi-k3" — comes
            # from here rather than being re-derived, wrongly, in the GUI.
            "model": row["model"], "model_name": _sql_model_name(row["model"] or ""),
            "preset": row["preset"],
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
    """User fields and summary provenance from `<id>.meta.json`; empty when it has none.

    `summary` is there for a session summarised while it was not loaded: the session JSON has no
    summary of its own then, so the meta file is what a rebuild reads it back from. A close-time
    recap can also be newer than a session JSON that already has an older summary.
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
        for name in ("summary_turn", "summary_success_turn"):
            if type(meta.get(name)) is int and meta[name] >= 0:
                out[name] = meta[name]
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
    for name in ("summary_turn", "summary_success_turn"):
        if name in fields and type(fields[name]) is int and fields[name] >= 0:
            meta[name] = fields[name]
    if "summary_time" in fields and type(fields["summary_time"]) in (int, float) and fields["summary_time"] >= 0:
        meta["summary_time"] = fields["summary_time"]
    fd, temp = tempfile.mkstemp(dir=path.parent, prefix=".session-")
    try:
        with os.fdopen(fd, "w", encoding="utf-8") as out:
            chmod_fd(out.fileno(), 0o600)
            json.dump(meta, out, ensure_ascii=False)
        os.replace(temp, path)
    finally:
        if os.path.exists(temp):
            os.unlink(temp)
    return True


# ----- the guest rows' meta store (since v4) ---------------------------------------------------

def read_guest_meta(path: str | Path) -> dict:
    """`{source: {id: {custom_title?, pinned?, forgotten?, forgotten_at?}}}` from `guest-meta.json`.

    Anything unreadable — missing, truncated by a crash mid-write, a JSON array where an object
    belongs — is an empty store, not an error: this file is what makes a pin survive a cache wipe,
    and refusing to open the index because it is corrupt would be a worse failure than losing the
    pin. The next write rewrites it whole. Entries that are not dicts are dropped the same way, so
    one bad row cannot take the others with it.
    """
    try:
        with open(path, encoding="utf-8") as handle:
            data = json.load(handle)
    except (OSError, ValueError):
        return {}
    if not isinstance(data, dict):
        return {}
    out: dict[str, dict] = {}
    for source, rows in data.items():
        if source not in GUEST_SOURCES or not isinstance(rows, dict):
            continue
        kept = {str(key): dict(value) for key, value in rows.items()
                if key and isinstance(value, dict)}
        if kept:
            out[source] = kept
    return out


def write_guest_meta(path: str | Path, data: dict) -> None:
    """Write the whole store back (0600, atomic replace), creating its directory if need be.

    Atomic because the reader treats a half-written file as an empty store: a rename of a session
    interrupted by a crash must lose that rename, never every pin in the file.
    """
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True, mode=0o700)
    fd, temp = tempfile.mkstemp(dir=path.parent, prefix=".guest-meta-")
    try:
        with os.fdopen(fd, "w", encoding="utf-8") as out:
            chmod_fd(out.fileno(), 0o600)
            json.dump(data, out, ensure_ascii=False)
        os.replace(temp, path)
    finally:
        if os.path.exists(temp):
            os.unlink(temp)


def _meta_updated(path: Path) -> float | None:
    """`updated` of a session from its small meta file (cheaper than the session JSON)."""
    try:
        with open(path.with_name(path.name[:-5] + ".meta.json"), encoding="utf-8") as handle:
            meta = json.load(handle)
        value = meta.get("updated") if isinstance(meta, dict) else None
        return float(value) if isinstance(value, (int, float)) else None
    except (OSError, ValueError):
        return None
