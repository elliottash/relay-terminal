# SPDX-License-Identifier: AGPL-3.0-or-later
"""A very small Chrome DevTools Protocol driver, so tests can run the real web client.

The web app is the part a phone runs, and it is the part that holds the device key. Testing it
through Node stubs would test something else, so this drives an actual headless Chrome over CDP
using the WebSocket client the rest of the code already has. It is about a hundred lines because
the tests need exactly three things: navigate, evaluate, and read the console.
"""
from __future__ import annotations

import asyncio
import contextlib
import json
import shutil
import subprocess
import tempfile
import urllib.request
from pathlib import Path

from remote import ws

CHROME_NAMES = ("google-chrome", "chromium", "chromium-browser", "google-chrome-stable")


def shown(element_id: str) -> str:
    """JavaScript that is true when an element is actually drawn, not merely un-`hidden`.

    Checking the `hidden` property is not enough: an author `display` rule beats the browser's own
    [hidden] rule, and then an element can be "hidden" and on screen at the same time.
    """
    return (f"(e => !!e && getComputedStyle(e).display !== 'none' && e.getClientRects().length > 0)"
            f"(document.getElementById('{element_id}'))")


# How many of the app's screens are drawn right now. It must always be exactly one.
SCREENS_SHOWN = ("[...document.querySelectorAll('.screen')]"
                 ".filter(e => getComputedStyle(e).display !== 'none').length")


def find_chrome() -> str | None:
    for name in CHROME_NAMES:
        path = shutil.which(name)
        if path:
            return path
    return None


class Browser:
    """One headless Chrome with one page."""

    def __init__(self, binary: str | None = None, *, insecure: bool = False,
                 microphone: bool = False):
        self.binary = binary or find_chrome()
        # `insecure` is for the development certificate only: the GUI shares over https with a
        # self-signed certificate, and a phone gets a warning it can accept. Chrome cannot.
        self.insecure = insecure
        # `microphone` gives the page Chrome's own fake capture device and answers the permission
        # prompt for it, so a voice test records a real clip through a real MediaRecorder without
        # any hardware. Off by default: a page that asks for the microphone should have to.
        self.microphone = microphone
        self.process: subprocess.Popen | None = None
        self.socket: ws.WebSocket | None = None
        self.profile: tempfile.TemporaryDirectory | None = None
        self.port = 0
        self._next_id = 0
        self.console: list[str] = []
        self._pump: asyncio.Task | None = None
        self._replies: dict[int, asyncio.Future] = {}

    async def start(self) -> None:
        if not self.binary:
            raise RuntimeError("no Chrome or Chromium on this machine.")
        self.profile = tempfile.TemporaryDirectory(ignore_cleanup_errors=True)
        self.port = _free_port()
        arguments = [self.binary, "--headless=new", "--disable-gpu", "--no-first-run",
                     "--no-default-browser-check", "--disable-extensions",
                     "--disable-dev-shm-usage", f"--remote-debugging-port={self.port}",
                     f"--user-data-dir={self.profile.name}"]
        if self.insecure:
            arguments.append("--ignore-certificate-errors")
        if self.microphone:
            arguments += ["--use-fake-device-for-media-stream",
                          "--use-fake-ui-for-media-stream"]
        arguments.append("about:blank")
        self.process = subprocess.Popen(arguments, stdout=subprocess.DEVNULL,
                                        stderr=subprocess.DEVNULL)

        target = None
        for _ in range(200):
            await asyncio.sleep(0.1)
            if self.process.poll() is not None:
                raise RuntimeError("Chrome exited while starting.")
            try:
                target = await asyncio.to_thread(self._page_target)
            except Exception:
                continue
            if target:
                break
        if not target:
            raise RuntimeError("Chrome never opened a debuggable page.")
        self.socket = await ws.connect(target)
        self._pump = asyncio.create_task(self._read())
        await self.call("Runtime.enable")
        await self.call("Page.enable")
        await self.call("Log.enable")

    def _page_target(self) -> str | None:
        with urllib.request.urlopen(f"http://127.0.0.1:{self.port}/json/list", timeout=2) as reply:
            for entry in json.loads(reply.read()):
                if entry.get("type") == "page" and entry.get("webSocketDebuggerUrl"):
                    return entry["webSocketDebuggerUrl"]
        return None

    async def _read(self) -> None:
        try:
            while True:
                raw = await self.socket.recv()
                message = json.loads(raw if isinstance(raw, str) else raw.decode())
                if "id" in message:
                    future = self._replies.pop(message["id"], None)
                    if future and not future.done():
                        future.set_result(message)
                    continue
                method = message.get("method")
                if method == "Runtime.consoleAPICalled":
                    parts = [str(arg.get("value", arg.get("description", "")))
                             for arg in message["params"].get("args", [])]
                    self.console.append(" ".join(parts))
                elif method == "Runtime.exceptionThrown":
                    detail = message["params"]["exceptionDetails"]
                    self.console.append("EXCEPTION " + json.dumps(detail.get("text", ""))
                                        + " " + str(detail.get("exception", {}).get("description", "")))
                elif method == "Log.entryAdded":
                    entry = message["params"]["entry"]
                    self.console.append(f"{entry.get('level')}: {entry.get('text')}")
        except (ws.ConnectionClosed, asyncio.CancelledError):
            pass

    async def call(self, method: str, params: dict | None = None, timeout: float = 30) -> dict:
        self._next_id += 1
        request_id = self._next_id
        future = asyncio.get_running_loop().create_future()
        self._replies[request_id] = future
        await self.socket.send(json.dumps({"id": request_id, "method": method,
                                           "params": params or {}}))
        message = await asyncio.wait_for(future, timeout)
        if "error" in message:
            raise RuntimeError(f"{method}: {message['error']}")
        return message.get("result", {})

    async def navigate(self, url: str) -> None:
        await self.call("Page.navigate", {"url": url})

    async def evaluate(self, expression: str, timeout: float = 30):
        result = await self.call("Runtime.evaluate", {
            "expression": expression, "returnByValue": True, "awaitPromise": True}, timeout)
        details = result.get("exceptionDetails")
        if details:
            # `text` is "Uncaught" and nothing else, which names no file, line or cause. The
            # description under `exception` is the message a person would read in the console, so
            # a failing drive says what broke rather than that something did.
            described = details.get("exception", {}).get("description", "")
            raise RuntimeError(described or details.get("text", "evaluation failed"))
        return result.get("result", {}).get("value")

    async def wait_for(self, expression: str, timeout: float = 30, interval: float = 0.2):
        """Poll a JavaScript expression until it is truthy; return its value."""
        deadline = asyncio.get_running_loop().time() + timeout
        last = None
        while asyncio.get_running_loop().time() < deadline:
            last = await self.evaluate(expression)
            if last:
                return last
            await asyncio.sleep(interval)
        raise AssertionError(f"timed out waiting for {expression!r}; last value {last!r}; "
                             f"console: {self.console[-8:]}")

    async def stop(self) -> None:
        if self._pump:
            self._pump.cancel()
        if self.socket:
            await self.socket.close()
        if self.process:
            self.process.terminate()
            try:
                await asyncio.to_thread(self.process.wait, 10)
            except Exception:
                self.process.kill()
        if self.profile:
            # Chrome writes to its profile as it exits, so a strict cleanup races it.
            with contextlib.suppress(OSError):
                self.profile.cleanup()


def _free_port() -> int:
    import socket
    with socket.socket() as probe:
        probe.bind(("127.0.0.1", 0))
        return probe.getsockname()[1]
