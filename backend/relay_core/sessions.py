# SPDX-License-Identifier: AGPL-3.0-or-later
"""Session persistence: one JSON file per conversation plus a small metadata file for listing.

Layout under session_dir (0700):
    <id>.json        full state (messages, checkpoints, compaction snapshots), 0600
    <id>.meta.json   {id, title, updated, turns, model}, 0600
    <id>.rewound.jsonl  one line per rewind: the turns it undid, verbatim (newest REWOUND_KEPT), 0600
    <id>.scrollback.txt the session's saved terminal text (written by the GUI), 0600
    <id>.rewound-<n>.scrollback.txt  the terminal text a rewind undid, beside its record, 0600
    <id>.blobs/      checkpoint file pre-images (content-addressed)
    <id>.threads/    subagent threads this session started, one <thread-id>.json each, 0600:
                     {kind: "relay_subagent_thread", id, owner_session, parent_thread, spawn_turn,
                      agent_id, type, description, status, model, models, usage, messages, ...}
                     A thread belongs to the session that was in the pane when it was started (its
                     *owner session*); a thread started by another thread names it in parent_thread.
Sessions can contain tool output and file contents, so they never leave this machine.

Every save also refreshes the full-text index (`relay_core.conv_index`), a cache beside the
sessions that the conversation list searches. Indexing failures never fail a save: the index can
always be rebuilt from these files.
"""
from __future__ import annotations

import hashlib
import json
import os
from .filelock import chmod_fd
import re
import sqlite3
import tempfile
import uuid
from pathlib import Path

from . import conv_index, logs
from .titles import MAX_SUMMARY

log = logs.get("sessions")

SESSION_ID = re.compile(r"^[0-9a-f]{32}$")
MAX_LISTED = 200
STATE_VERSION = 1
ROLES = {"user", "assistant", "tool"}
MAX_STATE_MESSAGES = 50000
REWOUND_KEPT = 20           # rewound branches a session keeps; the oldest drop off the front


def new_id() -> str:
    return uuid.uuid4().hex


def default_session_dir(workspace: str | Path) -> Path:
    base = os.environ.get("XDG_DATA_HOME") or str(Path.home() / ".local" / "share")
    # One spelling of the workspace for the directory and the index (conv_index.normalize_workspace).
    digest = hashlib.sha256(conv_index.normalize_workspace(workspace).encode("utf-8")).hexdigest()[:16]
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


# ----- usage totals and models (session info, card #Y63Z) -----------------------------------

USAGE_KEYS = ("prompt_tokens", "completion_tokens", "total_tokens", "requests")
# Reported by some providers and not others, so they are summed only once one of them does: a
# missing key is "not reported", not zero. `cost` is OpenRouter's; the two cache counters are
# `provider.cache_counts()`'s normalisation of everyone's spelling (#GMCF decision 5).
OPTIONAL_USAGE_KEYS = ("cached_tokens", "cache_write_tokens")


def empty_usage() -> dict:
    return {key: 0 for key in USAGE_KEYS}


def add_usage(totals: dict, usage: dict) -> dict:
    """Add one provider `usage` report to running totals, in place.

    Only what the provider reported is counted; nothing is estimated. `cost` appears only once a
    provider reports one (OpenRouter's `usage.cost`), so a missing key means "not reported", never 0.
    `cached_tokens` / `cache_write_tokens` follow the same rule: a provider that says nothing about
    its prefix cache leaves them off the totals entirely, and the displays say "not reported by this
    provider" rather than claiming nothing was cached.
    """
    if not isinstance(usage, dict):
        return totals
    prompt, completion = usage.get("prompt_tokens"), usage.get("completion_tokens")
    total = usage.get("total_tokens")
    for key, value in (("prompt_tokens", prompt), ("completion_tokens", completion)):
        if isinstance(value, int) and not isinstance(value, bool) and value >= 0:
            totals[key] = totals.get(key, 0) + value
    if not (isinstance(total, int) and not isinstance(total, bool)):
        total = sum(v for v in (prompt, completion) if isinstance(v, int) and not isinstance(v, bool))
    totals["total_tokens"] = totals.get("total_tokens", 0) + max(0, total)
    totals["requests"] = totals.get("requests", 0) + 1
    for key in OPTIONAL_USAGE_KEYS:
        value = usage.get(key)
        if isinstance(value, int) and not isinstance(value, bool) and value >= 0:
            totals[key] = totals.get(key, 0) + value
    cost = usage.get("cost")
    if isinstance(cost, (int, float)) and not isinstance(cost, bool) and cost >= 0:
        totals["cost"] = round(totals.get("cost", 0.0) + float(cost), 6)
    return totals


