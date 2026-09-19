# SPDX-License-Identifier: GPL-3.0-or-later
"""Guest agent sessions: the `claude` and `codex` conversation sources (protocol 26.7).

The sessions pane lists guest sessions beside Relay's own. Both guests keep their own
transcripts — claude one `<session-id>.jsonl` per working directory under
``~/.claude/projects/<cwd-slug>/``, codex one ``rollout-*.jsonl`` under ``~/.codex/sessions/``
with its threads database (the highest ``state_*.sqlite``) holding names and working
directories — and those files stay the only truth. This module reads them and hands parsed
rows to ``ConversationIndex.update_guest()`` (``conv_index`` owns the table), so a guest
session is a row like any other: listed by the same search, found by the same full-text query,
dropped from the index when its file goes away, never deleted for real.

A record is what protocol 26.7 says it is, plus the directory that command needs::

    {source, id, title, mtime, workspace, message_count, resume_command, resume_cwd}

`mtime` is the transcript file's; `message_count` counts the main chain's user prompts and
assistant replies (claude sidechains, tool-result carriers and codex developer messages are
not conversation messages); `resume_command` is the argv that respawns the guest in the chosen
pane — ``claude -r <id> [--fork-session]``, ``codex resume <id>`` / ``codex fork <id>``.

**The spawner must chdir to `resume_cwd` before running `resume_command`.** Both guests look a
session up under the working directory they were started in — claude by the `<cwd-slug>`
directory its transcripts live in — so `claude -r <id>` run from another directory reports an
unknown session. `resume_cwd` is the session's own workspace (empty when the transcript never
named one, and then the pane's own directory is as good a guess as any);
:func:`resume_spawn` hands the argv and the directory back as one payload.

Search spans all sources because the rows share the one index; the default listing still shows
Relay's own conversations only, and a query names ``claude``/``codex`` in `sources` to see the
guests (the same rule that keeps subagent threads out unless asked for).

The active pane's live transcript is tailed read-only by :class:`LiveTail`, so a session the
guest is writing right now appears (and stays current) without a rescan. Nothing here writes
``guest.json`` — the guest event channel belongs to the hooks phase — and nothing here writes
into the guests' own directories at all.
"""
from __future__ import annotations

import json
import os
import re
import sqlite3
import time
from datetime import datetime, timezone
from pathlib import Path

from . import conv_index, guest, logs

log = logs.get("guest_sessions")

# The two source kinds this module serves (the listing itself lives in conv_index, section 26.3).
GUEST_SOURCES = conv_index.GUEST_SOURCES

MAX_TITLE = 200                  # record title cap, one line
MAX_PROMPT_PREVIEW = 80          # title fallback: the first prompt, cut like titles.fallback_title
MAX_TOOL_ARGUMENTS = 200         # tool_call entry: the arguments string is capped, not dropped
CODEX_ROLLOUT = re.compile(r"rollout-.+\.jsonl$", re.IGNORECASE)
# A session UUID at the end of a transcript file name (`rollout-2026-09-18T22-36-30-<uuid>.jsonl`).
UUID_TAIL = re.compile(r"([0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12})$")
# Codex threads database columns this module reads; older schemas simply lack the later ones.
CODEX_THREAD_COLUMNS = ("id", "rollout_path", "cwd", "name", "title", "first_user_message",
                        "created_at", "updated_at")


# ----- records and resume commands --------------------------------------------------------------


def resume_command(source: str, session_id: str, *, fork: bool = False) -> list[str]:
    """The argv that respawns a guest session in a pane (protocol 26.7).

    claude continues a session with ``-r <id>`` and forks it with ``--fork-session``; codex
    resumes with ``codex resume <id>`` and forks with ``codex fork <id>`` — a subcommand of its
    own, not a flag. An unknown guest id is a ValueError, as everywhere in the registry.
    """
    guest.spec(source)                      # ValueError for anything but claude/codex
    session_id = str(session_id or "")
    if not session_id:
        raise ValueError("A guest session needs its id.")
    if source == "claude":
        argv = ["claude", "-r", session_id]
        if fork:
            argv.append("--fork-session")
        return argv
    return ["codex", "fork" if fork else "resume", session_id]


def resume_spawn(source: str, session_id: str, workspace: str | None = None, *,
                 fork: bool = False) -> dict:
    """`{argv, cwd}`: what a pane needs to respawn a guest session, in one payload.

    The argv alone is not enough. Both guests resolve a session id against the directory they
    are started in — claude looks for ``<projects>/<cwd-slug>/<id>.jsonl`` — so the pane must
    chdir to `cwd` first, or ``claude -r <id>`` reports an unknown session from anywhere but the
    directory the session was held in. `cwd` is "" when the transcript named no workspace, and
    the spawner then keeps the pane's own directory.
    """
    return {"argv": resume_command(source, session_id, fork=fork), "cwd": str(workspace or "")}


