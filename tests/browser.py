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
import os
import shutil
import signal
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


# Chrome's process singleton listens on $TMPDIR/com.google.Chrome.XXXXXX/SingletonSocket, and a
# Unix socket path must fit sun_path: 108 bytes on Linux, NUL included. A Relay pane's TMPDIR is
# its per-session scratch dir (#DVV2), long enough that Chrome dies with "Socket path too long"
# before it opens a page (#H1BS). 104 leaves a margin under the 107 usable bytes.
SOCKET_PATH_LIMIT = 104
_SINGLETON_SUFFIX = "/com.google.Chrome.XXXXXX/SingletonSocket"


def _chrome_tmpdir() -> str | None:
    """A private TMPDIR for one Chrome, short enough for its singleton socket; the caller removes it.

    Always private, not only when the inherited TMPDIR is too long: a terminated Chrome leaves its
    com.google.Chrome.* singleton and url-fetcher directories behind, and every test run used to
    add a few to /tmp for good (#H1BS). Made under the inherited TMPDIR when that fits, else under
    /dev/shm or /tmp. None only if none of them can be made: Chrome then inherits TMPDIR as before.
    """
    for parent in (tempfile.gettempdir(), "/dev/shm", "/tmp"):
        if len(parent) + len("/chrome-XXXXXXXX") + len(_SINGLETON_SUFFIX) > SOCKET_PATH_LIMIT:
            continue
        with contextlib.suppress(OSError):
            return tempfile.mkdtemp(prefix="chrome-", dir=parent)
    return None


# Card #XY13: a test killed outright runs no stop(), so its private TMPDIR and its profile stay
# behind — in a Relay pane the TMPDIR is under /dev/shm (the pane's own is too long for Chrome's
# socket), which nothing else ever cleans. Each private dir records the test process that owns it
# (pid and start time, so a recycled pid is not mistaken for it) and the profile beside it; the
# next Browser.start() removes the ones whose owner is gone. Only dirs carrying this record are
# touched, and a recorded profile only when it still looks like a Chrome profile.
_OWNER_RECORD = "relay-browser-owner"


def _starttime(pid: int) -> str | None:
    try:
        stat = Path(f"/proc/{pid}/stat").read_text()
    except OSError:
        return None
    fields = stat[stat.rfind(")") + 2:].split()
    return fields[19] if len(fields) > 19 else None


def _mark_leftover_owner(tmpdir: str, profile: str) -> None:
    started = _starttime(os.getpid())
    if started is None:
        return   # no /proc: nothing can be proven dead later, so record nothing
    with contextlib.suppress(OSError):
        Path(tmpdir, _OWNER_RECORD).write_text(
            f"pid {os.getpid()}\nstarttime {started}\nprofile {profile}\n")


def _reap_leftovers(parents: tuple[str, ...] | None = None) -> list[str]:
    """Remove private Chrome TMPDIRs (and their profiles) whose owning test process is dead."""
    removed: list[str] = []
    me = os.getuid()
    for parent in dict.fromkeys(parents or (tempfile.gettempdir(), "/dev/shm", "/tmp")):
        try:
            entries = list(os.scandir(parent))
        except OSError:
            continue
        for entry in entries:
            if not entry.name.startswith("chrome-"):
                continue
            try:
                if entry.is_symlink() or not entry.is_dir() or entry.stat().st_uid != me:
                    continue
                record = dict(line.split(" ", 1) for line in
                              Path(entry.path, _OWNER_RECORD).read_text().splitlines() if " " in line)
            except (OSError, ValueError):
                continue
            pid, started = record.get("pid", ""), record.get("starttime")
            if not pid.isdigit() or not started or _starttime(int(pid)) == started:
                continue   # unreadable, or its test is still running
            profile = record.get("profile", "")
            if profile and os.path.isdir(profile) and not os.path.islink(profile) \
                    and os.stat(profile).st_uid == me \
                    and any(os.path.exists(os.path.join(profile, n)) for n in ("Local State", "Default")):
                shutil.rmtree(profile, ignore_errors=True)
            shutil.rmtree(entry.path, ignore_errors=True)
            removed.append(entry.path)
    return removed


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
        self.tmpdir: str | None = None
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
        # Chrome's own temp files go in a dir stop() removes; the profile stays where it is.
        _reap_leftovers()
        self.tmpdir = _chrome_tmpdir()
        if self.tmpdir:
            _mark_leftover_owner(self.tmpdir, self.profile.name)
        env = {**os.environ, "TMPDIR": self.tmpdir} if self.tmpdir else None
        # Its own process group, so stop() can end the zygote and GPU children too: one that
        # outlives the browser writes its cache back into a profile stop() already removed.
        self.process = subprocess.Popen(_die_with_parent(arguments), stdout=subprocess.DEVNULL,
                                        stderr=subprocess.DEVNULL, env=env,
                                        start_new_session=True)

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
            await _end_group(self.process)
        if self.profile:
            await _remove(self.profile.name)
            self.profile = None
        if self.tmpdir:
            await _remove(self.tmpdir)
            self.tmpdir = None


def _pdeathsig_launcher() -> list[str]:
    """`setpriv --pdeathsig KILL --` where util-linux has it (card #XY13): the kernel kills Chrome
    when the test process dies, SIGKILL and OOM included, which no `finally` or atexit can do. A
    session of its own (for stop()'s group kill) is exactly what kept Chrome alive after its test
    died: four ran for days, reparented to systemd --user, filling /tmp. setpriv sets the flag and
    execs Chrome in place, so the pid Popen returns is still Chrome's, and nothing runs in the
    forked child the way a preexec_fn would in this threaded process."""
    setpriv = shutil.which("setpriv")
    if not setpriv:
        return []
    try:
        probe = subprocess.run([setpriv, "--pdeathsig", "KILL", "--", "true"],
                               stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=5)
    except (OSError, subprocess.SubprocessError):
        return []
    return [setpriv, "--pdeathsig", "KILL", "--"] if probe.returncode == 0 else []


_LAUNCHER: list[str] | None = None


def _die_with_parent(arguments: list[str]) -> list[str]:
    global _LAUNCHER
    if _LAUNCHER is None:
        _LAUNCHER = _pdeathsig_launcher()
    return _LAUNCHER + list(arguments)


async def _end_group(process: subprocess.Popen) -> None:
    """Terminate Chrome's whole process group and wait until every member has gone."""
    group = process.pid   # start_new_session made Chrome the leader of its own group
    for sig, seconds in ((signal.SIGTERM, 10), (signal.SIGKILL, 5)):
        with contextlib.suppress(ProcessLookupError):
            os.killpg(group, sig)
        for _ in range(seconds * 10):
            process.poll()   # reap the leader, or the group never looks empty
            try:
                os.killpg(group, 0)
            except ProcessLookupError:
                return
            await asyncio.sleep(0.1)


async def _remove(path: str) -> None:
    """Remove a directory Chrome was using, retrying while its exiting children still write to it.

    One ignore-errors pass left a profile (or a com.google.Chrome.* dir) behind on every few runs
    (#H1BS): the main process is gone but a zygote or GPU child is still flushing into it.
    """
    for _ in range(50):
        shutil.rmtree(path, ignore_errors=True)
        if not os.path.exists(path):
            return
        await asyncio.sleep(0.1)


def _free_port() -> int:
    import socket
    with socket.socket() as probe:
        probe.bind(("127.0.0.1", 0))
        return probe.getsockname()[1]
