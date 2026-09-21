# SPDX-License-Identifier: AGPL-3.0-or-later
"""The Switchboard on a paired device (docs/REMOTE-PROTOCOL.md section 17, card #SWPH).

The desktop's Switchboard runs on a per-window ``BoardWorker`` the hub never sees. The GUI bridges
it: a ``full`` device sends ``board_request {rid, request}``, the hub hands the GUI a
``board_request`` line, and the GUI answers with ``board_event {rid | null, event}`` lines carrying
what its worker emitted. This module is the two gates in between, so that no caller has to
remember the rules:

* **Requests are an allow-list, rebuilt field by field.** :func:`clean_request` knows ten request
  types and, per type, which fields exist, their shapes and their caps. A card id has the board's
  own alphabet, a status is one of the board's statuses, a mode, a kind and an action are
  enumerations. Anything else the device sent is not copied. Three things are *refused* rather
  than dropped, because each is somebody trying a door: a type on the never-list (deleting,
  folders, cleanup, claiming, attaching or initializing a board, imports, GitHub sync), a key named
  like a path anywhere in the request, and an unlisted field whose value looks like an absolute
  path. The phone never names a file; the desktop's worker resolves a card id against its own
  board.
* **Events are an allow-list of types, scrubbed recursively.** :func:`clean_event` passes only the
  event types the contract names and drops (and counts) the rest. Inside one that passes, every
  key named like a path is dropped at every depth, a desktop-written string that is a path is
  dropped and one that contains a path has it cut out, strings and lists are capped, and the whole
  event is fitted under the wire's message size. A card's body and its thread entries pass as
  text: they are the owner's notes, already in git, and the device reading them is the owner's.
* **Never to a guest, and only to ``full``.** The hub's sender re-reads the capability as it sends
  (remote/host.py); ``board_request`` is in ``wire.GUEST_NEVER`` and ``board_event`` is absent
  from ``wire.GUEST_SERVER_TYPES``.

Nothing here imports the backend: the sidecar runs without it on its path. The statuses, the card
id alphabet and the comment kinds are therefore written out, and ``tests/test_remote_board.py``
compares each with ``backend/relay_core`` so they cannot drift.
"""
from __future__ import annotations

import hashlib
import json
import math
import re
import secrets
import time
from collections import Counter, OrderedDict
from dataclasses import dataclass, field

from . import wire
from .pane_state import _CONTROL, _SECRET

# ---- what the board calls things ---------------------------------------------------------------
# Pinned copies of backend/relay_core/board.py's `ID_RE` and `ALL_STATUSES`, and a subset of
# board_tools.py's `COMMENT_KINDS`; a test compares them.

CARD_ID = re.compile(r"^[0-9A-HJKMNP-TV-Z]{4}$")
STATUSES = ("inbox", "discussing", "planning", "planned", "ready", "executing", "in-progress",
            "needs-verification", "needs-qa-llm", "needs-qa-human", "needs-review",
            "needs-labels", "needs-ab", "deferred", "done", "dropped", "active", "retired")
# A person's kinds only: `evidence` and `progress` are what an agent or the desktop's own hand-off
# writes, and the desktop bridge (src/BoardRemote.cpp) accepts exactly these three from a device.
COMMENT_KINDS = ("note", "question", "decision")
ASK_MODES = ("discuss", "plan")
ACTIONS = ("execute", "verify")

TAB_ID = re.compile(r"^[a-z0-9][a-z0-9_-]{0,39}$")
LABEL = re.compile(r"^[A-Za-z0-9][A-Za-z0-9 _.:+-]{0,39}$")
LABELS_MAX = 16
_TYPE_NAME = re.compile(r"^[a-z_]{1,40}$")
RID = re.compile(r"^[A-Za-z0-9_.:-]{1,64}$")
RID_INT_MAX = 2 ** 53

# ---- requests: the caps --------------------------------------------------------------------------

TEXT_MAX = 8_000
TITLE_MAX = 200
QUERY_MAX = 200
REASON_MAX = 500

#: Requests that only read. Everything else in REQUESTS writes (or starts a turn that does).
READS = frozenset({"board_open", "board_refresh", "board_card_get", "board_search"})

#: type -> the fields that exist for it. The cleaner below is the one reader.
REQUESTS: dict[str, tuple[str, ...]] = {
    "board_open": (),
    "board_refresh": (),
    "board_card_get": ("id",),
    "board_search": ("query",),
    "board_comment": ("id", "text", "kind"),
    "board_move": ("id", "status", "reason"),
    "board_create": ("tab", "title", "request", "labels"),
    "board_ask": ("id", "text", "mode"),
    "board_cancel": ("id",),
    "board_action": ("id", "action"),
}

