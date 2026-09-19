# SPDX-License-Identifier: AGPL-3.0-or-later
"""``pane_state``: a desktop pane as a paired phone draws it (docs/REMOTE-PROTOCOL.md section 16).

The phone, tablet or laptop browser is a thin view of one desktop pane. The GUI builds the state
(``src/PaneState.{h,cpp}``, gathered by ``Pane::remoteState()``) and sends it to the sidecar as a
``pane_state`` line; the hub cleans it here and fans it out to the devices watching that pane.

Three rules live in this module, so that no caller has to remember them:

* **An allow-list, not a pass-through.** :func:`clean` rebuilds the message from the fields section
  16 names, with every string capped and every enumeration checked; anything else the GUI sent —
  a preset id, a base URL, a key, a session file path — is simply not copied. Ids must have the
  shapes the desktop mints (a queue row id, ``m<n>``, ``s<n>``, a theme id), so an id can never
  *be* a preset id or a path either.
* **Per capability, read live.** :func:`for_capability` is applied per device on every message: a
  ``view`` device gets no row actions, no model choices and no ``can_new``, and the composer's
  ``modes`` list only what that device's ``compose`` may use.
* **Never to a guest.** A participant (section 10) is never sent a ``pane_state`` or a
  ``queue_edit_text``; the hub's sender refuses them before this module is consulted, and every
  client type that asks for either is in ``wire.GUEST_NEVER``.
"""
from __future__ import annotations

import copy
import itertools
import re
from dataclasses import dataclass, field

from . import wire

VERSION = 1
LABEL_MAX = 400
TAIL_MAX = 2000
ROWS_MAX = 64
SESSIONS_MAX = 50
CHOICES_MAX = 32
EDIT_TEXT_MAX = 32_000
WAITERS_MAX = 16              # pane_state_get requests waiting for a pane's first state
EDITS_MAX = 64                # queue_edit requests waiting for the desktop's answer

PHASES = ("idle", "thinking", "tool", "waiting")
KINDS = ("steer", "agent", "command")
STATES = ("waiting", "withdrawing", "queued", "editing", "paused")
ACTIONS = ("remove", "edit", "to_queue", "send_now", "steer", "up", "down")
MODES = ("auto", "shell", "agent")
MOVES = ("to_queue", "steer", "up", "down")   # queue_move's `to`

# Ids the desktop mints. A row id is Pane::queueRows()'s own; a model choice and a session are
# per-publisher tokens. None of these shapes can hold a preset id ("openrouter-kimi"), a path or a
# session file name, which is the point: the phone sends back only what it was shown.
PANE_ID = re.compile(r"^[A-Za-z0-9_.:-]{1,80}$")
# The desktop theme's id ("relay-dark"), which the phone's pane follows (owner, 2026-09-19). The
# same kind of shape check as the ids above, and for the same reason: it is written into the view's
# `data-theme`, so it may be an id and nothing else — not a path, not a file name, not a URL.
THEME_ID = re.compile(r"^[a-z0-9-]{1,40}$")
ROW_ID = re.compile(r"^(?:steer:[A-Za-z0-9_-]{1,40}|entry:[0-9]{1,19})$")
CHOICE_ID = re.compile(r"^m[1-9][0-9]{0,8}$")
SESSION_ID = re.compile(r"^s[1-9][0-9]{0,8}$")

# Defence in depth for text the desktop wrote. Section 4 says key material never appears in any
# RRP message; these catch the shapes a key takes if one ever reached a label or the reasoning.
_SECRET = re.compile(
    r"(?:\b(?:sk|pk|rk)-[A-Za-z0-9_-]{12,}"                      # OpenAI / Anthropic style
    r"|\b(?:gh[pousr]_|github_pat_|xox[abprs]-|AKIA|AIza)[A-Za-z0-9_-]{12,}"
    r"|(?i:\b(?:api[_-]?key|secret|token|password|bearer)\b\s*[:=]?\s*[A-Za-z0-9_.+/=-]{16,}))")
# A model label is the desktop's name for a model. Never where a provider's base URL or a local
# path belongs, so both are cut out of model labels even if a preset's label carried one.
_URL = re.compile(r"[A-Za-z][A-Za-z0-9+.-]*://\S*")
_PATH = re.compile(r"(?:(?<=\s)|^)(?:~|/)[^\s·]*/[^\s·]*")
_CONTROL = re.compile(r"[\x00-\x08\x0b-\x1f\x7f]")


