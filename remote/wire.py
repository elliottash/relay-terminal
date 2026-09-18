# SPDX-License-Identifier: GPL-3.0-or-later
"""RRP/1 messages: the allow-lists, capabilities and per-stream sequencing.

The rule the rest of the code leans on is that **everything is denied unless it is named here**.
A new worker event, or a new client message type, is refused until somebody classifies it, which is
why ``KNOWN_WORKER_EVENTS`` is written out in full and tested against the two lists: adding an
event to the worker without deciding whether a phone may see it fails the suite.

docs/REMOTE-PROTOCOL.md sections 6 and 7.
"""
from __future__ import annotations

import base64
import json
import re
from collections import deque
from dataclasses import dataclass, field

PROTOCOL_VERSION = 1
MAX_MESSAGE = 1 << 20

# ---- capabilities ---------------------------------------------------------------------------

VIEW, AGENT, FULL = "view", "agent", "full"
CAPABILITIES = (VIEW, AGENT, FULL)
_RANK = {VIEW: 0, AGENT: 1, FULL: 2}


def allows(capability: str, needed: str) -> bool:
    if capability not in _RANK or needed not in _RANK:
        return False
    return _RANK[capability] >= _RANK[needed]


# ---- roles (multiplayer, section 10) ----------------------------------------------------------
# A capability belongs to a **device** the owner paired; a role belongs to a **participant** the
# owner invited to one pane. They are separate ladders on purpose: there is no function from one to
# the other, so nothing can turn `editor` into `full`, and a participant's channel has no device
# record for `allows` to read in the first place.

VIEWER, EDITOR, OWNER = "viewer", "editor", "owner"
GUEST_ROLES = (VIEWER, EDITOR)          # what an invite may grant; `owner` is never granted
_ROLE_RANK = {VIEWER: 0, EDITOR: 1}


def role_allows(role: str, needed: str) -> bool:
    if role not in _ROLE_RANK or needed not in _ROLE_RANK:
        return False
    return _ROLE_RANK[role] >= _ROLE_RANK[needed]


# ---- client → desktop -----------------------------------------------------------------------
# Every type a client may send, with the capability it needs. Anything absent is refused, which is
# what keeps store_key, import_warp, configure, skills imports and settings changes unreachable.

CLIENT_TYPES: dict[str, str | None] = {
    "hello": None,                 # before a device is known
    "pair_prove": None,
    "knock": None,                 # a guest's first message on an invite channel (section 10.2)
    "ping": None,
    "pong": None,
    "bye": None,
    "resume": VIEW,
    "transport_switch": None,      # session-level: re-binds the Noise stream to a new transport
    "client_state": VIEW,
    "pane_focus": VIEW,
    "pane_blur": VIEW,
    "panes_get": VIEW,
    "turn_transcript_get": VIEW,
    "tool_output_get": VIEW,
    "history_get": VIEW,
    "screen_get": VIEW,
    # Notifications (section 9). Any paired device may ask to be told; the subscription and the
    # seal key travel inside the Noise session and are never seen by the rendezvous.
    "push_subscribe": VIEW,
    "push_unsubscribe": VIEW,
    "compose": AGENT,
    "agent_stop": AGENT,
    "queue_remove": AGENT,
    "set_mode": AGENT,
    "plan_execute": AGENT,
    "recap_request": AGENT,
    "voice": AGENT,
    "keys": FULL,
    "paste": FULL,
    "line": FULL,
    "control_request": FULL,
    "control_release": FULL,
    "secret_input": FULL,          # plus the per-device password switch; see host.py
}

# The owner's controls exist on the desktop only (section 10.5). They cross the GUI↔sidecar stdio
# line as JSON, and they are refused on the wire from **any** device — the owner's own paired phone
# included — because a phone that could mint an invite or change a role would be a second key to
# the share, held by whoever holds the phone.
OWNER_ONLY = frozenset({
    "invite_create", "invite_revoke", "knock_answer", "role_set", "participant_remove",
    "share_pause", "share_end", "control_answer", "prompt_answer",
})

# Named so a reader can see they were considered and refused, and so a test can assert it.
NEVER_FROM_CLIENT = frozenset({
    "store_key", "remove_key", "test_key", "import_warp", "configure", "set_model",
    "import_skills_preview", "import_skills_confirm", "refine_skills", "skills_check_updates",
    "keybindings", "presets", "model_roles", "agent_options", "load_state", "fork", "reset",
    "rewind", "scan_instructions", "index_rebuild", "conversation_delete",
}) | OWNER_ONLY

