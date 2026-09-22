# SPDX-License-Identifier: AGPL-3.0-or-later
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
    # The owner's three decisions from away (card #PH0N, owner's decision 6, 2026-09-20): a
    # `full` device may admit a knock, decide a guest's prompt and grant the keyboard, with the
    # same three messages the desktop's Sharing pane sends the sidecar (section 10.5). `full`
    # is the level that can already run any command on this desktop, so answering "may alice
    # type here?" from it gives the phone nothing it did not have. The hub tells such devices
    # what is waiting with `owner_asks`, and applies an answer exactly as it applies the GUI's.
    "knock_answer": FULL,
    "prompt_answer": FULL,
    "control_answer": FULL,
}

# The owner's controls exist on the desktop only (section 10.5). They cross the GUI↔sidecar stdio
# line as JSON, and they are refused on the wire from **any** device — the owner's own paired phone
# included — because a phone that could mint an invite or change a role would be a second key to
# the share, held by whoever holds the phone. The three *answers* to what a guest asked
# (`knock_answer`, `prompt_answer`, `control_answer`) left this set on 2026-09-20 (#PH0N): they
# decide one person's one request, within what the invite already granted, and are `full` above.
OWNER_ONLY = frozenset({
    "invite_create", "invite_revoke", "role_set", "participant_remove",
    "share_pause", "share_end",
    # Handing the keyboard back to the desktop (10.3) and the two per-share switches (10.4,
    # 10.5). `control_take` is the owner's *physical* keystroke, so a message that claimed to be
    # one, arriving over the wire, would be exactly the thing it exists to outrank.
    "control_take", "control_revoke", "share_options",
    # A meeting code and its PIN (card #97EG): minted on the desktop, read out by the owner.
    "code_create", "code_revoke",
    # The same, for pairing the owner's own phone (card #FR1C). A code that mints a *device*
    # record is the last thing that may ever arrive from a client, paired or not.
    "pair_code", "pair_code_revoke",
})

# Named so a reader can see they were considered and refused, and so a test can assert it.
NEVER_FROM_CLIENT = frozenset({
    "store_key", "remove_key", "test_key", "import_warp", "configure", "set_model",
    "import_skills_preview", "import_skills_confirm", "refine_skills", "skills_check_updates",
    "keybindings", "presets", "model_roles", "agent_options", "load_state", "fork", "reset",
    "rewind", "scan_instructions", "index_rebuild", "conversation_delete",
    "globals_list", "globals_get", "globals_save", "globals_retire",
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
    # The owner's answers to what guests asked: a guest deciding a knock, a prompt or the keyboard
    # would be admitting themselves.
    "knock_answer": "admitting people is the owner's",
    "prompt_answer": "approving prompts is the owner's",
    "control_answer": "granting the keyboard is the owner's",
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
    # The answers and the state that follow them: how the owner decided a guest's prompt (10.4),
    # who is driving now (10.3), and why typing is refused when it is (10.5). All desktop →
    # client; nothing new is accepted *from* a client, because 10.3's two requests
    # (`control_request`, `control_release`) already existed in section 6.6.
    "prompt_decided", "control", "share_state",
    # What is waiting for the owner (#PH0N): the knocks, guest prompts and control requests not
    # yet decided, sent whole to `full` devices whenever the list changes, so a phone can answer
    # them. Never to a guest: it names other people and carries their prompt text.
    "owner_asks",
})

# What a **participant** may be sent, which is an allow-list for the same reason `GUEST_TYPES` is
# one: a desktop→client type added next month reaches a guest only when somebody decides it may.
# The alternative — letting everything through unless it is named — has already been tried in
# `Host.guest_view`, and it means every new message about the owner's panes, their models or their
# other sessions is a guest-visible leak from the day it lands.
#
# Absent on purpose: `paired` and `revoked` (a guest has no device record), `push_state`
# (notifications belong to a paired device) and `transport_switched` (a guest cannot ask for one).
GUEST_SERVER_TYPES = frozenset({
    "welcome", "knock_pending", "admitted", "bye", "error", "ping", "pong", "resumed",
    "panes", "participants", "control", "control_pending", "prompt_pending", "prompt_decided",
    "share_state",
    # The shared pane itself: its screen, its scrollback, and the agent events of GUEST_EVENTS.
    "agent", "screen_snapshot", "screen_diff", "history",
})


