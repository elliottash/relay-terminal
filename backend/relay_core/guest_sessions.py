# SPDX-License-Identifier: AGPL-3.0-or-later
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
unknown session. `resume_cwd` is the working directory the transcript itself names, **as it is
written there** (`raw_cwd`), not the resolved `workspace`: the index resolves a workspace so that
the same project reached two ways groups as one, and a workspace reached through a symlink
resolves to a directory claude never made a `<cwd-slug>` for — `claude -r <id>` then runs in a
real directory that does not hold the session (GT7X). It is empty when the transcript never named
a directory at all, and then the pane's own is as good a guess as any; :func:`resume_spawn` hands
the argv and the directory back as one payload.

Search spans all sources because the rows share the one index; the default listing still shows
Relay's own conversations only, and a query names ``claude``/``codex`` in `sources` to see the
guests (the same rule that keeps subagent threads out unless asked for).

The active pane's live transcript is tailed read-only by :class:`LiveTail`, so a session the
guest is writing right now appears (and stays current) without a rescan. Nothing here writes
onto the guest event spool — that channel belongs to the hooks phase — and nothing here writes
into the guests' own directories at all.
"""
from __future__ import annotations

import hashlib
import json
import os
import re
import sqlite3
import threading
import time
from datetime import datetime, timezone
from pathlib import Path

from . import conv_index, guest, logs

log = logs.get("guest_sessions")

# The two source kinds this module serves (the listing itself lives in conv_index, section 26.3).
GUEST_SOURCES = conv_index.GUEST_SOURCES

# What a session with nothing to call itself is called. One spelling, because the same session
# reached two ways — tailed live (`to_record`) and out of the index (`item_to_record`) — used to be
# blank one way and "Untitled" the other, and the pane's row changed under the user as it indexed.
UNTITLED = "Untitled"
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
                 fork: bool = False, raw_cwd: str | None = None) -> dict:
    """`{argv, cwd}`: what a pane needs to respawn a guest session, in one payload.

    The argv alone is not enough. Both guests resolve a session id against the directory they
    are started in — claude looks for ``<projects>/<cwd-slug>/<id>.jsonl`` — so the pane must
    chdir to `cwd` first, or ``claude -r <id>`` reports an unknown session from anywhere but the
    directory the session was held in. `cwd` is "" when the transcript named no workspace, and
    the spawner then keeps the pane's own directory.

    `raw_cwd` — the directory the transcript names, before Relay resolved it — wins over
    `workspace` when it is known, because that is the spelling the guest filed the session under
    (see the module docstring).
    """
    return {"argv": resume_command(source, session_id, fork=fork),
            "cwd": str(raw_cwd or workspace or "")}


def to_record(data: dict, *, fork: bool = False) -> dict:
    """The protocol 26.7 record of one parsed guest session (see the module docstring)."""
    workspace = str(data.get("workspace") or "")
    return {"source": data.get("source") or "",
            "id": str(data.get("id") or ""),
            "title": " ".join(str(data.get("title") or "").split())[:MAX_TITLE] or UNTITLED,
            "mtime": float(data.get("mtime") or 0.0),
            "workspace": workspace,
            "message_count": int(data.get("message_count") or 0),
            "resume_command": resume_command(data.get("source") or "", data.get("id") or "", fork=fork),
            "resume_cwd": str(data.get("raw_cwd") or "") or workspace}


def item_to_record(item: dict, *, fork: bool = False) -> dict:
    """The same record out of an index row (`search()`'s item shape): the cache's spelling of
    what `to_record` computed at index time, for a listing that reads the index instead of the
    guests' files."""
    source = str(item.get("source") or "")
    updated = item.get("updated") or item.get("created") or 0.0
    workspace = str(item.get("workspace") or "")
    return {"source": source,
            "id": str(item.get("session_id") or item.get("id") or ""),
            "title": " ".join(str(item.get("title") or "").split())[:MAX_TITLE] or UNTITLED,
            "mtime": float(updated if isinstance(updated, (int, float)) else 0.0),
            "workspace": workspace,
            "message_count": int(item.get("turns") or 0),
            "resume_command": resume_command(source, item.get("session_id") or item.get("id") or "",
                                             fork=fork),
            # The row keeps both: `workspace` resolved, for grouping and filters, and the cwd the
            # transcript wrote, which is the one the guest resumes by (see the module docstring).
            "resume_cwd": str(item.get("raw_cwd") or "") or workspace}


def annotate_items(items, *, fork: bool = False) -> list[dict]:
    """Index items with the protocol 26.7 record fields merged into the guest rows.

    A `conversations` answer lists every source at once, so the worker does not have to know
    which rows are guests: a guest item gains `id`, `mtime`, `message_count`, `resume_command`
    (the argv the pane spawns to resume it, `--fork-session`/`codex fork` when `fork`) and
    `resume_cwd` (the directory it must be spawned in — see `resume_spawn`), and every other
    item comes back exactly as the index gave it.

    `workspace` is restated from the record rather than left to the row so the spawn payload is
    complete on its own: a caller that reads only the record fields still knows where to run, and
    `resume_cwd` is the unresolved one the guest filed the session under.
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
    """Fields accumulated from one guest transcript. Subclasses take JSON lines.

    A parser can be stopped and picked up again: `state()` is everything it learned that is not
    already in the index (the counters, the titles, the workspace, how many entries it has
    written), and `restore()` puts it back, so the next run feeds it only the bytes the guest
    appended. `entries` then holds the *new* entries alone — their `seq` continues past the ones
    the index already has, and the per-session entry cap counts both.
    """

    source = ""

    def __init__(self):
        self.session_id = ""
        self.workspace = ""
        # The working directory the transcript itself names, exactly as it is written there.
        # `workspace` is the same path, but the index resolves it (`normalize_workspace`), and a
        # resolved path is the wrong directory to resume a guest in: claude files a session under
        # the slug of the cwd it was *started* in, so a workspace reached through a symlink
        # resumes into a directory where `claude -r <id>` does not know the session (GT7X, B4).
        self.raw_cwd = ""
        self.created: float | None = None
        self.first_prompt = ""
        self.message_count = 0
        self.entries: list[dict] = []
        self._turn = 0
        self._base = 0                  # entries already in the index before this run

    # The title a guest gave the session, by precedence; subclasses fill `titles`.
    titles: dict[str, str]

    def _stamp(self, when) -> None:
        if self.created is None and when is not None:
            self.created = when

    def _add(self, kind: str, text: str, when) -> None:
        text = (text or "").strip()
        if not text or self._base + len(self.entries) >= conv_index.MAX_ENTRIES_PER_SESSION:
            return
        cap = conv_index.MAX_PROMPT if kind == "prompt" else conv_index.MAX_TEXT
        self.entries.append({"turn": max(self._turn, 1), "seq": self._base + len(self.entries) + 1,
                             "kind": kind, "time": when, "text": text[:cap]})

    def title(self) -> tuple[str, str]:
        for key in self.TITLE_KEYS:
            value = _one_line(self.titles.get(key), MAX_TITLE)
            if value:
                return value, key
        if self.first_prompt:
            return _one_line(self.first_prompt, MAX_PROMPT_PREVIEW), "prompt"
        return "", ""

    def state(self) -> dict:
        """What a later run needs to carry on where this one stopped. Small on purpose: it is
        stored per transcript in the index, and the entries themselves are already rows there.
        `first_prompt` is kept only as far as the title fallback reads it."""
        return {"session_id": self.session_id, "workspace": self.workspace, "raw_cwd": self.raw_cwd,
                "created": self.created, "first_prompt": _one_line(self.first_prompt, MAX_PROMPT_PREVIEW),
                "message_count": self.message_count, "turn": self._turn,
                "entries": self._base + len(self.entries), "titles": dict(self.titles)}

    def restore(self, state: dict) -> bool:
        """Adopt a `state()` from a previous run. False when there is nothing usable to adopt."""
        if not isinstance(state, dict) or not state:
            return False
        self.session_id = str(state.get("session_id") or "")
        self.workspace = str(state.get("workspace") or "")
        self.raw_cwd = str(state.get("raw_cwd") or "")
        created = state.get("created")
        self.created = (float(created) if isinstance(created, (int, float))
                        and not isinstance(created, bool) else None)
        self.first_prompt = str(state.get("first_prompt") or "")
        self.message_count = max(0, int(state.get("message_count") or 0))
        self._turn = max(0, int(state.get("turn") or 0))
        self._base = max(0, int(state.get("entries") or 0))
        titles = state.get("titles") if isinstance(state.get("titles"), dict) else {}
        self.titles = {str(key): str(value) for key, value in titles.items()}
        self.entries = []
        return True

    def finish(self, path, mtime: float) -> dict:
        title, title_kind = self.title()
        return {"source": self.source, "id": self.session_id, "file": str(path), "title": title,
                "title_kind": title_kind, "workspace": self.workspace, "raw_cwd": self.raw_cwd,
                "created": self.created if self.created is not None else mtime,
                "mtime": float(mtime), "message_count": self.message_count,
                "entries": self.entries, "entry_base": self._base}


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
            self.workspace = self.raw_cwd = data["cwd"]
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
                self.workspace = self.raw_cwd = str(payload.get("cwd") or "")
        elif kind == "turn_context":
            if not self.workspace:
                self.workspace = self.raw_cwd = str(payload.get("cwd") or "")
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


def _new_parser(source: str) -> _Parser:
    guest.spec(source)
    return _ClaudeParser() if source == "claude" else _CodexParser()


class _Cursor:
    """How far one guest transcript has been read, and the parser that got there.

    Both readers of a transcript want the same four things — where the last read stopped, whether
    the file is still the file it stopped in, a partial trailing line left for next time, and a
    parser carried across reads — so they share this instead of each keeping their own. The pane's
    :class:`LiveTail` holds one in memory between refreshes; `reconcile` stores one per transcript
    in the index (`ConversationIndex.guest_cursors`) and picks it up on the next run.

    That is what makes a reconcile cheap. Before it, any change to a transcript's mtime re-read
    the whole file and rewrote every entry: the largest transcript on the machine this was written
    on is 99 MB and caps out at 20 000 entries, and a guest that had just answered one prompt paid
    for all of it. jsonl is append-only, so a file that only grew is read from where the last run
    stopped.

    A file that did **not** only grow starts again from zero: it shrank (truncated, or a compacted
    history rewritten shorter), it is a different file under the same name (`st_dev`/`st_ino`), or
    its first bytes are not the ones we read last time (a rewrite that happened to keep the size
    and the inode — a copy back over it, a checkout, `claude -r` rewriting a compacted history).
    """

    HEAD_BYTES = 4096            # of the file's start, fingerprinted to catch a same-size rewrite

    def __init__(self, path: str | Path, source: str | None = None):
        self.path = Path(path)
        self.source = source or guess_source(self.path)
        self.parser = _new_parser(self.source)
        # `offset` is the first byte not yet fed to the parser; a partial trailing line stays below
        # it, so it is read again (not twice) once its newline arrives.
        self.offset = 0
        self.size = -1
        self.mtime: float | None = None
        self.mtime_ns = 0
        self.identity: tuple[int, int] | None = None     # (st_dev, st_ino): which file this is
        self.head = ""                                   # fingerprint of the first HEAD_BYTES
        self.resumed = False                             # started from a stored state
        self.restarted = False                           # …and that state did not fit the file
        self.added = 0                                   # entries the last advance() appended
        self._verify = False                             # a restored state still to be proven

    # ----- saved state ---------------------------------------------------------------
    def state(self) -> dict:
        return {"path": str(self.path), "source": self.source, "size": int(max(self.size, 0)),
                "mtime_ns": int(self.mtime_ns), "read_to": int(self.offset), "head": self.head,
                "identity": list(self.identity) if self.identity else None,
                "parser": self.parser.state()}

    def restore(self, state) -> bool:
        """Pick up where a stored `state()` stopped. The file is not touched here: whether the
        state still fits it is decided by the next `advance()`, which has the stat in hand."""
        if not isinstance(state, dict) or not state.get("read_to"):
            return False
        if str(state.get("path") or "") != str(self.path):
            return False                      # the same session id under a different file
        if not self.parser.restore(state.get("parser") or {}):
            return False
        self.offset = max(0, int(state.get("read_to") or 0))
        self.size = int(state.get("size") or 0)
        self.mtime_ns = int(state.get("mtime_ns") or 0)
        self.head = str(state.get("head") or "")
        identity = state.get("identity")
        named = isinstance(identity, list) and len(identity) == 2
        self.identity = (int(identity[0]), int(identity[1])) if named else None
        self.resumed = True
        self._verify = True
        return True

    def _rewind(self) -> None:
        self.parser = _new_parser(self.source)
        self.offset = 0
        self.size = -1
        self.head = ""
        self.restarted = True

    @staticmethod
    def _fingerprint(head: bytes) -> str:
        """`<length>:<digest>` of a file's first bytes. The length is part of it because the
        window is only as long as the file was when we first read it: a short transcript
        fingerprinted whole would not match itself once the guest had written another line."""
        return f"{len(head)}:{hashlib.sha256(head).hexdigest()[:16]}"

    def _head_now(self) -> str:
        want = self.head.split(":", 1)[0]
        try:
            with open(self.path, "rb") as handle:
                return self._fingerprint(handle.read(int(want)))
        except (OSError, ValueError):
            return ""

    # ----- reading -------------------------------------------------------------------
    def advance(self, stat=None):
        """Feed the parser whatever the guest appended. Returns the file's stat, or None when it
        could not be read at all (and then nothing about the cursor has moved).

        `added` and `restarted` describe **this** read, not the cursor's whole life: a tail that
        had to start over once may still append on its next tick."""
        self.added = 0
        self.restarted = False
        try:
            stat = self.path.stat() if stat is None else stat
        except OSError:
            return None
        identity = (stat.st_dev, stat.st_ino)
        if self._verify:
            self._verify = False
            if (stat.st_size < self.offset or (self.identity is not None and identity != self.identity)
                    or (self.head and self._head_now() != self.head)):
                self._rewind()
        elif self.offset and (stat.st_size < self.offset
                              or (self.identity is not None and identity != self.identity)):
            self._rewind()
        self.identity = identity
        if self.offset and stat.st_mtime == self.mtime and stat.st_size == self.size:
            return stat                                  # nothing appended
        started = self.offset
        try:
            with open(self.path, "rb") as handle:
                handle.seek(started)
                buffer = handle.read()
        except OSError:
            return None
        cut = buffer.rfind(b"\n") + 1                    # a partial last line waits for its newline
        complete = buffer[:cut]
        before = len(self.parser.entries)
        if complete:
            for line in complete.decode("utf-8", "replace").splitlines():
                self.parser.feed(line)
            self.offset += len(complete)
        self.added = len(self.parser.entries) - before
        self.size, self.mtime, self.mtime_ns = stat.st_size, stat.st_mtime, stat.st_mtime_ns
        if not started:
            self.head = self._fingerprint(buffer[:self.HEAD_BYTES])
        return stat

    @property
    def appending(self) -> bool:
        """The entries the parser holds are new ones to add to a row, not the whole session."""
        return self.resumed and not self.restarted

    def record(self, mtime: float | None = None, *, meta: dict | None = None) -> dict:
        """The parsed session as it stands, with the fixups that belong to each guest.

        The file's name is the session id both guests resume by (see `parse_claude_transcript`);
        a claude transcript that never named a working directory takes it from its project
        directory, and a codex rollout takes its name from the threads database.
        """
        stamp = self.mtime if mtime is None else mtime
        finished = self.parser.finish(self.path, stamp if stamp is not None else 0.0)
        finished["id"] = _file_id(self.path) or finished["id"]
        finished["append"] = self.appending
        finished["cursor"] = self.state()
        if self.source == "claude":
            if not finished["workspace"]:
                finished["workspace"] = claude_workspace_from_slug(self.path.parent.name)
            # The project directory's name is claude's own spelling of the cwd it was started in,
            # so it is a raw cwd too — and the only one a transcript without a `cwd` line has.
            if not finished["raw_cwd"]:
                finished["raw_cwd"] = finished["workspace"]
            return finished
        return _apply_codex_meta(finished, meta)


def parse_transcript(path: str | Path, source: str | None = None, *, state: dict | None = None,
                     meta: dict | None = None) -> dict | None:
    """One transcript as a parsed record, continuing from `state` when it still fits the file.

    This is the one parse in the module: the whole-file callers below, `reconcile` and the live
    tail all come through here, so "what a guest transcript means" is written once. The record
    carries two keys the index reads and nobody else needs: `append` (its `entries` are the ones
    added since `entry_base`, not the session's whole history) and `cursor` (where this read
    stopped, stored with the entries so the two can never drift apart).

    None when the file cannot be read at all — which is not the same as "the session is gone".
    """
    cursor = _Cursor(path, source)
    if state:
        cursor.restore(state)
    if cursor.advance() is None:
        return None
    return cursor.record(meta=meta)


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


def parse_claude_transcript(path: str | Path, *, state: dict | None = None) -> dict | None:
    """One claude session as a parsed record.

    The file's name is the session id: claude writes ``<slug>/<session-id>.jsonl`` and resolves
    ``claude -r <id>`` back to that path, so the name is what resumes and what the index row is
    keyed by. It wins over a `sessionId` line that disagrees (a copied or hand-edited
    transcript): keyed any other way, the row the last run indexed is not the one `_walk` looks
    up, its mtime skip never fires and every reconcile re-parses the file. An empty transcript
    is still a session the picker should show, and it has nothing but its name.

    `state` carries a previous read's position (`parse_transcript`); without one the whole file
    is read, which is what a caller that has never seen it before wants."""
    return parse_transcript(path, "claude", state=state)


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
    # The database's `cwd` is the directory codex was started in, written as the user gave it —
    # a raw cwd like the rollout's own, and the only one a rollout with no `session_meta` has.
    if not parsed.get("raw_cwd") and parsed["workspace"]:
        parsed["raw_cwd"] = parsed["workspace"]
    return parsed


def parse_codex_rollout(path: str | Path, *, meta: dict | None = None,
                        state: dict | None = None) -> dict | None:
    """One codex rollout as a parsed record. `meta` is the session's row from the threads
    database (see `codex_thread_meta`); without one the record is the rollout alone.

    The UUID the rollout's name ends in is the thread id the database and ``codex resume`` use,
    so it is the record's id (and the index's key) whenever the name carries one — the same rule
    as claude's, and for the same reason: `_walk` looks a file up by its name before reading it
    (see `parse_claude_transcript`)."""
    return parse_transcript(path, "codex", state=state, meta=meta)


# ----- claude's directory naming -----------------------------------------------------------------


def claude_slug(cwd: str) -> str:
    """The directory name claude gives a working directory: every character that is not a
    letter or a digit becomes a dash (``/home/u/x`` → ``-home-u-x``, ``Dropbox/_Ash_Admin`` →
    ``Dropbox--Ash-Admin``). Encoding is exact; decoding is not (see the reverse)."""
    return re.sub(r"[^A-Za-z0-9]", "-", str(cwd or ""))


def _slug_walk(tokens: list[str], root: str) -> str:
    """The path whose components slugify to `tokens`, found by asking the filesystem.

    The encoding is lossy — a dash, an underscore, a space and a dot all become a dash — so the
    only way back is to look at the directories that exist and see which of them claude would
    have spelled this way. At each level the longest run of tokens that matches exactly one real
    child wins: `relay-terminal` is one directory, not `relay/terminal`, and `_Ash_Admin` comes
    back with its underscores. Two children that slugify alike, or a level with no match at all,
    is an answer this cannot give — "" then, and the caller falls back to the naive split.
    """
    here = Path(root)
    index = 0
    while index < len(tokens):
        try:
            children = [entry.name for entry in os.scandir(here) if entry.is_dir()]
        except OSError:
            return ""
        chosen = ""
        for take in range(len(tokens) - index, 0, -1):
            candidate = "-".join(tokens[index:index + take])
            matches = [name for name in children if claude_slug(name) == candidate]
            if len(matches) > 1:
                return ""                      # two real names claude spells the same way
            if matches:
                chosen, index = matches[0], index + take
                break
        if not chosen:
            return ""
        here = here / chosen
    return str(here)


def claude_workspace_from_slug(slug: str, *, root: str = "/") -> str:
    """The path behind a claude project directory name.

    The transcript's own `cwd` lines are still the truth and `parse_claude_transcript` prefers
    them; this is for the transcripts that never named one. It used to be a naive split on the
    dashes, which is wrong for any real directory whose own name holds one — four of the seven
    project directories on the machine this was written on, including
    `-home-elliott-repos-relay-terminal` → `/home/elliott/repos/relay/terminal`. That is not a
    display wart: the value becomes the row's `workspace` (so the pane scopes it to a project
    that does not exist) and its `resume_cwd` (so `claude -r <id>` runs in the wrong directory and
    reports an unknown session). So the filesystem is asked first, and the naive split is only
    what is left when the directory is gone or two of them are spelled alike.
    """
    text = str(slug or "")
    if not text:
        return ""
    if not text.startswith("-"):
        return text.replace("-", "/")
    tokens = text[1:].split("-")
    return _slug_walk(tokens, root) or "/" + text[1:].replace("-", "/")


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
    """Paths newest first, ties broken by name; one that cannot be stat'ed sorts last.

    Two passes, because one `sorted(..., reverse=True)` over `(mtime, name)` reverses the name
    too: same-mtime files then came back in *descending* name order, which is not what the
    docstring said and made "the newest transcript" look arbitrary whenever a copy or a checkout
    gave several files one timestamp."""
    def stamp(path: Path) -> float:
        try:
            return path.stat().st_mtime
        except OSError:
            return float("-inf")
    return sorted(sorted(paths, key=str), key=stamp, reverse=True)


def source_root(source: str, home: str | None = None) -> Path:
    """The directory a guest keeps its sessions in. `reconcile` asks whether it is there before
    it trusts an empty scan."""
    guest.spec(source)
    return Path(guest.claude_projects_dir(home) if source == "claude"
                else guest.codex_sessions_dir(home))


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
          known: dict[str, float | None] | None = None,
          skip: set[str] | None = None,
          cursors: dict[str, dict] | None = None) -> tuple[list[dict], set[str]]:
    """Newest-first, at most `limit` transcripts through `parse`; a transcript whose indexed
    mtime (`known`, keyed by session id) already matches the file's is not read at all.

    Returns `(records, kept)`. `kept` is every session id that was *seen on disk* but produced no
    record — the ones skipped as unchanged, and the ones that could not be read at all. Both are
    still there, and a caller that prunes by "ids I did not see" must not mistake either for a
    session that went away: a transient `stat` failure or an unreadable transcript used to drop
    the row, and with it the pin or the title the user had given it (there is no file to restore
    those from). The id a file is looked up by here is `_file_id`, which is also the id its parse
    puts on the record and the index keys the row by, so the lookup finds the row the last run
    wrote.

    `skip` names session ids the user deleted (`ConversationIndex.forget`): they are on disk and
    stay there, but they are not read and not indexed, so a delete is not undone by the next scan.

    `cursors` maps a session id to where the last run stopped reading its transcript, so a file
    that only grew is parsed from there (`parse_transcript`). The `parse` callable takes
    `(path, state)`.

    `limit` is a count of files: `None` is every one of them, and a limit of zero or less reads
    none. `[:limit or None]` read *everything* at zero and silently dropped the oldest file at -1.
    """
    records: list[dict] = []
    kept: set[str] = set()
    ordered = _by_age(paths)
    if limit is not None:
        ordered = ordered[:max(int(limit), 0)]
    for path in ordered:
        identifier = _file_id(path)
        if skip and identifier in skip:
            kept.add(identifier)      # forgotten on purpose: seen, deliberately not read
            continue
        try:
            mtime = path.stat().st_mtime
        except OSError:
            kept.add(identifier)      # it is on disk; we just could not look at it this time
            continue
        indexed = (known or {}).get(identifier)
        if indexed is not None and abs(mtime - float(indexed)) < 1e-6:
            kept.add(identifier)
            continue
        record = parse(path, (cursors or {}).get(identifier))
        if record is None:
            kept.add(identifier)      # unreadable or unparsable, but not gone
            continue
        records.append(record)
    return records, kept


def _scan(source: str, home: str | None, *, limit: int | None,
          known: dict[str, float | None] | None, skip: set[str] | None = None,
          cursors: dict[str, dict] | None = None) -> tuple[list[dict], set[str]]:
    """One guest's transcripts (newest first) and the session ids skipped as unchanged."""
    guest.spec(source)
    if source == "claude":
        return _walk(_claude_paths(home),
                     lambda path, state: parse_claude_transcript(path, state=state),
                     limit=limit, known=known, skip=skip, cursors=cursors)
    meta: dict[str, dict] | None = None

    def parse(path: Path, state: dict | None) -> dict | None:
        nonlocal meta
        if meta is None:
            meta = codex_thread_meta(guest.codex_state_db(home))
        return parse_codex_rollout(path, meta=meta.get(_file_id(path)), state=state)

    return _walk(_codex_paths(home), parse, limit=limit, known=known, skip=skip, cursors=cursors)


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


