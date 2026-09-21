# SPDX-License-Identifier: AGPL-3.0-or-later
"""Per-card consoles for the Switchboard tests (protocol 19.16), with no model behind them.

A card turn runs on its own `Agent` and, since card #CTRN, on that card's own
`TurnSupervisor` (`relay_core.board_turns`), so the tests that used to read the stub
supervisor's `submitted` list read `CardAgents.prompts` here instead.  Everything else about
the turn is real: the pool, the queue, the per-card conversation, the `CardScope` the
supervisor opens through `set_card_turn`, and the events the worker sends.

    self.cards = fake_cards.CardAgents(self.commands, str(self.repo))   # installs itself
    self.send(type="board_ask", card=card_id, text="why?")              # waits for the turn
    self.cards.prompts[-1]["prompt"]                                    # what the model saw
    self.cards.agent(card_id).emitted                                   # what it sent back

A turn can be held open with `hold(card_id)` — the pool has really started a thread, so two
cards can be running at once in a test, which is the whole point of the change.
"""
from __future__ import annotations

import threading
import time

from relay_core.provider import ProviderConfig

CONFIG = ProviderConfig(api_key="", base_url="https://example.invalid", model="stub/one")


class FakeCardAgent:
    """One card's agent: records its prompts, streams a scripted answer, ends how it is told."""

    def __init__(self, owner, card_id: str, tools, emit):
        self.owner = owner
        self.card_id = card_id
        self.board = tools
        self.emit = emit
        self.config = CONFIG
        self.session_id = "s-card"
        self.cancel_event = threading.Event()
        self.emitted: list[dict] = []
        self.turn_id: str | None = None
        #: What the turn says before it ends, as one or more deltas.
        self.answer: list[str] = ["Answered."]
        #: "done", "error" or "cancelled".
        self.outcome = "done"
        #: Tool calls to announce between the deltas, as (index, tool name).
        self.tools_at: list[tuple[int, str]] = []
        #: Set to hold the turn open until `release()`.
        self.gate: threading.Event | None = None
        #: The scope this card's tools had while the turn ran, kept for the assertions that
        #: check a Plan cannot be widened (the supervisor closes it when the turn ends).
        self.scope_during = None
        #: What a console keeps between turns: `end_card_turn` puts the `ConsoleScope` back.
        self.scope_between = getattr(tools, "card_scope", None)
        #: `TurnSupervisor.set_agent` hands the agent its steering source.
        self.steer_source = None

    # ---- what the agent does ----------------------------------------------
    def ask(self, prompt, reset_cancellation=True, turn_id=None, **kw):
        self.turn_id = turn_id
        self.owner.prompts.append({"card": self.card_id, "prompt": prompt, "turn_id": turn_id})
        self.scope_during = getattr(self.board, "card_scope", None)
        if self.gate is not None:
            self.gate.wait(10)
        if self.cancel_event.is_set():
            self.send({"event": "cancelled", "turn_id": turn_id})
            return
        tools_at = dict((index, name) for index, name in self.tools_at)
        for index, chunk in enumerate(self.answer):
            if index in tools_at:
                self.send({"event": "tool_started", "tool": tools_at[index], "turn_id": turn_id})
            self.send({"event": "delta", "text": chunk, "turn_id": turn_id})
        self.send({"event": "turn_summary", "turn_id": turn_id, "outcome": self.outcome})
        self.send({"event": self.outcome, "turn_id": turn_id,
                   **({"text": "the provider said no"} if self.outcome == "error" else {})})

    def set_card_turn(self, mode, card_id):
        """What `relay_core.agent.Agent.set_card_turn` does, for a card turn's two lines (#CTRN).

        The supervisor calls it around the ask; the board opens and closes the `CardScope` that
        says what this mode may touch, exactly as it does behind the real agent.
        """
        if mode and card_id:
            self.board.begin_card_turn(mode, card_id)
        else:
            self.board.end_card_turn()
            self.scope_between = getattr(self.board, "card_scope", None)

    def stop(self):
        self.cancel_event.set()
        self.release()

    # ---- the test's handles ------------------------------------------------
    def send(self, event: dict) -> None:
        self.emitted.append(event)
        self.emit(event)

    def release(self) -> None:
        if self.gate is not None:
            self.gate.set()

    def events(self, name: str) -> list[dict]:
        return [e for e in self.owner.events if e.get("event") == name
                and e.get("card_id") == self.card_id]


class CardAgents:
    """Installs fake per-card agents on a `BoardCommands` and watches what they are handed."""

    def __init__(self, commands, workspace: str, events: list | None = None):
        self.commands = commands
        self.workspace = workspace
        #: The worker's event list, when the test shares one; only for `FakeCardAgent.events`.
        self.events = events if events is not None else []
        self.prompts: list[dict] = []
        #: Card ids in the order their agents were built.  A build is a reseeded conversation:
        #: the first question about a card, or one after the card changed.
        self.builds: list[str] = []
        self.agents: dict[str, FakeCardAgent] = {}
        self.tools: dict[str, object] = {}
        #: Gates by card, for turns a test wants to hold open (set before the ask).
        self.gates: dict[str, threading.Event] = {}
        #: `send()` waits for the card turns to finish unless a test is holding one open.
        self.autowait = True
        commands._build_card_console = self.build

    def build(self, card_id: str, emit):
        tools = self.commands.agent_tools(self.workspace, {"board": self.commands.settings["raw"]})
        agent = FakeCardAgent(self, card_id, tools, emit)
        agent.gate = self.gates.get(card_id)
        self.agents[card_id] = agent
        self.tools[card_id] = tools
        self.builds.append(card_id)
        return agent, tools

    def agent(self, card_id: str) -> FakeCardAgent:
        return self.agents[card_id]

    def hold(self, card_id: str) -> threading.Event:
        """Make this card's turn wait until the event is set (or the agent is stopped)."""
        gate = self.gates.setdefault(card_id, threading.Event())
        if card_id in self.agents:
            self.agents[card_id].gate = gate
        self.autowait = False
        return gate

    def wait(self, timeout: float = 5.0) -> bool:
        """Until every card turn has finished and every card queue is empty (#CTRN).

        A card has a queue of its own now, so "the turn is over" is no longer "nothing is
        running": a prompt that queued behind it has still to run.
        """
        deadline = time.time() + timeout
        while time.time() < deadline:
            if self.commands.cards.idle():
                return True
            time.sleep(0.005)
        return False

    def wait_running(self, count: int = 1, timeout: float = 5.0) -> bool:
        deadline = time.time() + timeout
        while time.time() < deadline:
            if self.commands.cards.count() >= count:
                return True
            time.sleep(0.005)
        return False