def may_send_to_guest(kind: str) -> bool:
    return kind in GUEST_SERVER_TYPES




# pane_state (relay-terminal-71) ----------------------------------------------------------------
# One pane model, two views (section 16): the desktop pane publishes `pane_state`, the phone draws
# it and sends back actions on the rows and choices it was shown. Every id in these messages was
# minted by the desktop (remote/pane_state.py checks their shapes), so none of them names a preset,
# a path or a session file. Added here, as one block, rather than inside the literals above.
CLIENT_TYPES.update({
    "pane_state_get": VIEW,        # reading, like screen_get: a view device is sent pane_state too
    "queue_move": AGENT,           # {pane, row, to: to_queue | steer | up | down}
    "queue_edit": AGENT,           # {pane, row}: withdraws the row, answered by queue_edit_text
    "queue_send_now": AGENT,       # {pane, row}: a steer, now, interrupting the running turn
    "model_pick": AGENT,           # {pane, choice}: only a model with a stored key is ever offered
    # The owner's three levels (2026-09-18): viewer observes, partner types, owner also reaches
    # the conversations before this one. So these two are FULL, not AGENT.
    "conversation_new": FULL,      # {pane}: start a new conversation in this pane
    "conversation_open": FULL,     # {pane, session}: open one of this pane's past conversations
})
GUEST_NEVER.update({
    "pane_state_get": "the owner's queue, models and sessions are the owner's pane, not the share",
    "queue_move": "queue edits of other people's items",
    "queue_edit": "queue edits of other people's items",
    "queue_send_now": "interrupts the owner's turn; not among an editor's actions",
    "model_pick": "model changes are never a guest's (section 10.1)",
    "conversation_new": "resets the owner's conversation",
    "conversation_open": "the owner's other conversations are not part of a share",
})
SERVER_TYPES = SERVER_TYPES | frozenset({"pane_state", "queue_edit_text"})

# The Switchboard on a device (card #SWPH, section 17) -----------------------------------------------
# One client type and one server type. `board_request {rid, request}` wraps the eleven board requests a
# device may make — which ones, their fields and their caps are remote/board_state.py's allow-list,
# and everything else about a board (deleting, folders, cleanup, claiming, attaching, initializing,
# imports, GitHub sync, anything carrying a path) is refused there. It is `full` and nothing less:
# the Switchboard is every card of every project the desktop has open, not the one conversation a
# viewer or a partner was paired for. `board_event {rid, event}` carries the desktop's answer,
# scrubbed by the same module, to `full` devices only; it is absent from GUEST_SERVER_TYPES, so
# `Channel.send` drops it for a participant whatever a sender does.
CLIENT_TYPES.update({
    "board_request": FULL,         # {rid, request: {type, …}}; see board_state.REQUESTS
})
GUEST_NEVER.update({
    "board_request": "the Switchboard is the owner's cards and threads, not the shared pane",
})
SERVER_TYPES = SERVER_TYPES | frozenset({"board_event"})
# A device never *sends* a board event: it would be a forged answer from the desktop's own worker.
NEVER_FROM_CLIENT = NEVER_FROM_CLIENT | frozenset({"board_event"})

