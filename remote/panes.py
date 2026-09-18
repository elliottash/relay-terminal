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

    # A source that can page scrollback says so, and the hub then advertises `history` in
    # `welcome`. An agent-only source has no screen behind it and simply leaves this False, which
    # is why the feature list is built from the source rather than hard-coded.
    scrollback = False

    def snapshot(self) -> list[dict]:
        """The pane list, already shaped for the `panes` message."""
        raise NotImplementedError

    def on_panes(self, callback: Callable[[], None]) -> None:
        raise NotImplementedError

    def on_agent(self, callback: Callable[[str, dict], None]) -> None:
        raise NotImplementedError

    def has_pane(self, pane: str) -> bool:
        return any(item["id"] == pane for item in self.snapshot())

    async def compose(self, pane: str, text: str, *, to_agent: bool, when: str, origin: str,
                      origin_name: str = "") -> None:
        """A prompt, tagged with where it came from.

        ``origin`` is what the transcript and the queue row record (``remote:<device>`` or, for a
        guest the owner approved, ``guest:<participant>``); ``origin_name`` is the display name
        that goes with it, so the desktop's queue row can say *alice* rather than a hex id. It is
        desktop-side only and never reaches another guest — see `Host.guest_view`.
        """
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

    async def history(self, pane: str, before_row: int, count: int) -> dict:
        """One page of scrollback (docs/REMOTE-PROTOCOL.md section 6.5).

        ``before_row`` is an absolute scrollback row and the page ends just below it — rows
        ``[before_row - count, before_row)`` — or, when it is negative, the newest ``count`` rows.
        The answer is ``{"from_row", "total", "more", "lines"}``, where a line is the same
        ``{row, segs}`` shape the live screen sends, so the client paints history with the run
        painter it already has.

        Reading history must never move the desktop's own viewport; that rule lives in the engine
        (``VtCore::historyLines`` is const) and is repeated here because a source is free to
        implement it some other way.
        """
        raise wire.WireError("not_permitted", "this desktop does not share scrollback.")

    # ---- password prompts (docs/REMOTE-PROTOCOL.md section 6.7) --------------------------------
    # The hub mints the nonce; these three are what binds it and what eventually writes the line.

    def secret_state(self, pane: str) -> dict | None:
        """The pids behind a password prompt: ``shell_pid`` for a fresh termios read and
        ``foreground_pid`` to bind the nonce to the process that is actually asking, or None
        when the pane is not at a prompt the source can see."""
        return None

    def secret_prompt(self, pane: str) -> bool:
        """A fresh password-prompt check. The write path re-checks at the moment of the write;
        this is the earlier of the two gates."""
        return False

    async def send_secret(self, pane: str, data: bytes, *, device: str) -> None:
        """Write a password line. Only ever reached after the nonce and both prompt checks."""
        raise wire.WireError("not_permitted", "password entry is not available on this source.")

    def turn_transcript(self, pane: str, turn_id: str) -> dict | None:
        return None

    def tool_output(self, pane: str, tool_id: str) -> dict | None:
        return None


