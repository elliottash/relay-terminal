# SPDX-License-Identifier: GPL-3.0-or-later
"""Session persistence: one JSON file per conversation plus a small metadata file for listing.

Layout under session_dir (0700):
    <id>.json        full state (messages, checkpoints, compaction snapshots), 0600
    <id>.meta.json   {id, title, updated, turns, model}, 0600
    <id>.blobs/      checkpoint file pre-images (content-addressed)
Sessions can contain tool output and file contents, so they never leave this machine.

Every save also refreshes the full-text index (`relay_core.conv_index`), a cache beside the
sessions that the conversation list searches. Indexing failures never fail a save: the index can
always be rebuilt from these files.
"""
from __future__ import annotations

import hashlib
import json
import os
import re
import sqlite3
import tempfile
import uuid
from pathlib import Path

from . import conv_index, logs

log = logs.get("sessions")

SESSION_ID = re.compile(r"^[0-9a-f]{32}$")
MAX_LISTED = 200
STATE_VERSION = 1
ROLES = {"user", "assistant", "tool"}
MAX_STATE_MESSAGES = 50000


def new_id() -> str:
    return uuid.uuid4().hex


def default_session_dir(workspace: str | Path) -> Path:
    base = os.environ.get("XDG_DATA_HOME") or str(Path.home() / ".local" / "share")
    digest = hashlib.sha256(str(Path(workspace).expanduser().resolve()).encode("utf-8")).hexdigest()[:16]
    return Path(base) / "relay" / "sessions" / digest


def check_id(session_id) -> str:
    if not isinstance(session_id, str) or not SESSION_ID.match(session_id):
        raise ValueError("Invalid session id.")
    return session_id


def validate_messages(messages) -> list[dict]:
    """Accept only chat messages Relay itself produces (no system messages; the prompt is rebuilt)."""
    if not isinstance(messages, list) or len(messages) > MAX_STATE_MESSAGES:
        raise ValueError("State messages must be a list.")
    for message in messages:
        if not isinstance(message, dict) or message.get("role") not in ROLES:
            raise ValueError("State contains an invalid message.")
        content = message.get("content")
        if content is not None and not isinstance(content, str):
            raise ValueError("State message content must be text.")
        if message["role"] == "tool" and not isinstance(message.get("tool_call_id"), str):
            raise ValueError("Tool message without tool_call_id.")
        if "tool_calls" in message and not isinstance(message["tool_calls"], list):
            raise ValueError("Invalid tool_calls in state.")
    return messages


def _atomic_json(path: Path, data: dict) -> None:
    path.parent.mkdir(parents=True, exist_ok=True, mode=0o700)
    fd, temp = tempfile.mkstemp(dir=path.parent, prefix=".session-")
    try:
        with os.fdopen(fd, "w", encoding="utf-8") as out:
            os.fchmod(out.fileno(), 0o600)
            json.dump(data, out, ensure_ascii=False)
        os.replace(temp, path)
    finally:
        if os.path.exists(temp):
            os.unlink(temp)


class SessionStore:
    def __init__(self, directory: str | Path, index=None):
        self.directory = Path(directory).expanduser()
        if not self.directory.is_absolute():
            raise ValueError("session_dir must be an absolute path.")
        # None: open the shared index lazily on the first save. False: never index (tests, RELAY_INDEX=off).
        self._index = index

    def index(self):
        """The conversation index, or None when it is off, elsewhere, or could not be opened.

        Only sessions in Relay's own data directory are indexed: the index lives beside them, and
        a pane pointed at some other `session_dir` (tests, throwaway directories) stays out of it.
        """
        if self._index is None:
            root = conv_index.sessions_root()
            inside = self.directory == root or root in self.directory.parents
            if not inside or not conv_index.enabled():
                self._index = False
            else:
                try:
                    self._index = conv_index.ConversationIndex()
                except (OSError, sqlite3.Error):
                    self._index = False
        return self._index or None

    def path(self, session_id: str) -> Path:
        return self.directory / f"{check_id(session_id)}.json"

    def blob_dir(self, session_id: str) -> Path:
        return self.directory / f"{check_id(session_id)}.blobs"

    def save(self, data: dict) -> None:
        session_id = check_id(data["id"])
        _atomic_json(self.path(session_id), data)
        meta = {key: data.get(key) for key in ("id", "title", "created", "updated", "turns", "model", "preset", "open_requests")}
        _atomic_json(self.directory / f"{session_id}.meta.json", meta)
        index = self.index()
        if index is not None:
            # The index is a cache: a failure here must never lose the conversation.
            try:
                index.update_session(data, self.directory)
            except (OSError, ValueError, sqlite3.Error):
                log.exception("session index update failed for %s", session_id)

    def delete(self, session_id: str) -> dict:
        """Remove a session, its metadata, its checkpoint blobs and its index rows."""
        session_id = check_id(session_id)
        removed = 0
        for name in (f"{session_id}.json", f"{session_id}.meta.json"):
            try:
                (self.directory / name).unlink()
                removed += 1
            except OSError:
                pass
        blobs = self.blob_dir(session_id)
        if blobs.is_dir():
            for child in blobs.iterdir():
                try:
                    child.unlink()
                except OSError:
                    pass
            try:
                blobs.rmdir()
                removed += 1
            except OSError:
                pass
        index = self.index()
        if index is not None:
            try:
                index.delete_session(session_id, remove_files=False)
            except (OSError, ValueError, sqlite3.Error):
                pass
        if not removed:
            raise ValueError("No saved session with that id.")
        return {"session_id": session_id, "files": removed}

    def load(self, session_id: str) -> dict:
        path = self.path(session_id)
        if not path.is_file():
            raise ValueError("No saved session with that id.")
        with open(path, encoding="utf-8") as handle:
            data = json.load(handle)
        if not isinstance(data, dict) or data.get("id") != session_id:
            raise ValueError("Saved session is unreadable.")
        return data

    def listing(self) -> list[dict]:
        if not self.directory.is_dir():
            return []
        items = []
        for path in self.directory.glob("*.meta.json"):
            try:
                with open(path, encoding="utf-8") as handle:
                    meta = json.load(handle)
                check_id(meta.get("id"))
            except (OSError, ValueError, json.JSONDecodeError):
                continue
            items.append({"id": meta["id"], "title": meta.get("title") or "", "updated": meta.get("updated"),
                          "turns": meta.get("turns") or 0, "model": meta.get("model") or "",
                          "open_requests": meta.get("open_requests") or 0})
        items.sort(key=lambda item: item.get("updated") or 0, reverse=True)
        return items[:MAX_LISTED]