def to_record(data: dict, *, fork: bool = False) -> dict:
    """The protocol 26.7 record of one parsed guest session (see the module docstring)."""
    workspace = str(data.get("workspace") or "")
    return {"source": data.get("source") or "",
            "id": str(data.get("id") or ""),
            "title": " ".join(str(data.get("title") or "").split())[:MAX_TITLE],
            "mtime": float(data.get("mtime") or 0.0),
            "workspace": workspace,
            "message_count": int(data.get("message_count") or 0),
            "resume_command": resume_command(data.get("source") or "", data.get("id") or "", fork=fork),
            "resume_cwd": workspace}


def item_to_record(item: dict, *, fork: bool = False) -> dict:
    """The same record out of an index row (`search()`'s item shape): the cache's spelling of
    what `to_record` computed at index time, for a listing that reads the index instead of the
    guests' files."""
    source = str(item.get("source") or "")
    updated = item.get("updated") or item.get("created") or 0.0
    workspace = str(item.get("workspace") or "")
    return {"source": source,
            "id": str(item.get("session_id") or item.get("id") or ""),
            "title": " ".join(str(item.get("title") or "").split())[:MAX_TITLE] or "Untitled",
            "mtime": float(updated if isinstance(updated, (int, float)) else 0.0),
            "workspace": workspace,
            "message_count": int(item.get("turns") or 0),
            "resume_command": resume_command(source, item.get("session_id") or item.get("id") or "",
                                             fork=fork),
            "resume_cwd": workspace}


def annotate_items(items, *, fork: bool = False) -> list[dict]:
    """Index items with the protocol 26.7 record fields merged into the guest rows.

    A `conversations` answer lists every source at once, so the worker does not have to know
    which rows are guests: a guest item gains `id`, `mtime`, `message_count`, `resume_command`
    (the argv the pane spawns to resume it, `--fork-session`/`codex fork` when `fork`) and
    `resume_cwd` (the directory it must be spawned in — see `resume_spawn`), and every other
    item comes back exactly as the index gave it.

    `workspace` is restated from the record rather than left to the row so the spawn payload is
    complete on its own: a caller that reads only the record fields still knows where to run.
    """
    out = []
    for item in items or ():
        if not isinstance(item, dict) or item.get("source") not in GUEST_SOURCES:
            out.append(item)
            continue
        record = item_to_record(item, fork=fork)
        out.append({**item, "id": record["id"], "mtime": record["mtime"],
                    "message_count": record["message_count"],
                    "workspace": record["workspace"],
                    "resume_command": record["resume_command"],
                    "resume_cwd": record["resume_cwd"]})
    return out


# ----- transcript parsing -----------------------------------------------------------------------
#
# One parser per guest accumulates a transcript's fields line by line, so the same code reads a
# whole file (scan/reconcile) and tails a live one (LiveTail): `feed()` one JSON line at a time,
# `finish()` stamps it with the file's stat. Everything a guest writes that is not a
# conversation message — claude's snapshots, queue operations, attachments and sidechains,
# codex's turn contexts, token counts and events — is recognised enough to be ignored.


def _epoch(value) -> float | None:
    """Epoch seconds of a transcript timestamp: ISO 8601 text or a number, else None.

    Both guests write UTC (claude's trailing ``Z``, codex's rollout stamps). A stamp that names
    no zone is read as UTC too, not as this machine's local time: Python would otherwise place a
    naive stamp hours away from the one beside it that did carry the ``Z``, which reorders a
    session's entries and dates the session itself wrong in the listing.
    """
    if isinstance(value, bool):
        return None
    if isinstance(value, (int, float)):
        return float(value)
    if not isinstance(value, str) or not value:
        return None
    try:
        stamp = datetime.fromisoformat(value.replace("Z", "+00:00"))
    except ValueError:
        return None
    if stamp.tzinfo is None:
        stamp = stamp.replace(tzinfo=timezone.utc)
    return stamp.timestamp()


def _one_line(text, cap: int) -> str:
    return " ".join(str(text or "").split())[:cap]


