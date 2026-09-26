# SPDX-License-Identifier: AGPL-3.0-or-later
"""One console per card: its own conversation, its own queue (protocol 19.16, card #CTRN).

Until 2026-09-19 the whole Switchboard had **one** turn: the worker's single `TurnSupervisor`,
its single `Agent` with one conversation, and one `CardScope` slot on that agent's board tools.
So a Plan on a second card was refused outright — `board_busy`, "The Switchboard agent is busy
with a question on #ZW95" — and moving to another card reset the conversation of the one you
left.  The owner's report (2026-09-19): *"if i was planning in one card, i couldnt plan in
another card."*  Each card got a `CardSession` of its own that day: its own `Agent`, its own
`BoardTools` — and therefore its own `card_scope`, which is why nothing in `board_tools.py`
changed — and one bare thread that called `agent.ask()`.

That bare thread is what card #CTRN takes away (owner, 2026-09-21: *"that sounds sensible to
me, scope that"*).  A card turn was the last surface in Relay with a turn runner of its own, so
it was the last one with no queue, no steering, no request ledger, no interrupt and no turn
boundary it did not have to infer — the whole of #AGNT's table, on one page.  A second prompt on
a busy card was refused (`board_busy`) because the runner underneath could only hold one.

So each session now holds a **`TurnSupervisor`** — the pane's, unchanged, one deque and one
daemon thread apiece — and a card turn is an ordinary supervised turn: `queue`, `queue_move`,
`queue_steer`, Esc, the ledger and the §12 strip, all of it, per card.  Turns on *different*
cards still run at the same time, as many as the owner clicks (the concurrent cap was removed
the day it landed, owner 2026-09-19: *"remove the cap on number of agents in the switchboard"*),
and a second prompt on the *same* card now **queues** instead of being refused.  What a mode may
touch is still enforced for the length of the turn, by `Agent.set_card_turn` opening the
`CardScope` that has not moved — the stage machine of 19.20 is a rule about the turn, never a
narrower tool list (owner decision 3 on #CTRN).

Sessions outlive their turn, so a follow-up Discuss on a card continues that card's conversation
instead of reseeding, and since #CTRN that conversation is *persisted* per (tab, card), so it
survives a restart.  The least recently used ones are dropped past `MAX_SESSIONS`; one that is
running, or that has anything waiting in its queue, is never dropped.
"""
from __future__ import annotations

import threading
import time
from collections import OrderedDict
from dataclasses import dataclass, field
from typing import Callable

from . import localtext
# The pane's own turn runner. A card turn is one of those now, which is the whole of this card;
# it is imported here rather than late because there is nothing else underneath a card session.
from .queue import TurnSupervisor

#: Card conversations kept for a follow-up.  Each is an `Agent` holding its messages and a
#: supervisor holding a thread, so this is a memory ceiling, not a policy: past it the least
#: recently used card reseeds from its file.  A session that is *running* or has anything
#: queued is never dropped, however many there are.
MAX_SESSIONS = 6

#: Turn events that are tagged with the card they belong to (protocol 19.10, unchanged).
CARD_TAGGED = ("delta", "done", "error", "cancelled", "turn_summary", "thinking",
               "thinking_delta", "thinking_done", "tool_started", "tool_result", "status")

#: Turn events that also carry the mode.  `turn_started` was listed here and in `board_chat`
#: and consumed by the helper panel, and nothing has ever emitted it; #AGNT dropped the name
#: rather than leave a third copy of a tag no worker sends.
MODE_TAGGED = ("delta", "done", "error", "cancelled", "turn_summary")

#: The supervisor's own events (protocol 12 and 33), tagged with the card whose queue they are
#: about.  One worker now runs several queues — the tab's and one per live card — so a
#: `queue_changed` that says nothing addresses the wrong strip.  `agent_started` and
#: `agent_finished` already carry `card_id` and `mode` from the queue item that is running
#: (`queue._card_of`); they are listed because tagging them again with the same values costs
#: nothing and makes "every event of a card's turn names its card" true by construction.
QUEUE_TAGGED = ("queued", "queue_changed", "queue_ack", "steer_delivered", "steer_returned",
                "steer_removed", "steer_escalated", "interrupting",
                "agent_started", "agent_finished")

TERMINAL = ("done", "error", "cancelled")


def surface_of(card_id: str) -> str:
    """The `surface` a card's turn events carry (protocol 33, card #AGNT).

    A card console is one of the surfaces a console can be, so its turns are addressed the way
    every other console's are — `card:AGNT` — rather than only by `card_id`. Both ride: the id
    is what the board side routes by and has since 19.10, and the surface is what an
    `AgentConsole` matches against the ask it made, and since #CTRN what a queue op names when
    it operates that card's queue rather than the tab's.
    """
    return f"card:{card_id}"