def guests_enabled() -> bool:
    """Whether Relay indexes the guests' sessions at all — the library-level default for
    `reconcile(enabled=…)`.

    `RELAY_INDEX=off` turns the whole index off (`conv_index.enabled`); this is the narrower
    switch behind the GUI's "Index my other agents' sessions" (`sessions/index_guests`, on by
    default), for someone who wants Relay's own search without Relay reading `~/.claude` and
    `~/.codex` at all. The worker passes the setting explicitly; the environment variable is what
    a headless run, a test or a launcher has.
    """
    return os.environ.get("RELAY_INDEX_GUESTS", "").lower() not in ("0", "off", "no", "false")


def purge(index: conv_index.ConversationIndex, *, sources=GUEST_SOURCES) -> int:
    """Drop every guest row from Relay's index and return how many went.

    Index rows only: the guests' own transcripts are never touched (the rule this whole module
    is built on), and neither are the pins, the names and the forgotten set in the guest meta
    store — turn indexing back on and a reconcile brings the rows back with them.
    """
    return index.delete_guests(sources=tuple(s for s in sources if s in GUEST_SOURCES))


def reconcile(index: conv_index.ConversationIndex, home: str | None = None,
              *, sources=GUEST_SOURCES, limit: int | None = None,
              enabled: bool | None = None) -> dict:
    """Bring the guest rows of `index` in line with the guests' own files: index sessions that
    are new or whose transcript changed (its mtime), drop rows whose file is gone. A transcript
    whose mtime matches the indexed one is not even read, and one that only grew is read from
    where the last run stopped (`parse_transcript`), so a worker that reconciles on first use
    pays for the guests' history once and then only for what a guest actually wrote.

    `enabled` is the user's "index my other agents' sessions" setting (default: `guests_enabled()`).
    Switched off, nothing under `~/.claude` or `~/.codex` is read **and** the guest rows are
    purged, so the listing and the search show none of them from the next call on. It purges on
    every disabled call rather than only on the transition: the setting can change while this
    worker is not the one that hears about it, and a `DELETE` over rows that are already gone is
    one indexed lookup. Turning it back on re-indexes, pins and names and all (the meta store
    outlives the rows).

    `limit` caps each source at its N newest files — the first run over a long claude history
    is minutes of parsing, and a pane that only wants to list recent sessions can say so. **A
    limited reconcile never removes a row** (`removed` is always 0): it has not looked at the
    transcripts past the limit, so it cannot tell a session that went away from one it simply
    did not read, and dropping every id it did not see would empty the pane of everything but
    the N newest. Only a full reconcile — the one that stats every file — prunes.

    Returns `{added, refreshed, removed, ms}` like `ConversationIndex.reconcile()`, plus
    `skipped: True` when indexing the guests is switched off.
    """
    sources = tuple(source for source in sources if source in GUEST_SOURCES)
    started = time.time()
    if not (guests_enabled() if enabled is None else enabled):
        removed = purge(index, sources=sources)
        if removed:
            logs.event(log, "guest sessions not indexed; rows dropped", removed=removed,
                       sources=",".join(sources))
        return {"added": 0, "refreshed": 0, "removed": removed, "skipped": True,
                "ms": int((time.time() - started) * 1000)}
    known = index.guest_file_stamps(sources)
    # Only for sessions that still have a row: appending entries to a row that is not there would
    # index a session that starts in the middle (`update_guest` refuses, but not reading the file
    # twice is better than being refused).
    cursors = {identifier: state for identifier, state in index.guest_cursors(sources).items()
               if identifier in known}
    forgotten = index.forgotten(sources)
    seen: set[str] = set()
    prunable: set[str] = set()
    added = refreshed = 0
    for source in sources:
        records, kept = _scan(source, home, limit=limit,
                             known={identifier: row[2] for identifier, row in known.items()},
                             skip={identifier for guest_source, identifier in forgotten
                                   if guest_source == source},
                             cursors=cursors)
        seen |= kept
        # A source whose directory is not there has not "lost every session": `$HOME` can be
        # wrong, a network home can be late, the guest can be uninstalled with its history intact.
        # Pruning on that emptied the pane and took the pins and the custom titles with it — and
        # a guest row has no file beside it to restore them from. So an absent root prunes
        # nothing; a root that is there and empty still does.
        if source_root(source, home).is_dir():
            prunable |= {identifier for identifier, row in known.items() if row[0] == source}
        else:
            log.debug("guest reconcile: %s has no session directory; nothing pruned", source)
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
        for identifier in sorted(prunable - seen):
            # Rows only, never files — and not forgotten either: the transcript is gone, which is
            # not the user saying "never show me this again" (see `ConversationIndex.forget`).
            index.delete_session(identifier, remove_files=False, forget=False)
            removed += 1
    outcome = {"added": added, "refreshed": refreshed, "removed": removed,
               "ms": int((time.time() - started) * 1000)}
    # A reconcile that dropped rows is the one thing here worth a line in the log: it is the only
    # operation that loses something, and "the guest sessions disappeared" is otherwise a mystery.
    logs.event(log, "guest sessions reconciled", sources=",".join(sources),
               limit=-1 if limit is None else int(limit), **outcome)
    return outcome


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