# The same two names on the GUI↔sidecar stdio line (remote/gui_host.py), written down here so the
# vocabulary is in one place. They are different messages from the wire's: the sidecar's
# `board_request` carries the hub's own `rid`, the asking device's id and its name, and the GUI's
# `board_event` answers with that `rid` — or null for a change nobody asked for.
#   sidecar → GUI   {"t":"board_request","rid":n,"device":"<id>","name":"<device name>","request":{…}}
#   GUI → sidecar   {"t":"board_event","rid":n|null,"event":{…}}
SIDECAR_TO_GUI_BOARD = frozenset({"board_request"})
GUI_TO_SIDECAR_BOARD = frozenset({"board_event"})

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
    # Same call for a plan-mode turn's model (protocol 13.11, #Z0VG): the pane prints a ◆ line
    # when a plan turn runs on the planning role's model, so a phone watching the pane sees it too.
    "plan_route", "plan_route_ended",
    # Pane titles and session summaries: what a phone needs to label the panes it is showing. The
    # summary (protocol 18.4) is the title at greater length - this pane's own description, written
    # from this pane's own conversation, which the phone is already watching. Tab labels are the
    # GUI's own (protocol 18.3) and never cross this wire.
    "session_title", "session_summary",
    # Switchboard (protocol 19): cards, their threads and what the agent did to them. A phone
    # watching a pane should see the board move for the same reason the desktop does; the card
    # bodies are the user's own notes, already in git, not desktop-local configuration.
    "board", "board_activity", "board_card", "board_changed", "board_problems",
    "board_thread_appended", "board_undone", "board_written",
    # A whole-board cleanup (protocol 19.9) is board activity too: the phone shows its progress
    # and its changelog the same way the desktop does.
    "board_cleanup_started", "board_cleanup_summary",
    # Which card turns are still running after a Stop (protocol 19.16): a phone showing a card
    # as busy has to learn that it is not any more, and it is the same board activity.
    "board_cancelled",
    # The agent typing into the visible program: a phone watching a pane must see every keystroke
    # the agent sends and every refusal, for the same reason the desktop prints them inline.
    "program_input", "program_input_refused",
    # The agent handing a command to the user's real shell (protocol 22): same reason. The phone
    # only watches; the desktop pane is the one that answers it.
    "terminal_command",
    # The agent asking the user something (protocol 27, #MQ9C). A phone must see the card: the
    # turn is blocked on it, and a person holding the phone is exactly who it is waiting for. The
    # answer travels back as an ordinary prompt from the device, which the desktop pane reads as
    # the answer, so nothing new goes the other way.
    "question", "question_closed",
    # The commands the agent left running (backend/relay_core/jobs.py): a phone watching a pane
    # should know a server is still up, for the same reason it sees tool_result.
    "jobs",
})

