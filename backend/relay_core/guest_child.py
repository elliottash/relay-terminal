# SPDX-License-Identifier: AGPL-3.0-or-later
"""A Relay child backed by a guest; start on its worker thread, never inside MCP dispatch."""
from __future__ import annotations

from .provider import Cancelled


class GuestChildProvider:
    serves_side_calls = False
    record_guest_tools = True

    def __init__(self, config, workspace, *, permissions, effort, skills, instructions):
        self.config, self.workspace = config, workspace
        self.permissions, self.effort = permissions, effort
        self.skills, self.instructions = skills, instructions
        self.agent = None
        self.live = None
        self.session_id = None

    def response_open(self):
        return False

    def cancel(self):
        if self.live is not None:
            self.live.cancel()

    def resolve_question(self, message):
        return self.live.resolve_question(message) if self.live is not None else False

    def complete(self, messages, tools, emit, cancel):
        from . import guest_harness_provider as guests
        if self.agent is None or messages is not self.agent.messages:
            return {"role": "assistant", "content": ""}
        if cancel.is_set():
            raise Cancelled("Stopped.")
        options = {"model": self.config.model, "permissions": self.permissions,
                   "resume": self.session_id}
        if self.effort:
            options["effort"] = self.effort
        provider = guests.start_provider(
            guests.config_preset(self.config), {"guest": options}, self.workspace,
            config=self.config, skill_index=self.skills, instruction_suffix=self.instructions,
            delegation=False)
        self.live = provider
        try:
            guests.attach(self.agent, provider)
            self.session_id = provider.session_id
            if cancel.is_set():
                raise Cancelled("Stopped.")
            return provider.complete(messages, tools, emit, cancel)
        finally:
            # Follow-up messages resume the same guest session with a fresh process.
            # No guest processes or capability sockets survive a finished child run.
            provider.close()
            self.live = None