#: Considered and refused, with the reason, so the refusal is a decision a reader can find.
NEVER: dict[str, str] = {
    "board_delete": "deleting a card is the desktop's, behind its own confirmation",
    "board_cleanup": "a whole-board cleanup is started at the desktop",
    "board_cleanup_cancel": "a whole-board cleanup is the desktop's",
    "board_claim": "a claim names a desktop pane's session token",
    "board_update": "editing a card's text needs the hash it was read at; not a device action yet",
    "board_priority": "not among a device's actions",
    "board_undo": "undo belongs to the desktop's own toast",
    "board_check": "the board's problems list names local files",
    "set_board": "attaching a project names a local directory",
    "board_init": "initializing a board names a local directory",
    "board_init_answer": "initializing a board is the desktop's dialog",
    "board_folder": "the board's folder is a local path",
    "board_folder_set": "the board's folder is a local path",
    "board_folder_hide": "the board's folder is a local path",
    "board_folder_show": "the board's folder is a local path",
    "project_probe": "surveys a local directory",
    "board_import_propose": "imports read local files",
    "board_import_apply": "imports read local files",
    "board_survey": "surveys a local directory",
    "forge_sync_plan": "GitHub sync is started at the desktop",
    "forge_sync_run": "GitHub sync is started at the desktop",
    "configure": "provider configuration",
}
#: Any type starting with one of these is on the never-list too, whatever follows.
NEVER_PREFIXES = ("board_folder", "board_init", "board_import", "board_cleanup", "forge_",
                  "project_")

#: A key with one of these names is refused in a request and dropped from an event, at any depth.
PATH_KEYS = frozenset({"path", "paths", "root", "folder", "file", "files", "dir", "cwd",
                       "workspace", "project", "repo", "directory"})
_PATH_KEY_SUFFIX = ("_path", "_root", "_dir", "_file", "_folder", "_cwd")

# A whole value that is an absolute path: /a/b, ~/a, C:\a, file://…
_ABSOLUTE = re.compile(r"^(?:~?/[^\s/]+(?:/\S*)+|~/\S+|[A-Za-z]:[\\/]\S+|file:/\S*)$")
# An absolute path inside desktop-written prose (an error, a status line).
_EMBEDDED = re.compile(r"(?:(?<=[\s\"'(=:\[<])|^)(?:~|/(?!/))[^\s\"'<>)\]]*/[^\s\"'<>)\]]*")


class Refused(Exception):
    """A request the hub will not pass on. ``code`` is one of the wire's error codes."""

    def __init__(self, code: str, message: str, kind: str = ""):
        super().__init__(message)
        self.code, self.message, self.kind = code, message, kind


def is_path_key(key) -> bool:
    if not isinstance(key, str):
        return False
    low = key.lower()
    return low in PATH_KEYS or low.endswith(_PATH_KEY_SUFFIX)


def looks_absolute(value) -> bool:
    return isinstance(value, str) and bool(_ABSOLUTE.match(value.strip()))


def _carries_path(value, depth: int = 0) -> bool:
    """Does an unlisted value name a path — by a key, or by being one — at any depth?"""
    if depth > 6:
        return True                      # too deep to read is not something to wave through
    if isinstance(value, str):
        return looks_absolute(value)
    if isinstance(value, dict):
        return any(is_path_key(key) or _carries_path(item, depth + 1)
                   for key, item in value.items())
    if isinstance(value, list):
        return any(_carries_path(item, depth + 1) for item in value)
    return False


def _path_key_in(value, depth: int = 0) -> bool:
    if depth > 6:
        return True
    if isinstance(value, dict):
        return any(is_path_key(key) or _path_key_in(item, depth + 1) for key, item in value.items())
    if isinstance(value, list):
        return any(_path_key_in(item, depth + 1) for item in value)
    return False


def rid_of(message: dict):
    """The device's own request id: a whole number or a short token. It is echoed, never read."""
    rid = message.get("rid")
    if isinstance(rid, bool):
        rid = None
    if isinstance(rid, int) and 0 <= rid <= RID_INT_MAX:
        return rid
    if isinstance(rid, str) and RID.match(rid):
        return rid
    raise wire.WireError("unknown_type", "board_request needs a rid: a number or a short token.")