def claude_live_transcript(workspace: str | None = None, home: str | None = None, *,
                           session_id: str | None = None) -> Path | None:
    """The transcript claude is writing in `workspace` right now.

    With a `session_id` it is exactly ``<projects>/<cwd-slug>/<session-id>.jsonl`` — claude names
    the file after the session, so the id *is* the path. Ask for it whenever the caller knows it:
    two claudes running in one directory write two transcripts in the same slug, and "the newest
    file" is then whichever of the two typed last, so each pane tailed the other's session as soon
    as the other answered. Relay passes ``--session-id`` on the sessions it launches, and the
    headless harness knows its own, precisely so this is answerable.

    Without one — a claude the user started themselves, in a shell Relay only watches — it falls
    back to the newest ``.jsonl`` in the project directory (`claude_slug`), or the newest
    anywhere when no workspace is given either.
    """
    root = Path(guest.claude_projects_dir(home))
    if not root.is_dir():
        return None
    if session_id:
        if workspace:
            named = root / claude_slug(workspace) / f"{session_id}.jsonl"
            return named if named.is_file() else None
        for path in _by_age(root.glob(f"*/{session_id}.jsonl")):
            return path
        return None
    folder = root / claude_slug(workspace or "")
    paths = folder.glob("*.jsonl") if workspace else root.glob("*/*.jsonl")
    for path in _by_age(paths):
        return path
    return None


