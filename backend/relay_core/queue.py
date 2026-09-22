# SPDX-License-Identifier: AGPL-3.0-or-later
"""Single-dispatcher supervisor for agent turns: run now, queue, or interrupt.

Invariants:
* Exactly one dispatcher thread calls ``Agent.ask``; two turns never overlap.
* Every accepted prompt goes through one ordered queue. "now" and "interrupt"
  items are *forced*: they start even while the queue is paused.
* The cancellation flag is cleared only under the supervisor lock at the moment
  a turn is dequeued, and ``Agent.stop`` is only called under that lock, so a
  late stop can never hit the prompt that replaced the stopped turn.
* A user ``cancel`` or a failed turn pauses the queue. Queued (non-forced)
  prompts then wait for ``resume_queue`` -- or for the next prompt a person submits
  to this supervisor, which resumes it (card #7JD1). The pause resets when the queue
  empties. A turn that stops at the step/tool limit ends with ``done`` and does not
  pause it.
* Every accepted prompt gets a request ledger entry (``ledger_id``) before it is queued or
  delivered, so no path (queue, steer, interrupt, requeue) can lose it.
"""
from __future__ import annotations

import threading
import uuid
from collections import deque
from typing import Callable

from .agent import validate_context
from .agent_context import validate_screen, validate_surface

MAX_QUEUE = 32
MAX_PROMPT = 131072
#: How long `ask {mode}` and `ask {card}` may be (card #CTRN). A card id is four characters and
#: a mode is a word; this is the same "one short line" `surface` gets, for the same reason.
MAX_CARD_TURN = 64
PREVIEW = 120
TERMINAL = {"done", "error", "cancelled"}


def _surface_of(item: dict) -> dict:
    """`{"surface": …}` when the item has one, `{}` when it has not (protocol 33).

    Absent rather than empty, so a terminal pane's events are byte-for-byte what they were
    before the field existed and a client that does not know it never has to skip it.
    """
    return {"surface": item["surface"]} if item.get("surface") else {}


def _preview_of(item: dict) -> str:
    """What the queue strip and the request ledger show for an item (protocol 12, 33).

    The prompt, for everything a person types: it *is* what they typed. A card turn is the one
    caller that sends something else — `board_ask` builds the model's prompt out of the card's
    seed block and the mode's brief — and the strip on a card page would otherwise read
    "[Switchboard card #CRD1 — …] You are Relay's Switchboard agent…" where the owner's question
    belongs. `screen`'s rule, one field along: the record is what the person typed (#CTRN).
    """
    return item.get("preview") or item["prompt"]


def _card_of(item: dict) -> dict:
    """`{"mode": …, "card_id": …}` when this item is a card turn, `{}` when it is not (#CTRN).

    Absent rather than empty for `_surface_of`'s reason. The **ask** says `card`, because that is
    what `board_ask {card, mode}` has always called it; every *event* says `card_id`, because
    that is what 19.10 tags a card turn's events with and what the board side routes by.
    """
    return {"mode": item.get("mode") or "", "card_id": item["card"]} if item.get("card") else {}


def validate_card_turn(mode, card) -> tuple[str, str]:
    """`ask {mode, card}` (card #CTRN): the stage the owner pressed and the card it is about.

    Both or neither — a mode with no card names no card and a card with no mode names no stage
    rule — and the *spelling* of the mode is the board's business, refused where `CARD_MODES`
    already lives (`BoardTools.begin_card_turn`). What is checked here is the shape, exactly as
    `surface` is checked here rather than against a list of the consoles that exist.
    """
    for value, what in ((mode, "mode"), (card, "card")):
        if value in (None, ""):
            continue
        if not isinstance(value, str) or "\n" in value or len(value) > MAX_CARD_TURN:
            raise ValueError(f"{what} must be one line of at most {MAX_CARD_TURN} characters.")
    mode, card = (mode or "").strip(), (card or "").strip()
    if bool(mode) != bool(card):
        raise ValueError("A card turn is asked with both `mode` and `card`, or with neither.")
    return mode, card