def _words(request: dict, name: str, limit: int, *, required: bool, what: str) -> str:
    value = request.get(name)
    if value is None:
        value = ""
    if not isinstance(value, str):
        raise Refused("unknown_type", f"{what} must be text.")
    value = _CONTROL.sub("", value)
    if len(value) > limit:
        raise Refused("unknown_type", f"{what} is at most {limit:,} characters.")
    if required and not value.strip():
        raise Refused("unknown_type", f"{what} is empty.")
    return value


def _card(request: dict) -> str:
    card = request.get("id")
    if not isinstance(card, str) or not CARD_ID.match(card):
        raise Refused("unknown_type", "that needs a card id, four characters of the board's alphabet.")
    return card


def _choice(request: dict, name: str, allowed: tuple[str, ...], default: str | None) -> str:
    value = request.get(name)
    if value is None and default is not None:
        return default
    if not isinstance(value, str) or value not in allowed:
        raise Refused("unknown_type", f"{name} must be one of {', '.join(allowed)}.")
    return value


def request_type(request) -> str:
    """The type a request claims, for the audit line: a name this module knows, or "unknown"."""
    kind = request.get("type") if isinstance(request, dict) else None
    if isinstance(kind, str) and _TYPE_NAME.match(kind) and (kind in REQUESTS or is_never(kind)):
        return kind
    return "unknown"


def is_never(kind: str) -> bool:
    return kind in NEVER or kind.startswith(NEVER_PREFIXES)


def clean_request(request) -> dict:
    """The allow-listed form of a device's ``request``, or :class:`Refused`.

    Rebuilt from the fields REQUESTS names, so an unknown field is not copied. A never-type, a
    path-named key anywhere, or an unlisted field holding an absolute path is refused outright.
    """
    if not isinstance(request, dict):
        raise Refused("unknown_type", "board_request needs a request object.")
    kind = request.get("type")
    if not isinstance(kind, str) or not kind:
        raise Refused("unknown_type", "the request needs a type.")
    if is_never(kind):
        reason = NEVER.get(kind, "that is the desktop's own administration")
        raise Refused("not_permitted", f"never from a device: {reason}.", kind)
    fields = REQUESTS.get(kind)
    if fields is None:
        raise Refused("unknown_type", "that is not a Switchboard request a device may send.")
    if _path_key_in(request):
        raise Refused("not_permitted", "a device never names a file or a folder.", kind)
    for name, value in request.items():
        if name != "type" and name not in fields and _carries_path(value):
            raise Refused("not_permitted", "a device never names a file or a folder.", kind)

    out: dict = {"type": kind}
    if "id" in fields:
        out["id"] = _card(request)
    if kind == "board_search":
        out["query"] = _words(request, "query", QUERY_MAX, required=False, what="the search")
    elif kind == "board_comment":
        out["text"] = _words(request, "text", TEXT_MAX, required=True, what="the comment")
        out["kind"] = _choice(request, "kind", COMMENT_KINDS, "note")
    elif kind == "board_move":
        out["status"] = _choice(request, "status", STATUSES, None)
        out["reason"] = _words(request, "reason", REASON_MAX, required=False, what="the reason")
    elif kind == "board_create":
        tab = request.get("tab")
        if tab not in (None, ""):
            if not isinstance(tab, str) or not TAB_ID.match(tab):
                raise Refused("unknown_type", "tab must be one of the board's tab ids.")
            out["tab"] = tab
        out["title"] = _words(request, "title", TITLE_MAX, required=False, what="the title")
        out["request"] = _words(request, "request", TEXT_MAX, required=False, what="the card's text")
        if not out["title"].strip() and not out["request"].strip():
            raise Refused("unknown_type", "a new card needs a title or some text.")
        labels = request.get("labels")
        if labels is None:
            labels = []
        if not isinstance(labels, list) or len(labels) > LABELS_MAX:
            raise Refused("unknown_type", f"labels is a list of at most {LABELS_MAX}.")
        kept: list[str] = []
        for label in labels:
            if not isinstance(label, str) or not LABEL.match(label):
                raise Refused("unknown_type", "a label is a short word.")
            if label not in kept:
                kept.append(label)
        out["labels"] = kept
    elif kind == "board_ask":
        out["mode"] = _choice(request, "mode", ASK_MODES, "discuss")
        out["text"] = _words(request, "text", TEXT_MAX, required=out["mode"] == "discuss",
                             what="the question")
    elif kind == "board_action":
        out["action"] = _choice(request, "action", ACTIONS, None)
    return out