def rollout_thread_id(path: str | Path) -> str:
    """The thread id a rollout's name carries — what ``codex resume`` takes and what the threads
    database keys a row by. Empty when the name ends in no UUID at all."""
    path = Path(path)
    if not CODEX_ROLLOUT.match(path.name):
        return ""
    identifier = _file_id(path)
    return identifier if UUID_TAIL.search(identifier) else ""


def codex_live_rollout(cwd: str | None = None, home: str | None = None, *,
                       thread_id: str | None = None) -> Path | None:
    """The rollout codex is writing in `cwd` right now.

    With a `thread_id` it is that thread's rollout and no other: the threads database is asked
    for its file, and failing that the rollout whose own name ends in that UUID. Two codexes in
    one directory otherwise share every rule below and the newer one wins both tails (the same
    trouble claude has — see `claude_live_transcript`).

    Without one: of the threads the database says belong to that directory, the one whose file
    was written last; without a database (or without a match) the rollouts are walked newest-first
    and their `session_meta` line names the working directory.

    `SELECT … FROM threads` has no `ORDER BY`, so taking the first row whose `cwd` matched handed
    back whichever thread sqlite happened to return first — in practice the *oldest* one the user
    had ever opened in that directory. The tail then followed a transcript nothing was writing
    to, which looks exactly like a guest that has stopped talking."""
    root = Path(guest.codex_sessions_dir(home))
    if not root.is_dir():
        return None
    rollouts = [path for path in root.rglob("*.jsonl") if CODEX_ROLLOUT.match(path.name)]
    if thread_id:
        meta = codex_thread_meta(guest.codex_state_db(home)).get(thread_id) or {}
        named = Path(meta.get("file") or "")
        if str(named) != "." and named.is_file():
            return named
        for path in _by_age(rollouts):
            if rollout_thread_id(path) == thread_id:
                return path
        return None
    if cwd:
        candidates = [Path(meta.get("file") or "")
                      for meta in codex_thread_meta(guest.codex_state_db(home)).values()
                      if meta.get("workspace") == cwd]
        matched = _by_age(path for path in candidates if path.is_file())
        if matched:
            return matched[0]
    newest = _by_age(rollouts)
    if not cwd:
        return newest[0] if newest else None
    for path in newest:
        if _rollout_cwd(path) == cwd:
            return path
    return None