class _Parser:
    """Fields accumulated from one guest transcript. Subclasses take JSON lines."""

    source = ""

    def __init__(self):
        self.session_id = ""
        self.workspace = ""
        self.created: float | None = None
        self.first_prompt = ""
        self.message_count = 0
        self.entries: list[dict] = []
        self._turn = 0

    # The title a guest gave the session, by precedence; subclasses fill `titles`.
    titles: dict[str, str]

    def _stamp(self, when) -> None:
        if self.created is None and when is not None:
            self.created = when

    def _add(self, kind: str, text: str, when) -> None:
        text = (text or "").strip()
        if not text or len(self.entries) >= conv_index.MAX_ENTRIES_PER_SESSION:
            return
        cap = conv_index.MAX_PROMPT if kind == "prompt" else conv_index.MAX_TEXT
        self.entries.append({"turn": max(self._turn, 1), "seq": len(self.entries) + 1, "kind": kind,
                             "time": when, "text": text[:cap]})

    def title(self) -> tuple[str, str]:
        for key in self.TITLE_KEYS:
            value = _one_line(self.titles.get(key), MAX_TITLE)
            if value:
                return value, key
        if self.first_prompt:
            return _one_line(self.first_prompt, MAX_PROMPT_PREVIEW), "prompt"
        return "", ""

    def finish(self, path, mtime: float) -> dict:
        title, title_kind = self.title()
        return {"source": self.source, "id": self.session_id, "file": str(path), "title": title,
                "title_kind": title_kind, "workspace": self.workspace,
                "created": self.created if self.created is not None else mtime,
                "mtime": float(mtime), "message_count": self.message_count,
                "entries": self.entries}


class _ClaudeParser(_Parser):
    """One ``<session-id>.jsonl`` under ``~/.claude/projects/<cwd-slug>/``.

    Message lines carry `message` in the provider's own shape (a string or a list of blocks),
    plus `cwd`, `sessionId`, `timestamp` and `isSidechain`. Titles are re-emitted as lines of
    their own near the end of the file, so the last one wins.
    """

    source = "claude"
    TITLE_KEYS = ("custom", "ai", "summary")

    def __init__(self):
        super().__init__()
        self.titles = {}

    def feed(self, line: str) -> None:
        try:
            data = json.loads(line)
        except ValueError:
            return
        if not isinstance(data, dict):
            return
        if not self.workspace and isinstance(data.get("cwd"), str) and data["cwd"]:
            self.workspace = data["cwd"]
        if not self.session_id and isinstance(data.get("sessionId"), str) and data["sessionId"]:
            self.session_id = data["sessionId"]
        when = _epoch(data.get("timestamp"))
        self._stamp(when)
        kind = data.get("type")
        if kind == "custom-title":
            self.titles["custom"] = str(data.get("customTitle") or "")
        elif kind == "ai-title":
            self.titles["ai"] = str(data.get("aiTitle") or "")
        elif kind == "summary":                      # claude's own compaction summaries
            self.titles["summary"] = str(data.get("summary") or "")
        elif kind in ("user", "assistant") and not data.get("isSidechain"):
            message = data.get("message")
            if not isinstance(message, dict):
                return
            texts, calls = _claude_content(message.get("content"))
            if kind == "user":
                if not texts:
                    return                           # a tool-result carrier, not a prompt
                self._turn += 1
                self.message_count += 1
                text = "\n".join(texts)
                if not self.first_prompt:
                    self.first_prompt = text
                self._add("prompt", text, when)
            else:
                self.message_count += 1
                for text in texts:
                    self._add("reply", text, when)
                for name, arguments in calls:
                    self._add("tool_call", f"{name} {arguments}", when)


def _claude_content(content) -> tuple[list[str], list[tuple[str, str]]]:
    """(text pieces, tool calls) of a claude message content: a string or a block list."""
    if isinstance(content, str):
        return [content] if content.strip() else [], []
    texts: list[str] = []
    calls: list[tuple[str, str]] = []
    if not isinstance(content, list):
        return texts, calls
    for block in content:
        if not isinstance(block, dict):
            continue
        if block.get("type") == "text" and isinstance(block.get("text"), str):
            texts.append(block["text"])
        elif block.get("type") == "tool_use":
            arguments = block.get("input")
            if not isinstance(arguments, str):
                arguments = json.dumps(arguments, ensure_ascii=False) if arguments else ""
            calls.append((str(block.get("name") or "tool"), _one_line(arguments, MAX_TOOL_ARGUMENTS)))
    return texts, calls


