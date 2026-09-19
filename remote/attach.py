# SPDX-License-Identifier: AGPL-3.0-or-later
"""Attach this terminal to a shared pane, so the desktop and the phone drive the same shell.

The pane already exists as a PTY inside ``relay-screen-bridge``; this puts the local terminal in
raw mode, forwards what you type into that PTY, and prints the raw bytes coming back. The phone is
watching the same shell through screen state, so both see the same thing without either being a
copy of the other.

Detach with **Ctrl-\\** — the shell keeps running and the phone keeps its view.

The local terminal's size is authoritative while attached: it sets the pane size on attach and on
every SIGWINCH, and the phone scales to fit. That is the protocol's rule (§6.5) and the reason the
Warp mobile-viewer resize bug cannot happen here.
"""
from __future__ import annotations

import asyncio
import base64
import contextlib
import fcntl
import os
import signal
import struct
import sys
import termios
import tty

DETACH = 0x1C          # Ctrl-\


def terminal_size(fd: int = 1) -> tuple[int, int]:
    try:
        packed = fcntl.ioctl(fd, termios.TIOCGWINSZ, b"\0" * 8)
        rows, cols, _, _ = struct.unpack("HHHH", packed)
        if rows and cols:
            return rows, cols
    except OSError:
        pass
    return 24, 80


class Attachment:
    """One local attach to one pane. Only one at a time."""

    def __init__(self, source, pane_id: str, on_input=None):
        self.source = source
        self.pane_id = pane_id
        # The owner's physical keystroke takes control back without asking (section 10.3). This
        # terminal *is* the desktop for `remote.cli share`, so its keys are that keystroke: the
        # callback is what the GUI sends as `control_take`.
        self.on_input = on_input
        self.loop = asyncio.get_event_loop()
        self.saved: list | None = None
        self.detached = asyncio.Event()
        self.suspended = False
        self._reading = False

    # ---- terminal state ----------------------------------------------------------------------

    def _raw(self) -> None:
        if self.saved is None and sys.stdin.isatty():
            self.saved = termios.tcgetattr(sys.stdin.fileno())
        if sys.stdin.isatty():
            tty.setraw(sys.stdin.fileno())

    def _cooked(self) -> None:
        if self.saved is not None and sys.stdin.isatty():
            termios.tcsetattr(sys.stdin.fileno(), termios.TCSADRAIN, self.saved)

    @contextlib.contextmanager
    def suspend(self):
        """Hand the terminal back for a prompt (a phone asking to pair), then take it again."""
        self.suspended = True
        self._stop_reading()
        self._cooked()
        os.write(1, b"\r\n")
        try:
            yield
        finally:
            if not self.detached.is_set():
                self._raw()
                self._start_reading()
                os.write(1, b"\r\n")
                self.loop.create_task(self.source.request_snapshot(self.pane_id))
            self.suspended = False

    # ---- plumbing ----------------------------------------------------------------------------

    def _start_reading(self) -> None:
        if not self._reading:
            self.loop.add_reader(sys.stdin.fileno(), self._on_stdin)
            self._reading = True

    def _stop_reading(self) -> None:
        if self._reading:
            with contextlib.suppress(Exception):
                self.loop.remove_reader(sys.stdin.fileno())
            self._reading = False

    def _on_stdin(self) -> None:
        try:
            data = os.read(sys.stdin.fileno(), 4096)
        except OSError:
            data = b""
        if not data:
            self.detach()
            return
        if DETACH in data:
            data = data[:data.index(DETACH)]
            if data:
                self.loop.create_task(self._send(data))
            self.detach()
            return
        self.loop.create_task(self._send(data))

    async def _send(self, data: bytes) -> None:
        # Straight to the PTY: the local user is the owner, not a remote device, so none of the
        # take-over rules apply. The password-prompt refusal is about *remote* input.
        pane = self.source.panes.get(self.pane_id)
        if pane is None:
            return
        if self.on_input is not None:
            with contextlib.suppress(Exception):
                self.on_input(self.pane_id)
        await self.source._write(pane, {"t": "input",
                                        "bytes": base64.b64encode(data).decode()})

    def _on_output(self, pane_id: str, data: bytes) -> None:
        if pane_id == self.pane_id and not self.suspended:
            with contextlib.suppress(OSError):
                os.write(1, data)

    def _on_winch(self) -> None:
        rows, cols = terminal_size()
        pane = self.source.panes.get(self.pane_id)
        if pane is not None:
            pane.rows, pane.cols = rows, cols
            self.loop.create_task(self.source._write(pane, {"t": "resize", "rows": rows,
                                                            "cols": cols}))

    # ---- the session -------------------------------------------------------------------------

    async def run(self) -> None:
        self.source.on_raw(self._on_output)
        self._raw()
        self._start_reading()
        with contextlib.suppress(NotImplementedError, ValueError):
            self.loop.add_signal_handler(signal.SIGWINCH, self._on_winch)
        self._on_winch()
        await self.source.request_snapshot(self.pane_id)
        try:
            await self.detached.wait()
        finally:
            self._stop_reading()
            with contextlib.suppress(NotImplementedError, ValueError):
                self.loop.remove_signal_handler(signal.SIGWINCH)
            self._cooked()
            self.source.off_raw(self._on_output)
            os.write(1, b"\r\n")

    def detach(self) -> None:
        self.detached.set()
