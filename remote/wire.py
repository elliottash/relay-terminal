# SPDX-License-Identifier: GPL-3.0-or-later
"""RRP/1 messages: the allow-lists, capabilities and per-stream sequencing.

The rule the rest of the code leans on is that **everything is denied unless it is named here**.
A new worker event, or a new client message type, is refused until somebody classifies it, which is
why ``KNOWN_WORKER_EVENTS`` is written out in full and tested against the two lists: adding an
event to the worker without deciding whether a phone may see it fails the suite.

docs/REMOTE-PROTOCOL.md sections 6 and 7.
"""
from __future__ import annotations

import json
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


# ---- client → desktop -----------------------------------------------------------------------
# Every type a client may send, with the capability it needs. Anything absent is refused, which is
# what keeps store_key, import_warp, configure, skills imports and settings changes unreachable.

CLIENT_TYPES: dict[str, str | None] = {
    "hello": None,                 # before a device is known
    "pair_prove": None,
    "ping": None,
    "pong": None,
    "bye": None,
    "resume": VIEW,
    "client_state": VIEW,
    "pane_focus": VIEW,
    "pane_blur": VIEW,
    "panes_get": VIEW,
    "turn_transcript_get": VIEW,
    "tool_output_get": VIEW,
    "history_get": VIEW,
    "screen_get": VIEW,
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

# Named so a reader can see they were considered and refused, and so a test can assert it.
NEVER_FROM_CLIENT = frozenset({
    "store_key", "remove_key", "test_key", "import_warp", "configure", "set_model",
    "import_skills_preview", "import_skills_confirm", "refine_skills", "skills_check_updates",
    "keybindings", "presets", "model_roles", "agent_options", "load_state", "fork", "reset",
    "rewind", "scan_instructions", "index_rebuild", "conversation_delete",
})

# ---- desktop → client -----------------------------------------------------------------------

SERVER_TYPES = frozenset({
    "welcome", "error", "ping", "pong", "bye", "paired", "revoked", "resumed",
    "panes", "agent", "screen_snapshot", "screen_diff", "history",
})

# ---- worker events --------------------------------------------------------------------------
# Forwarded to a phone, wrapped in an `agent` message.

FORWARDED_EVENTS = frozenset({
    "agent_finished", "agent_message_delivered", "agent_started", "agent_stopped", "cancelled",
    "checkpoints", "compacted", "compaction_started", "completion_check", "context",
    "conversation", "conversations", "delta", "done", "effort_changed", "error", "interrupting",
    "mode_changed", "model_changed", "plan_written", "provider_retry", "queue_changed", "queued",
    "ready", "recap", "request", "request_audit", "requests", "sessions", "status",
    "steer_delivered", "steer_escalated", "steer_returned", "subagent_event", "subagent_finished",
    "subagent_handoff", "subagent_progress", "subagent_started", "subagent_transcript",
    "suggestion", "thinking_delta", "thinking_done", "todos", "tool_output", "tool_result",
    "tool_started", "transcribed", "turn_summary", "turn_transcript", "usage",
    # Image context: which model actually served a turn, and why a picture could not be sent.
    # User-facing like model_changed, not routing internals like route: the owner's decision for
    # #EM1E is that Relay says when it swaps to a vision model, and a phone is a user surface.
    "vision_route", "vision_route_ended", "vision_unavailable",
    # Pane titles and tab labels: what a phone needs to label the panes it is showing.
    "session_title", "tab_label",
    # Switchboard (protocol 19): cards, their threads and what the agent did to them. A phone
    # watching a pane should see the board move for the same reason the desktop does; the card
    # bodies are the user's own notes, already in git, not desktop-local configuration.
    "board", "board_activity", "board_card", "board_changed", "board_problems",
    "board_thread_appended", "board_undone", "board_written",
    # The agent typing into the visible program: a phone watching a pane must see every keystroke
    # the agent sends and every refusal, for the same reason the desktop prints them inline.
    "program_input", "program_input_refused",
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
    "conversation_pinned": "desktop-local administration",
    "conversation_renamed": "desktop-local administration",
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
}

# Every event name backend/relay_core and backend/worker.py emit today. The test that compares this
# to the two lists above is the thing that makes "denied by default" true rather than aspirational.
KNOWN_WORKER_EVENTS = frozenset(FORWARDED_EVENTS) | frozenset(WITHHELD_EVENTS)


def may_forward(event: str) -> bool:
    return event in FORWARDED_EVENTS


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
