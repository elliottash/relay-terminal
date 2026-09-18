# SPDX-License-Identifier: GPL-3.0-or-later
"""Real shells as a ``PaneSource``: one ``relay-screen-bridge`` per shared pane.

Each pane is a shell in a PTY, parsed by Relay's own emulator in the bridge process, arriving here
as screen state rather than bytes. That is the whole point of the shape: the phone renders a cell
grid, never resizes the host, and cannot be shown half an escape sequence.

Two rules from the protocol are enforced here rather than in the UI, because a client must not be
able to skip them:

* **the host owns the size.** There is no message that lets a phone resize a pane;
* **no ordinary input while a password prompt is up.** ``keys``, ``paste`` and ``line`` are refused
  whenever the terminal is in canonical mode with echo off, which is Relay's own password test
  (`ARCHITECTURE.md` section 9), read fresh from the tty at the moment of the write rather than
  from a cached flag.
"""
from __future__ import annotations

import asyncio
import base64
import contextlib
import json
import logging
import os
import shutil
import termios
import time
from pathlib import Path

from . import panes as panes_mod
from . import wire

log = logging.getLogger("relay.terminal")

BRIDGE_NAMES = ("relay-screen-bridge",)
BRIDGE_PATHS = ("build-engine/engine", "build/engine")
DEFAULT_ROWS, DEFAULT_COLS = 24, 100
MAX_SCROLLBACK = 5000
# A scrollback page is a read of memory the bridge already holds, so this is a wedged-process
# timeout rather than a work budget. What it must never be is unbounded: a phone is holding a
# scroll gesture open until the page arrives.
HISTORY_TIMEOUT = 10.0


def find_bridge(root: Path | None = None) -> Path | None:
    """The screen bridge binary, from a build tree or the PATH."""
    root = root or Path(__file__).resolve().parent.parent
    for directory in BRIDGE_PATHS:
        for name in BRIDGE_NAMES:
            candidate = root / directory / name
            if candidate.is_file() and os.access(candidate, os.X_OK):
                return candidate
    for name in BRIDGE_NAMES:
        found = shutil.which(name)
        if found:
            return Path(found)
    return None


BUILD_HINT = """the screen bridge is not built. From the repo root:

  cmake -S . -B build-engine -DRELAY_BUILD_APP=OFF -DRELAY_BUILD_ENGINE=ON
  cmake --build build-engine --target relay-screen-bridge"""


def secret_prompt(shell_pid: int) -> bool:
    """True when the tty is in canonical mode with echo off: a password is being asked for.

    The same test as `Pane::checkPasswordPrompt`. Full-screen programs and Readline turn ICANON
    off, so they do not match; `sudo`, `ssh` and `su` do.
    """
    if shell_pid <= 0:
        return False
    try:
        fd = os.open(f"/proc/{shell_pid}/fd/0", os.O_RDONLY | os.O_NONBLOCK)
    except OSError:
        return False
    try:
        attributes = termios.tcgetattr(fd)
    except (termios.error, OSError):
        return False
    finally:
        os.close(fd)
    local = attributes[3]
    return bool(local & termios.ICANON) and not bool(local & termios.ECHO)


class TerminalPane:
    """One shell, its screen, and what the host needs to describe it."""

    def __init__(self, pane_id: str, title: str, cwd: str, rows: int, cols: int):
        self.id = pane_id
        self.title = title
        self.cwd = cwd
        self.rows = rows
        self.cols = cols
        self.alt = False
        self.base = 0            # absolute scrollback row of the screen's first line
        self.history_rows = 0    # how many scrollback rows the core holds
        self.lines: dict[int, list] = {}
        self.cursor = {"row": 0, "col": 0, "visible": True, "shape": 0}
        self.shell_pid = 0
        self.foreground_pid = 0
        self.running = False
        self.exit_code: int | None = None
        self.updated = time.time()
        self.process: asyncio.subprocess.Process | None = None
        self.driver: str | None = None      # device id currently typing, if any
        # The bridge's `history` reply carries no request id, so pages are matched by strict
        # ordering: the lock keeps one request per pane in flight, and the reader hands the next
        # reply to whoever is waiting. Two devices paging at once queue behind each other rather
        # than reading each other's pages.
        self.history_lock = asyncio.Lock()
        self.history_waiter: asyncio.Future | None = None

    @property
    def status(self) -> str:
        if self.exit_code is not None:
            return "failed" if self.exit_code else "finished"
        if secret_prompt(self.shell_pid):
            return "password"
        return "running" if self.running else "idle"

    def snapshot(self) -> dict:
        return {"pane": self.id, "rows": self.rows, "cols": self.cols, "alt": self.alt,
                "cursor": dict(self.cursor), "base": self.base, "history": self.history_rows,
                "lines": [{"row": row, "segs": self.lines.get(row, [])}
                          for row in range(self.rows)]}

    def describe(self) -> dict:
        return {"id": self.id, "window": 1, "tab": self.title, "title": self.title,
                "cwd": self.cwd, "program": "", "control": self.driver and f"remote:{self.driver}"
                or "human", "status": self.status, "unread": 0, "queue": 0,
                "updated": self.updated, "rows": self.rows, "cols": self.cols}