class DemoPaneSource(PaneSource):
    """Two panes and a scripted agent, so the phone has something live to show.

    It is not a mock in the testing sense — it is what the dev harness runs against until the GUI
    bridge exists, and the phone client cannot tell the difference at the protocol level.
    """

    # The tool events carry the `label` of AGENT-SESSIONS-PROTOCOL.md section 23, shaped exactly as
    # ``relay_core.tool_labels`` builds it (this package never imports the backend). The phone draws
    # one concise line per call from it, so the demo has to show what the real thing shows: a run of
    # consecutive reads folding into one line, a command that failed, and an edit whose diff is
    # short enough to print under the line without a tap.
    SCRIPT = [
        (0.35, {"event": "agent_started"}),
        (0.45, {"event": "thinking_delta", "text": "Looking at the failing test first"}),
        (0.50, {"event": "thinking_done", "elapsed": 0.9}),
        (0.40, {"event": "tool_started", "tool": "read_file", "call_id": "c1",
                "preview": "READ FILE\n\ntests/test_router.py",
                "label": {"kind": "read", "running": "reading test_router.py",
                          "title": "read test_router.py", "path": "tests/test_router.py",
                          "merge": {"key": "read", "singular": "file", "plural": "files"}}}),
        (0.45, {"event": "tool_result", "tool": "read_file", "call_id": "c1", "ms": 40,
                "result": {"path": "tests/test_router.py"},
                "label": {"kind": "read", "running": "reading test_router.py",
                          "title": "read test_router.py", "stats": ["168 lines"], "ok": True,
                          "path": "tests/test_router.py",
                          "open": {"type": "file", "path": "tests/test_router.py"},
                          "merge": {"key": "read", "singular": "file", "plural": "files",
                                    "lines": 168}}}),
        (0.30, {"event": "tool_started", "tool": "read_file", "call_id": "c2",
                "preview": "READ FILE\n\nbackend/relay_core/router.py",
                "label": {"kind": "read", "running": "reading router.py", "title": "read router.py",
                          "path": "backend/relay_core/router.py",
                          "merge": {"key": "read", "singular": "file", "plural": "files"}}}),
        (0.45, {"event": "tool_result", "tool": "read_file", "call_id": "c2", "ms": 35,
                "result": {"path": "backend/relay_core/router.py"},
                "label": {"kind": "read", "running": "reading router.py", "title": "read router.py",
                          "stats": ["244 lines"], "ok": True, "path": "backend/relay_core/router.py",
                          "open": {"type": "file", "path": "backend/relay_core/router.py"},
                          "merge": {"key": "read", "singular": "file", "plural": "files",
                                    "lines": 244}}}),
        (0.35, {"event": "tool_started", "tool": "run_command", "call_id": "c3",
                "preview": "RUN COMMAND\n\npytest -q tests/test_router.py",
                "label": {"kind": "run", "running": "running pytest", "title": "ran pytest"}}),
        (0.80, {"event": "tool_result", "tool": "run_command", "call_id": "c3", "ms": 2100,
                "result": {"exit_code": 1, "stdout": "1 failed, 12 passed"},
                "label": {"kind": "run", "running": "running pytest", "title": "ran pytest",
                          "stats": ["12 lines", "exit 1", "2.1 s"], "ok": False,
                          "open": {"type": "fold"}}}),
        (0.35, {"event": "delta", "text": "The router treats "}),
        (0.30, {"event": "delta", "text": "a bare word with a slash as a path, "}),
        (0.30, {"event": "delta", "text": "so `git status` routes to the shell and "}),
        (0.30, {"event": "delta", "text": "`why is this failing?` routes to the agent."}),
        (0.35, {"event": "tool_started", "tool": "edit_file", "call_id": "c4",
                "preview": "EDIT FILE\n\nbackend/relay_core/router.py",
                "label": {"kind": "edit", "running": "editing router.py", "title": "edited router.py",
                          "path": "backend/relay_core/router.py"}}),
        (0.50, {"event": "tool_result", "tool": "edit_file", "call_id": "c4", "ms": 60,
                "result": {"path": "backend/relay_core/router.py", "added": 2, "removed": 1,
                           "replacements": 1},
                "diff": ("--- a/backend/relay_core/router.py\n"
                         "+++ b/backend/relay_core/router.py\n"
                         "@@ -41,3 +41,4 @@\n"
                         " def classify(text):\n"
                         "-    if \"/\" in text:\n"
                         "+    if text.startswith(\"/\") or \"/\" in text.split()[0]:\n"
                         "+        # a bare word with a slash is a path, not a question\n"),
                "label": {"kind": "edit", "running": "editing router.py", "title": "edited router.py",
                          "stats": ["+2 −1"], "ok": True, "path": "backend/relay_core/router.py",
                          "inline_diff": True, "open": {"type": "fold"}}}),
        (0.40, {"event": "turn_summary", "turn_id": "t1", "elapsed_ms": 6400, "thinking_ms": 900,
                "tools": [
                    {"call_id": "c1", "name": "read_file", "ok": True, "ms": 40,
                     "preview": "READ FILE\n\ntests/test_router.py",
                     "label": {"kind": "read", "running": "reading test_router.py",
                               "title": "read test_router.py", "stats": ["168 lines"],
                               "merge": {"key": "read", "singular": "file", "plural": "files",
                                         "lines": 168}}},
                    {"call_id": "c2", "name": "read_file", "ok": True, "ms": 35,
                     "preview": "READ FILE\n\nbackend/relay_core/router.py",
                     "label": {"kind": "read", "running": "reading router.py",
                               "title": "read router.py", "stats": ["244 lines"],
                               "merge": {"key": "read", "singular": "file", "plural": "files",
                                         "lines": 244}}},
                    {"call_id": "c3", "name": "run_command", "ok": False, "ms": 2100,
                     "exit_code": 1, "preview": "RUN COMMAND\n\npytest -q tests/test_router.py",
                     "label": {"kind": "run", "running": "running pytest", "title": "ran pytest",
                               "stats": ["12 lines", "exit 1", "2.1 s"]}},
                    {"call_id": "c4", "name": "edit_file", "ok": True, "ms": 60,
                     "preview": "EDIT FILE\n\nbackend/relay_core/router.py",
                     "label": {"kind": "edit", "running": "editing router.py",
                               "title": "edited router.py", "stats": ["+2 −1"]}},
                ]}),
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
        self._secret: tuple[str, bool, int, int] = (None, False, 0, 0)
        self._secrets: list[tuple[str, bytes]] = []

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

    async def compose(self, pane: str, text: str, *, to_agent: bool, when: str, origin: str,
                      origin_name: str = "") -> None:
        item = self._pane(pane)
        if item is None:
            raise wire.WireError("no_such_pane", "no such pane.")
        if when == "queue" and pane in self._running:
            item["queue"] += 1
            self._emit(pane, {"event": "queued", "text": text, "origin": origin,
                              **({"author": origin_name} if origin_name else {})})
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

    # ---- password prompts ----------------------------------------------------------------------

    def set_password_prompt(self, pane: str, up: bool, shell_pid: int = 0,
                            foreground_pid: int = 0) -> None:
        self._secret = (pane, up, shell_pid, foreground_pid)
        self.set_status(pane, "password" if up else "idle")

    def secret_state(self, pane: str) -> dict | None:
        which, up, shell_pid, foreground_pid = getattr(self, "_secret", (None, False, 0, 0))
        if which != pane or not up:
            return None
        return {"shell_pid": shell_pid, "foreground_pid": foreground_pid}

    def secret_prompt(self, pane: str) -> bool:
        return self.secret_state(pane) is not None

    async def send_secret(self, pane: str, data: bytes, *, device: str) -> None:
        if not self.secret_prompt(pane):
            raise wire.WireError("not_permitted", "that pane is not at a password prompt.")
        self._emit(pane, {"event": "status", "text": "Password sent."})
        self._secrets.append((pane, bytes(data)))