def _text(value, limit: int = LABEL_MAX, *, keep_newlines: bool = False, tail: bool = False) -> str:
    if not isinstance(value, str):
        return ""
    value = _CONTROL.sub("", value)
    if not keep_newlines:
        value = value.replace("\n", " ").replace("\t", " ")
    value = _SECRET.sub("[redacted]", value)
    if len(value) > limit:
        value = value[-limit:] if tail else value[:limit - 1] + "…"
    return value


def _model_text(value) -> str:
    value = _text(value)
    value = _URL.sub("", value)
    value = _PATH.sub("", value)
    return re.sub(r"\s{3,}", "  ", value).strip()


def _flag(value) -> bool:
    return value is True


def _object(value) -> dict:
    return value if isinstance(value, dict) else {}


def _list(value) -> list:
    return value if isinstance(value, list) else []


def _enum(value, allowed, default: str) -> str:
    return value if isinstance(value, str) and value in allowed else default


def clean(message) -> dict | None:
    """The allow-listed form of a ``pane_state`` from the GUI, or None when it is not one.

    Rebuilt field by field, so an unknown field is dropped rather than passed on, at every level.
    ``seq`` is not copied: the hub numbers each pane's stream itself (:class:`Book`).
    """
    if not isinstance(message, dict) or message.get("t") != "pane_state":
        return None
    if message.get("v") != VERSION:
        return None
    pane = message.get("pane")
    if not isinstance(pane, str) or not PANE_ID.match(pane):
        return None

    turn = _object(message.get("turn"))
    thinking = _object(message.get("thinking"))
    queue = _object(message.get("queue"))
    model = _object(message.get("model"))
    composer = _object(message.get("composer"))
    context = _object(message.get("context"))
    allowance = _object(message.get("allowance"))
    sessions = _object(message.get("sessions"))

    rows = []
    for row in _list(queue.get("rows")):
        if len(rows) >= ROWS_MAX:
            break
        row = _object(row)
        row_id = row.get("id")
        if not isinstance(row_id, str) or not ROW_ID.match(row_id) or row.get("kind") not in KINDS:
            continue
        actions = []
        for action in _list(row.get("actions")):
            if action in ACTIONS and action not in actions:
                actions.append(action)
        rows.append({"id": row_id, "kind": row["kind"], "label": _text(row.get("label")),
                     "state": _enum(row.get("state"), STATES, "queued"), "actions": actions})

    running = queue.get("running")
    running = {"label": _text(running.get("label"))} if isinstance(running, dict) else None

    choices = []
    for choice in _list(model.get("choices")):
        if len(choices) >= CHOICES_MAX:
            break
        choice = _object(choice)
        choice_id = choice.get("id")
        if not isinstance(choice_id, str) or not CHOICE_ID.match(choice_id):
            continue
        choices.append({"id": choice_id, "label": _model_text(choice.get("label")),
                        "current": _flag(choice.get("current"))})

    session_rows = []
    for row in _list(sessions.get("rows")):
        if len(session_rows) >= SESSIONS_MAX:
            break
        row = _object(row)
        session_id = row.get("id")
        if not isinstance(session_id, str) or not SESSION_ID.match(session_id):
            continue
        session_rows.append({"id": session_id, "title": _text(row.get("title")),
                             "when": _text(row.get("when"), 40),
                             "current": _flag(row.get("current")),
                             "running": _flag(row.get("running"))})

    modes = [mode for mode in MODES if mode in _list(composer.get("modes"))]
    percent = context.get("percent_left")
    if isinstance(percent, bool) or not isinstance(percent, (int, float)):
        percent = None
    else:
        percent = max(0, min(100, int(percent)))

    # The Relay Free chip (docs/RELAY-FREE.md): the desktop's words, no secret, nothing to act on,
    # so every level sees it — the pane's owner is the one whose allowance it is. `warn` is read
    # off the cleaned percentage, the same 10% the desktop's chip warns by, and the whole object is
    # dropped when the desktop sent no label: a pane on a provider with a key publishes none.
    allowance_left = allowance.get("percent_left")
    if isinstance(allowance_left, bool) or not isinstance(allowance_left, (int, float)):
        allowance_left = None
    else:
        allowance_left = max(0, min(100, int(allowance_left)))
    allowance_out = None
    if _text(allowance.get("label")).strip():
        allowance_out = {"label": _text(allowance.get("label")), "percent_left": allowance_left,
                         "warn": allowance_left is not None and allowance_left <= 10,
                         "detail": _text(allowance.get("detail"))}

    cleaned = {
        "t": "pane_state", "v": VERSION, "pane": pane,
        "turn": {"phase": _enum(turn.get("phase"), PHASES, "idle"),
                 "clock": _text(turn.get("clock")), "busy": _flag(turn.get("busy"))},
        "thinking": {"visible": _flag(thinking.get("visible")),
                     "header": _text(thinking.get("header")),
                     "tail": _text(thinking.get("tail"), TAIL_MAX, keep_newlines=True, tail=True)},
        "queue": {"paused": _flag(queue.get("paused")),
                  "pause_reason": _text(queue.get("pause_reason")),
                  "running": running, "rows": rows, "hint": _text(queue.get("hint"))},
        "model": {"label": _model_text(model.get("label")), "choices": choices},
        "composer": {"mode": _enum(composer.get("mode"), MODES, "auto"),
                     "placeholder": _text(composer.get("placeholder")), "modes": modes},
        "context": {"label": _text(context.get("label")), "percent_left": percent},
        "sessions": {"rows": session_rows, "can_new": _flag(sessions.get("can_new")),
                     "can_open": _flag(sessions.get("can_open"))},
    }
    if allowance_out is not None:
        cleaned["allowance"] = allowance_out
    # The desktop's theme, so the terminal on the phone is the colour the terminal on the desktop
    # is. Every level sees it: it is how the pane looks, there is nothing to press and nothing in
    # an id to leak. Absent — not empty — when the desktop sent none or sent something that is not
    # an id, and the view then keeps the theme it is already showing.
    theme = message.get("theme")
    if isinstance(theme, str) and THEME_ID.match(theme):
        cleaned["theme"] = theme
    return cleaned