class _CodexParser(_Parser):
    """One ``rollout-*.jsonl`` under ``~/.codex/sessions/YYYY/MM/DD/``.

    The first line is `session_meta` (the session id and the working directory); messages are
    `response_item` payloads (`message` with input/output text, `function_call`); the rollouts
    also carry turn contexts, token counts and events, which say nothing a record needs. The
    developer message is codex's instruction blob, not a conversation message.
    """

    source = "codex"
    TITLE_KEYS = ("name", "title")   # filled from the threads database by `_apply_codex_meta`

    def __init__(self):
        super().__init__()
        self.titles = {}

    def feed(self, line: str) -> None:
        try:
            data = json.loads(line)
        except ValueError:
            return
        if not isinstance(data, dict):
            return
        when = _epoch(data.get("timestamp"))
        self._stamp(when)
        kind = data.get("type")
        payload = data.get("payload")
        if not isinstance(payload, dict):
            return
        if kind == "session_meta":
            identifier = payload.get("session_id") or payload.get("id")
            if not self.session_id and isinstance(identifier, str) and identifier:
                self.session_id = identifier
            if not self.workspace:
                self.workspace = str(payload.get("cwd") or "")
        elif kind == "turn_context":
            if not self.workspace:
                self.workspace = str(payload.get("cwd") or "")
        elif kind == "response_item":
            ptype = payload.get("type")
            if ptype == "message":
                role = payload.get("role")
                text = _codex_text(payload.get("content"))
                if role == "user" and text:
                    self._turn += 1
                    self.message_count += 1
                    if not self.first_prompt:
                        self.first_prompt = text
                    self._add("prompt", text, when)
                elif role == "assistant" and text:
                    self.message_count += 1
                    self._add("reply", text, when)
            elif ptype in ("function_call", "custom_tool_call"):
                arguments = payload.get("arguments")
                if arguments is None:
                    arguments = payload.get("input")
                if not isinstance(arguments, str):
                    arguments = json.dumps(arguments, ensure_ascii=False) if arguments else ""
                self._add("tool_call", f"{payload.get('name') or 'tool'} "
                                       f"{_one_line(arguments, MAX_TOOL_ARGUMENTS)}", when)


def _codex_text(content) -> str:
    """The text of a codex message content: a list of input/output text blocks."""
    if isinstance(content, str):
        return content
    if not isinstance(content, list):
        return ""
    parts = [block.get("text") for block in content
             if isinstance(block, dict) and isinstance(block.get("text"), str)]
    return "\n".join(parts)


def _parse_file(path: Path, parser: _Parser) -> dict | None:
    """One whole transcript through a parser, or None when it cannot be read at all."""
    try:
        with open(path, encoding="utf-8", errors="replace") as handle:
            for line in handle:
                parser.feed(line)
        mtime = path.stat().st_mtime
    except OSError:
        return None
    return parser.finish(path, mtime)


def _file_id(path: Path) -> str:
    """The session id a transcript file's own name carries: its trailing UUID (claude names the
    file after the session, codex ends the rollout name with the session's UUID) or, failing
    that, the whole stem.

    This is the one id the module keys a session by — the record's, the index row's and the one
    `_walk` looks a file up by before it reads it — so all three agree by construction."""
    stem = path.name
    if stem.endswith(".jsonl"):
        stem = stem[: -len(".jsonl")]
    match = UUID_TAIL.search(stem)
    return match.group(1) if match else stem


def parse_claude_transcript(path: str | Path) -> dict | None:
    """One claude session as a parsed record.

    The file's name is the session id: claude writes ``<slug>/<session-id>.jsonl`` and resolves
    ``claude -r <id>`` back to that path, so the name is what resumes and what the index row is
    keyed by. It wins over a `sessionId` line that disagrees (a copied or hand-edited
    transcript): keyed any other way, the row the last run indexed is not the one `_walk` looks
    up, its mtime skip never fires and every reconcile re-parses the file. An empty transcript
    is still a session the picker should show, and it has nothing but its name."""
    path = Path(path)
    parsed = _parse_file(path, _ClaudeParser())
    if parsed is None:
        return None
    parsed["id"] = _file_id(path) or parsed["id"]
    if not parsed["workspace"]:
        parsed["workspace"] = claude_workspace_from_slug(path.parent.name)
    return parsed


def _apply_codex_meta(parsed: dict, meta: dict | None) -> dict:
    """Fold a threads-database row into a parsed codex session: the thread's `name` (or
    `title`, or the first user message) beats the transcript's own first prompt, and the
    database knows the working directory when the rollout does not."""
    if not isinstance(meta, dict):
        return parsed
    for key in ("name", "title"):
        value = _one_line(meta.get(key), MAX_TITLE)
        if value:
            parsed["title"], parsed["title_kind"] = value, key
            break
    if not parsed["title"] and meta.get("first_user_message"):
        parsed["title"] = _one_line(meta["first_user_message"], MAX_TITLE)
        parsed["title_kind"] = "title"
    if not parsed["workspace"] and meta.get("workspace"):
        parsed["workspace"] = str(meta["workspace"])
    return parsed