class TerminalPaneSource(panes_mod.PaneSource):
    """Real terminals, shared read-only until a `full` device takes over."""

    scrollback = True

    def __init__(self, bridge: Path | None = None, *, rows: int = DEFAULT_ROWS,
                 cols: int = DEFAULT_COLS, shell: str | None = None, raw_out: bool = False):
        self.bridge = bridge or find_bridge()
        self.rows, self.cols = rows, cols
        self.shell = shell or os.environ.get("SHELL") or "/bin/bash"
        self.raw_out = raw_out
        self.panes: dict[str, TerminalPane] = {}
        self._panes_callbacks: list = []
        self._agent_callbacks: list = []
        self._screen_callbacks: list = []
        self._raw_callbacks: list = []
        self._counter = 0
        self._readers: dict[str, asyncio.Task] = {}

    # ---- observation -------------------------------------------------------------------------

    def snapshot(self) -> list[dict]:
        return [pane.describe() for pane in self.panes.values()]

    def on_panes(self, callback) -> None:
        self._panes_callbacks.append(callback)

    def on_agent(self, callback) -> None:
        self._agent_callbacks.append(callback)

    def on_screen(self, callback) -> None:
        """callback(pane_id, message) for a `screen_snapshot` or `screen_diff`."""
        self._screen_callbacks.append(callback)

    def on_raw(self, callback) -> None:
        """callback(pane_id, bytes) for raw PTY output. Local attach only; never sent remotely."""
        self._raw_callbacks.append(callback)

    def off_raw(self, callback) -> None:
        with contextlib.suppress(ValueError):
            self._raw_callbacks.remove(callback)

    def has_pane(self, pane: str) -> bool:
        return pane in self.panes

    def pane(self, pane_id: str) -> TerminalPane:
        pane = self.panes.get(pane_id)
        if pane is None:
            raise wire.WireError("no_such_pane", "no such pane.")
        return pane

    def screen_snapshot(self, pane_id: str) -> dict:
        return {"t": "screen_snapshot", **self.pane(pane_id).snapshot()}

    def _changed(self) -> None:
        for callback in list(self._panes_callbacks):
            callback()

    def _screen(self, pane_id: str, message: dict) -> None:
        for callback in list(self._screen_callbacks):
            callback(pane_id, message)

    # ---- opening and closing -----------------------------------------------------------------

    async def open(self, title: str = "", cwd: str = "") -> TerminalPane:
        if self.bridge is None:
            raise wire.WireError("internal", BUILD_HINT)
        self._counter += 1
        pane_id = f"pane-{self._counter}"
        directory = cwd or os.getcwd()
        pane = TerminalPane(pane_id, title or Path(directory).name or "terminal", directory,
                            self.rows, self.cols)
        arguments = [str(self.bridge), "--rows", str(self.rows), "--cols", str(self.cols),
                     "--shell", self.shell, "--cwd", directory,
                     "--scrollback", str(MAX_SCROLLBACK)]
        if self.raw_out:
            arguments.append("--raw-out")
        pane.process = await asyncio.create_subprocess_exec(
            *arguments, stdin=asyncio.subprocess.PIPE, stdout=asyncio.subprocess.PIPE,
            stderr=asyncio.subprocess.DEVNULL, limit=4 * 1024 * 1024)
        self.panes[pane_id] = pane
        self._readers[pane_id] = asyncio.create_task(self._read(pane))
        self._changed()
        log.info("opened %s (%s) in %s", pane_id, self.shell, directory)
        return pane

    async def close(self, pane_id: str) -> None:
        pane = self.panes.pop(pane_id, None)
        task = self._readers.pop(pane_id, None)
        if task:
            task.cancel()
        if pane and pane.process and pane.process.returncode is None:
            with contextlib.suppress(Exception):
                await self._write(pane, {"t": "quit"})
                await asyncio.wait_for(pane.process.wait(), 5)
            if pane.process.returncode is None:
                pane.process.kill()
        self._changed()

    async def close_all(self) -> None:
        for pane_id in list(self.panes):
            await self.close(pane_id)

    # ---- the bridge --------------------------------------------------------------------------

    async def _write(self, pane: TerminalPane, message: dict) -> None:
        if pane.process is None or pane.process.stdin is None:
            return
        pane.process.stdin.write(json.dumps(message).encode() + b"\n")
        with contextlib.suppress(ConnectionResetError, BrokenPipeError):
            await pane.process.stdin.drain()

    async def _read(self, pane: TerminalPane) -> None:
        assert pane.process and pane.process.stdout
        try:
            while True:
                line = await pane.process.stdout.readline()
                if not line:
                    break
                try:
                    message = json.loads(line)
                except ValueError:
                    continue
                self._handle(pane, message)
        except asyncio.CancelledError:
            raise
        finally:
            if pane.exit_code is None:
                pane.exit_code = pane.process.returncode if pane.process else 0
            self._changed()

    def _handle(self, pane: TerminalPane, message: dict) -> None:
        kind = message.get("t")
        if kind == "hello":
            pane.shell_pid = int(message.get("shell_pid") or 0)
            pane.rows = int(message.get("rows") or pane.rows)
            pane.cols = int(message.get("cols") or pane.cols)
        elif kind in ("snapshot", "diff"):
            self._screenUpdate(pane, message, full=kind == "snapshot")
            return
        elif kind == "title":
            pane.title = str(message.get("text") or pane.title)[:120]
        elif kind == "cwd":
            pane.cwd = str(message.get("path") or pane.cwd)[:400]
        elif kind == "status":
            previous = pane.status
            pane.foreground_pid = int(message.get("foreground_pid") or 0)
            pane.running = bool(message.get("running"))
            if pane.status == previous:
                return
        elif kind == "exit":
            pane.exit_code = int(message.get("code") or 0)
        elif kind == "out":
            data = message.get("bytes")
            if isinstance(data, str):
                raw = base64.b64decode(data)
                for callback in list(self._raw_callbacks):
                    callback(pane.id, raw)
            return
        elif kind == "history":
            waiter = pane.history_waiter
            pane.history_waiter = None
            if waiter is not None and not waiter.done():
                waiter.set_result(message)
            return
        elif kind in ("bell", "mark"):
            return
        else:
            return
        pane.updated = time.time()
        self._changed()

    def _screenUpdate(self, pane: TerminalPane, message: dict, *, full: bool) -> None:
        if full:
            pane.rows = int(message.get("rows") or pane.rows)
            pane.cols = int(message.get("cols") or pane.cols)
            pane.alt = bool(message.get("alt"))
            pane.lines = {}
        for row in message.get("lines", []):
            pane.lines[int(row.get("row", 0))] = row.get("segs", [])
        pane.cursor = message.get("cursor", pane.cursor)
        # Where this screen sits in the scrollback. A client holding a page of history needs it to
        # know whether its rows still join the live block (section 6.5).
        pane.base = int(message.get("base", pane.base))
        pane.history_rows = int(message.get("history", pane.history_rows))
        pane.updated = time.time()
        self._screen(pane.id, {"t": "screen_snapshot" if full else "screen_diff",
                               "pane": pane.id, "cursor": pane.cursor,
                               "base": pane.base, "history": pane.history_rows,
                               **({"rows": pane.rows, "cols": pane.cols, "alt": pane.alt}
                                  if full else {}),
                               "lines": message.get("lines", [])})

    # ---- input -------------------------------------------------------------------------------

    def _check_writable(self, pane: TerminalPane) -> None:
        """Refuse ordinary input at a password prompt, read fresh from the tty.

        A cached flag is not enough: between the phone seeing `status: password` and the bytes
        arriving, the prompt may have ended, and the line would be typed at the shell instead —
        into history, onto the screen, and out to every device watching.
        """
        if pane.exit_code is not None:
            raise wire.WireError("busy", "that terminal has exited.")
        if secret_prompt(pane.shell_pid):
            raise wire.WireError("not_permitted",
                                 "that pane is at a password prompt; ordinary input is refused.")

    async def send_keys(self, pane_id: str, data: bytes, *, device: str) -> None:
        pane = self.pane(pane_id)
        self._check_writable(pane)
        pane.driver = device
        await self._write(pane, {"t": "input", "bytes": base64.b64encode(data).decode()})
        self._changed()

    async def send_line(self, pane_id: str, text: str, *, device: str) -> None:
        if "\n" in text or "\r" in text:
            raise wire.WireError("unknown_type", "a line may not contain a newline.")
        await self.send_keys(pane_id, text.encode() + b"\n", device=device)

    async def paste(self, pane_id: str, text: str, *, device: str) -> None:
        pane = self.pane(pane_id)
        self._check_writable(pane)
        # Bracketed paste, so a program that supports it does not treat pasted newlines as Enter.
        payload = b"\x1b[200~" + text.encode() + b"\x1b[201~"
        await self._write(pane, {"t": "input", "bytes": base64.b64encode(payload).decode()})

    async def interrupt(self, pane_id: str) -> None:
        await self._write(self.pane(pane_id), {"t": "signal", "name": "int"})

    async def history(self, pane_id: str, before_row: int, count: int) -> dict:
        """A page of scrollback from the bridge, in the live screen's own row shape.

        The request goes out under the pane's lock and the next `history` line back is its answer;
        the bridge serves them in order on one pipe, so ordering is the id. The cursor is the
        absolute `before_row` the protocol carries, which the bridge understands directly.
        """
        pane = self.pane(pane_id)
        if pane.exit_code is not None:
            raise wire.WireError("busy", "that terminal has exited.")
        request = {"t": "history", "count": count}
        if before_row is not None and before_row >= 0:
            request["before_row"] = before_row
        async with pane.history_lock:
            if pane.exit_code is not None:
                raise wire.WireError("busy", "that terminal has exited.")
            future = asyncio.get_event_loop().create_future()
            pane.history_waiter = future
            try:
                await self._write(pane, request)
                page = await asyncio.wait_for(future, HISTORY_TIMEOUT)
            except asyncio.TimeoutError:
                raise wire.WireError("unavailable",
                                     "that terminal did not answer in time.") from None
            finally:
                if pane.history_waiter is future:
                    pane.history_waiter = None
        return {"from_row": int(page.get("from", 0)), "total": int(page.get("total", 0)),
                "more": bool(page.get("more")), "lines": page.get("lines") or []}

    async def request_snapshot(self, pane_id: str) -> None:
        await self._write(self.pane(pane_id), {"t": "snapshot"})

    def release(self, pane_id: str, device: str) -> None:
        pane = self.panes.get(pane_id)
        if pane and pane.driver == device:
            pane.driver = None
            self._changed()

    def release_device(self, device: str) -> None:
        for pane in self.panes.values():
            if pane.driver == device:
                pane.driver = None
        self._changed()

    # ---- the agent half is not wired to a worker in this harness -------------------------------

    async def compose(self, pane: str, text: str, *, to_agent: bool, when: str, origin: str,
                      origin_name: str = "") -> None:
        if to_agent:
            raise wire.WireError("not_permitted",
                                 "this terminal is shared without an agent; use the Terminal tab.")
        await self.send_line(pane, text, device=origin.removeprefix("remote:"))

    async def agent_stop(self, pane: str) -> None:
        await self.interrupt(pane)

    async def queue_remove(self, pane: str, item_id: str) -> None:
        raise wire.WireError("not_permitted", "no agent queue in a shared terminal.")

    async def recap_request(self, pane: str) -> None:
        raise wire.WireError("not_permitted", "no agent in a shared terminal.")

    async def plan_execute(self, pane: str, plan_id: str, origin: str) -> None:
        raise wire.WireError("not_permitted", "no agent in a shared terminal.")

    async def transcribe(self, pane: str, audio: bytes, audio_format: str) -> str:
        raise wire.WireError("not_permitted", "voice needs the agent worker.")

    # ---- password prompts (section 6.7) ---------------------------------------------------------

    def secret_state(self, pane: str) -> dict | None:
        item = self.panes.get(pane)
        if item is None or not secret_prompt(item.shell_pid):
            return None
        return {"shell_pid": item.shell_pid, "foreground_pid": item.foreground_pid}

    def secret_prompt(self, pane: str) -> bool:
        item = self.panes.get(pane)
        return bool(item) and secret_prompt(item.shell_pid)

    async def send_secret(self, pane: str, data: bytes, *, device: str) -> None:
        """A password line, typed only after the fresh termios check that secret_prompt is."""
        item = self.pane(pane)
        if not secret_prompt(item.shell_pid):
            raise wire.WireError("not_permitted",
                                 "that pane is no longer at a password prompt.")
        payload = data + b"\n"           # Secret::take() adds the newline on the GUI side; the
        await self._write(item, {"t": "input",       # bridge takes raw bytes, so it is added here
                                 "bytes": base64.b64encode(payload).decode()})
