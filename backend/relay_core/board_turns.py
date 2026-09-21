# SPDX-License-Identifier: AGPL-3.0-or-later
"""One agent per card, so several cards can be planned at once (protocol 19.16).

Until 2026-09-19 the whole Switchboard had **one** turn: the worker's single `TurnSupervisor`,
its single `Agent` with one conversation, and one `CardScope` slot on that agent's board tools.
So a Plan on a second card was refused outright — `board_busy`, "The Switchboard agent is busy
with a question on #ZW95" — and moving to another card reset the conversation of the one you
left.  The owner's report (2026-09-19): *"if i was planning in one card, i couldnt plan in
another card."*

A card turn does not need any of what that one runner provides.  It is a single prompt with no
queue, no steering and no request ledger; Plan is read-only by construction (`CardScope`) and
writes one section of its own card, hash-checked under the board's lock like every other write.
What it does need is its own conversation and its own scope, which is exactly what it never had.

So each card gets a `CardSession`: its own `Agent` built from the pane agent's provider config,
its own `BoardTools` instance — and therefore its own `card_scope`, which is why nothing in
`board_tools.py` changes — and one thread that calls `agent.ask()`.  Turns on *different* cards
run at the same time, as many as the owner clicks: the concurrent cap (`MAX_RUNNING` 3, ceiling
12, the `board.limits.max_card_turns` option) was removed the day it landed (owner, 2026-09-19,
*"remove the cap on number of agents in the switchboard"* — every turn is a stream the owner
started on purpose, the way a dozen panes are).  A second turn on the *same* card is still
refused, because two agents writing one card's `## Plan` would each undo the other.  A
whole-board cleanup stays exclusive: it rewrites cards the card turns are talking about.

Sessions outlive their turn, so a follow-up Discuss on a card continues that card's conversation
instead of reseeding — the thing the old single conversation could only do for the most recent
card.  The least recently used ones are dropped past `MAX_SESSIONS`.
"""
from __future__ import annotations

import threading
import time
import uuid
from collections import OrderedDict
from dataclasses import dataclass, field
from typing import Callable

from . import localtext

#: Card conversations kept for a follow-up.  Each is an `Agent` holding its messages, so this is
#: a memory ceiling, not a policy: past it the least recently used card reseeds from its file.
#: A *running* card's session is never dropped, however many are running.
MAX_SESSIONS = 6

#: Turn events that are tagged with the card they belong to (protocol 19.10, unchanged).
CARD_TAGGED = ("delta", "done", "error", "cancelled", "turn_summary", "thinking",
               "thinking_delta", "thinking_done", "tool_started", "tool_result", "status")

#: Turn events that also carry the mode.
MODE_TAGGED = ("delta", "done", "error", "cancelled", "turn_summary", "turn_started")

TERMINAL = ("done", "error", "cancelled")


def surface_of(card_id: str) -> str:
    """The `surface` a card's turn events carry (protocol 33, card #AGNT).

    A card console is one of the surfaces a console can be, so its turns are addressed the way
    every other console's are — `card:AGNT` — rather than only by `card_id`. Both ride: the id
    is what the board side routes by and has since 19.10, and the surface is what an
    `AgentConsole` matches against the ask it made.
    """
    return f"card:{card_id}"


@dataclass
class CardSession:
    """One card's conversation: its agent, its tools (and so its scope), and its running turn."""

    card_id: str
    agent: object
    tools: object
    #: The mode whose brief this conversation last carried, so a second Discuss sends the
    #: owner's words alone and a change of mode sends the brief (19.10, unchanged).
    brief_mode: str | None = None
    #: The card hash the conversation was seeded at: an edited card reseeds rather than being
    #: answered from a stale copy.
    seed_hash: str | None = None
    mode: str = "discuss"
    turn_id: str | None = None
    request_id: object = None
    #: The answer as it streams, joined and appended to the card's thread when the turn ends.
    text: list[str] = field(default_factory=list)
    thread: threading.Thread | None = None
    #: True from `start` until the turn's thread exits; `ended` from its terminal event, which
    #: is emitted a hair before the thread unwinds.
    active: bool = False
    ended: bool = False
    #: How the turn's own terminal event read, for the `agent_finished` that closes the boundary.
    outcome: str | None = None
    started: float = 0.0

    def seconds(self) -> float:
        return max(0.0, time.time() - self.started) if self.started else 0.0


