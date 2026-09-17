# SPDX-License-Identifier: GPL-3.0-or-later
"""Single-dispatcher supervisor for agent turns: run now, queue, or interrupt.

Invariants:
* Exactly one dispatcher thread calls ``Agent.ask``; two turns never overlap.
* Every accepted prompt goes through one ordered queue. "now" and "interrupt"
  items are *forced*: they start even while the queue is paused.
* The cancellation flag is cleared only under the supervisor lock at the moment
  a turn is dequeued, and ``Agent.stop`` is only called under that lock, so a
  late stop can never hit the prompt that replaced the stopped turn.
* A user ``cancel`` or a failed turn pauses the queue. Queued (non-forced)
  prompts then wait for ``resume_queue``. The pause resets when the queue empties.
"""
from __future__ import annotations

import threading
import uuid
from collections import deque
from typing import Callable

MAX_QUEUE = 32
MAX_PROMPT = 131072
PREVIEW = 120
TERMINAL = {"done", "error", "cancelled"}


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
        self._thread = threading.Thread(target=self._dispatch, name="relay-turns", daemon=True)
        self._thread.start()

    # ----- agent wiring -------------------------------------------------
    def agent_emit(self, event: dict) -> None:
        """Emit callback to give the Agent; records how each turn ended."""
        if event.get("event") in TERMINAL:
            self._outcome = event["event"]
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
            self._clear_locked()

    def reset(self) -> None:
        with self._lock:
            if self._running is not None:
                raise ValueError("Stop the active turn first.")
            if self._agent is not None:
                self._agent.messages = self._agent.messages[:1]
            self._clear_locked()

    # ----- requests -------------------------------------------------------
    def submit(self, prompt, when: str = "now", request_id=None) -> str:
        if when not in {"now", "queue", "interrupt"}:
            raise ValueError('"when" must be "now", "queue", or "interrupt".')
        with self._lock:
            if self._agent is None:
                raise ValueError("Configure a provider and workspace first.")
            validate_prompt(prompt)
            if self._closed:
                raise ValueError("Worker is shutting down.")
            if len(self._queue) >= MAX_QUEUE:
                raise ValueError(f"Queue is full ({MAX_QUEUE} prompts).")
            busy = self._running is not None or (self._queue and not self._paused)
            if when == "now" and busy:
                raise ValueError("An agent turn is already active.")
            item = {"id": uuid.uuid4().hex, "prompt": prompt, "force": when != "queue"}
            if item["force"]:
                # Interrupts are FIFO among themselves, ahead of ordinary queued prompts.
                position = sum(1 for _ in self._leading_forced())
                self._queue.insert(position, item)
            else:
                self._queue.append(item)
                position = len(self._queue) - 1
            self._emit({"event": "queued", "id": item["id"], "request_id": request_id,
                        "when": when, "position": position})
            if when == "interrupt" and self._running is not None:
                self._emit({"event": "interrupting", "id": self._running, "by": item["id"]})
                self._stop_locked()
            self._changed_locked()
            self._lock.notify_all()
            return item["id"]

    def cancel(self) -> None:
        """Stop the running turn and pause the queue (resume with resume_queue)."""
        with self._lock:
            # Drop forced items that were waiting for this turn: the user asked to stop.
            self._queue = deque(i for i in self._queue if not i["force"])
            self._paused = bool(self._queue)
            if self._running is not None:
                self._stop_locked()
            self._changed_locked()

    def resume(self) -> None:
        with self._lock:
            self._paused = False
            self._changed_locked()
            self._lock.notify_all()

    def remove(self, item_id) -> None:
        with self._lock:
            for item in self._queue:
                if item["id"] == item_id:
                    self._queue.remove(item)
                    break
            else:
                raise ValueError("That prompt is not queued (it may already have started).")
            if not self._queue:
                self._paused = False
            self._changed_locked()

    def clear(self) -> None:
        with self._lock:
            self._clear_locked()

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
        agent = self._agent
        pending = agent.gate.pending
        agent.stop()
        if pending is not None:
            self._emit({"event": "approval_cancelled", "id": pending})

    def _clear_locked(self) -> None:
        had = bool(self._queue) or self._paused
        self._queue.clear()
        self._paused = False
        if had:
            self._changed_locked()

    def _changed_locked(self) -> None:
        self._emit({"event": "queue_changed", "running": self._running, "paused": self._paused,
                    "items": [{"id": i["id"], "preview": i["prompt"][:PREVIEW], "forced": i["force"]}
                              for i in self._queue]})

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
                self._emit({"event": "agent_started", "id": item["id"]})
                self._changed_locked()
            try:
                agent.ask(item["prompt"], reset_cancellation=False)
            except Exception as exc:  # ask() handles its own errors; this is defensive.
                self._emit({"event": "error", "text": f"Agent error ({type(exc).__name__})."})
                self._outcome = "error"
            with self._lock:
                outcome = self._outcome or "error"
                self._emit({"event": "agent_finished", "id": item["id"], "outcome": outcome})
                self._running = None
                if outcome == "error" and any(not i["force"] for i in self._queue):
                    # Approved actions may already have run; do not fire queued prompts blindly.
                    self._paused = True
                self._changed_locked()
                self._lock.notify_all()
