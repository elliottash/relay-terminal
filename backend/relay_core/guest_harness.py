# SPDX-License-Identifier: GPL-3.0-or-later
"""Tier A: a guest agent as a headless harness the worker drives (GT7X, protocol 29).

Tier B (protocol 26) runs Claude Code or Codex as a TUI in the pane and types into it. Tier A,
un-deferred by the owner on 2026-09-19, runs the guest's own **headless harness** instead —
`claude -p --input-format stream-json --output-format stream-json` and `codex app-server` — and
makes it the pane's agent the way a provider is: the prompt goes through the worker's ordinary
`ask`, the guest's tool calls print as Relay's own call lines, its usage feeds the context chip,
and the pane's shell stays the user's terminal. Nothing is typed into a TUI and nothing is scraped.

This module is the **contract** the two adapters (`guest_harness_claude`, `guest_harness_codex`)
implement and the worker side (`guest_harness_provider`) consumes. It is deliberately small and
has no I/O of its own, so it can be held to by tests with a scripted fake on either side.

Vocabulary: a **harness** is one guest process for one pane, started once and asked for a turn at
a time; a **turn** is one prompt in and one final answer out, with tool calls in between; an
**event** is what the harness reports while a turn runs, in the vocabulary below, which the
provider maps one-to-one onto Relay's worker events (protocol 29.3).

Protocol: docs/AGENT-SESSIONS-PROTOCOL.md section 29. Card:
issues/features/2026-09-19-claude-codex-guest-integration.md (GT7X, task t:x2).
"""
from __future__ import annotations

import threading
from dataclasses import dataclass, field
from typing import Callable, Protocol

# The guest ids are the registry's (guest.GUESTS); the harness is one more thing a guest has.
HARNESS_GUESTS = ("claude", "codex")

# Permission postures a harness is started with. `bypass` is the owner's rule (2026-09-19: the
# guest moves around the file system like Relay's own agent, no per-action approvals); `ask`
# routes every approval the guest raises to the pane as a question (29.3); `deny` refuses them.
PERMISSIONS = ("bypass", "ask", "deny")

# ----- events ---------------------------------------------------------------------------------
#
# One kind, one flat `data` dict. The provider translates each into exactly one Relay event, so
# an adapter never needs to know Relay's vocabulary and Relay never needs to know the guest's.
#
#   kind            data                                             Relay event (29.3)
#   started         {session_id, model}                              (configured / model_changed)
#   delta           {text}                                           delta
#   thinking        {text}                                           thinking_delta
#   tool_started    {call_id, tool, input, label?}                   tool_started
#   tool_result     {call_id, tool, output, ok, diff?, ms?}          tool_result
#   approval        {id, kind: command|patch|tool|other, detail}     question (Allow / Deny)
#   question        {id, questions: [{header, question, options?}]}  question (protocol 27)
#   usage           {input_tokens, output_tokens, context_pct?,       context
#                    cost_usd?, model?}
#   notice          {text}                                           status
#   done            {text, stop_reason: end|interrupted|error}        (the turn's answer)
#   error           {text, code?}                                    error
#
# `tool` is the guest's own tool name mapped onto Relay's where the meaning is the same
# (run_command, read_file, write_file, edit_file, list_directory, search, web, agent, other —
# `tool_labels` already knows the first four), with the guest's original name kept in
# `input["_guest_tool"]` so nothing is lost. `diff` is a unified diff of the edit the guest made,
# when the harness reports one; Relay prints it under the call line exactly as it does for its own
# edits (protocol 23).

EVENT_KINDS = ("started", "delta", "thinking", "tool_started", "tool_result", "approval",
               "question", "usage", "notice", "done", "error")

TOOL_NAMES = ("run_command", "read_file", "write_file", "edit_file", "list_directory", "search",
              "web", "agent", "other")


@dataclass(frozen=True)
class HarnessEvent:
    kind: str
    data: dict = field(default_factory=dict)

    def __post_init__(self):
        if self.kind not in EVENT_KINDS:
            raise ValueError(f"unknown harness event {self.kind!r}; one of {', '.join(EVENT_KINDS)}.")
        if not isinstance(self.data, dict):
            raise ValueError("a harness event's data is a dict.")


Emit = Callable[[HarnessEvent], None]


# ----- errors ---------------------------------------------------------------------------------


class HarnessError(RuntimeError):
    """The harness could not do what it was asked: not installed, not logged in, the process
    died, the protocol was not what the adapter expects. The text is for the person."""