def for_capability(state: dict, capability: str | None) -> dict | None:
    """What one device may be sent of a cleaned state, or None for no capability at all.

    Called per device on every message with the capability read from the live device record, so a
    downgrade applies to the very next state. The owner named the three levels on 2026-09-18:

    * **viewer** (``view``) observes this conversation: the rows, the model it is on, the reasoning.
      Nothing to press — no row actions, no model choices, no composer modes.
    * **partner** (``agent``) can type in this conversation: it composes to the agent (section 6.6),
      so its ``modes`` is ``["agent"]``, and it may act on the queue rows it was offered.
    * **owner** (``full``) can type in this conversation *and* see the ones before it: the session
      list, opening one, and starting a new one are this level alone.

    So the whole ``sessions`` block is dropped below ``full``: a partner is not shown the titles of
    the owner's other conversations, which is what "observes this convo" and "can type in this
    convo" mean literally.
    """
    if capability not in wire.CAPABILITIES:
        return None
    out = copy.deepcopy(state)
    if capability != wire.FULL:
        out.pop("sessions", None)
    if capability == wire.VIEW:
        for row in out["queue"]["rows"]:
            row.pop("actions", None)
        out["model"].pop("choices", None)
        out["composer"]["modes"] = []
    elif capability == wire.AGENT:
        out["composer"]["modes"] = [mode for mode in out["composer"]["modes"] if mode == "agent"]
    return out


# ---- what a client may send back -------------------------------------------------------------

def row_of(message: dict) -> str:
    row = message.get("row")
    if not isinstance(row, str) or not ROW_ID.match(row):
        raise wire.WireError("unknown_type", "that needs a queue row id from pane_state.")
    return row


def move_of(message: dict) -> str:
    to = message.get("to")
    if to not in MOVES:
        raise wire.WireError("unknown_type", 'to must be "to_queue", "steer", "up" or "down".')
    return to


def choice_of(message: dict) -> str:
    choice = message.get("choice")
    if not isinstance(choice, str) or not CHOICE_ID.match(choice):
        # Never a preset id: only a token this desktop minted in a pane_state it sent.
        raise wire.WireError("unknown_type", "model_pick needs a choice id from pane_state.")
    return choice


def session_of(message: dict) -> str:
    """The session token in a `conversation_open`. Never a path or a session file name: only an id
    this desktop minted in a `pane_state` it sent, which the pane resolves against its own list."""
    session = message.get("session")
    if not isinstance(session, str) or not SESSION_ID.match(session):
        raise wire.WireError("unknown_type", "conversation_open needs a session id from pane_state.")
    return session