def parse_codex_rollout(path: str | Path, *, meta: dict | None = None) -> dict | None:
    """One codex rollout as a parsed record. `meta` is the session's row from the threads
    database (see `codex_thread_meta`); without one the record is the rollout alone.

    The UUID the rollout's name ends in is the thread id the database and ``codex resume`` use,
    so it is the record's id (and the index's key) whenever the name carries one — the same rule
    as claude's, and for the same reason: `_walk` looks a file up by its name before reading it
    (see `parse_claude_transcript`)."""
    path = Path(path)
    parsed = _parse_file(path, _CodexParser())
    if parsed is None:
        return None
    parsed["id"] = _file_id(path) or parsed["id"]
    return _apply_codex_meta(parsed, meta)


# ----- claude's directory naming -----------------------------------------------------------------


def claude_slug(cwd: str) -> str:
    """The directory name claude gives a working directory: every character that is not a
    letter or a digit becomes a dash (``/home/u/x`` → ``-home-u-x``, ``Dropbox/_Ash_Admin`` →
    ``Dropbox--Ash-Admin``). Encoding is exact; decoding is not (see the reverse)."""
    return re.sub(r"[^A-Za-z0-9]", "-", str(cwd or ""))


def claude_workspace_from_slug(slug: str) -> str:
    """The best-effort path behind a claude project directory name: the leading dash is the
    root and the rest are separators. A dash in a real name is indistinguishable from a
    separator, so this is only a display fallback — the transcript's own `cwd` lines are the
    truth and `parse_claude_transcript` prefers them."""
    text = str(slug or "")
    return "/" + text[1:].replace("-", "/") if text.startswith("-") else text.replace("-", "/")


# ----- the codex threads database ----------------------------------------------------------------


def codex_thread_meta(db_path: str | Path | None) -> dict[str, dict]:
    """`{session_id: {workspace, file, name, title, first_user_message}}` from the codex
    threads database. Opened read-only (never a writer, so a running codex keeps it): a
    missing, busy or unreadable database is simply no metadata, and the rollouts stand alone —
    but the reason is logged, because "the guest's titles silently stopped appearing" is not
    something to debug from an empty pane.

    The read-only URI comes from `Path.as_uri()`, which percent-escapes the path: pasted
    together by hand, a home directory holding ``?`` (query separator), ``#`` (fragment) or
    ``%`` (escape introducer) either truncates the file name or decodes into a different one.
    """
    if not db_path:
        return {}
    try:
        uri = Path(db_path).absolute().as_uri() + "?mode=ro"
    except ValueError as error:           # not a path that can be named as a URI at all
        logs.event(log, "codex threads database path unusable", level_name="error",
                   error=type(error).__name__)
        return {}
    try:
        db = sqlite3.connect(uri, uri=True)
    except sqlite3.Error as error:
        logs.event(log, "codex threads database not opened", level_name="error",
                   db=Path(db_path).name, error=type(error).__name__)
        return {}
    try:
        columns = {row[1] for row in db.execute("PRAGMA table_info(threads)").fetchall()}
        wanted = [name for name in CODEX_THREAD_COLUMNS if name in columns]
        if "id" not in wanted:
            return {}
        out: dict[str, dict] = {}
        for row in db.execute("SELECT %s FROM threads" % ", ".join(wanted)).fetchall():
            item = dict(zip(wanted, row))
            identifier = item.get("id")
            if not isinstance(identifier, str) or not identifier:
                continue
            out[identifier] = {"workspace": str(item.get("cwd") or ""),
                               "file": str(item.get("rollout_path") or ""),
                               "name": str(item.get("name") or ""),
                               "title": str(item.get("title") or ""),
                               "first_user_message": str(item.get("first_user_message") or "")}
        return out
    except sqlite3.Error as error:
        logs.event(log, "codex threads database not read", level_name="error",
                   db=Path(db_path).name, error=type(error).__name__)
        return {}
    finally:
        db.close()


# ----- scanning a guest's sessions ----------------------------------------------------------------


def _by_age(paths) -> list[Path]:
    """Paths newest first, ties broken by name; one that cannot be stat'ed sorts last."""
    def stamp(path: Path) -> tuple[float, str]:
        try:
            return (path.stat().st_mtime, str(path))
        except OSError:
            return (float("-inf"), str(path))
    return sorted(paths, key=stamp, reverse=True)