def card_of_surface(surface) -> str:
    """The card a `surface` names (`card:AGNT` → `AGNT`), or "" for anything else (#CTRN)."""
    if isinstance(surface, str) and surface.startswith("card:"):
        return surface[len("card:"):].strip()
    return ""


@dataclass
class CardSession:
    """One card's console: its agent, its tools (and so its scope), and its own queue."""

    card_id: str
    agent: object
    tools: object
    #: This card's turn runner (`relay_core.queue.TurnSupervisor`), the pane's own, one per card.
    turns: TurnSupervisor | None = None
    #: The mode whose brief this conversation last carried, so a second Discuss sends the
    #: owner's words alone and a change of mode sends the brief (19.10, unchanged).
    brief_mode: str | None = None
    #: The card hash the conversation was seeded at: an edited card reseeds rather than being
    #: answered from a stale copy.
    seed_hash: str | None = None
    #: The running turn's mode, read from its own `agent_started` — with a queue, "submitted"
    #: and "running" are two moments, so a Plan queued behind a Discuss is this card's mode only
    #: once it starts.
    mode: str = "discuss"
    turn_id: str | None = None
    request_id: object = None
    #: The answer as it streams, joined and appended to the card's thread when the turn ends.
    text: list[str] = field(default_factory=list)
    #: How many prompts are waiting in this card's queue, from its own `queue_changed`: what
    #: keeps a session with a queue from being evicted under the LRU.
    pending: int = 0
    #: True from the turn's terminal event, which is emitted a hair before the supervisor's
    #: `agent_finished`.
    ended: bool = False
    #: How the turn's own terminal event read.
    outcome: str | None = None
    started: float = 0.0

    @property
    def active(self) -> bool:
        """Whether a turn is running on this card right now (the supervisor's own answer)."""
        return bool(self.turns is not None and self.turns.busy)

    @property
    def idle(self) -> bool:
        """Nothing running and nothing waiting: what the LRU may drop and a test may stop at."""
        return not self.active and not self.pending

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

    def working(self) -> list[CardSession]:
        """Sessions with a turn running **or** one waiting in their queue (card #CTRN).

        The distinction is new because the queue is: `board_ask` returns the moment the prompt
        is accepted, and the dispatcher picks it up a heartbeat later, so "is a turn running on
        this card" stopped being the right question for anything that must not race a write.
        A delete, a priority change and a cleanup ask this one; `board_cancelled {cards}` — the
        lamp on a phone — still asks `running`, because a paused queue is not a running turn.
        """
        with self._lock:
            return [s for s in self._sessions.values() if not s.idle]

    def working_cards(self) -> list[str]:
        return [s.card_id for s in self.working()]

    def idle(self) -> bool:
        """Nothing running and nothing queued on any card (what a test waits for)."""
        with self._lock:
            return all(s.idle for s in self._sessions.values())

    def session(self, card_id: str) -> CardSession | None:
        with self._lock:
            return self._sessions.get(card_id)

    def mode_of(self, card_id: str) -> str | None:
        session = self.session(card_id)
        return session.mode if session and session.active else None

    def queue_of(self, card_id: str) -> TurnSupervisor | None:
        """One card's queue, or None when that card has no live session (33, #CTRN).

        This is the whole of what `queue_remove`, `queue_move`, `queue_steer`, `queue_unsteer`
        and `cancel` gained: with `surface: "card:<ID>"` they operate that card's queue, and a
        message that names no card — every message a pane or a tab console sends — reaches the
        worker's own supervisor exactly as it did.
        """
        session = self.session(card_id) if card_id else None
        return session.turns if session is not None else None

    # ---- starting and stopping -------------------------------------------------
    def submit(self, card_id: str, mode: str, prompt: str, request_id=None,
               seed_hash: str | None = None, preview: str = "") -> str:
        """Put `prompt` on this card's own queue.  Returns the turn id (the queue item's).

        The caller has already validated the mode, written the owner's entry to the thread and
        built the prompt from the mode's brief; what is left here is the conversation and the
        queue.  A prompt sent while this card is busy **queues** — that is the change of card
        #CTRN, and it is why there is no `is_running` check left in this method — while turns on
        different cards go on running at the same time, uncounted.

        `when` is `queue` rather than `now` for exactly that reason: `now` is the pane's "refuse
        if something is already running", which is the refusal this card removes.

        `preview` is the owner's own words: the prompt is the card's seed block and the mode's
        brief, and the queue row and the request ledger show what was typed.
        """
        evicted: list[CardSession] = []
        with self._lock:
            session = self._sessions.get(card_id)
            if session is None:
                session = self._new_session(card_id, evicted)
            self._sessions.move_to_end(card_id)
            session.seed_hash = seed_hash
            session.request_id = request_id
            turns = session.turns
        for old in evicted:                                # outside the lock: it joins a thread
            old.turns.shutdown(timeout=1.0)
            self._close_guest(old)
        # Outside the lock too: `submit` emits `queued` and `queue_changed` through `_observe`.
        return turns.submit(prompt, "queue", request_id, surface=surface_of(card_id),
                            mode=mode, card=card_id, preview=preview)

    def stop(self, card_id: str) -> bool:
        """Stop the turn running on one card (the card's Stop button).  True if one was.

        The supervisor's own `cancel`, so it does to a card's queue exactly what Esc does to a
        pane's: the running turn is stopped and what is queued waits for `resume_queue`.
        """
        with self._lock:
            session = self._sessions.get(card_id)
            if session is None or not session.active:
                return False
            turns = session.turns
        turns.cancel()
        return True

    def stop_all(self) -> int:
        stopped = 0
        for session in self.running():
            stopped += 1 if self.stop(session.card_id) else 0
        return stopped

    def drop(self, wait: float = 2.0) -> None:
        """Forget every session: the worker was pointed at another board, or is shutting down."""
        self.stop_all()
        with self._lock:
            sessions = list(self._sessions.values())
            self._sessions.clear()
        for session in sessions:
            session.turns.shutdown(wait)
            self._close_guest(session)

    def forget(self, card_id: str) -> None:
        """Drop one card's conversation (its card changed under it, or it was deleted)."""
        with self._lock:
            session = self._sessions.get(card_id)
            if session is None or not session.idle:
                return
            del self._sessions[card_id]
        session.turns.shutdown(timeout=1.0)
        self._close_guest(session)

    @staticmethod
    def _close_guest(session: CardSession) -> None:
        """A discarded card no longer owns its separate guest process (#ZPSG)."""
        from .guest_harness_provider import detach
        detach(session.agent)

    # ---- internals -------------------------------------------------------------
    def _new_session(self, card_id: str, evicted: list) -> CardSession:
        """Build this card's console, and evict the oldest idle conversation past the cap.

        The supervisor is made first and the agent built on *its* emit, so every event of a card
        turn is tagged with the turn's surface by the supervisor (33) before `_observe` adds the
        card and the mode (19.10) — one order, whichever of the two a reader knows about.
        """
        session = CardSession(card_id=card_id, agent=None, tools=None)

        def emit(event: dict, session=session) -> None:
            self._observe(session, event)

        session.turns = TurnSupervisor(emit)
        try:
            agent, tools = self._build(card_id, session.turns.agent_emit)
        except Exception:
            session.turns.shutdown(timeout=1.0)
            raise
        session.agent, session.tools = agent, tools
        session.turns.set_agent(agent)
        self._sessions[card_id] = session
        while len(self._sessions) > self._max_sessions:
            for cid, old in list(self._sessions.items()):
                if old.idle and cid != card_id:
                    del self._sessions[cid]
                    evicted.append(old)
                    break
            else:
                break
        return session

    def _observe(self, session: CardSession, event: dict) -> None:
        """Tag one card's events with their card and mode, and collect the answer.

        Lock-free on purpose: it runs on the supervisor's dispatcher thread, inside `submit`
        and inside `agent.ask`, and taking the pool's lock here would let an eviction that is
        joining a supervisor's thread deadlock against it.
        """
        name = event.get("event")
        if name == "agent_started":
            # With a queue, a turn's mode is the *running* item's, not the last one submitted.
            session.turn_id = event.get("turn_id") or event.get("id")
            session.mode = event.get("mode") or session.mode
            session.text = []
            session.outcome = None
            session.ended = False
            session.started = time.time()
        elif name == "queue_changed":
            session.pending = len(event.get("items") or []) + len(event.get("steering") or [])
        if name in ("delta", "answer") and isinstance(event.get("text"), str):
            session.text.append(event["text"])
        # What the model says before a tool call and after it are two paragraphs, not one
        # run-on line (a live Plan turn, 2026-09-18).
        if name == "tool_started" and session.text and not session.text[-1].endswith("\n\n"):
            session.text.append("\n\n")
        if name in CARD_TAGGED or name in QUEUE_TAGGED:
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