# Never forwarded, with the reason. Key material, provider configuration, desktop-local
# administration, and opaque session state a client could use to rebuild a conversation.
WITHHELD_EVENTS: dict[str, str] = {
    # Desktop worker events stay off the pane stream. Phone board access has its own
    # capability-gated, scrubbed board_event bridge (remote/board_state.py).
    "app_catalog_updated": "desktop application catalog",
    "app_command": "desktop application command; never execute through the pane stream",
    "board_cards": "board state uses the separate owner-only board bridge",
    "board_search": "board search uses the separate owner-only board bridge",
    "board_resumed": "board queue acknowledgement uses the separate owner-only board bridge",
    "board_survey": "desktop project survey; local paths",
    "custom_provider_deleted": "provider configuration",
    "custom_provider_saved": "provider configuration",
    "custom_providers": "provider configuration and endpoint URLs",
    "loop_check": "internal agent loop diagnostics",
    "loop_detected": "internal agent loop diagnostics",
    "profile": "desktop project profile",
    "queue_ack": "desktop queue acknowledgement",
    "recitation": "internal agent request audit",
    "signal_thread": "desktop test signal diagnostics",
    "signals_changed": "desktop test signal diagnostics",
    "signals_written": "desktop test signal diagnostics",
    "tests_check": "desktop test administration",
    "tests_history": "desktop test administration; local paths",
    "tests_list": "desktop test administration; local paths",
    "tests_run": "desktop test administration; local paths",
    "tests_suggest": "desktop test administration",
    "usage_limits": "provider account usage information",
    "tryit": "desktop QA staging and results; local paths and instructions",

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
    # A board made in a repo: the workspace path is a local file path (Switchboard).
    "board_created": "desktop-local administration; carries a local file path",
    # Which project a pane is attached to, and the question that creating a board asks
    # (protocol 19.11 and 19.12). Both carry local directory paths, and the question can only be
    # answered by the desktop: `set_board`, `board_init` and `board_init_answer` are not in
    # CLIENT_TYPES, so a remote participant has no way to take part in either.
    "board_state": "desktop-local administration; carries local file paths",
    "board_init_request": "desktop-local administration; the desktop's own dialog",
    # Initializing a project and importing what is already in it (protocol 19.13). The probe's
    # answer is a survey of one directory on this machine — every path in it is local, and it is
    # read for a dialog only the desktop can show; the proposals and what an import created carry
    # the same paths. Same call as `board_state`: the *cards* an import made reach a phone in the
    # `board_changed` that follows, which is the part a phone can use.
    "project_probe_result": "desktop-local administration; local file paths",
    "board_import_proposals": "desktop-local administration; local file paths",
    "board_imported": "desktop-local administration; local file paths",
    "board_folder_changed": "desktop-local administration; the board folder was renamed on disk (19.17)",
    # Syncing the board with its GitHub repository (protocol 19.14). Started from the desktop
    # (`forge_sync_plan`/`forge_sync_run` are not in CLIENT_TYPES), and the plan, the per-card
    # progress and the summary all name the repository, the local card paths and whatever the
    # forge refused. The card changes themselves arrive as `board_changed`.
    "forge_sync_planned": "desktop-local administration; local file paths",
    "forge_sync_progress": "desktop-local administration; local file paths",
    "forge_sync_done": "desktop-local administration; local file paths",
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
    # Globals (protocol 34): saved guidance and original local instruction sources.
    "globals_state": "global definitions and local instruction paths",
    "globals_record": "global memory, alias or instruction source contents",
    "globals_saved": "global memory, alias or instruction source contents",
    "globals_error": "desktop-local editor errors can contain local paths",
    "aliases": "alias definitions and local file paths",
    "alias_expanded": "a command line built for this desktop's shell",
    "alias_saved": "local file paths",
    "alias_deleted": "local file paths",
    "alias_import_preview": "unreviewed text read from Warp's database and shell startup files",
    "alias_imported": "local file paths",
    # Model servers on the desktop (protocol 28, card #24XJ). What serves on the desktop's loopback
    # ports, under which model ids, is provider configuration, and saving or deleting an endpoint
    # is desktop administration. Same call as `presets`.
    "local_probed": "provider configuration and loopback URLs",
    "local_endpoints": "provider configuration and loopback URLs",
    "local_endpoint_saved": "provider configuration and loopback URLs",
    "local_endpoint_deleted": "desktop-local administration",
    # Relay Free's allowance (protocol 13.9): `{limit, used, resets_at}` for the owner's hosted
    # account, and the reply to the desktop's own `hosted_quota` request rather than anything
    # about the pane a phone is watching. Same call as `presets` and `configured`: what the
    # owner's provider arrangement is stays on the desktop. Classified by the security review of
    # 2026-09-18 because the "denied by default" test had gone red waiting for somebody to; the
    # session that added it can move it to FORWARDED_EVENTS if a phone should show the chip.
    "hosted_quota": "the owner's hosted-account allowance; a reply to the desktop's own request",
}

# Every event name backend/relay_core and backend/worker.py emit today. The test that compares this
# to the two lists above is the thing that makes "denied by default" true rather than aspirational.
KNOWN_WORKER_EVENTS = frozenset(FORWARDED_EVENTS) | frozenset(WITHHELD_EVENTS)

# The guest event channel of protocol 26.3 (issue GT7X). These are **not** worker events: they are
# names in a file a guest's shim writes into a pane's runtime directory, read by the GUI process
# and never sent over the wire at all. They are written down here so the classification is one
# reader can find, and kept out of KNOWN_WORKER_EVENTS on purpose — folding them in would
# pre-classify a *worker* event that one day happens to be called `state`, `hook` or `slash`, and
# the test that catches an unclassified worker event would silently stop catching that one.
#
# Everything on the channel is pane-internal: `hook` forwards a hook's raw JSON whose tool inputs
# carry local file paths and contents, `bridge` carries tool arguments with whole file paths,
# contents and diffs, and `statusline`, `state` and `slash` feed desktop surfaces (chips, routing,
# the `/` popup). The subset a phone has any use for — the guest's model, context fill and busy
# flag — already rides `program_state` beside `guest`, so nothing is lost.
GUEST_CHANNEL_EVENTS: dict[str, str] = {
    "hook": "raw guest hook JSON; tool inputs carry local file paths and contents",
    "statusline": "shim parse of the guest statusline; the pane-visible subset rides program_state",
    "state": "guest turn state; guest_busy already rides program_state",
    "bridge": "guest tool payloads carry local file paths and whole file contents",
    "slash": "guest slash catalog for the desktop composer popup",
}