def _claude_paths(home: str | None) -> list[Path]:
    """claude's session transcripts: one ``<cwd-slug>/<session-id>.jsonl`` per session. The
    ``subagents/`` transcripts claude nests below them are not sessions of their own (the same
    line the index draws between a conversation and a subagent thread)."""
    root = Path(guest.claude_projects_dir(home))
    if not root.is_dir():
        return []
    return [path for path in root.glob("*/*.jsonl") if not path.name.startswith(".")]


def _codex_paths(home: str | None) -> list[Path]:
    """codex's rollouts: ``~/.codex/sessions/YYYY/MM/DD/rollout-*.jsonl``."""
    root = Path(guest.codex_sessions_dir(home))
    if not root.is_dir():
        return []
    return [path for path in root.rglob("*.jsonl") if CODEX_ROLLOUT.match(path.name)]


def _walk(paths: list[Path], parse, *, limit: int | None = None,
          known: dict[str, float | None] | None = None) -> tuple[list[dict], set[str]]:
    """Newest-first, at most `limit` transcripts through `parse`; a transcript whose indexed
    mtime (`known`, keyed by session id) already matches the file's is not read at all.

    Returns `(records, unchanged)` — `unchanged` being the session ids that were skipped, which
    are still indexed and must not be mistaken for files that have gone away. The id a file is
    looked up by here is `_file_id`, which is also the id its parse puts on the record and the
    index keys the row by, so the lookup finds the row the last run wrote."""
    records: list[dict] = []
    unchanged: set[str] = set()
    for path in _by_age(paths)[:limit or None]:
        identifier = _file_id(path)
        try:
            mtime = path.stat().st_mtime
        except OSError:
            continue
        indexed = (known or {}).get(identifier)
        if indexed is not None and abs(mtime - float(indexed)) < 1e-6:
            unchanged.add(identifier)
            continue
        record = parse(path)
        if record is not None:
            records.append(record)
    return records, unchanged


def _scan(source: str, home: str | None, *, limit: int | None,
          known: dict[str, float | None] | None) -> tuple[list[dict], set[str]]:
    """One guest's transcripts (newest first) and the session ids skipped as unchanged."""
    guest.spec(source)
    if source == "claude":
        return _walk(_claude_paths(home), parse_claude_transcript, limit=limit, known=known)
    meta: dict[str, dict] | None = None

    def parse(path: Path) -> dict | None:
        nonlocal meta
        if meta is None:
            meta = codex_thread_meta(guest.codex_state_db(home))
        return parse_codex_rollout(path, meta=meta.get(_file_id(path)))

    return _walk(_codex_paths(home), parse, limit=limit, known=known)


def scan_claude(home: str | None = None, *, limit: int | None = None,
                known: dict[str, float | None] | None = None) -> list[dict]:
    """Every claude session under ``guest.claude_projects_dir()``, newest first: one parsed
    record per ``<cwd-slug>/<session-id>.jsonl``. `known` maps session ids to the mtime already
    indexed, so a reconcile reads only what changed."""
    return _scan("claude", home, limit=limit, known=known)[0]


def scan_codex(home: str | None = None, *, limit: int | None = None,
               known: dict[str, float | None] | None = None) -> list[dict]:
    """Every codex session under ``guest.codex_sessions_dir()``, newest first: one parsed
    record per rollout, enriched by the threads database (`guest.codex_state_db()`, the highest
    ``state_*.sqlite``). A database row whose rollout is gone has nothing to list or search, so
    the rollouts on disk decide.

    `known` maps session ids to the mtime already indexed; the database is read only when a
    rollout is actually parsed."""
    return _scan("codex", home, limit=limit, known=known)[0]


def scan(source: str, home: str | None = None, *, limit: int | None = None) -> list[dict]:
    """Every session of one guest source; an unknown source is a ValueError."""
    return _scan(source, home, limit=limit, known=None)[0]


# ----- the index cache ---------------------------------------------------------------------------