def load_usage(value) -> dict:
    """Totals read back from a session file; anything malformed counts as nothing."""
    totals = empty_usage()
    if isinstance(value, dict):
        for key in USAGE_KEYS:
            if isinstance(value.get(key), int) and not isinstance(value.get(key), bool) and value[key] >= 0:
                totals[key] = value[key]
        for key in OPTIONAL_USAGE_KEYS:
            if isinstance(value.get(key), int) and not isinstance(value.get(key), bool) and value[key] >= 0:
                totals[key] = value[key]
        if isinstance(value.get("cost"), (int, float)) and not isinstance(value.get("cost"), bool):
            totals["cost"] = float(value["cost"])
    return totals


def note_model(models: list, model: str) -> list:
    """Remember a model this conversation ran on, in first-use order (at most 50)."""
    if isinstance(model, str) and model and model not in models and len(models) < 50:
        models.append(model)
    return models


def models_with(models: list, current: str) -> list:
    """The models to record: those that reported usage, plus the current one."""
    return note_model(list(models), current)


def _atomic_text(path: Path, text: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True, mode=0o700)
    fd, temp = tempfile.mkstemp(dir=path.parent, prefix=".session-")
    try:
        with os.fdopen(fd, "w", encoding="utf-8") as out:
            chmod_fd(out.fileno(), 0o600)
            out.write(text)
        os.replace(temp, path)
    finally:
        if os.path.exists(temp):
            os.unlink(temp)


def _atomic_json(path: Path, data: dict) -> None:
    _atomic_text(path, json.dumps(data, ensure_ascii=False))


def _meta_stamp(path: Path):
    """(inode, mtime_ns, size) of a meta file, or None when there is none.

    The cheap "these are still the bytes this process wrote" test: every writer of
    ``<id>.meta.json`` — save() below, conv_index.write_user_fields — puts it there with
    os.replace, so anyone else's write lands as a new inode with a new timestamp.
    """
    try:
        status = os.stat(path)
    except OSError:
        return None
    return (status.st_ino, status.st_mtime_ns, status.st_size)


def _meta_unchanged(fresh: dict, written: dict) -> bool:
    """Whether two meta bodies say the same thing. `updated` is the clock, not news: it moves at
    every save, and the listing reads it from the next save that does change something."""
    if fresh.keys() != written.keys():
        return False
    return all(value == written[key] for key, value in fresh.items() if key != "updated")


def _user_fields_after(user: dict, meta: dict) -> dict:
    """What conv_index.read_user_fields would read back out of the meta file save() has just
    written: the fields it read, with this session's own summary on top — the one thing save()
    puts there that did not come from the file. Cached so the next save need not open it to find
    out (#GMCF).
    """
    summary = meta.get("summary") or ""
    if summary == (user.get("summary") or ""):
        return user
    return {**user, "summary": summary, "custom_title": user.get("custom_title"),
            "pinned": bool(user.get("pinned"))}


def read_meta(directory: str | Path, session_id: str) -> dict:
    """``<id>.meta.json`` as a dict; empty when it is missing or unreadable.

    The *user* fields inside it (custom title, pin, a summary written while nobody had the session
    open) are conv_index's, read and written through read_user_fields/write_user_fields; this is
    the whole file, for the listing and for what save() must not drop.
    """
    try:
        with open(Path(directory) / f"{check_id(session_id)}.meta.json", encoding="utf-8") as handle:
            meta = json.load(handle)
    except (OSError, ValueError):
        return {}
    return meta if isinstance(meta, dict) else {}