# ---- what a participant may send (section 10.1) ------------------------------------------------
# A second, narrower allow-list on top of CLIENT_TYPES, keyed by the role floor. Anything absent is
# refused whatever the role, so this is denied-by-default in exactly the way CLIENT_TYPES is: a new
# client message reaches a guest only when somebody adds it here.

GUEST_TYPES: dict[str, str] = {
    "hello": VIEWER,               # a reconnect; `knock` is handled before a role exists
    "knock": VIEWER,
    "ping": VIEWER,
    "pong": VIEWER,
    "bye": VIEWER,
    "resume": VIEWER,
    "client_state": VIEWER,
    "panes_get": VIEWER,
    "pane_focus": VIEWER,
    "pane_blur": VIEWER,
    "screen_get": VIEWER,
    "history_get": VIEWER,         # scrollback of the shared pane, which is already on their screen
    # An editor's two actions are both *requests*: the hub parks them and answers `prompt_pending`
    # or `control_pending`. Neither changes the pane by itself (sections 10.3 and 10.4).
    "compose": EDITOR,
    "plan_execute": EDITOR,        # 10.4: a guest's plan_execute is treated as a prompt
    "control_request": EDITOR,
    "control_release": EDITOR,
    # Typing is an editor's *while holding control*; the hub refuses it with `not_driving` until
    # the handoff exists, so a role alone never reaches the keyboard.
    "keys": EDITOR,
    "paste": EDITOR,
    "line": EDITOR,
}

# Section 10.1's "whatever their role a participant never gets". Every one of these is also absent
# from GUEST_TYPES; the set is written out so the refusal is a decision a reader can find, and so a
# test can assert each is refused for both roles.
GUEST_NEVER: dict[str, str] = {
    "secret_input": "the password field is never offered to a guest",
    "set_mode": "changes how the owner's agent behaves, not what it is asked",
    "voice": "spends the owner's provider key, and is not the shared pane",
    "queue_remove": "queue edits of other people's items",
    "agent_stop": "stops the owner's turn; not among an editor's actions",
    "recap_request": "spends the owner's provider key on stored conversation",
    "turn_transcript_get": "stored transcripts hold every file the agent read",
    "tool_output_get": "stored tool output holds every file the agent read",
    "push_subscribe": "notifications belong to a device the owner paired",
    "push_unsubscribe": "notifications belong to a device the owner paired",
    # There is no client message that lists devices, so there is nothing to name here for the
    # device list; it is unreachable because no type serves it, not because it is refused.
}

# ---- desktop → client -----------------------------------------------------------------------

SERVER_TYPES = frozenset({
    "welcome", "error", "ping", "pong", "bye", "paired", "revoked", "resumed",
    "transport_switched", "panes", "agent", "screen_snapshot", "screen_diff", "history",
    "push_state",
    # Multiplayer (section 10). `knock_pending` carries the five digits on both screens,
    # `admitted` the participant's own record, `participants` the presence list for a pane, and
    # the two `*_pending` replies say the owner has been asked.
    "knock_pending", "admitted", "participants", "prompt_pending", "control_pending",
})

# ---- worker events --------------------------------------------------------------------------
# Forwarded to a phone, wrapped in an `agent` message.

FORWARDED_EVENTS = frozenset({
    "agent_finished", "agent_message_delivered", "agent_started", "agent_stopped", "cancelled",
    "checkpoints", "compacted", "compaction_started", "completion_check", "context",
    "conversation", "conversations", "delta", "done", "effort_changed", "error", "interrupting",
    "mode_changed", "model_applied", "model_changed", "model_switch_refused", "plan_written", "provider_retry",
    "queue_changed", "queued",
    "ready", "recap", "request", "request_audit", "requests", "sessions", "status",
    "steer_delivered", "steer_escalated", "steer_removed", "steer_returned", "subagent_event", "subagent_finished",
    "subagent_handoff", "subagent_model", "subagent_progress", "subagent_started", "subagent_transcript",
    "suggestion", "thinking_delta", "thinking_done", "todos", "tool_output", "tool_result",
    "tool_started", "transcribed", "turn_summary", "turn_transcript", "usage",
    # Image context: which model actually served a turn, and why a picture could not be sent.
    # User-facing like model_changed, not routing internals like route: the owner's decision for
    # #EM1E is that Relay says when it swaps to a vision model, and a phone is a user surface.
    "vision_route", "vision_route_ended", "vision_unavailable",
    # Pane titles and tab labels: what a phone needs to label the panes it is showing. The
    # session summary (protocol 18.4) is the same thing at greater length - this pane's own
    # description, written from this pane's own conversation, which the phone is already watching.
    "session_title", "tab_label", "session_summary",
    # Switchboard (protocol 19): cards, their threads and what the agent did to them. A phone
    # watching a pane should see the board move for the same reason the desktop does; the card
    # bodies are the user's own notes, already in git, not desktop-local configuration.
    "board", "board_activity", "board_card", "board_changed", "board_problems",
    "board_thread_appended", "board_undone", "board_written",
    # A whole-board cleanup (protocol 19.9) is board activity too: the phone shows its progress
    # and its changelog the same way the desktop does.
    "board_cleanup_started", "board_cleanup_summary",
    # The agent typing into the visible program: a phone watching a pane must see every keystroke
    # the agent sends and every refusal, for the same reason the desktop prints them inline.
    "program_input", "program_input_refused",
    # The agent handing a command to the user's real shell (protocol 22): same reason. The phone
    # only watches; the desktop pane is the one that answers it.
    "terminal_command",
    # The commands the agent left running (backend/relay_core/jobs.py): a phone watching a pane
    # should know a server is still up, for the same reason it sees tool_result.
    "jobs",
})