def reconcile(index: conv_index.ConversationIndex, home: str | None = None,
              *, sources=GUEST_SOURCES, limit: int | None = None) -> dict:
    """Bring the guest rows of `index` in line with the guests' own files: index sessions that
    are new or whose transcript changed (its mtime), drop rows whose file is gone. A transcript
    whose mtime matches the indexed one is not even read, so a worker that reconciles on first
    use pays for the guests' history once and then only for the sessions that changed.

    `limit` caps each source at its N newest files — the first run over a long claude history
    is minutes of parsing, and a pane that only wants to list recent sessions can say so. **A
    limited reconcile never removes a row** (`removed` is always 0): it has not looked at the
    transcripts past the limit, so it cannot tell a session that went away from one it simply
    did not read, and dropping every id it did not see would empty the pane of everything but
    the N newest. Only a full reconcile — the one that stats every file — prunes.

    Returns `{added, refreshed, removed, ms}` like `ConversationIndex.reconcile()`.
    """
    sources = tuple(source for source in sources if source in GUEST_SOURCES)
    started = time.time()
    known = index.guest_file_stamps(sources)
    seen: set[str] = set()
    added = refreshed = 0
    for source in sources:
        records, unchanged = _scan(source, home, limit=limit,
                                   known={identifier: row[2] for identifier, row in known.items()})
        seen |= unchanged
        for record in records:
            identifier = record["id"]
            if not identifier:
                continue
            seen.add(identifier)
            row = known.get(identifier)
            index.update_guest(record)
            added += row is None
            refreshed += row is not None
    removed = 0
    if limit is None:                    # a capped scan saw only the newest N: it may not prune
        for identifier in known:
            if identifier not in seen:
                index.delete_session(identifier, remove_files=False)   # rows only, never files
                removed += 1
    return {"added": added, "refreshed": refreshed, "removed": removed,
            "ms": int((time.time() - started) * 1000)}


def list_sessions(index: conv_index.ConversationIndex, *, sources=GUEST_SOURCES,
                  scope: str = "all", workspace: str | None = None, limit: int = 100,
                  fork: bool = False) -> list[dict]:
    """Guest session records out of the index, newest first — the half of the sessions pane's
    listing that is not Relay's own conversations (`item_to_record` maps the rows)."""
    result = index.search("", sources=[s for s in sources if s in GUEST_SOURCES], scope=scope,
                          workspace=workspace, limit=limit, sort="recent")
    return [item_to_record(item, fork=fork) for item in result["items"]]


def search_sessions(index: conv_index.ConversationIndex, query: str, *,
                    sources=GUEST_SOURCES, scope: str = "all", workspace: str | None = None,
                    limit: int = 50, fork: bool = False) -> dict:
    """Guest conversations matching a full-text query: the index's own search result, with each
    item carrying the protocol 26.7 record fields (`id`, `mtime`, `message_count`,
    `resume_command`) beside its matches and snippet. Pass Relay's sources too and the one
    query spans everything the sessions pane shows."""
    result = index.search(query, sources=[s for s in sources if s in GUEST_SOURCES], scope=scope,
                          workspace=workspace, limit=limit, sort="relevance")
    items = []
    for item in result["items"]:
        record = item_to_record(item, fork=fork)
        items.append({**record, "matches": item.get("matches") or [],
                      "snippet": item.get("snippet") or "", "match_count": item.get("match_count") or 0})
    result["items"] = items
    return result


# ----- the active pane's live transcript -----------------------------------------------------------


def claude_live_transcript(workspace: str | None = None, home: str | None = None) -> Path | None:
    """The transcript claude is writing in `workspace` right now: the newest ``.jsonl`` in its
    project directory (`claude_slug`); the newest anywhere when no workspace is given."""
    root = Path(guest.claude_projects_dir(home))
    if not root.is_dir():
        return None
    folder = root / claude_slug(workspace or "")
    paths = folder.glob("*.jsonl") if workspace else root.glob("*/*.jsonl")
    for path in _by_age(paths):
        return path
    return None


def codex_live_transcript(workspace: str | None = None, home: str | None = None) -> Path | None:
    """The rollout codex is writing in `workspace` right now. The threads database names the
    newest thread's rollout outright; without one (or without a match) the rollouts are walked
    newest-first and their `session_meta` line names the working directory."""
    root = Path(guest.codex_sessions_dir(home))
    if not root.is_dir():
        return None
    if workspace:
        for meta in codex_thread_meta(guest.codex_state_db(home)).values():
            path = Path(meta.get("file") or "")
            if meta.get("workspace") == workspace and path.is_file():
                return path
    newest = _by_age(path for path in root.rglob("*.jsonl") if CODEX_ROLLOUT.match(path.name))
    if not workspace:
        return newest[0] if newest else None
    for path in newest:
        if _rollout_cwd(path) == workspace:
            return path
    return None


def _rollout_cwd(path: Path) -> str:
    """The working directory a rollout's first line names, read alone."""
    try:
        with open(path, encoding="utf-8", errors="replace") as handle:
            data = json.loads(handle.readline())
    except (OSError, ValueError):
        return ""
    payload = data.get("payload") if isinstance(data, dict) else None
    return str((payload or {}).get("cwd") or "") if isinstance(payload, dict) else ""


def live_transcript(source: str, workspace: str | None = None,
                    home: str | None = None) -> Path | None:
    """The transcript a guest is writing right now in `workspace`; an unknown source is a
    ValueError."""
    guest.spec(source)
    if source == "claude":
        return claude_live_transcript(workspace, home)
    return codex_live_transcript(workspace, home)