class SessionStore:
    def __init__(self, directory: str | Path, index=None):
        self.directory = Path(directory).expanduser()
        if not self.directory.is_absolute():
            raise ValueError("session_dir must be an absolute path.")
        # None: open the shared index lazily on the first save. False: never index (tests, RELAY_INDEX=off).
        self._index = index
        # Per session id, what the last save() wrote to `<id>.meta.json`: its stamp, its body, and
        # the user fields that body reads back as. A save used to open and parse that file twice
        # for bytes it had written itself a moment earlier (#GMCF); with this it opens it only
        # when somebody else has been there. A pane's store holds one session, so the dict stays
        # tiny; a store handed a stream of ids (the session manager) drops it rather than grow.
        self._wrote: dict[str, dict] = {}

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
                    # A schema change discards the cache; whoever opens it first refills it.
                    self._index = conv_index.ConversationIndex(rebuild_on_reset=True)
                except (OSError, sqlite3.Error):
                    self._index = False
        return self._index or None

    def path(self, session_id: str) -> Path:
        return self.directory / f"{check_id(session_id)}.json"

    def blob_dir(self, session_id: str) -> Path:
        return self.directory / f"{check_id(session_id)}.blobs"

    def thread_dir(self, owner_id: str) -> Path:
        return self.directory / f"{check_id(owner_id)}.threads"

    def save(self, data: dict) -> None:
        session_id = check_id(data["id"])
        meta_path = self.directory / f"{session_id}.meta.json"
        wrote = self._wrote.get(session_id)
        stamp = _meta_stamp(meta_path)
        mine = wrote is not None and stamp is not None and stamp == wrote["stamp"]
        # The conversation itself is written every time it is asked for, and #GMCF left it that
        # way: every save of a turn carries something a crash must not take with it, and a save
        # that truly carries nothing is rare enough that recognising one — re-serialising the
        # session to compare it with the file — measured as dear as the write it would have saved
        # (docs/qa_evidence/2026-09-20-perf-fixes/saves). What is skippable is below: the two
        # reads of the small file beside it, and the write of that file when it has no news.
        _atomic_json(self.path(session_id), data)
        meta = {key: data.get(key) for key in ("id", "title", "created", "updated", "turns", "model", "preset",
                                               "open_requests", "models", "usage",
                                               # the agent-written summary and the workspace's branch
                                               "summary", "summary_turn", "branch")}
        # The turn a summary written elsewhere covered; the summary itself comes back below.
        # A title or pin the user set in the session manager lives here, not in the index (a cache),
        # and so does a summary written while nobody had the session open; an autosave from the pane
        # must not drop either. Both used to be a fresh open and parse of the same small file, twice
        # per save, of bytes this store had written itself (#GMCF): when the file is still exactly
        # the one the last save left, what was read out of it then is what is in it now.
        if mine:
            kept, user = wrote["meta"], wrote["user"]
        else:
            kept = read_meta(self.directory, session_id)
            user = conv_index.read_user_fields(self.directory, session_id)
        for key in ("summary_turn", "branch"):
            if not meta.get(key) and kept.get(key):
                meta[key] = kept[key]
        meta.update({k: v for k, v in user.items() if v})
        # This worker holds the session, so its own summary is the newer one (conv_index's
        # index_session_file merges in the same order).
        if data.get("summary"):
            meta["summary"] = data["summary"]
        if not (mine and _meta_unchanged(meta, wrote["meta"])):
            # Otherwise nothing the sessions list reads out of this file has changed since the last
            # save wrote it, and the only difference would be a newer `updated` — which the next
            # save that does change something carries (#GMCF). A turn running a batch of tools
            # saves the conversation after each result but writes this file once.
            _atomic_json(meta_path, meta)
            if len(self._wrote) >= 8:
                self._wrote.clear()     # a pane's store holds one session; more means a throwaway
            self._wrote[session_id] = {"stamp": _meta_stamp(meta_path), "meta": meta,
                                       "user": _user_fields_after(user, meta)}
        index = self.index()
        if index is not None:
            # The index is a cache: a failure here must never lose the conversation.
            try:
                index.update_session({**data, "custom_title": user.get("custom_title"),
                                      "pinned": bool(user.get("pinned"))}, self.directory)
            except (OSError, ValueError, sqlite3.Error):
                log.exception("session index update failed for %s", session_id)

    def note_summary(self, session_id: str, summary: str, *, meta: bool = True) -> bool:
        """Record a fresh agent-written summary for a saved session.

        It goes into ``<id>.meta.json`` as a user field and into the index, never into the session
        file: another worker may have that session open and owns those bytes. ``meta=False`` is for
        the pane that holds the session itself, whose own save() has just written the same field.
        Returns False when there is no meta file to hold it (the session is gone).
        """
        session_id = check_id(session_id)
        summary = " ".join(str(summary or "").split())[:MAX_SUMMARY]
        if not summary:
            return False
        if meta and not conv_index.write_user_fields(self.directory, session_id, summary=summary):
            return False
        index = self.index()
        # conv_index owns the index schema; set_summary arrived with it, so ask before calling.
        setter = getattr(index, "set_summary", None) if index is not None else None
        if setter is not None:
            try:
                setter(session_id, summary)
            except (OSError, ValueError, TypeError, sqlite3.Error):
                log.exception("session index summary failed for %s", session_id)
        return True

    def set_user_fields(self, session_id: str, **fields) -> None:
        """Rename (custom_title; empty restores the generated one) or pin a saved session."""
        session_id = check_id(session_id)
        if not conv_index.write_user_fields(self.directory, session_id, **fields):
            raise ValueError("No saved session with that id.")
        index = self.index()
        if index is not None:
            try:
                user = conv_index.read_user_fields(self.directory, session_id)
                if "custom_title" in fields:
                    index.rename(session_id, user.get("custom_title") or "")
                if "pinned" in fields:
                    index.set_pinned(session_id, bool(user.get("pinned")))
            except (OSError, ValueError, sqlite3.Error):
                pass

    # ----- rewound branches (card #0TJ9) ------------------------------------------------------
    def rewound_path(self, session_id: str) -> Path:
        return self.directory / f"{check_id(session_id)}.rewound.jsonl"

    def rewound(self, session_id: str) -> list[dict]:
        """The rewound branches kept for a session, oldest first.

        A line that does not parse is skipped rather than failing the read: the file is appended
        to, so a crash mid-write leaves a truncated last line and everything before it still
        counts. Returns [] when there is no file.
        """
        out: list[dict] = []
        try:
            with open(self.rewound_path(session_id), encoding="utf-8") as handle:
                lines = handle.read().splitlines()
        except OSError:
            return out
        for line in lines:
            if not line.strip():
                continue
            try:
                record = json.loads(line)
            except ValueError:
                continue
            if isinstance(record, dict):
                out.append(record)
        return out

    def append_rewound(self, session_id: str, record: dict) -> int:
        """Keep one rewound branch beside the session and return its `n`.

        `n` is 1-based, counts up for the life of the session and is never reused — the GUI names
        `<id>.rewound-<n>.scrollback.txt` after it — so it is taken from the records still here,
        not from how many there are. Only the newest REWOUND_KEPT are kept; trimming rewrites the
        file atomically, appending does not.
        """
        session_id = check_id(session_id)
        path = self.rewound_path(session_id)
        kept = self.rewound(session_id)
        number = max([r.get("n") for r in kept if isinstance(r.get("n"), int)] or [0]) + 1
        record = {"n": number, **{key: value for key, value in record.items() if key != "n"}}
        line = json.dumps(record, ensure_ascii=False) + "\n"
        if len(kept) >= REWOUND_KEPT:
            older = kept[len(kept) - REWOUND_KEPT + 1:]
            _atomic_text(path, "".join(json.dumps(r, ensure_ascii=False) + "\n" for r in older) + line)
        else:
            path.parent.mkdir(parents=True, exist_ok=True, mode=0o700)
            with os.fdopen(os.open(path, os.O_WRONLY | os.O_CREAT | os.O_APPEND, 0o600),
                           "a", encoding="utf-8") as out:
                out.write(line)
        return number

    def sidecars(self, session_id: str) -> list[Path]:
        """The per-session files that are not the session itself: its saved terminal text, its
        rewound branches and the text each of those rewinds undid. The id is validated, so the
        glob below can only match this session's own files."""
        session_id = check_id(session_id)
        found = [self.directory / f"{session_id}.scrollback.txt", self.rewound_path(session_id)]
        try:
            found += sorted(self.directory.glob(f"{session_id}.rewound-*.scrollback.txt"))
        except OSError:
            pass
        return found

    def delete(self, session_id: str) -> dict:
        """Remove a session, its metadata, its sidecars, its checkpoint blobs and its index rows."""
        session_id = check_id(session_id)
        self._wrote.pop(session_id, None)
        removed = 0
        for name in (f"{session_id}.json", f"{session_id}.meta.json"):
            try:
                (self.directory / name).unlink()
                removed += 1
            except OSError:
                pass
        for sidecar in self.sidecars(session_id):
            try:
                sidecar.unlink()
                removed += 1
            except OSError:
                pass
        for folder in (self.blob_dir(session_id), self.thread_dir(session_id)):
            if folder.is_dir():
                for child in folder.iterdir():
                    try:
                        child.unlink()
                    except OSError:
                        pass
                try:
                    folder.rmdir()
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
            items.append({"id": meta["id"], "title": meta.get("custom_title") or meta.get("title") or "",
                          "updated": meta.get("updated"),
                          "turns": meta.get("turns") or 0, "model": meta.get("model") or "",
                          "open_requests": meta.get("open_requests") or 0,
                          "summary": meta.get("summary") or "", "branch": meta.get("branch") or ""})
        items.sort(key=lambda item: item.get("updated") or 0, reverse=True)
        return items[:MAX_LISTED]

    # ----- subagent threads (card #Y63Z) ------------------------------------------------------
    def thread_path(self, owner_id: str, thread_id: str) -> Path:
        return self.thread_dir(owner_id) / f"{check_id(thread_id)}.json"

    def save_thread(self, data: dict) -> Path:
        """Write one subagent thread beside its owner session and refresh its index row."""
        if data.get("kind") != THREAD_KIND:
            raise ValueError("Not a subagent thread.")
        path = self.thread_path(data.get("owner_session"), data.get("id"))
        path.parent.mkdir(parents=True, exist_ok=True, mode=0o700)
        # A rename or pin from the session manager survives the next save of a running thread.
        if path.is_file() and not ("custom_title" in data or "pinned" in data):
            try:
                with open(path, encoding="utf-8") as handle:
                    old = json.load(handle)
                for key in ("custom_title", "pinned"):
                    if isinstance(old, dict) and old.get(key):
                        data = {**data, key: old[key]}
            except (OSError, ValueError):
                pass
        _atomic_json(path, data)
        index = self.index()
        if index is not None:
            try:
                index.update_thread(data, self.directory, path.stat().st_mtime)
            except (OSError, ValueError, sqlite3.Error):
                log.exception("thread index update failed for %s", data.get("id"))
        return path

    def load_thread(self, thread_id: str, owner_id: str | None = None) -> dict:
        """One thread by id. Without the owner, every owner folder here is looked in."""
        check_id(thread_id)
        if owner_id:
            candidates = [self.thread_path(owner_id, thread_id)]
        else:
            candidates = sorted(self.directory.glob(f"*.threads/{thread_id}.json"))
        for path in candidates:
            if path.is_file():
                try:
                    with open(path, encoding="utf-8") as handle:
                        data = json.load(handle)
                except (OSError, ValueError):
                    continue
                if isinstance(data, dict) and data.get("kind") == THREAD_KIND and data.get("id") == thread_id:
                    data["_path"] = str(path)
                    return data
        raise ValueError("No saved subagent thread with that id.")

    def set_thread_fields(self, thread_id: str, **fields) -> None:
        data = self.load_thread(thread_id)
        path = Path(data.pop("_path"))
        if "custom_title" in fields:
            title = " ".join(str(fields["custom_title"] or "").split())[:200]
            data["custom_title"] = title or None
        if "pinned" in fields:
            data["pinned"] = bool(fields["pinned"])
        if path != self.thread_path(data.get("owner_session"), thread_id):
            raise ValueError("That thread file does not belong to its owner session.")
        self.save_thread(data)

    def threads(self, owner_id: str) -> list[dict]:
        """Summaries of the threads a session owns, in the order they were started."""
        folder = self.thread_dir(owner_id)
        out = []
        if folder.is_dir():
            for path in folder.glob("*.json"):
                try:
                    with open(path, encoding="utf-8") as handle:
                        data = json.load(handle)
                except (OSError, ValueError):
                    continue
                if isinstance(data, dict) and data.get("kind") == THREAD_KIND and SESSION_ID.match(str(data.get("id"))):
                    record = thread_summary(data)
                    record["file"] = str(path)
                    out.append(record)
        out.sort(key=lambda item: (item.get("created") or 0, item.get("agent_id") or ""))
        return out