# The name this had before a session could be named (GT7X, B7); one guest, one spelling.
codex_live_transcript = codex_live_rollout


def _rollout_cwd(path: Path) -> str:
    """The working directory a rollout's first line names, read alone."""
    try:
        with open(path, encoding="utf-8", errors="replace") as handle:
            data = json.loads(handle.readline())
    except (OSError, ValueError):
        return ""
    payload = data.get("payload") if isinstance(data, dict) else None
    return str((payload or {}).get("cwd") or "") if isinstance(payload, dict) else ""


def live_transcript(source: str, workspace: str | None = None, home: str | None = None, *,
                    session_id: str | None = None) -> Path | None:
    """The transcript a guest is writing right now in `workspace`, or — when the caller knows
    which session it is after — that session's own file, whatever else is being written in the
    same directory. An unknown source is a ValueError."""
    guest.spec(source)
    if source == "claude":
        return claude_live_transcript(workspace, home, session_id=session_id)
    return codex_live_rollout(workspace, home, thread_id=session_id)


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
    guests' directories, not the guest event spool (that channel belongs to the hooks phase).

    The reading itself is a :class:`_Cursor`, which `reconcile` uses too: "how far have we read
    this transcript, and is it still the same file" is one piece of code, tested once.

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
        self._cursor = _Cursor(self.path, self.source)
        self._record: dict | None = None
        # codex's threads database, read at most every `META_REFRESH` seconds (see the class).
        self._meta: dict[str, dict] = {}
        self._meta_at: float | None = None
        self.refresh()

    @classmethod
    def for_session(cls, source: str, workspace: str | None = None, *,
                    session_id: str | None = None, home: str | None = None) -> "LiveTail | None":
        """A tail of the named session's transcript — or, with no id, of whatever the guest is
        writing in `workspace` (`live_transcript`). None when there is no such transcript yet.

        This is the constructor a caller that knows *which* session it wants should use: two
        guests in one directory write two transcripts, and only the id tells them apart.
        """
        path = live_transcript(source, workspace, home, session_id=session_id)
        return cls(path, source=source, home=home) if path else None

    def refresh(self) -> dict | None:
        """Consume what the guest appended and return the record as it now stands."""
        stat = self._cursor.advance()
        if stat is None:
            return self._record
        if self._record is not None and not self._cursor.added and not self._cursor.restarted \
                and self._cursor.mtime == self._record.get("mtime"):
            return self._record                      # nothing appended
        self._record = self._parsed(self._cursor.mtime if self._cursor.mtime is not None
                                    else stat.st_mtime)
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
            meta = self._thread_meta().get(_file_id(self.path) or self._cursor.parser.session_id)
        return self._cursor.record(mtime, meta=meta)

    @property
    def parsed(self) -> dict | None:
        """The tailed session in the parser's own shape (what `update_guest` takes)."""
        return self._record

    @property
    def entries_read(self) -> int:
        """How many entries this tail has parsed so far — what a writer uses to send only the
        new ones."""
        return self._cursor.parser._base + len(self._cursor.parser.entries)

    @property
    def restarted(self) -> bool:
        """The transcript was replaced or truncated under the tail, so everything read before it
        is no longer what the file says."""
        return self._cursor.restarted

    def record(self, *, fork: bool = False) -> dict | None:
        """The protocol 26.7 record as it stands; `fork` spells the resume command's fork form."""
        return to_record(self._record, fork=fork) if self._record else None