def guess_source(path: str | Path) -> str:
    """The guest a transcript file belongs to, from where it sits and what it is named: a
    ``rollout-*.jsonl`` is codex's, any other ``.jsonl`` under a projects directory claude's."""
    path = Path(path)
    if CODEX_ROLLOUT.match(path.name):
        return "codex"
    return "claude"


class LiveTail:
    """A read-only, incremental tail of one guest transcript (protocol 26.7).

    The pane holds one of these for the transcript its guest is writing. `refresh()` reads the
    bytes appended since last time — a trailing line still being written waits for its newline —
    and `record()` is the session as it stands, so a running session is in the listing (and
    stays current) without a rescan. Nothing is written anywhere: not the transcript, not the
    guests' directories, not `guest.json` (the event channel belongs to the hooks phase).

    A codex tail also wants the thread's name, which lives in the threads database rather than
    in the rollout. Reading it is a fresh sqlite connection, a `PRAGMA table_info` and a full
    `SELECT` over every thread the user has ever had, so it is cached for `META_REFRESH` seconds
    instead of being paid on every `refresh()` — the pane refreshes its tail as fast as the
    guest writes, and a thread's name changes about once a session.
    """

    META_REFRESH = 30.0            # seconds between reads of codex's threads database

    def __init__(self, path: str | Path, *, source: str | None = None, home: str | None = None):
        self.path = Path(path)
        self.source = source or guess_source(self.path)
        guest.spec(self.source)
        self.home = home
        self._parser = _ClaudeParser() if self.source == "claude" else _CodexParser()
        # `_offset` is the first byte not yet fed to the parser; a partial trailing line stays
        # below it, so it is read again (not twice) once its newline arrives.
        self._offset = 0
        self._size = -1
        self._mtime: float | None = None
        self._record: dict | None = None
        # codex's threads database, read at most every `META_REFRESH` seconds (see the class).
        self._meta: dict[str, dict] = {}
        self._meta_at: float | None = None
        self.refresh()

    def refresh(self) -> dict | None:
        """Consume what the guest appended and return the record as it now stands."""
        try:
            stat = self.path.stat()
        except OSError:
            return self._record
        if stat.st_size < self._offset:              # truncated or rotated: read it again
            self._parser = _ClaudeParser() if self.source == "claude" else _CodexParser()
            self._offset = 0
        if stat.st_mtime == self._mtime and stat.st_size == self._size:
            return self._record                      # nothing appended
        try:
            with open(self.path, "rb") as handle:
                handle.seek(self._offset)
                buffer = handle.read()
        except OSError:
            return self._record
        cut = buffer.rfind(b"\n") + 1                # a partial last line waits for its newline
        complete = buffer[:cut]
        if complete:
            for line in complete.decode("utf-8", "replace").splitlines():
                self._parser.feed(line)
            self._offset += len(complete)
        self._size, self._mtime = stat.st_size, stat.st_mtime
        self._record = self._parsed(stat.st_mtime)
        return self._record

    def _thread_meta(self) -> dict[str, dict]:
        """codex's threads rows, re-read at most every `META_REFRESH` seconds. The whole table
        is cached, not one row, so a thread that appears in it mid-tail is still found without
        another read."""
        now = time.monotonic()
        if self._meta_at is None or now - self._meta_at >= self.META_REFRESH:
            self._meta = codex_thread_meta(guest.codex_state_db(self.home))
            self._meta_at = now
        return self._meta

    def _parsed(self, mtime: float) -> dict | None:
        """The parser's state as a parsed session, with codex's thread name when the database
        has one."""
        meta = None
        if self.source == "codex":
            # The rollout's name carries the thread id before its first line has been read.
            meta = self._thread_meta().get(_file_id(self.path) or self._parser.session_id)
        parser = self._parser
        finished = parser.finish(self.path, mtime)
        # The file's name is the session id both guests resume by (see `parse_claude_transcript`).
        finished["id"] = _file_id(self.path) or finished["id"]
        if self.source == "claude":
            if not finished["workspace"]:
                finished["workspace"] = claude_workspace_from_slug(self.path.parent.name)
            return finished
        return _apply_codex_meta(finished, meta)

    @property
    def parsed(self) -> dict | None:
        """The tailed session in the parser's own shape (what `update_guest` takes)."""
        return self._record

    def record(self, *, fork: bool = False) -> dict | None:
        """The protocol 26.7 record as it stands; `fork` spells the resume command's fork form."""
        return to_record(self._record, fork=fork) if self._record else None