def may_forward(event: str) -> bool:
    return event in FORWARDED_EVENTS


# Events a client that draws the desktop's **screen** already has, in the terminal, and so must not
# be sent twice (card #3H5T). The transcript renderer in `app/app.js` is the agent-companion
# fallback: `transcribe()` is the only reader of a `tool_output`'s text and it runs only when the
# desktop does *not* advertise `screen` (`app/app.js:728`), because `openTerminal()` hides the whole
# transcript when it does (`app/app.js:480-482`). Measured on the owner's Pixel 8 on 2026-09-20: a
# tool-heavy turn put **1.33 MB of 1.46 MB** on the air as `agent` messages the phone parsed and
# threw away, and because RRP is one ordered Noise stream, three of that turn's forty-five screen
# markers painted 1.1-3.1 s late behind them. Same reasoning as GUEST_EVENTS below, applied to the
# owner's own devices: "those already reach them on the screen, because Relay prints them into the
# terminal."
#
# A share with no screen — a source with no `on_screen`, which is the headless/agent-companion
# shape — still gets the text, because there the transcript is the only thing the client can draw.
# A future client that mounts a transcript *beside* a screen has to ask for the text, and this is
# where that opt-in would be read.
SCREEN_REDUNDANT_EVENTS = frozenset({"tool_output", "tool_result"})


def may_forward_with_screen(event: str) -> bool:
    """Whether a share whose clients draw the desktop's screen should be sent this event."""
    return event in FORWARDED_EVENTS and event not in SCREEN_REDUNDANT_EVENTS


# The owner's three levels (2026-09-18) are about *conversations*, not only about buttons: a
# viewer observes this conversation and a partner types in it, and neither is shown the ones
# before it. `pane_state` drops its `sessions` block below `full` for that reason, and these
# worker events carry the same titles, so they need the same floor — otherwise the rule would
# hold in one stream and leak in the other.
EVENT_FLOOR: dict[str, str] = {
    "sessions": FULL,          # the session manager's list
    "conversations": FULL,     # a listing, including other panes' conversations
    "conversation": FULL,      # one conversation's record, by id
}


def floor_for(event: str) -> str:
    """The capability a device needs for this event. `view` unless it names other conversations."""
    return EVENT_FLOOR.get(event, VIEW)


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


# ---- screen frames --------------------------------------------------------------------------


def apply_scroll(rows: list[dict], scroll: dict | None, height: int) -> list[dict]:
    """Move a held screen the way a frame's ``scroll`` says it moved (section 6.5).

    ``rows`` is one ``{row, segs}`` per viewport row; the answer is the same list with rows
    ``[top, bottom)`` shifted up by ``by`` — a negative ``by`` shifts them down — and the rows the
    shift vacated blank. The frame's own ``lines`` are applied on top, and they are exactly the
    rows the desktop could not carry over, which is what turns a whole-screen snapshot into a
    diff of a row or two while output streams (#3H5T).
    """
    if not scroll:
        return rows
    by = int(scroll.get("by", 0))
    top = max(0, int(scroll.get("top", 0)))
    bottom = min(height, int(scroll.get("bottom", height)))
    if by == 0 or bottom <= top:
        return rows
    held = {int(row.get("row", -1)): row.get("segs", []) for row in rows}
    # In place, so the walk has to run away from the rows it has already written: up the screen
    # when the content moved up, down it when the content moved down.
    order = range(top, bottom) if by > 0 else reversed(range(top, bottom))
    for row in order:
        source = row + by
        held[row] = held.get(source, []) if top <= source < bottom else []
    return [{"row": row, "segs": held.get(row, [])} for row in range(height)]