# Never forwarded, with the reason. Key material, provider configuration, desktop-local
# administration, and opaque session state a client could use to rebuild a conversation.
WITHHELD_EVENTS: dict[str, str] = {
    "key_stored": "key material",
    "key_removed": "key material",
    "key_tested": "key material",
    "warp_imported": "may carry imported credentials",
    "presets": "provider configuration and base URLs",
    "model_roles": "provider configuration",
    "configured": "provider configuration",
    "agent_options": "provider and tool configuration",
    "agents": "agent definitions from disk",
    "agents_status": "agent definitions from disk",
    "agent_tools_imported": "tool configuration",
    "skills": "local file paths",
    "skills_import_preview": "local file paths",
    "skills_imported": "local file paths",
    "skills_refined": "local file paths",
    "skills_updates": "local file paths",
    "instructions_found": "local file paths",
    "instructions_synthesized": "local file paths",
    "keybindings_updated": "desktop-local settings",
    "index_rebuilt": "desktop-local administration",
    "terminal_history_indexed": "desktop-local administration",
    "conversation_deleted": "desktop-local administration",
    "job_output": "reply to the desktop's own request (up to 256 KiB of command output)",
    "conversation_pinned": "desktop-local administration",
    "conversation_renamed": "desktop-local administration",
    # Summarising saved sessions (protocol 18.4). Same call as a rename or a pin: the reply to a
    # click on the desktop's session list, about sessions other than the pane the phone is
    # watching. The batch stream would hand a phone every other session's summary at once.
    "conversation_summary": "reply to the desktop's own request; another session's content",
    "conversations_summarize_estimate": "desktop-local administration",
    "conversations_summarize_progress": "desktop-local administration; other sessions' summaries",
    "conversations_summarize_cancelled": "desktop-local administration",
    # The ⓘ view (protocol 25): session file paths, instruction paths and the whole history.
    "session_info": "reply to the desktop's own request; local file paths",
    # A todo handed to a subagent from the task list: the answer to the desktop's own click. The
    # phone sees the result in `todos` and `subagent_started`.
    "todo_subagent": "reply to the desktop's own request",
    "reset": "desktop-local administration",
    "rewound": "desktop-local administration",
    "fork_state": "opaque conversation state",
    "state_loaded": "opaque conversation state",
    # Consent to drive a program is granted by a gesture on the desktop pane that owns the
    # keyboard; a remote client is not that pane, so the grant's state is desktop-local.
    "program_control": "desktop-local control state",
    "route": "routing internals the phone does not render",
    "route_assisted": "routing internals the phone does not render",
    # Aliases (issue G8DK). An alias card is a file on the desktop and its body is a command
    # that would run there, so the list, an expansion and an import all carry local file paths
    # and stored shell text. Same call as `skills` and `agents`: a phone may see the *result* of
    # a turn, not the desktop's saved definitions. Running an alias remotely is a separate
    # decision and a separate client message; none exists yet.
    "aliases": "alias definitions and local file paths",
    "alias_expanded": "a command line built for this desktop's shell",
    "alias_saved": "local file paths",
    "alias_deleted": "local file paths",
    "alias_import_preview": "unreviewed text read from Warp's database and shell startup files",
    "alias_imported": "local file paths",
    # Model servers on the desktop (protocol 23, card #24XJ). What serves on the desktop's loopback
    # ports, under which model ids, is provider configuration, and saving or deleting an endpoint
    # is desktop administration. Same call as `presets`.
    "local_probed": "provider configuration and loopback URLs",
    "local_endpoints": "provider configuration and loopback URLs",
    "local_endpoint_saved": "provider configuration and loopback URLs",
    "local_endpoint_deleted": "desktop-local administration",
}