def validate_prompt(prompt) -> str:
    if not isinstance(prompt, str) or not prompt.strip() or len(prompt.encode("utf-8")) > MAX_PROMPT:
        raise ValueError("Prompt must contain 1–131072 bytes.")
    return prompt


class TurnSupervisor:
    def __init__(self, emit: Callable[[dict], None]):
        self._emit = emit
        self._lock = threading.Condition()
        self._queue: deque[dict] = deque()
        self._agent = None
        self._running: str | None = None
        self._paused = False
        self._closed = False
        self._outcome: str | None = None
        self._stop_reason: str | None = None
        #: Which console the running turn was asked from (`ask {surface}`, protocol 33), or "".
        #: Every event of that turn carries it, so several consoles can share one conversation
        #: and each still knows which of its own asks an event belongs to — owner decision 1 on
        #: card #AGNT: one conversation, drawn everywhere. A pane sends no surface and its
        #: events are byte-for-byte what they always were.
        self._surface = ""
        # Steering prompts waiting for the running turn's next step boundary.
        self._steer: list[dict] = []
        self._steer_inflight: list[dict] = []
        self._thread = threading.Thread(target=self._dispatch, name="relay-turns", daemon=True)
        self._thread.start()

    # ----- agent wiring -------------------------------------------------
    def agent_emit(self, event: dict) -> None:
        """Emit callback to give the Agent; records how each turn ended, and tags the surface."""
        if event.get("event") in TERMINAL:
            self._outcome = event["event"]
            self._stop_reason = event.get("stop_reason")
        if self._surface:
            event = {**event, "surface": self._surface}
        self._emit(event)

    @property
    def busy(self) -> bool:
        with self._lock:
            return self._running is not None

    @property
    def agent(self):
        return self._agent

    def set_agent(self, agent) -> None:
        """Replace the agent (configure). Refused while a turn runs; clears the queue."""
        with self._lock:
            if self._running is not None:
                raise ValueError("Stop the active agent turn before changing provider or workspace.")
            self._agent = agent
            if agent is not None:
                agent.steer_source = self.take_steer
                agent.steer_reserve = self.reserve_steer
                agent.steer_settle = self.settle_steer
            self._clear_locked()

    def reset(self) -> None:
        with self._lock:
            if self._running is not None:
                raise ValueError("Stop the active turn first.")
            if self._agent is not None:
                if hasattr(self._agent, "reset_conversation"):
                    self._agent.reset_conversation()  # new session; the old one stays saved
                else:
                    self._agent.messages = self._agent.messages[:1]
            self._clear_locked()

    # ----- requests -------------------------------------------------------
    def _ledger_add(self, prompt, when: str, origin: str, attachments, ledger_id=None) -> str | None:
        """Record the prompt in the agent's request ledger before it is queued (None without a ledger)."""
        agent = self._agent
        ledger = getattr(agent, "requests", None) if getattr(agent, "track_requests", False) else None
        if ledger is None:
            return None
        if ledger_id is not None:
            ledger.get(ledger_id)
            return ledger_id
        return ledger.add(prompt, {"now": "ask"}.get(when, when), origin=origin, attachments=attachments)["id"]

    def _ledger_cancel(self, items, reason: str) -> None:
        agent = self._agent
        ledger = getattr(agent, "requests", None) if getattr(agent, "track_requests", False) else None
        for item in items:
            if ledger is not None and item.get("ledger_id") and ledger.find(item["ledger_id"]) is not None:
                ledger.set_status(item["ledger_id"], "cancelled_by_user", reason)

    def submit(self, prompt, when: str = "now", request_id=None, context=None, attachments=None,
               origin: str = "user", requeue: bool = True, ledger_id=None,
               surface: str = "", screen: str = "", readonly: bool = False,
               mode: str = "", card: str = "", preview: str = "") -> str:
        """origin "relay" marks prompts Relay queued itself (e.g. a background subagent finished).

        A submit from anyone else **resumes a paused queue** on its way past (#7JD1); the comment
        at the bottom of this method says why the rule lives here and not in the GUI.

        when="steer" delivers the prompt inside the running turn at its next step boundary. If no
        turn is running it is queued like "queue". A steer prompt the turn never reached (the model
        answered without another tool call, or the turn stopped) is reported with steer_returned and,
        when requeue is true, becomes the next queued turn.

        `surface`, `screen` and `readonly` are protocol 33 (card #AGNT), and all three are the
        *console's* fields rather than the terminal's:

        * `surface` names which console asked. It rides on `queued`, on the queue rows and on
          every event of the turn, so four consoles sharing one conversation each know which of
          their own asks an event belongs to. A pane sends none and nothing changes for it.
        * `screen` is at most 2 000 characters of what the asking surface is showing — the rows
          being read, the search in the box. It reaches the model as an "On screen now:" line
          above the prompt and is **not** put into the prompt the queue and the ledger keep: the
          record is what the person typed. (It is not `context`, which is already the program
          context object.)
        * `readonly` is the turn that writes nothing by design — the Switchboard's survey, which
          offers an import and waits for the owner's answer.

        `mode` and `card` ride beside them (card #CTRN, owner 2026-09-21): a Discuss or Plan turn
        on one card, which is now an ordinary supervised turn rather than a runner of its own. They
        constrain the *turn* the way `readonly` does — `set_card_turn` opens the card's stage scope
        around the ask and closes it after — and they leave the tool **list** alone, so a card
        conversation that goes Discuss → Plan → Discuss re-prefills nothing.

        `preview` is what the queue row and the request ledger show when that is not the prompt
        — a card turn's prompt is the card's seed block and the mode's brief, and the row on the
        card page is the owner's question.
        """
        if when not in {"now", "queue", "interrupt", "steer"}:
            raise ValueError('"when" must be "now", "queue", "interrupt", or "steer".')
        if type(requeue) is not bool:
            raise ValueError("requeue must be a boolean.")
        if type(readonly) is not bool:
            raise ValueError("readonly must be a boolean.")
        surface, screen = validate_surface(surface), validate_screen(screen)
        mode, card = validate_card_turn(mode, card)
        preview = preview if isinstance(preview, str) else ""
        requested_steer = when == "steer"
        with self._lock:
            if when == "steer":
                if self._agent is None:
                    raise ValueError("Configure a provider and workspace first.")
                validate_prompt(prompt)
                if self._running is not None and not self._running.startswith(("compact-",)):
                    validate_context(context)
                    item = {"id": uuid.uuid4().hex, "prompt": prompt, "request_id": request_id,
                            "context": context, "attachments": attachments, "origin": origin,
                            "requeue": requeue, "surface": surface, "screen": screen,
                            "readonly": readonly, "mode": mode, "card": card,
                            "preview": preview}
                    item["ledger_id"] = self._ledger_add(_preview_of(item), "steer", origin,
                                                        attachments, ledger_id)
                    if ledger_id is not None and item["ledger_id"] is not None:
                        self._agent.requests.queued(ledger_id, item["id"])
                    else:
                        self._set_queue_item(item)
                    self._steer.append(item)
                    self._emit({"event": "queued", "id": item["id"], "request_id": request_id,
                                "when": "steer", "position": len(self._steer) - 1, "origin": origin,
                                "ledger_id": item["ledger_id"], **_surface_of(item), **_card_of(item)})
                    self._changed_locked()
                    return item["id"]
                when = "queue"
        with self._lock:
            if self._agent is None:
                raise ValueError("Configure a provider and workspace first.")
            validate_prompt(prompt)
            validate_context(context)
            if self._closed:
                raise ValueError("Worker is shutting down.")
            if len(self._queue) >= MAX_QUEUE:
                raise ValueError(f"Queue is full ({MAX_QUEUE} prompts).")
            busy = self._running is not None or (self._queue and not self._paused)
            if when == "now" and busy:
                raise ValueError("An agent turn is already active.")
            item = {"id": uuid.uuid4().hex, "prompt": prompt, "force": when != "queue", "context": context,
                    "attachments": attachments, "origin": origin, "surface": surface,
                    "screen": screen, "readonly": readonly, "mode": mode, "card": card,
                    "preview": preview}
            item["ledger_id"] = self._ledger_add(_preview_of(item),
                                                 "steer" if requested_steer else when, origin,
                                                 attachments, ledger_id)
            if ledger_id is not None and item["ledger_id"] is not None:
                self._agent.requests.queued(ledger_id, item["id"])
            else:
                self._set_queue_item(item)
            if item["force"]:
                # Interrupts are FIFO among themselves, ahead of ordinary queued prompts.
                position = sum(1 for _ in self._leading_forced())
                self._queue.insert(position, item)
            else:
                self._queue.append(item)
                position = len(self._queue) - 1
            self._emit({"event": "queued", "id": item["id"], "request_id": request_id,
                        "when": when, "position": position, "origin": origin,
                        "ledger_id": item["ledger_id"], **_surface_of(item), **_card_of(item)})
            if when == "interrupt" and self._running is not None:
                self._emit({"event": "interrupting", "id": self._running, "by": item["id"]})
                self._stop_locked()
            # **A submit is a resume** (card #7JD1; owner, 2026-09-21: "why don't we just copy
            # the functionality and have enter resume"). A `cancel` pauses this queue, and until
            # this card the only way back was `resume_queue` — which the pane sends from the
            # strip's Resume button and a device cannot send at all, so a prompt a phone queued
            # behind a turn it stopped waited for somebody at the desk. Going on with a new
            # prompt *is* going on: what was waiting behind it runs after it.
            #
            # It is done here rather than by the GUI sending `resume_queue` after its `ask`
            # because that leaves a pane's wire byte-for-byte what it was — nothing new is sent
            # when nothing is paused, and nothing new is sent when something is — and because
            # every surface that can submit gets the rule without asking for it: the pane, a
            # card console, and a device's `ask`/`board_ask`.
            #
            # `origin` "relay" is Relay's own prompt (a background subagent reporting back,
            # subagents.py), not somebody pressing Enter, and it does not undo a person's Stop.
            # It is cleared *after* the `busy` check above, which reads `_paused`: a submit that
            # arrives while a paused queue waits is "now" for the pane, and clearing the pause
            # first would make it "an agent turn is already active".
            if self._paused and origin == "user":
                self._paused = False
            self._changed_locked()
            self._lock.notify_all()
            return item["id"]

    def waiting(self, item_id) -> bool:
        """Whether a queue item is still queued or waiting to steer."""
        with self._lock:
            return item_id is not None and any(i["id"] == item_id for i in list(self._queue) + self._steer_inflight + self._steer)

    def now_or_later(self, now: Callable, later: Callable):
        """now() when nothing runs, else later(); decided under the lock, so a turn cannot start (or
        finish) between the check and the call. set_model uses it (issue 3ES1)."""
        with self._lock:
            return now() if self._running is None else later()

    def _settle_model_locked(self, agent, turn_id: str | None = None) -> None:
        """A model switch that arrived after the turn's last request applies once the turn is over.

        One that must compact the conversation first (a smaller window) runs as an exclusive task
        instead: the summary is a network call, which must not hold this lock, and queued prompts
        wait for it so the next turn runs on the new model."""
        apply = getattr(agent, "apply_pending_model", None)
        if apply is None:
            return
        try:
            compacts = getattr(agent, "pending_model_compacts", None)
            if compacts is not None and compacts():
                self.start_exclusive_locked("set_model", lambda a: a.apply_pending_model(turn_id, at="turn_end"))
                return
            # turn_id: the turn it waited for, so the landing can be tied to it.
            apply(turn_id, at="turn_end")
        except Exception as exc:  # never let it wedge the queue; the old model simply stays
            self._emit({"event": "error", "source": "set_model",
                        "text": str(exc)[:2000] if isinstance(exc, ValueError) else f"Model switch failed ({type(exc).__name__})."})

    def run_exclusive(self, name: str, task: Callable) -> None:
        """Run task(agent) on a background thread while no turn runs (compaction and other
        conversation rewrites). The supervisor counts as busy meanwhile, so queued prompts wait."""
        with self._lock:
            if self._agent is None:
                raise ValueError("Configure a provider and workspace first.")
            if self._closed:
                raise ValueError("Worker is shutting down.")
            if self._running is not None or (self._queue and not self._paused):
                raise ValueError("An agent turn is active; try again when it finishes.")
            self.start_exclusive_locked(name, task)

    def start_exclusive_locked(self, name: str, task: Callable) -> None:
        """run_exclusive without its checks, for a caller already holding the lock with nothing
        running (a model switch that compacts first, issue 3ES1). Queued prompts wait for it."""
        agent = self._agent
        self._running = f"{name}-{uuid.uuid4().hex}"
        agent.cancel_event.clear()

        def runner():
            try:
                task(agent)
            except Exception as exc:
                self._emit({"event": "error", "source": name,
                            "text": str(exc)[:2000] if isinstance(exc, (ValueError, OSError)) or type(exc).__name__ in {"ProviderError", "Cancelled"}
                            else f"{name} failed ({type(exc).__name__})."})
            finally:
                with self._lock:
                    self._running = None
                    self._settle_model_locked(agent)
                    self._lock.notify_all()

        threading.Thread(target=runner, name=f"relay-{name}", daemon=True).start()

    def _set_queue_item(self, item: dict) -> None:
        if item.get("ledger_id"):
            entry = self._agent.requests.find(item["ledger_id"])
            if entry is not None:
                entry["queue_item"] = item["id"]

    def reserve_steer(self) -> list[dict]:
        """Lease input to a harness without claiming that the guest accepted it."""
        with self._lock:
            if self._steer_inflight:
                return []
            self._steer_inflight, self._steer = self._steer, []
            return list(self._steer_inflight)

    def settle_steer(self, delivered: bool) -> None:
        with self._lock:
            items, self._steer_inflight = self._steer_inflight, []
            if not items:
                return
            if delivered:
                self._emit({"event": "steer_delivered", "ids": [i["id"] for i in items],
                            "request_ids": [i.get("request_id") for i in items],
                            "ledger_ids": [i.get("ledger_id") for i in items]})
            else:
                self._steer = items + self._steer
            self._changed_locked()

    def take_steer(self) -> list[dict]:
        """Called by the running agent at a step boundary: the steering items to add now
        ({prompt, ledger_id, context, attachments}); the agent frames them."""
        with self._lock:
            items, self._steer = self._steer, []
            if not items:
                return []
            self._emit({"event": "steer_delivered", "ids": [i["id"] for i in items],
                        "request_ids": [i["request_id"] for i in items],
                        "ledger_ids": [i.get("ledger_id") for i in items]})
            self._changed_locked()
        return [{"prompt": i["prompt"], "ledger_id": i.get("ledger_id"), "context": i.get("context"),
                 "attachments": i.get("attachments")} for i in items]

    def steer(self, item_id) -> None:
        """Upgrade a queued prompt to steer the running turn at its next step boundary."""
        with self._lock:
            if self._running is None:
                raise ValueError("No agent turn is running to steer.")
            for item in self._queue:
                if item["id"] == item_id:
                    self._queue.remove(item)
                    item.setdefault("request_id", None)
                    item["requeue"] = True
                    self._steer.append(item)
                    self._emit({"event": "queued", "id": item["id"], "request_id": item["request_id"],
                                "when": "steer", "position": len(self._steer) - 1, "origin": item.get("origin", "user"),
                                "ledger_id": item.get("ledger_id")})
                    self._changed_locked()
                    return
            raise ValueError("That prompt is not queued (it may already have started).")

    def unsteer(self, request_id, as_request_id=None) -> bool:
        """Third Enter in the pane: a steering prompt the running turn has not taken yet stops that
        turn and runs as its own prompt instead, keeping its ledger entry.

        Nothing is escalated once the turn has taken the prompt (``steer_delivered``) or given it
        back (``steer_returned``): the agent has it either way, so there is nothing to interrupt
        for. ``steer_escalated {escalated: false}`` says so.
        """
        with self._lock:
            item = next((i for i in self._steer if i.get("request_id") == request_id), None)
            if item is None:
                self._emit({"event": "steer_escalated", "request_id": request_id,
                            "new_request_id": as_request_id, "escalated": False, "ledger_id": None})
                return False
            self._steer.remove(item)
            self._changed_locked()
        try:
            self.submit(item["prompt"], "interrupt", as_request_id or request_id, item.get("context"),
                        item.get("attachments"), origin=item.get("origin", "user"),
                        ledger_id=item.get("ledger_id"), surface=item.get("surface", ""),
                        screen=item.get("screen", ""), readonly=bool(item.get("readonly")),
                        mode=item.get("mode", ""), card=item.get("card", ""),
                        preview=item.get("preview", ""))
        except Exception:
            # Never lose the prompt: it goes back to the head of the queue instead (the invariant
            # at the top of this file), and the caller still sees the error.
            with self._lock:
                self._queue.appendleft({"id": item["id"], "prompt": item["prompt"], "force": False,
                                        "context": item.get("context"), "attachments": item.get("attachments"),
                                        "origin": item.get("origin", "user"), "ledger_id": item.get("ledger_id"),
                                        "surface": item.get("surface", ""), "screen": item.get("screen", ""),
                                        "readonly": bool(item.get("readonly")),
                                        "mode": item.get("mode", ""), "card": item.get("card", ""),
                                        "preview": item.get("preview", "")})
                self._changed_locked()
                self._lock.notify_all()
            raise
        self._emit({"event": "steer_escalated", "request_id": request_id, "new_request_id": as_request_id,
                    "escalated": True, "ledger_id": item.get("ledger_id")})
        return True

    def _return_steer_locked(self) -> None:
        # The provider normally settles its lease first; never strand one if it exits early.
        items = self._steer_inflight + self._steer
        self._steer_inflight, self._steer = [], []
        front = []
        for item in items:
            self._emit({"event": "steer_returned", "id": item["id"], "request_id": item.get("request_id"),
                        "prompt": item["prompt"], "requeued": bool(item.get("requeue", True)),
                        "ledger_id": item.get("ledger_id")})
            if item.get("requeue", True):
                front.append({"id": item["id"], "prompt": item["prompt"], "force": False,
                              "context": item.get("context"), "attachments": item.get("attachments"),
                              "origin": item.get("origin", "user"), "ledger_id": item.get("ledger_id"),
                              "surface": item.get("surface", ""), "screen": item.get("screen", ""),
                              "readonly": bool(item.get("readonly")),
                              "mode": item.get("mode", ""), "card": item.get("card", ""),
                              "preview": item.get("preview", "")})
        for item in reversed(front):
            self._queue.appendleft(item)

    def cancel(self) -> None:
        """Stop the running turn and pause the queue.

        Back out of the pause by `resume_queue` (the pane's Resume button, a console's Enter on
        an empty prompt box) or simply by submitting the next prompt, which resumes it (#7JD1).
        """
        with self._lock:
            # Drop forced items that were waiting for this turn: the user asked to stop.
            self._ledger_cancel([i for i in self._queue if i["force"]], "Dropped when the user stopped the turn.")
            self._queue = deque(i for i in self._queue if not i["force"])
            self._paused = bool(self._queue)
            if self._running is not None:
                self._stop_locked()
            self._changed_locked()

    def resume(self) -> None:
        """resume_queue: run what is queued again after a cancel or a failed turn.

        Enter on an empty prompt box sends this, on a pane and on a card's console alike, and
        the strip's Resume button is the mouse path (#7JD1). A prompt submitted instead of it
        resumes on its own way past, in `submit`.
        """
        with self._lock:
            self._paused = False
            self._changed_locked()
            self._lock.notify_all()

    def remove(self, item_id, request_id=None) -> None:
        """queue_remove: a queued prompt, or a steer the running turn has not taken yet (the ×
        on the pane's "next tool call" row). A steer the turn already took cannot be withdrawn."""
        with self._lock:
            steer = next((i for i in self._steer if i["id"] == item_id), None)
            if steer is not None:
                self._steer.remove(steer)
                self._ledger_cancel([steer], "Withdrawn by the user before the agent's next tool call.")
                self._emit({"event": "steer_removed", "id": steer["id"], "request_id": steer.get("request_id"),
                            "ledger_id": steer.get("ledger_id")})
                self._changed_locked()
                self._ack_locked("remove", item_id, request_id)
                return
            for item in self._queue:
                if item["id"] == item_id:
                    self._queue.remove(item)
                    self._ledger_cancel([item], "Removed from the queue by the user.")
                    break
            else:
                raise ValueError("That prompt is not queued (it may already have started).")
            if not self._queue:
                self._paused = False
            self._changed_locked()
            self._ack_locked("remove", item_id, request_id)

    def move(self, item_id, to, request_id=None) -> None:
        """queue_move: drag a queued prompt to another place in the line (protocol 33, #AGNT).

        The helper's own FIFO had reorder and the pane's queue did not, which is one line of the
        Issue's table. It moves a *queued* prompt only: a steer is already inside the running
        turn and a forced item is an interrupt, so reordering either would mean something else.
        `to` is clamped rather than refused — a drag past the end of a list that shrank under it
        means "last", not "error".
        """
        with self._lock:
            index = next((i for i, item in enumerate(self._queue) if item["id"] == item_id), -1)
            if index < 0:
                raise ValueError("That prompt is not queued (it may already have started).")
            if not isinstance(to, int) or isinstance(to, bool):
                raise ValueError("queue_move `to` must be a position in the queue.")
            to = max(0, min(len(self._queue) - 1, to))
            item = self._queue[index]
            del self._queue[index]
            self._queue.insert(to, item)
            self._changed_locked()
            self._ack_locked("move", item_id, request_id, to=to)

    def clear(self, request_id=None) -> None:
        """queue_clear (and before a session load): waiting prompts are cancelled by the user."""
        with self._lock:
            self._ledger_cancel(list(self._queue) + list(self._steer), "Cleared from the queue by the user.")
            self._clear_locked()
            if self._steer_inflight:
                self._emit({"event": "status", "text": "Steering already sent to the guest is awaiting "
                            "acknowledgement and cannot be withdrawn by clearing the queue."})
            self._ack_locked("clear", None, request_id)

    def _ack_locked(self, op: str, item_id, request_id, **extra) -> None:
        """The queue op's own answer, carrying its request id (protocol 33, #AGNT).

        `queue_changed` says what the queue is now, but it carries no request id, so a client
        that sent three ops could not tell which of them had landed — `board_chat_queue_remove`
        and `_move` answered with nothing at all, which is one of the dead ends this card
        collected. Sent only when the caller gave an id, so nothing new appears on the wire for
        a GUI that does not ask.
        """
        if request_id is None:
            return
        self._emit({"event": "queue_ack", "id": request_id, "op": op,
                    **({"item": item_id} if item_id is not None else {}), **extra})

    def shutdown(self, timeout: float = 1.0) -> None:
        with self._lock:
            self._closed = True
            self._queue.clear()
            if self._running is not None:
                self._stop_locked()
            self._lock.notify_all()
        self._thread.join(timeout)

    # ----- internals ------------------------------------------------------
    def _leading_forced(self):
        for item in self._queue:
            if not item["force"]:
                return
            yield item

    def _stop_locked(self) -> None:
        self._agent.stop()

    def _clear_locked(self) -> None:
        had = bool(self._queue) or self._paused or bool(self._steer) or bool(self._steer_inflight)
        if self._running is None:
            self._steer_inflight.clear()
        self._queue.clear()
        self._steer.clear()
        self._paused = False
        if had:
            self._changed_locked()

    def _changed_locked(self) -> None:
        self._emit({"event": "queue_changed", "running": self._running, "paused": self._paused,
                    "items": [{"id": i["id"], "preview": _preview_of(i)[:PREVIEW], "forced": i["force"],
                               "origin": i.get("origin", "user"), **_surface_of(i), **_card_of(i)}
                              for i in self._queue],
                    "steering": [{"id": i["id"], "preview": _preview_of(i)[:PREVIEW],
                                  "origin": i.get("origin", "user"), **_surface_of(i), **_card_of(i)}
                                 for i in self._steer_inflight + self._steer]})

    def _next_locked(self):
        if self._closed or self._running is not None or not self._queue or self._agent is None:
            return None
        if self._paused and not self._queue[0]["force"]:
            return None
        return self._queue.popleft()

    def _dispatch(self) -> None:
        while True:
            with self._lock:
                item = self._next_locked()
                while item is None:
                    if self._closed:
                        return
                    self._lock.wait()
                    item = self._next_locked()
                agent = self._agent
                self._running = item["id"]
                self._outcome = None
                agent.cancel_event.clear()
                if not self._queue:
                    self._paused = False
                # Protocol 33: this turn's console, so every event of it is addressed. Set under
                # the lock with `_running`, because `agent_emit` reads it from the agent's own
                # thread the moment the first delta arrives.
                self._surface = item.get("surface", "")
                # The queue item id doubles as the turn id (protocol 11).
                # A card turn's boundary pair says which card and which mode, as it has since
                # 19.10 — from the item that is actually running, so a Plan queued behind a
                # Discuss is bracketed as the Plan it is (#CTRN).
                self._emit({"event": "agent_started", "id": item["id"], "turn_id": item["id"],
                            **_surface_of(item), **_card_of(item)})
                self._changed_locked()
            readonly = bool(item.get("readonly"))
            set_readonly = getattr(agent, "set_readonly", None)
            # A card turn (card #CTRN): one Discuss or Plan on one card, constrained for the
            # length of this turn and not a byte longer — the same shape as `readonly` above and
            # for the same reason. What it may touch is the stage machine of 19.20, which lives
            # in `board_tools.CardScope`; this pair is what opens and closes it.
            card = item.get("card") or ""
            set_card_turn = getattr(agent, "set_card_turn", None) if card else None
            try:
                extra = {"attachments": item["attachments"]} if item.get("attachments") else {}
                if item.get("ledger_id"):
                    extra["ledger_id"] = item["ledger_id"]
                if readonly and set_readonly is not None:
                    set_readonly(True)
                if set_card_turn is not None:
                    set_card_turn(item.get("mode") or "", card)
                # The "On screen now:" hint goes to the model and **nowhere else**: not into the
                # prompt the queue and the ledger hold, and not into the title, the session
                # summary or the checkpoint, which are all made of `prompt` inside `ask`. It used
                # to be composed into the string here, and the Switchboard console's pane title
                # came out reading "On screen now: Inbox 2, Discussing 1, …" (protocol 33).
                agent.ask(item["prompt"], reset_cancellation=False, context=item.get("context"),
                          turn_id=item["id"], screen=item.get("screen"), **extra)
            except Exception as exc:  # ask() handles its own errors; this is defensive.
                self._emit({"event": "error", "text": f"Agent error ({type(exc).__name__})."})
                self._outcome = "error"
            finally:
                if readonly and set_readonly is not None:
                    set_readonly(False)
                if set_card_turn is not None:
                    set_card_turn(None, None)
            with self._lock:
                outcome = self._outcome or "error"
                finished = {"event": "agent_finished", "id": item["id"], "outcome": outcome,
                            **_surface_of(item), **_card_of(item)}
                if self._stop_reason:
                    finished["stop_reason"] = self._stop_reason
                self._emit(finished)
                if self._steer or self._steer_inflight:
                    self._return_steer_locked()
                self._running = None
                self._surface = ""
                self._settle_model_locked(agent, item["id"])
                if outcome == "error" and any(not i["force"] for i in self._queue):
                    # Tool actions may already have run; do not fire queued prompts blindly.
                    self._paused = True
                self._changed_locked()
                self._lock.notify_all()