THREAD_KIND = "relay_subagent_thread"
THREAD_FIELDS = ("id", "agent_id", "type", "description", "title", "status", "owner_session", "parent_thread",
                 "spawn_turn", "spawn_call", "background", "workspace", "model", "models", "created", "updated",
                 "runs", "result_preview")


def thread_summary(data: dict) -> dict:
    """A thread without its messages: what a list, a link or an index row needs."""
    out = {key: data.get(key) for key in THREAD_FIELDS}
    out["title"] = (data.get("custom_title") or out.get("title") or out.get("description")
                    or out.get("agent_id") or "Subagent")
    out["usage"] = load_usage(data.get("usage"))
    out["message_count"] = len(data.get("messages") or [])
    return out


# ----- session info (card #Y63Z) -------------------------------------------------------------

HISTORY_PROMPT_CHARS = 400
THREAD_TEXT_CHARS = 4000
MAX_HISTORY_ITEMS = 2000


def git_branch(workspace: str | Path) -> str:
    """The checked-out branch of the workspace's repository, read from .git/HEAD (no git process).
    Empty when it is not a repository; a detached HEAD gives the short commit."""
    try:
        root = Path(workspace).expanduser()
        for folder in (root, *root.parents):
            dot = folder / ".git"
            if dot.is_file():   # a worktree or submodule: "gitdir: <path>"
                target = dot.read_text(encoding="utf-8", errors="replace").strip()
                if target.startswith("gitdir:"):
                    dot = (folder / target[7:].strip()).resolve()
            if dot.is_dir():
                head = (dot / "HEAD").read_text(encoding="utf-8", errors="replace").strip()
                if head.startswith("ref: refs/heads/"):
                    return head[len("ref: refs/heads/"):]
                return head[:12]
    except OSError:
        return ""
    return ""