def edit_answer(message: dict) -> tuple[bool, str]:
    """The GUI's `queue_edit_text` line: (ok, text or the reason it failed)."""
    if message.get("ok") is False:
        return False, _text(message.get("error")) or "that row could not be taken back."
    text = message.get("text")
    if not isinstance(text, str):
        return False, "that row could not be taken back."
    return True, _CONTROL.sub("", text)[:EDIT_TEXT_MAX]


# ---- the hub's book ----------------------------------------------------------------------------

@dataclass
class PendingEdit:
    channel: object
    request_id: object
    pane: str
    row: str


@dataclass
class Book:
    """The latest state per pane, its stream number, and the requests waiting on the desktop.

    Deliberately **not** a ``wire.Stream`` in ``Host.streams``: a replay from that ring goes out
    exactly as stored, unfiltered by capability and past the guest rule, and a pane_state is only
    ever worth its latest value anyway. A client that reconnects sends ``pane_state_get``.
    """
    latest: dict = field(default_factory=dict)          # pane -> cleaned state, with seq
    seqs: dict = field(default_factory=dict)            # pane -> last seq given out
    waiting: dict = field(default_factory=dict)         # pane -> [(channel, request id)]
    edits: dict = field(default_factory=dict)           # hub-minted id -> PendingEdit
    _ids: itertools.count = field(default_factory=lambda: itertools.count(1))

    def store(self, state: dict) -> dict:
        pane = state["pane"]
        self.seqs[pane] = self.seqs.get(pane, 0) + 1
        stamped = {**state, "seq": self.seqs[pane]}
        self.latest[pane] = stamped
        return stamped

    def forget(self, pane: str) -> None:
        self.latest.pop(pane, None)
        self.waiting.pop(pane, None)

    def wait(self, pane: str, channel, request_id) -> bool:
        queue = self.waiting.setdefault(pane, [])
        if len(queue) >= WAITERS_MAX:
            return False
        queue.append((channel, request_id))
        return True

    def take_waiting(self, pane: str) -> list:
        return self.waiting.pop(pane, [])

    def new_edit(self, channel, request_id, pane: str, row: str) -> str | None:
        if len(self.edits) >= EDITS_MAX:
            return None
        edit_id = f"qe{next(self._ids)}"
        self.edits[edit_id] = PendingEdit(channel, request_id, pane, row)
        return edit_id

    def take_edit(self, edit_id) -> PendingEdit | None:
        return self.edits.pop(edit_id, None) if isinstance(edit_id, str) else None


# The contract's own example (section 16), as the desktop would send it. Tests hold the cleaner to
# it, and a web view can be built against it before a desktop is running.
EXAMPLE = {
    "t": "pane_state", "v": 1, "pane": "p1", "seq": 42,
    "turn": {"phase": "thinking", "clock": "thinking · 12 s · step 1/256 · Esc stops", "busy": True},
    "thinking": {"visible": True, "header": "Thinking… · fake · 12 s",
                 "tail": "The readme names three commands; the second one is the test runner."},
    "queue": {"paused": False, "pause_reason": "", "running": {"label": "✦ please plan this out"},
              "rows": [{"id": "steer:steer-3", "kind": "steer",
                        "label": "↪ next tool call  ✦ check the readme", "state": "waiting",
                        "actions": ["remove", "edit", "to_queue", "send_now"]},
                       {"id": "entry:4", "kind": "agent", "label": "✦ then run the tests",
                        "state": "queued", "actions": ["remove", "edit", "steer", "down"]},
                       {"id": "entry:5", "kind": "command", "label": "$ git status",
                        "state": "queued", "actions": ["remove", "edit", "up"]}],
              "hint": "↑ select a row · Ctrl+↑↓ move · Shift+Del remove"},
    "model": {"label": "fake · local",
              "choices": [{"id": "m1", "label": "kimi-k2", "current": False},
                          {"id": "m2", "label": "Main · fake", "current": True}]},
    "composer": {"mode": "auto", "placeholder": "Shell commands or agent prompts…",
                 "modes": ["auto", "shell", "agent"]},
    "context": {"label": "96% left", "percent_left": 96},
    "theme": "relay-dark",
    "allowance": {"label": "Free · 73% left", "percent_left": 73, "warn": False,
                  "detail": "182,400 of 250,000 tokens today · resets at 02:00"},
    "sessions": {"rows": [{"id": "s1", "title": "Thinking copy test", "when": "14:02",
                           "current": True, "running": False}],
                 "can_new": True, "can_open": True},
}
