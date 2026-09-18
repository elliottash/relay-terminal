# SPDX-License-Identifier: GPL-3.0-or-later
"""What the hub is allowed to know about panes, and a demo source to test against.

``PaneSource`` is the seam between RRP and Relay itself. The real one will be fed by the GUI, which
already has the pane list, the worker events and the composer. Keeping it abstract means the
protocol, the crypto and the web app can be tested end to end before any of that exists — and it
forces the rule from the security review that the hub **constructs** every worker request from a
fixed shape, rather than passing client fields through.

Nothing here accepts a path from the wire. ``plan_execute`` names a ``plan_id`` that the desktop
itself minted in a ``plan_written`` event; resolving it to a file is this module's job.
"""
from __future__ import annotations

import asyncio
import secrets
import time
from typing import Callable

from . import wire


class PaneSource:
    """The interface the hub talks to. Every method is desktop-side and trusted."""

    def snapshot(self) -> list[dict]:
        """The pane list, already shaped for the `panes` message."""
        raise NotImplementedError

    def on_panes(self, callback: Callable[[], None]) -> None:
        raise NotImplementedError

    def on_agent(self, callback: Callable[[str, dict], None]) -> None:
        raise NotImplementedError

    def has_pane(self, pane: str) -> bool:
        return any(item["id"] == pane for item in self.snapshot())

    async def compose(self, pane: str, text: str, *, to_agent: bool, when: str, origin: str) -> None:
        raise NotImplementedError

    async def agent_stop(self, pane: str) -> None:
        raise NotImplementedError

    async def queue_remove(self, pane: str, item_id: str) -> None:
        raise NotImplementedError

    async def recap_request(self, pane: str) -> None:
        raise NotImplementedError

    async def plan_execute(self, pane: str, plan_id: str, origin: str) -> None:
        raise NotImplementedError

    async def transcribe(self, pane: str, audio: bytes, audio_format: str) -> str:
        raise NotImplementedError

    def turn_transcript(self, pane: str, turn_id: str) -> dict | None:
        return None

    def tool_output(self, pane: str, tool_id: str) -> dict | None:
        return None