class HarnessNotAvailable(HarnessError):
    """The guest is not installed here, or its harness mode is not usable (say why)."""


# ----- the harness -----------------------------------------------------------------------------


@dataclass
class TurnResult:
    """What `send()` returns once the turn is over: the final answer, how it ended, and the
    usage the guest reported (empty when it reported none)."""
    text: str
    stop_reason: str = "end"          # end | interrupted | error
    usage: dict = field(default_factory=dict)


@dataclass
class HarnessStart:
    """What `start()` returns: the guest's own session id (what its transcript is filed under and
    what a later `resume` names) and the model it is running on."""
    session_id: str
    model: str


class Harness(Protocol):
    """One guest process for one pane. Adapters are ordinary classes matching this shape; the
    provider holds one and only ever calls these members.

    Threading: `send()` blocks for the whole turn and is called from the worker's turn thread;
    `interrupt()`, `answer()` and `close()` may be called from any thread while it blocks, and
    must not block themselves. `emit` is called from the turn thread only.
    """

    guest: str                       # "claude" | "codex"

    def start(self, *, cwd: str, model: str | None = None, resume: str | None = None,
              fork: bool = False, permissions: str = "bypass") -> HarnessStart:
        """Start the guest process in `cwd`. `resume` is a guest session id to continue (with
        `fork`, as a new session branched from it); `permissions` is one of PERMISSIONS.
        Raises HarnessNotAvailable when the guest cannot be started here."""

    def send(self, prompt: str, *, attachments: list[dict] | None = None, emit: Emit,
             cancel: threading.Event) -> TurnResult:
        """Run one turn: the prompt (and images, as `{"kind": "image", "media_type", "data"}`
        base64 attachments) in, events out through `emit`, the final answer back. Returns with
        stop_reason "interrupted" when `cancel` is set or `interrupt()` was called; raises
        HarnessError when the guest died or answered nonsense."""

    def interrupt(self) -> None:
        """Ask the guest to stop the running turn. Idempotent; a no-op when nothing runs."""

    def set_model(self, model: str) -> str:
        """Switch the guest's model for the next turn; returns the model as the guest names it."""

    def compact(self) -> None:
        """Ask the guest to compact its own context, when it can (a no-op otherwise)."""

    def answer(self, request_id: str, decision: dict) -> None:
        """Answer an `approval` (`{"behavior": "allow"|"deny", "message"?}`) or a `question`
        (`{"answers": [[...], ...]}`, protocol 27.3) the harness raised during `send()`."""

    def close(self) -> None:
        """End the guest process. Idempotent."""

    @property
    def session_id(self) -> str: ...

    @property
    def model(self) -> str: ...


def validate_permissions(value) -> str:
    if value is None:
        return "bypass"
    if value not in PERMISSIONS:
        raise ValueError(f"permissions must be one of {', '.join(PERMISSIONS)}.")
    return value


def map_tool_name(guest: str, name: str) -> str:
    """The guest's tool name in Relay's vocabulary (TOOL_NAMES), so protocol 23's labels apply.
    Unknown names are `other`; the original travels in the event's input as `_guest_tool`."""
    key = (name or "").strip()
    table = _CLAUDE_TOOLS if guest == "claude" else _CODEX_TOOLS if guest == "codex" else {}
    return table.get(key, table.get(key.lower(), "other"))


# Claude Code's built-in tools (the names on the wire are the tool_use `name` fields).
_CLAUDE_TOOLS = {
    "Bash": "run_command", "Read": "read_file", "Write": "write_file", "Edit": "edit_file",
    "MultiEdit": "edit_file", "NotebookEdit": "edit_file", "Glob": "list_directory",
    "LS": "list_directory", "Grep": "search", "WebFetch": "web", "WebSearch": "web",
    "Task": "agent", "Agent": "agent", "TodoWrite": "other", "AskUserQuestion": "other",
}
# Codex app-server item types (`item/started` → `item.type`), lower-cased on the wire.
_CODEX_TOOLS = {
    "commandexecution": "run_command", "command_execution": "run_command",
    "filechange": "edit_file", "file_change": "edit_file",
    "mcptoolcall": "other", "mcp_tool_call": "other", "websearch": "web", "web_search": "web",
    "collabagenttoolcall": "agent", "collab_agent_tool_call": "agent",
    "dynamictoolcall": "other", "dynamic_tool_call": "other",
    "imageview": "read_file", "image_view": "read_file",
}