class CardTurns:
    """The live card sessions of one Switchboard worker."""

    def __init__(self, emit: Callable[[dict], None], build: Callable[[str, Callable], tuple],
                 on_answer: Callable[[CardSession, str | None, str], None] | None = None,
                 *, max_sessions: int = MAX_SESSIONS):
        #: Where a card turn's events go.  The worker's raw emit, deliberately: these events are
        #: tagged here, and they are not the pane agent's turns — the subagent manager and the
        #: pane-title code must not read them as a main turn ending.
        self._emit = emit
        #: `build(card_id, emit) -> (agent, tools)`; the protocol object owns what an agent needs.
        self._build = build
        #: Called on a finished turn with the answer text, to append it to the card's thread.
        self._on_answer = on_answer
        self._max_sessions = max(1, int(max_sessions))
        self._lock = threading.RLock()
        self._sessions: "OrderedDict[str, CardSession]" = OrderedDict()

    # ---- what is running -------------------------------------------------------
    def running(self) -> list[CardSession]:
        with self._lock:
            return [s for s in self._sessions.values() if s.active]

    def running_cards(self) -> list[str]:
        return [s.card_id for s in self.running()]

    def is_running(self, card_id: str) -> bool:
        with self._lock:
            session = self._sessions.get(card_id)
            return bool(session and session.active)

    def count(self) -> int:
        return len(self.running())

    def session(self, card_id: str) -> CardSession | None:
        with self._lock:
            return self._sessions.get(card_id)

    def mode_of(self, card_id: str) -> str | None:
        session = self.session(card_id)
        return session.mode if session and session.active else None

    # ---- starting and stopping -------------------------------------------------
    def start(self, card_id: str, mode: str, prompt: str, request_id=None,
              seed_hash: str | None = None) -> str:
        """Run `prompt` on this card's own agent, on its own thread.  Returns the turn id.

        The caller has already checked `is_running` and written the owner's entry to the
        thread; what is left here is the conversation, the scope and the thread.  How many
        cards may run at once is not checked anywhere: there is no cap (2026-09-19).
        """
        # A turn whose terminal event has gone out but whose thread has not unwound yet: the
        # pane sends the next ask the moment it sees `done`, and handing one agent two turns
        # would interleave two conversations. Waited for outside the lock, so the thread it is
        # waiting for can take it to finish.
        self._await_unwinding(card_id)
        with self._lock:
            session = self._sessions.get(card_id)
            if session is not None and session.active:
                raise ValueError(f"A turn is already running on #{card_id}.")
            if session is None:
                session = self._new_session(card_id)
            self._sessions.move_to_end(card_id)
            session.mode = mode
            session.seed_hash = seed_hash
            session.text = []
            session.turn_id = uuid.uuid4().hex
            session.request_id = request_id
            session.active, session.ended = True, False
            session.outcome = None
            session.started = time.time()
            session.tools.begin_card_turn(mode, card_id)
            turn_id = session.turn_id
            # The turn boundary a pane's queue has had since protocol 11 and a card turn had
            # not: the panel used to *infer* where a turn began and ended from the events it
            # saw, which is one line of #AGNT's table. `id` is the turn id, as it is for a pane
            # whose queue item id doubles as one.
            self._emit({"event": "agent_started", "id": turn_id, "turn_id": turn_id,
                        "card_id": card_id, "mode": mode, "surface": surface_of(card_id)})
            session.thread = threading.Thread(target=self._run, args=(session, prompt, turn_id),
                                              name=f"relay-card-{card_id}", daemon=True)
            session.thread.start()
            return turn_id

    def stop(self, card_id: str) -> bool:
        """Stop the turn running on one card (the card's Stop button).  True if one was."""
        with self._lock:
            session = self._sessions.get(card_id)
            if session is None or not session.active:
                return False
            stop = getattr(session.agent, "stop", None)
        if stop is not None:
            stop()
        return True

    def stop_all(self) -> int:
        stopped = 0
        for session in self.running():
            stopped += 1 if self.stop(session.card_id) else 0
        return stopped

    def drop(self, wait: float = 2.0) -> None:
        """Forget every session: the worker was pointed at another board, or is shutting down."""
        sessions = list(self._sessions.values())
        self.stop_all()
        for session in sessions:
            thread = session.thread
            if thread is not None and thread.is_alive():
                thread.join(wait)
        with self._lock:
            self._sessions.clear()

    def forget(self, card_id: str) -> None:
        """Drop one card's conversation (its card changed under it, or it was deleted)."""
        with self._lock:
            session = self._sessions.get(card_id)
            if session is not None and not session.active:
                del self._sessions[card_id]

    # ---- internals -------------------------------------------------------------
    def _await_unwinding(self, card_id: str, wait: float = 2.0) -> None:
        with self._lock:
            session = self._sessions.get(card_id)
            thread = session.thread if session is not None and session.active and session.ended else None
        if thread is not None and thread.is_alive():
            thread.join(wait)

    def _new_session(self, card_id: str) -> CardSession:
        """Build this card's agent, and drop the oldest idle conversation if we are over the cap."""
        session = CardSession(card_id=card_id, agent=None, tools=None)

        def emit(event: dict, session=session) -> None:
            self._observe(session, event)

        agent, tools = self._build(card_id, emit)
        session.agent, session.tools = agent, tools
        self._sessions[card_id] = session
        while len(self._sessions) > self._max_sessions:
            for cid, old in list(self._sessions.items()):
                if not old.active and cid != card_id:
                    del self._sessions[cid]
                    break
            else:
                break
        return session

    def _run(self, session: CardSession, prompt: str, turn_id: str) -> None:
        agent = session.agent
        outcome = "done"
        try:
            agent.cancel_event.clear()
            agent.ask(prompt, reset_cancellation=False, turn_id=turn_id)
        except Exception as exc:                        # ask() reports its own; this is defensive
            outcome = "error"
            self._emit({"event": "error", "card_id": session.card_id, "mode": session.mode,
                        "turn_id": turn_id, "surface": surface_of(session.card_id),
                        "text": f"The turn on #{session.card_id} failed "
                                f"({type(exc).__name__})."})
        finally:
            try:
                session.tools.end_card_turn()
            finally:
                with self._lock:
                    session.active = False
                    session.thread = None
                    outcome = session.outcome or outcome
                self._emit({"event": "agent_finished", "id": turn_id, "outcome": outcome,
                            "card_id": session.card_id, "mode": session.mode,
                            "surface": surface_of(session.card_id)})

    def _observe(self, session: CardSession, event: dict) -> None:
        """Tag one card turn's events with their card and mode, and collect the answer."""
        name = event.get("event")
        if name in ("delta", "answer") and isinstance(event.get("text"), str):
            session.text.append(event["text"])
        # What the model says before a tool call and after it are two paragraphs, not one
        # run-on line (a live Plan turn, 2026-09-18).
        if name == "tool_started" and session.text and not session.text[-1].endswith("\n\n"):
            session.text.append("\n\n")
        if name in CARD_TAGGED:
            event = {**event, "card_id": session.card_id, "surface": surface_of(session.card_id)}
        if name in MODE_TAGGED:
            event = {**event, "mode": session.mode}
        if name in TERMINAL:
            session.ended = True
            session.outcome = name
            if name == "done":
                # A provider that broke a tool call can have streamed part of it as content
                # (#VN69): the fragment rides the deltas into `session.text` and would land on
                # the card's thread as if the model had written it. Cut it before the write.
                answer = localtext.strip_tool_fragments("".join(session.text)).strip()
                session.text = []
                session.brief_mode = session.mode
                if answer and self._on_answer is not None:
                    try:
                        self._on_answer(session, event.get("turn_id") or session.turn_id, answer)
                    except Exception as exc:            # a thread the board would not take
                        self._emit({"event": "error", "card_id": session.card_id,
                                    "text": f"The answer could not be written to #{session.card_id}: {exc}"})
            else:
                session.text = []
        self._emit(event)