def _thread_link(summary: dict) -> dict:
    keep = ("id", "agent_id", "type", "title", "description", "status", "model", "spawn_turn", "spawn_call",
            "parent_thread", "owner_session", "created", "updated", "usage", "file", "runs", "custom_title")
    return {key: summary.get(key) for key in keep if key in summary}


def session_history(data: dict, threads: list[dict]) -> tuple[list[dict], list[dict]]:
    """The session's turns in order, each with the subagent threads started during it.

    Returns (turns, unplaced): threads whose turn is unknown or no longer listed (rewound) are
    returned separately so nothing is hidden. Only threads the session itself started are placed
    on turns; a thread started by another thread hangs under its parent (`children`)."""
    items = [i for i in ((data.get("checkpoints") or {}).get("items") or []) if isinstance(i, dict)]
    by_parent: dict[str, list[dict]] = {}
    top = []
    for thread in threads:
        if thread.get("parent_thread"):
            by_parent.setdefault(thread["parent_thread"], []).append(thread)
        else:
            top.append(thread)

    def link(thread: dict) -> dict:
        out = _thread_link(thread)
        out["children"] = [link(child) for child in by_parent.get(thread.get("id"), [])]
        return out

    turns, placed = [], set()
    for item in items[-MAX_HISTORY_ITEMS:]:
        number = item.get("turn")
        prompt = str(item.get("prompt") or item.get("prompt_preview") or "")
        entry = {"turn": number, "prompt": " ".join(prompt.split())[:HISTORY_PROMPT_CHARS],
                 "time": item.get("time"), "ended": item.get("ended"),
                 "files": len(item.get("files") or {}), "threads": []}
        for thread in top:
            if thread.get("spawn_turn") == number and number is not None:
                entry["threads"].append(link(thread))
                placed.add(thread.get("id"))
        turns.append(entry)
    unplaced = [link(t) for t in top if t.get("id") not in placed]
    return turns, unplaced