class DemoPaneSource(PaneSource):
    """Two panes and a scripted agent, so the phone has something live to show.

    It is not a mock in the testing sense — it is what the dev harness runs against until the GUI
    bridge exists, and the phone client cannot tell the difference at the protocol level.
    """

    SCRIPT = [
        (0.35, {"event": "agent_started"}),
        (0.45, {"event": "thinking_delta", "text": "Looking at the failing test first"}),
        (0.50, {"event": "thinking_done", "elapsed": 0.9}),
        (0.40, {"event": "tool_started", "tool": "read_file", "preview": "tests/test_router.py"}),
        (0.60, {"event": "tool_result", "tool": "read_file", "ok": True, "summary": "168 lines"}),
        (0.35, {"event": "delta", "text": "The router treats "}),
        (0.30, {"event": "delta", "text": "a bare word with a slash as a path, "}),
        (0.30, {"event": "delta", "text": "so `git status` routes to the shell and "}),
        (0.30, {"event": "delta", "text": "`why is this failing?` routes to the agent."}),
        (0.40, {"event": "turn_summary", "tools": 1, "elapsed": 3.4}),
        (0.20, {"event": "agent_finished", "turn_id": "t1"}),
    ]

    def __init__(self):
        now = time.time()
        self._panes = [
            {"id": "pane-1", "window": 1, "tab": "relay-terminal", "title": "relay-terminal",
             "cwd": "~/repos/relay-terminal", "program": "", "control": "human",
             "status": "idle", "unread": 0, "queue": 0, "updated": now},
            {"id": "pane-2", "window": 1, "tab": "engine", "title": "engine tests",
             "cwd": "~/repos/relay-terminal/build-engine", "program": "ctest",
             "control": "human", "status": "running", "unread": 0, "queue": 0, "updated": now},
        ]
        self._plans: dict[str, str] = {}
        self._panes_callbacks: list[Callable[[], None]] = []
        self._agent_callbacks: list[Callable[[str, dict], None]] = []
        self._running: dict[str, asyncio.Task] = {}

    # ---- observation -------------------------------------------------------------------------

    def snapshot(self) -> list[dict]:
        return [dict(pane) for pane in self._panes]

    def on_panes(self, callback) -> None:
        self._panes_callbacks.append(callback)

    def on_agent(self, callback) -> None:
        self._agent_callbacks.append(callback)

    def _pane(self, pane: str) -> dict | None:
        return next((item for item in self._panes if item["id"] == pane), None)

    def _changed(self) -> None:
        for callback in list(self._panes_callbacks):
            callback()

    def _emit(self, pane: str, event: dict) -> None:
        for callback in list(self._agent_callbacks):
            callback(pane, event)

    def set_status(self, pane: str, status: str) -> None:
        item = self._pane(pane)
        if item and item["status"] != status:
            item["status"] = status
            item["updated"] = time.time()
            self._changed()

    # ---- actions -----------------------------------------------------------------------------

    async def compose(self, pane: str, text: str, *, to_agent: bool, when: str, origin: str) -> None:
        item = self._pane(pane)
        if item is None:
            raise wire.WireError("no_such_pane", "no such pane.")
        if when == "queue" and pane in self._running:
            item["queue"] += 1
            self._emit(pane, {"event": "queued", "text": text, "origin": origin})
            self._changed()
            return
        if pane in self._running:
            self._running[pane].cancel()
        self._emit(pane, {"event": "status", "text": f"Prompt from {origin}"})
        self._running[pane] = asyncio.create_task(self._turn(pane, text, origin))

    async def _turn(self, pane: str, text: str, origin: str) -> None:
        self.set_status(pane, "running")
        # The prompt is echoed back so every device sees what was asked and who asked.
        self._emit(pane, {"event": "queued", "text": text, "origin": origin, "started": True})
        try:
            for delay, event in self.SCRIPT:
                await asyncio.sleep(delay)
                self._emit(pane, dict(event))
            plan_id = secrets.token_hex(6)
            self._plans[plan_id] = f"/tmp/relay-demo-plan-{plan_id}.md"
            self._emit(pane, {"event": "plan_written", "plan_id": plan_id,
                              "title": "Fix the router's path heuristic",
                              "preview": "1. Reproduce with a bare word\n2. Tighten the rule\n3. Add a test"})
        except asyncio.CancelledError:
            self._emit(pane, {"event": "cancelled"})
            raise
        finally:
            self._running.pop(pane, None)
            self.set_status(pane, "finished")

    async def agent_stop(self, pane: str) -> None:
        task = self._running.get(pane)
        if task is None:
            raise wire.WireError("busy", "nothing is running in that pane.")
        task.cancel()

    async def queue_remove(self, pane: str, item_id: str) -> None:
        item = self._pane(pane)
        if item and item["queue"]:
            item["queue"] -= 1
            self._emit(pane, {"event": "queue_changed", "items": item["queue"]})
            self._changed()

    async def recap_request(self, pane: str) -> None:
        self._emit(pane, {"event": "recap", "text": "You asked why the router sends bare words to "
                                                    "the shell; the heuristic is in classify().",
                          "turns_covered": 1})

    async def plan_execute(self, pane: str, plan_id: str, origin: str) -> None:
        path = self._plans.get(plan_id)
        if path is None:
            # The id came from the wire; it only means something if we minted it.
            raise wire.WireError("not_permitted", "unknown plan id.")
        self._emit(pane, {"event": "status", "text": f"Executing the plan ({origin})"})
        await self.compose(pane, f"Execute the plan in {path}", to_agent=True, when="now",
                           origin=origin)

    async def transcribe(self, pane: str, audio: bytes, audio_format: str) -> str:
        await asyncio.sleep(0.2)
        return f"[demo transcript of {len(audio)} bytes of {audio_format}]"