# ----- the live session in the index --------------------------------------------------------------


class GuestTail:
    """The worker's live guest session: a :class:`LiveTail` whose record is kept in the index.

    A reconcile is a scan — it finds out that a transcript changed by stat'ing it, and it runs on
    a timer behind the listing. While a guest is answering in the pane in front of the user, that
    is both too slow and too much: this follows the one transcript that matters and writes only
    what the guest appended.

    The worker calls it like this::

        tail = guest_sessions.GuestTail()                    # once per pane
        tail.start(index, "claude", workspace, session_id)   # when the pane's guest starts
        if tail.poll():                                      # on the background loop's tick
            emit the conversations event again
        tail.stop()                                          # when the guest exits

    `poll()` is cheap enough to call on a timer: below `MIN_POLL` seconds apart it does nothing
    at all, and otherwise it is one `stat` plus whatever bytes the guest wrote. It answers True
    only when rows actually changed, so the pane redraws when there is something to redraw. It
    never raises on the guests' own files: an unreadable transcript is a False, not an error on
    a listing the user already has (the reconcile path has the same rule).
    """

    MIN_POLL = 0.5                 # seconds; a tick sooner than this is free

    def __init__(self, home: str | None = None, *, min_poll: float | None = None):
        self.home = home
        self.min_poll = self.MIN_POLL if min_poll is None else max(0.0, float(min_poll))
        self._lock = threading.Lock()
        self._tail: LiveTail | None = None
        self._index = None
        self._source = ""
        self._session_id = ""
        self._written = 0            # entries already in the index for this session
        self._mtime: float | None = None   # the mtime the index row was written at
        self._at: float | None = None

    # ----- lifecycle -----------------------------------------------------------------
    def start(self, index: conv_index.ConversationIndex, source: str, workspace: str | None = None,
              session_id: str | None = None) -> bool:
        """Follow the named session's transcript (or whatever `source` is writing in `workspace`).

        False when there is no transcript to follow yet — a guest that has not written its first
        line — and the caller may simply try again on the next tick.
        """
        guest.spec(source)
        with self._lock:
            self._reset()
            tail = LiveTail.for_session(source, workspace, session_id=session_id, home=self.home)
            if tail is None:
                return False
            self._tail, self._index, self._source = tail, index, source
            self._session_id = str(session_id or "")
            self._at = None
            self._write()          # what the guest has written so far, before the first tick
            return True

    def stop(self) -> None:
        """Stop following. The last state is already in the index; the next reconcile owns it."""
        with self._lock:
            self._reset()

    def _reset(self) -> None:
        self._tail = self._index = None
        self._source = self._session_id = ""
        self._written = 0
        self._mtime = None
        self._at = None

    @property
    def path(self) -> Path | None:
        tail = self._tail
        return tail.path if tail else None

    @property
    def record(self) -> dict | None:
        """The protocol 26.7 record of the session being followed, as it stands."""
        tail = self._tail
        return tail.record() if tail else None

    # ----- polling -------------------------------------------------------------------
    def poll(self) -> bool:
        """Read what the guest appended and put it in the index. True when rows changed."""
        with self._lock:
            if self._tail is None or self._index is None:
                return False
            now = time.monotonic()
            if self._at is not None and now - self._at < self.min_poll:
                return False
            self._at = now
            return self._write()

    def _write(self) -> bool:
        """The tail's record into the index — appending the new entries where it can."""
        tail, index = self._tail, self._index
        if tail is None or index is None:
            return False
        try:
            parsed = tail.refresh()
        except OSError:
            return False
        if not parsed or not parsed.get("id"):
            return False
        appended = tail.entries_read - self._written
        if (self._mtime is not None and not tail.restarted and appended <= 0
                and parsed.get("mtime") == self._mtime):
            return False                       # the row already says what the file says
        record = dict(parsed)
        # Only the entries the guest added since the last write: a session that has been going
        # for an hour is not rewritten line by line every time it answers.
        record["append"] = self._mtime is not None and not tail.restarted
        if record["append"]:
            record["entries"] = [row for row in parsed.get("entries") or []
                                 if int(row.get("seq") or 0) > self._written]
            # Which entries the row is expected to already hold: if it does not hold them (the
            # row went while the pane was running), `update_guest` refuses rather than index a
            # session that starts in the middle.
            record["entry_base"] = self._written
        try:
            written = index.update_guest(record)
        except (ValueError, sqlite3.Error, OSError) as error:
            logs.event(log, "live guest session not indexed", level_name="error",
                       source=self._source, error=type(error).__name__)
            return False
        if not written and record.get("entries"):
            # Entries handed over and none written: the index refused the session (the user
            # deleted it, or the row it would append to is gone). Nothing changed, and saying so
            # keeps the pane from redrawing a list that is the same as the one it has.
            return False
        self._written = tail.entries_read
        self._mtime = parsed.get("mtime")
        return True