# ---- events ------------------------------------------------------------------------------------

#: What the GUI may pass on: the worker's board events the contract names, `board_search`'s answer
#: (the reply to a request the contract allows), and the GUI's own `board_action_result`.
EVENT_TYPES = frozenset({
    "board", "board_cards", "board_card", "board_changed", "board_search",
    "board_thread_appended", "board_written", "board_activity", "board_cancelled",
    "board_busy", "board_conflict", "error", "board_action_result",
})
EVENT_PREFIXES = ("board_chat_",)

#: A change every `full` device should learn of, whoever asked for it (see Host.board_event_from_gui).
BROADCAST_EVENTS = frozenset({"board_changed", "board_thread_appended", "board_activity",
                              "board_cancelled"})
#: Events with nothing of the owner's notes in them: every string is desktop-written, so a path
#: inside one is cut out rather than passed as prose.
_DESKTOP_WRITTEN = frozenset({"error", "board_busy", "board_conflict", "board_action_result"})
#: Keys whose text is the owner's notes (or the agent's words on a card).
_CONTENT_KEYS = frozenset({"body", "issue", "text", "title", "acceptance"})

BODY_MAX = 200_000
ENTRY_MAX = 20_000
STRING_MAX = 2_000
CARDS_MAX = 500
ENTRIES_MAX = 500
LIST_MAX = 500            # any other list of objects
SCALARS_MAX = 5_000       # a list of ids, labels or headings
KEYS_MAX = 400
KEY_MAX = 200
DEPTH_MAX = 10
TOKEN_SHOWN = 8           # a pane session token is drawn as its first eight characters; send those
EVENT_BUDGET = wire.MAX_MESSAGE - 64 * 1024     # room for the envelope around the event

_CAPS = {"body": BODY_MAX, "issue": BODY_MAX, "text": ENTRY_MAX, "title": 400}
_LIST_CAPS = {"cards": CARDS_MAX, "upserts": CARDS_MAX, "thread": ENTRIES_MAX,
              "entries": ENTRIES_MAX}
_TOKEN_KEYS = frozenset({"session", "pane_token"})

_SALT = secrets.token_bytes(16)


def board_key(root) -> str | None:
    """An opaque name for one board, stable while this hub runs. The root itself never leaves:
    the key is a salted hash, so it cannot be used to confirm a guess at the path either."""
    if not isinstance(root, str) or not root:
        return None
    return hashlib.sha256(_SALT + root.encode("utf-8", "replace")).hexdigest()[:12]


def _name_of(path) -> str | None:
    """The last component of the project's path — "relay-terminal" — as a label for the view."""
    if not isinstance(path, str):
        return None
    name = re.split(r"[\\/]", path.rstrip("\\/"))[-1]
    name = re.sub(r"[^A-Za-z0-9 _.+-]", "", name)[:80].strip()
    return name or None


class _Scrub:
    def __init__(self, desktop_written: bool):
        self.desktop_written = desktop_written
        self.truncated = False

    def string(self, key: str, value: str) -> str | None:
        value = _CONTROL.sub("", value)
        content = key in _CONTENT_KEYS and not self.desktop_written
        if not content:
            if looks_absolute(value):
                return None
            value = _EMBEDDED.sub("[path]", value)
        if key in _TOKEN_KEYS:
            value = value[:TOKEN_SHOWN]
        value = _SECRET.sub("[redacted]", value)
        limit = _CAPS.get(key, STRING_MAX)
        if len(value) > limit:
            value = value[:limit - 1] + "…"
            self.truncated = True
        return value

    def value(self, key: str, value, depth: int):
        if value is None or isinstance(value, bool):
            return value
        if isinstance(value, int):
            return value
        if isinstance(value, float):
            return value if math.isfinite(value) else None
        if isinstance(value, str):
            return self.string(key, value)
        if depth >= DEPTH_MAX:
            self.truncated = True
            return None
        if isinstance(value, dict):
            return self.object(value, depth + 1)
        if isinstance(value, list):
            objects = any(isinstance(item, (dict, list)) for item in value)
            limit = _LIST_CAPS.get(key, LIST_MAX if objects else SCALARS_MAX)
            if len(value) > limit:
                # A thread keeps its newest entries; every other list keeps its first.
                value = value[-limit:] if key in ("thread", "entries") else value[:limit]
                self.truncated = True
            out = []
            for item in value:
                cleaned = self.value(key, item, depth + 1)
                if cleaned is not None or item is None:
                    out.append(cleaned)
            return out
        return None

    def object(self, value: dict, depth: int) -> dict:
        out: dict = {}
        for key, item in value.items():
            if not isinstance(key, str) or is_path_key(key):
                continue
            key = _CONTROL.sub("", key)[:KEY_MAX]
            if not key or looks_absolute(key):
                continue
            if len(out) >= KEYS_MAX:
                self.truncated = True
                break
            cleaned = self.value(key, item, depth)
            if cleaned is None and item is not None:
                continue                     # dropped, not nulled: a path is absent, not empty
            out[key] = cleaned
        return out