def thread_history(data: dict, children: list[dict]) -> list[dict]:
    """A thread's own conversation for the info view: its messages in order (text capped, tool
    calls by name and a short argument preview), with each thread it started placed right after
    the message whose tool call started it."""
    by_call = {c.get("spawn_call"): c for c in children if c.get("spawn_call")}
    placed: set = set()
    out: list[dict] = []
    messages = [m for m in (data.get("messages") or []) if isinstance(m, dict)]
    if not messages and data.get("task"):
        messages = [{"role": "user", "content": data["task"]}]
    for message in messages[-MAX_HISTORY_ITEMS:]:
        role = message.get("role")
        if role not in ("user", "assistant", "tool"):
            continue
        item = {"role": role, "text": str(message.get("content") or "")[:THREAD_TEXT_CHARS]}
        calls = []
        for call in message.get("tool_calls") or []:
            if not isinstance(call, dict):
                continue
            function = call.get("function") or {}
            arguments = function.get("arguments")
            if not isinstance(arguments, str):
                arguments = json.dumps(arguments, ensure_ascii=False) if arguments else ""
            calls.append({"id": call.get("id"), "name": function.get("name") or "tool",
                          "arguments": " ".join(arguments.split())[:200]})
        if calls:
            item["tool_calls"] = calls
        started = [by_call[c["id"]] for c in calls if c.get("id") in by_call]
        if started:
            item["threads"] = [_thread_link(t) for t in started]
            placed.update(t.get("id") for t in started)
        out.append(item)
    rest = [_thread_link(c) for c in children if c.get("id") not in placed]
    if rest:
        out.append({"role": "threads", "text": "", "threads": rest})
    return out
