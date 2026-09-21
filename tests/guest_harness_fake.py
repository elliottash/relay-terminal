# SPDX-License-Identifier: AGPL-3.0-or-later
"""A scripted `guest_harness.Harness` for tests, so nothing ever starts a real claude or codex.

A real guest turn spends the owner's subscription (protocol 29: "no test starts a real guest"), so
every test of the worker side drives this instead. It is deliberately dumb: a turn is a list of
`HarnessEvent`s to replay and a `TurnResult` to return, and every call made on it is recorded.

    from guest_harness_fake import FakeHarness, ev
    harness = FakeHarness([{"events": [ev("delta", text="hi")], "result": ("hi", "end", {})}])

Every kind in `guest_harness.EVENT_KINDS` can be scripted, including the ones GT7X task t:a3
added — a call that prints while it runs, and a usage report that says how full the guest's own
window is::

    ev("tool_started", call_id="c1", tool="run_command", input={"command": "make"}),
    ev("tool_output", call_id="c1", text="cc a.c\n"),      # as often as the guest says something
    ev("tool_result", call_id="c1", tool="run_command", output="cc a.c\n", ok=True),
    ev("usage", input_tokens=9, output_tokens=2, context_tokens=13394, context_window=258400)

and an approval answered with a scope arrives in `answers` exactly as the provider sent it:
`("req-1", {"behavior": "allow", "scope": "session"})`.

Other test modules import it; keep the surface the contract's and nothing more.
"""
from __future__ import annotations

import threading

from relay_core.guest_harness import HarnessEvent, HarnessError, HarnessStart, TurnResult


def ev(kind: str, /, **data) -> HarnessEvent:
    """One scripted event. `ev("delta", text="hi")` is `HarnessEvent("delta", {"text": "hi"})`.

    `kind` is positional-only so an event whose own data has a `kind` field — an `approval` does
    (29.1) — can still be written `ev("approval", kind="command", ...)`.
    """
    return HarnessEvent(kind, data)


class FakeHarness:
    """One scripted harness. `script` is a list of turns, each either:

    * a dict `{"events": [...], "result": (text, stop_reason, usage)}` — the events are replayed
      through `emit`, in order, and the result is returned;
    * a dict with `"raise": HarnessError(...)` — replayed events first, then the error;
    * a callable `fn(prompt, attachments, emit, cancel, harness)` returning a `TurnResult`, for a
      turn that has to react to what it is given.

    `answers` collects every `answer(request_id, decision)` — including the optional `scope` the
    provider puts on an approval (`once` / `session` / `stop`), which a real adapter turns into
    its guest's own word for it; `calls` every method call in order — `start` with the effort it
    was given, and one entry per `set_effort` / `models` call, so a test can hold the worker to
    what it asked the guest for (29.3).
    """

    guest = "fake"

    def __init__(self, script=None, *, guest: str = "claude", session_id: str = "fake-session",
                 model: str = "fake-model", start_error: Exception | None = None,
                 efforts=("low", "medium", "high", "xhigh", "max"),
                 models=None, effort_error: Exception | None = None):
        self.guest = guest
        self.script = list(script or [])
        self.calls: list[tuple] = []
        self.answers: list[tuple[str, dict]] = []
        self.starts: list[dict] = []
        self.sent: list[dict] = []
        self.interrupts = 0
        self.compactions = 0
        self.closed = False
        self.started = False
        self._session_id = session_id
        self._model = model
        # The reasoning effort the pane asked for, as the contract's `effort` property reports it
        # ("" until one is set), and what `models()` answers.
        self._effort = ""
        self._efforts = list(efforts)
        self._models = list(models) if models is not None else None
        self._start_error = start_error
        self._effort_error = effort_error
        self._turn = 0
        # Set while `send()` is blocked inside an `emit`, so a test can answer a card from another
        # thread and know the card is already up.
        self.in_turn = threading.Event()

    # ----- lifecycle -------------------------------------------------------------------
    def start(self, *, cwd: str, model=None, resume=None, fork: bool = False,
              permissions: str = "bypass", effort=None, board_bridge=None,
              instructions=None) -> HarnessStart:
        self.board_bridge = board_bridge
        self.instructions = instructions
        self.calls.append(("start", cwd, model, resume, fork, permissions, effort))
        self.starts.append({"cwd": cwd, "model": model, "resume": resume, "fork": fork,
                            "permissions": permissions, "effort": effort})
        if self._start_error is not None:
            raise self._start_error
        if self.started:
            # Both adapters refuse this ("this claude harness is already started"): a harness is
            # one process for one guest session, so resuming another one means a new harness.
            raise HarnessError("this fake harness is already started.")
        self.started = True
        if model:
            self._model = model
        if resume:
            self._session_id = resume
        if effort:
            self._effort = effort
        return HarnessStart(session_id=self._session_id, model=self._model)

    def close(self) -> None:
        self.calls.append(("close",))
        self.closed = True

    # ----- a turn ----------------------------------------------------------------------
    def send(self, prompt: str, *, attachments=None, emit, cancel: threading.Event) -> TurnResult:
        self.calls.append(("send", prompt))
        self.sent.append({"prompt": prompt, "attachments": list(attachments or [])})
        if self._turn >= len(self.script):
            raise HarnessError("FakeHarness has no turn scripted for this send().")
        turn = self.script[self._turn]
        self._turn += 1
        self.in_turn.set()
        try:
            if callable(turn):
                return turn(prompt, attachments, emit, cancel, self)
            for event in turn.get("events") or ():
                emit(event)
            if turn.get("raise") is not None:
                raise turn["raise"]
            text, stop_reason, usage = turn.get("result", ("", "end", {}))
            return TurnResult(text=text, stop_reason=stop_reason, usage=dict(usage or {}))
        finally:
            self.in_turn.clear()

    def interrupt(self) -> None:
        self.calls.append(("interrupt",))
        self.interrupts += 1

    def set_model(self, model: str) -> str:
        self.calls.append(("set_model", model))
        self._model = model
        return model

    def set_effort(self, effort: str) -> str:
        self.calls.append(("set_effort", effort))
        if self._effort_error is not None:
            raise self._effort_error
        self._effort = effort
        return effort

    def models(self) -> list[dict]:
        self.calls.append(("models",))
        if self._models is not None:
            return [dict(row) for row in self._models]
        return [{"id": self._model, "label": self._model, "efforts": list(self._efforts),
                 "default_effort": self._efforts[0] if self._efforts else None, "current": True}]

    def compact(self) -> None:
        self.calls.append(("compact",))
        self.compactions += 1

    def answer(self, request_id: str, decision: dict) -> None:
        self.calls.append(("answer", request_id, dict(decision)))
        self.answers.append((request_id, dict(decision)))

    @property
    def effort(self) -> str:
        return self._effort

    @property
    def session_id(self) -> str:
        return self._session_id

    @property
    def model(self) -> str:
        return self._model