def _size(event: dict) -> int:
    return len(json.dumps(event, separators=(",", ":")).encode())


def _fit(event: dict) -> dict | None:
    """Bring an event under the wire's message size, saying so with ``truncated``.

    In the order that loses least: the duplicate of the issue text, then the oldest thread
    entries, then rows past what fits, then the tail of the body.
    """
    if _size(event) <= EVENT_BUDGET:
        return event
    event["truncated"] = True
    event.pop("issue", None)
    for name in ("thread", "entries"):
        entries = event.get(name)
        while isinstance(entries, list) and len(entries) > 1 and _size(event) > EVENT_BUDGET:
            del entries[:max(1, len(entries) // 4)]
    for name in ("cards", "upserts"):
        rows = event.get(name)
        while isinstance(rows, list) and rows and _size(event) > EVENT_BUDGET:
            del rows[-max(1, len(rows) // 4):]
    body = event.get("body")
    while isinstance(body, str) and body and _size(event) > EVENT_BUDGET:
        body = body[:len(body) // 2]
        event["body"] = body + "…"
    return event if _size(event) <= EVENT_BUDGET else None


def event_allowed(name) -> bool:
    return isinstance(name, str) and (name in EVENT_TYPES or name.startswith(EVENT_PREFIXES))


def clean_event(event) -> dict | None:
    """What a ``full`` device may be sent of one board event from the GUI, or None to drop it.

    ``board_key`` and ``board_name`` are the hub's own additions, made from the ``root`` and the
    project path it drops: an opaque name for the board the event is about, and the project
    folder's own name as a heading.
    """
    if not isinstance(event, dict) or not event_allowed(event.get("event")):
        return None
    scrub = _Scrub(event["event"] in _DESKTOP_WRITTEN)
    out = scrub.object(event, 0)
    if "event" not in out:
        return None
    key = board_key(event.get("root"))
    if key:
        out["board_key"] = key
    if event["event"] == "board":
        name = _name_of(event.get("project")) or _name_of(event.get("workspace"))
        if name:
            out["board_name"] = name
    if event["event"] == "board_action_result" and isinstance(out.get("pane"), str):
        out["pane"] = out["pane"][:TOKEN_SHOWN]       # the pane Execute opened: its session token
    if scrub.truncated:
        out["truncated"] = True
    return _fit(out)


def waiting_changes(event: dict) -> tuple[list[tuple[str, str]], list[str], bool | None]:
    """What a cleaned event says about who each card waits on, for remote/notify.py.

    Returns ``(seen, removed, complete)``: ``seen`` is ``(card id, waiting_on)`` pairs, ``removed``
    the card ids that left the board, and ``complete`` is True when this event finished a whole
    snapshot of the board, False when it is one more batch of one, None for a change.

    A card is always read from the card objects — a row's own ``id``, or ``card_id`` on a
    ``board_card`` — and never from the event's ``id``, which is the desktop's request id
    (``remote-7``). The board tools' own ``board_changed {upserts: ["K7Q2"]}`` names cards without
    their rows and says nothing about who they wait on; the worker's row-carrying one follows it.
    """
    name = event.get("event")
    seen: list[tuple[str, str]] = []
    removed: list[str] = []
    complete: bool | None = None

    def row(value) -> None:
        if isinstance(value, dict) and isinstance(value.get("id"), str) and CARD_ID.match(value["id"]):
            waiting = value.get("waiting_on")
            seen.append((value["id"], waiting if isinstance(waiting, str) else ""))

    if name in ("board", "board_cards"):
        for item in event.get("cards") or []:
            row(item)
        complete = not event.get("more") and not event.get("truncated")
    elif name == "board_changed":
        for item in event.get("upserts") or []:
            row(item)
        removed = [card for card in event.get("removed") or []
                   if isinstance(card, str) and CARD_ID.match(card)]
    elif name == "board_card":
        front = event.get("front")
        card = event.get("card_id")
        if isinstance(front, dict) and isinstance(card, str) and CARD_ID.match(card):
            waiting = front.get("waiting_on")
            seen.append((card, waiting if isinstance(waiting, str) else ""))
    return seen, removed, complete


# ---- the hub's book ------------------------------------------------------------------------------

READ_RATE, READ_BURST = 10.0, 20.0        # per device: ten reads a second, a screenful at once
WRITE_RATE, WRITE_BURST = 2.0, 4.0        # two writes a second, four in a burst
PENDING_MAX = 512
PENDING_TTL = 30 * 60.0                   # a Plan turn answers long after it was asked
ANSWER_TIMEOUT = 20.0                     # a GUI that says nothing is an error, not a spinner


@dataclass
class Pending:
    channel: object
    rid: object              # the device's own
    kind: str
    at: float
    answered: bool = False


@dataclass
class Book:
    """Which device asked what, the per-device buckets, and what was dropped."""
    pending: OrderedDict = field(default_factory=OrderedDict)      # hub rid -> Pending
    buckets: dict = field(default_factory=dict)                    # (device, class) -> [tokens, at]
    dropped: Counter = field(default_factory=Counter)              # event name -> count
    _next: int = 0

    def allow(self, device: str, kind: str, now: float | None = None) -> bool:
        """A token bucket per device and per class, so reading a board cannot starve a write."""
        now = time.monotonic() if now is None else now
        reading = kind in READS
        rate, burst = (READ_RATE, READ_BURST) if reading else (WRITE_RATE, WRITE_BURST)
        key = (device, "read" if reading else "write")
        tokens, at = self.buckets.get(key, (burst, now))
        tokens = min(burst, tokens + (now - at) * rate)
        if tokens < 1.0:
            self.buckets[key] = (tokens, now)
            return False
        self.buckets[key] = (tokens - 1.0, now)
        return True

    def new(self, channel, rid, kind: str, now: float | None = None) -> int:
        now = time.monotonic() if now is None else now
        self._purge(now)
        self._next += 1
        self.pending[self._next] = Pending(channel, rid, kind, now)
        while len(self.pending) > PENDING_MAX:
            self.pending.popitem(last=False)
        return self._next

    def find(self, rid, now: float | None = None) -> Pending | None:
        """The request a GUI event answers. Not popped: one request has several events."""
        if isinstance(rid, bool) or not isinstance(rid, int):
            return None
        self._purge(time.monotonic() if now is None else now)
        return self.pending.get(rid)

    def forget(self, rid: int) -> Pending | None:
        return self.pending.pop(rid, None)

    def count_dropped(self, event) -> None:
        name = event.get("event") if isinstance(event, dict) else None
        name = name if isinstance(name, str) and len(name) <= 64 else "(not an event)"
        if name in self.dropped or len(self.dropped) < 256:
            self.dropped[name] += 1

    def _purge(self, now: float) -> None:
        while self.pending:
            rid, first = next(iter(self.pending.items()))
            if now - first.at < PENDING_TTL:
                break
            self.pending.pop(rid, None)


def refusal(code: str, message: str) -> dict:
    """The hub's own answer to a request it will not pass on, shaped as the worker's `error` so a
    client has one path for both. ``source`` says the hub wrote it."""
    return {"event": "error", "code": code, "message": message, "source": "hub"}


# The contract's examples, as a device sends them and as the desktop's worker emits them. Tests
# hold the two cleaners to these, and the phone's view can be built against them.
EXAMPLE_REQUESTS = [
    {"type": "board_open"},
    {"type": "board_refresh"},
    {"type": "board_card_get", "id": "K7Q2"},
    {"type": "board_search", "query": "pairing"},
    {"type": "board_comment", "id": "K7Q2", "text": "Yes, go with the second option.", "kind": "note"},
    {"type": "board_move", "id": "K7Q2", "status": "planned", "reason": "agreed on the phone"},
    {"type": "board_create", "tab": "features", "title": "Dictated from the bus stop",
     "request": "make the inbox row show the count", "labels": ["remote"]},
    {"type": "board_ask", "id": "K7Q2", "text": "what is left?", "mode": "discuss"},
    {"type": "board_cancel", "id": "K7Q2"},
    {"type": "board_action", "id": "K7Q2", "action": "execute"},
]