# Every event name backend/relay_core and backend/worker.py emit today. The test that compares this
# to the two lists above is the thing that makes "denied by default" true rather than aspirational.
KNOWN_WORKER_EVENTS = frozenset(FORWARDED_EVENTS) | frozenset(WITHHELD_EVENTS)


def may_forward(event: str) -> bool:
    return event in FORWARDED_EVENTS


# A participant sees a **narrower** list than a device (section 10.1): the turn lifecycle, the
# pane's status and its queue. Not the agent's words — those already reach a guest on the screen,
# because Relay prints them into the terminal — and above all not `turn_summary`, `tool_started`,
# `tool_result` or `tool_output`, which carry the paths and commands of every file the agent
# touched. A test asserts this is a strict subset of FORWARDED_EVENTS, so an event a device may not
# see can never become one a guest may.
GUEST_EVENTS = frozenset({
    "agent_started", "agent_finished", "agent_stopped", "cancelled",
    "status", "queued", "queue_changed", "error",
})


def may_forward_to_guest(event: str) -> bool:
    return event in GUEST_EVENTS and event in FORWARDED_EVENTS


# ---- pane status ----------------------------------------------------------------------------

PANE_STATUSES = ("idle", "running", "waiting_input", "password", "finished", "failed")


# ---- messages -------------------------------------------------------------------------------

class WireError(Exception):
    def __init__(self, code: str, message: str):
        super().__init__(message)
        self.code, self.message = code, message


def encode(message: dict) -> bytes:
    data = json.dumps(message, separators=(",", ":")).encode()
    if len(data) > MAX_MESSAGE:
        raise WireError("internal", "message too large.")
    return data


def decode(data: bytes) -> dict:
    if len(data) > MAX_MESSAGE:
        raise WireError("internal", "message too large.")
    try:
        message = json.loads(data)
    except ValueError as exc:
        raise WireError("unknown_type", "not JSON.") from exc
    if not isinstance(message, dict) or not isinstance(message.get("t"), str):
        raise WireError("unknown_type", 'a message must be an object with a string "t".')
    return message


# The web client encodes bytes as base64url and drops the padding, which is what `btoa` plus a
# URL-safe swap gives you. Strict standard-base64 decoding rejects all of it, so binary fields are
# decoded here, in one place, for both spellings.
_BASE64 = re.compile(r"^[A-Za-z0-9+/=_-]*$")


def decode_bytes(value, limit: int, what: str) -> bytes:
    """A base64 or base64url field as bytes, or a WireError naming the field."""
    if not isinstance(value, str) or len(value) > limit:
        raise WireError("unknown_type", f"{what} must be base64, at most {limit} characters.")
    if not _BASE64.match(value):
        raise WireError("unknown_type", f"{what} is not base64.")
    padded = value.replace("-", "+").replace("_", "/")
    padded += "=" * (-len(padded) % 4)
    try:
        return base64.b64decode(padded, validate=True)
    except Exception as exc:
        raise WireError("unknown_type", f"{what} is not base64.") from exc


def error(code: str, message: str, request_id=None) -> dict:
    out = {"t": "error", "code": code, "message": message}
    if request_id is not None:
        out["id"] = request_id
    return out


# ---- streams --------------------------------------------------------------------------------

@dataclass
class Stream:
    """One sequenced stream with a bounded replay ring (section 7)."""
    name: str
    limit: int = 2000
    seq: int = 0
    ring: deque = field(default_factory=deque)

    def __post_init__(self):
        self.ring = deque(maxlen=self.limit)

    def add(self, message: dict) -> dict:
        self.seq += 1
        message = {**message, "seq": self.seq}
        self.ring.append(message)
        return message

    def since(self, seq: int) -> list[dict] | None:
        """Messages after ``seq``, or None when the ring no longer reaches back that far."""
        if seq == self.seq:
            return []
        if seq > self.seq or not self.ring:
            return None
        oldest = self.ring[0]["seq"]
        if seq < oldest - 1:
            return None
        return [m for m in self.ring if m["seq"] > seq]


class Deduplicator:
    """At-most-once application of client input queued while offline (section 7)."""

    def __init__(self, limit: int = 256):
        self.limit = limit
        self.seen: deque = deque(maxlen=limit)
        self._set: set[str] = set()

    def fresh(self, msg_id: str | None) -> bool:
        if not msg_id:
            return True                      # no id: the client is not asking for de-duplication
        if msg_id in self._set:
            return False
        if len(self.seen) == self.limit and self.seen:
            self._set.discard(self.seen[0])
        self.seen.append(msg_id)
        self._set.add(msg_id)
        return True
