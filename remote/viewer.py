# SPDX-License-Identifier: GPL-3.0-or-later
"""Relay-to-Relay: the process a laptop's Relay runs to open a pane shared by the owner's desktop.

The laptop pairs as one of the owner's own **devices** — the same pairing a phone does, the same
capability grant, the same pinned key — not as a guest. The Qt side (``src/RemotePane``) talks to
this process in JSON lines on stdin/stdout, in the style of ``remote/gui_host.py``; this process
holds the Noise session to the desktop through ``remote/client.py`` and never lets a key, the
pairing secret or a frame's contents reach a log.

The stdio contract. Field names are fixed; fields marked *optional* are additions the Qt side may
ignore.

  Qt → viewer
    {"t":"pair","url":"<pairing link>","name":"<this machine>","platform":"Relay"}
        optional "rendezvous": the rendezvous base, when it is not the link's own origin
    {"t":"connect"}                 reconnect with the stored paired record, if any
    {"t":"forget"}                  delete the stored record (and drop the session)
    {"t":"open","pane":"<id>"}      pane_focus, then pane_state_get; kept across reconnects
    {"t":"close","pane":"<id>"}     pane_blur
    {"t":"send","message":{…}}      one client → desktop wire message. Refused here when its type
                                    is not in wire.CLIENT_TYPES, is in wire.NEVER_FROM_CLIENT, or
                                    is session plumbing this process owns (hello, resume, …)
    {"t":"stop"}

  viewer → Qt
    {"t":"status","state":"unpaired|pairing|connecting|connected|reconnecting|offline",
     "message":"<one sentence>"}
    {"t":"code","code":"12345"}     the pairing confirmation code, as the desktop shows it
    {"t":"paired","desktop":"<name>","fingerprint":"AB12 CD34 EF56"}
    {"t":"welcome","capability":"full","features":[…]}
        optional "desktop" (its name), "device" (this laptop's device id), "hub_epoch"
    {"t":"message","message":{…}}   every server message, verbatim and in order, with replays
                                    of a sequenced frame this process already forwarded dropped
        optional, on `control` only: "driving" (this laptop holds the pane's keyboard) and
        "lost" (it held it until this message) — app/app.js's `onControl`, computed once here
    {"t":"error","message":"<one sentence>"}

Resume (docs/REMOTE-PROTOCOL.md section 7). The streams are the hub's own names — ``panes``,
``agent:<pane>`` and ``screen:<pane>`` (``Host.stream``). For each, this process keeps the highest
``seq`` it has forwarded and a window of the ones below it; on reconnect it sends
``resume {streams, hub_epoch}`` **before** re-focusing the open panes, so the ring replays what was
missed ahead of the fresh snapshot `pane_focus` sends, and anything that arrives twice — the ring
tail `pane_focus` repeats, the `panes` list `hello` sends before `resume` is read — is dropped
rather than forwarded twice. A new ``hub_epoch`` means the desktop restarted: the counters start
again at 1, so everything held is discarded.
"""
from __future__ import annotations

import asyncio
import contextlib
import json
import logging
import os
import sys
import time
from pathlib import Path
from urllib.parse import urlsplit

if __package__ in (None, ""):
    # Run as a script, the first path entry is remote/ itself, where remote/email.py would shadow
    # the standard library's `email` (which `cryptography` imports). Replace it with the root.
    sys.path[0] = str(Path(__file__).resolve().parent.parent)

from remote import client as client_mod, pairing, wire  # noqa: E402

log = logging.getLogger("relay.viewer")

RECORD_NAME = "paired.json"

# Session plumbing this process owns. They are client types, so the hub would accept them, but a
# `hello` or a `resume` from the Qt side would desynchronise the stream bookkeeping below, and
# `pair_prove`, `knock` and `transport_switch` have no meaning on a device session at all.
SESSION_TYPES = frozenset({"hello", "pair_prove", "knock", "transport_switch", "resume", "bye"})

# How long to wait between reconnect attempts, in seconds. The last value repeats.
RETRY = (1.0, 2.0, 5.0, 10.0, 30.0)
# Silence on the link longer than this gets a `ping`; a second silence of the same length means
# the link is dead even though no socket said so (a laptop lid closed on Wi-Fi does exactly this).
IDLE = 25.0
CONNECT_TIMEOUT = 30.0
# How many sequence numbers below the highest one seen are remembered per stream. A replay older
# than that is dropped as already seen, which is right: the hub's rings are shorter than this.
SEEN_WINDOW = 4096


def data_dir() -> Path:
    """``$XDG_DATA_HOME/relay/viewer``, 0700 — the laptop's own, never the desktop's ``remote``."""
    base = os.environ.get("XDG_DATA_HOME") or str(Path.home() / ".local" / "share")
    path = Path(base) / "relay" / "viewer"
    path.mkdir(parents=True, exist_ok=True)
    path.chmod(0o700)
    return path


def stream_of(message: dict) -> str | None:
    """The hub stream a sequenced message belongs to, or None when it is not one of them.

    ``pane_state`` also carries a ``seq``, but it is the pane model's own counter, kept outside
    the hub's rings on purpose (host.py, section 16), so it is neither resumed nor de-duplicated.
    """
    if not isinstance(message.get("seq"), int):
        return None
    kind = message.get("t")
    if kind == "panes":
        return "panes"
    pane = message.get("pane")
    if not isinstance(pane, str) or not pane:
        return None
    if kind == "agent":
        return f"agent:{pane}"
    if kind in ("screen_snapshot", "screen_diff"):
        return f"screen:{pane}"
    return None


class Seen:
    """The sequence numbers forwarded on one stream: the highest, and a window below it."""

    def __init__(self):
        self.high = 0
        self.recent: set[int] = set()

    def fresh(self, seq: int) -> bool:
        if seq in self.recent or seq <= self.high - SEEN_WINDOW:
            return False
        self.recent.add(seq)
        if seq > self.high:
            self.high = seq
            floor = self.high - SEEN_WINDOW
            if len(self.recent) > 2 * SEEN_WINDOW:
                self.recent = {value for value in self.recent if value > floor}
        return True


class Record:
    """What this laptop keeps to reconnect: the paired device record and where the rendezvous is.

    Written 0600 inside a 0700 directory, created with that mode rather than chmod-ed afterwards,
    so the device's private key is never readable by anyone else even for an instant. Never in
    QSettings and never logged.
    """

    def __init__(self, paired: client_mod.Paired, rendezvous: str, desktop_name: str = "",
                 paired_at: float = 0.0):
        self.paired = paired
        self.rendezvous = rendezvous
        self.desktop_name = desktop_name
        self.paired_at = paired_at or time.time()

    @property
    def fingerprint(self) -> str:
        return pairing.fingerprint(self.paired.desktop_public)

    @staticmethod
    def path(directory: Path) -> Path:
        return directory / RECORD_NAME

    @classmethod
    def load(cls, directory: Path) -> "Record | None":
        path = cls.path(directory)
        try:
            raw = json.loads(path.read_text())
            return cls(client_mod.Paired.from_json(json.dumps(raw["paired"])),
                       str(raw["rendezvous"]), str(raw.get("desktop_name", "")),
                       float(raw.get("paired_at", 0)))
        except FileNotFoundError:
            return None
        except (OSError, ValueError, KeyError, TypeError):
            log.warning("the stored pairing record is unreadable; pair again")
            return None

    def save(self, directory: Path) -> None:
        payload = json.dumps({"version": 1, "rendezvous": self.rendezvous,
                              "desktop_name": self.desktop_name, "paired_at": self.paired_at,
                              "paired": json.loads(self.paired.to_json())}, indent=2)
        path = self.path(directory)
        temporary = path.with_name(path.name + ".tmp")
        with contextlib.suppress(FileNotFoundError):
            temporary.unlink()
        descriptor = os.open(temporary, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
        with os.fdopen(descriptor, "w") as handle:
            handle.write(payload)
        os.replace(temporary, path)

    @classmethod
    def delete(cls, directory: Path) -> bool:
        try:
            cls.path(directory).unlink()
            return True
        except FileNotFoundError:
            return False


def rendezvous_of(url: str) -> str:
    """The rendezvous a pairing link belongs to: the origin it was served from (``pair_url``
    builds it on ``app_base``, which is the rendezvous). Only the scheme and host are read —
    the fragment, which holds the secret, is never touched here."""
    parts = urlsplit(url)
    if parts.scheme not in ("http", "https") or not parts.netloc:
        raise ValueError("that is not a Relay pairing link. Copy the whole link the desktop "
                         "shows under Pair device.")
    return f"{parts.scheme}://{parts.netloc}"


class Viewer:
    def __init__(self, emit=None, directory: Path | None = None):
        self._emit = emit or self._stdout
        self.directory = directory or data_dir()
        self.record = Record.load(self.directory)
        self.client: client_mod.Client | None = None
        self.connected = False
        self.capability = ""
        self.hub_epoch = None
        self.streams: dict[str, Seen] = {}
        self.open_panes: list[str] = []
        self.driving: dict[str, bool] = {}
        self.state = ""
        self._supervisor: asyncio.Task | None = None
        self._pairing: asyncio.Task | None = None
        self._send_lock = asyncio.Lock()
        self._wake = asyncio.Event()
        self._paired_announced = True       # False only between a pairing and its first welcome
        self.stopping = False

    # ---- stdio ---------------------------------------------------------------------------------

    @staticmethod
    def _stdout(message: dict) -> None:
        sys.stdout.write(json.dumps(message, separators=(",", ":")) + "\n")
        sys.stdout.flush()

    def emit(self, message: dict) -> None:
        self._emit(message)

    def status(self, state: str, message: str) -> None:
        self.state = state
        self.emit({"t": "status", "state": state, "message": message})

    def error(self, message: str) -> None:
        self.emit({"t": "error", "message": message})

    def announce(self) -> None:
        """Where things stand, said once at start so the Qt side need not guess."""
        if self.record is None:
            self.status("unpaired", "This computer is not paired with a desktop yet.")
        else:
            self.status("offline", f"Paired with {self._desktop()}; not connected yet.")

    async def read_forever(self) -> None:
        loop = asyncio.get_running_loop()
        reader = asyncio.StreamReader()
        await loop.connect_read_pipe(lambda: asyncio.StreamReaderProtocol(reader), sys.stdin)
        self.announce()
        while not self.stopping:
            line = await reader.readline()
            if not line:
                break
            try:
                message = json.loads(line)
            except ValueError:
                self.error("That line was not JSON.")
                continue
            if not isinstance(message, dict):
                self.error("That line was not a JSON object.")
                continue
            try:
                await self.handle(message)
            except Exception:                   # one bad line must not kill the link
                # The type only: a `send` can carry typed text, and `pair` carries the secret.
                log.exception("handling %s", str(message.get("t"))[:40])
                self.error("Something went wrong handling that; the link is still up.")

    # ---- commands ------------------------------------------------------------------------------

    async def handle(self, message: dict) -> None:
        kind = message.get("t")
        if kind == "pair":
            await self.start_pairing(message)
        elif kind == "connect":
            self.connect()
        elif kind == "forget":
            await self.forget()
        elif kind == "open":
            await self.open(str(message.get("pane") or ""))
        elif kind == "close":
            await self.close_pane(str(message.get("pane") or ""))
        elif kind == "send":
            await self.send(message.get("message"))
        elif kind == "stop":
            await self.stop()
        else:
            self.error(f"Unknown command {str(kind)[:40]!r}.")

    # -- pairing ---------------------------------------------------------------------------------

    async def start_pairing(self, message: dict) -> None:
        url = message.get("url")
        if not isinstance(url, str) or not url.strip():
            self.error("Paste the pairing link the desktop shows under Pair device.")
            return
        url = url.strip()
        try:
            rendezvous = str(message.get("rendezvous") or "") or rendezvous_of(url)
            pairing.parse_pair_url(url)
        except ValueError as problem:
            # parse_pair_url's sentences name a field, never its value; ours say what to do.
            text = str(problem) if "Relay pairing link" in str(problem) else \
                "That pairing link is incomplete or damaged. Copy it from the desktop again."
            self.error(text)
            return
        except Exception:
            self.error("That pairing link is incomplete or damaged. Copy it from the desktop "
                       "again.")
            return
        await self._end_session()
        if self._pairing and not self._pairing.done():
            self._pairing.cancel()
            with contextlib.suppress(asyncio.CancelledError, Exception):
                await self._pairing
        name = str(message.get("name") or "Relay")[:80]
        platform = str(message.get("platform") or "Relay")[:40]
        self._pairing = asyncio.create_task(self._pair(url, rendezvous, name, platform))

    async def _pair(self, url: str, rendezvous: str, name: str, platform: str) -> None:
        self.status("pairing", "Reaching the desktop…")
        client = client_mod.Client(rendezvous)
        task = asyncio.create_task(client.pair(url, name=name, platform=platform))
        try:
            # The code exists once the handshake is done, before the desktop is asked, and it is
            # what the person compares with the desktop's dialog — so it is shown the moment it
            # exists, not after the answer.
            shown = False
            while not shown:
                if client.auth_code:
                    shown = True
                    self.emit({"t": "code", "code": client.auth_code})
                    self.status("pairing", "Check that the desktop shows the same code, then "
                                           "allow this computer there.")
                elif task.done():
                    break
                else:
                    await asyncio.sleep(0.02)
            paired = await task
        except asyncio.CancelledError:
            task.cancel()
            with contextlib.suppress(BaseException):
                await task
            await self._close_client(client)
            raise
        except wire.WireError as problem:
            await self._close_client(client)
            self._pairing_failed(problem.message)
            return
        except client_mod.PinMismatch:
            await self._close_client(client)
            self._pairing_failed("The desktop did not answer. The link may have expired; open "
                                 "Pair device on the desktop again.")
            return
        except Exception:
            await self._close_client(client)
            log.warning("pairing failed", exc_info=False)
            self._pairing_failed("Could not reach the desktop's relay. Check the connection and "
                                 "try again.")
            return
        # A pairing channel is for pairing: drop it and come back as a paired device, so there is
        # one path into a working session and it is the one every later connection uses (rrp.js).
        await self._close_client(client)
        self.record = Record(paired, rendezvous)
        try:
            self.record.save(self.directory)
        except OSError:
            self.error("Paired, but this computer could not save the pairing; it will be lost "
                       "when Relay closes.")
        self._forget_streams()
        self._paired_announced = False
        self._connect()

    def _pairing_failed(self, why: str) -> None:
        sentence = why.strip() or "The desktop refused this computer."
        if not sentence.endswith("."):
            sentence += "."
        self.error(sentence[:1].upper() + sentence[1:])
        if self.record is not None:
            self.status("offline", f"Still paired with {self._desktop()}.")
            self._connect()
        else:
            self.status("unpaired", "This computer is not paired with a desktop yet.")

    async def forget(self) -> None:
        if self._pairing and not self._pairing.done():
            self._pairing.cancel()
            with contextlib.suppress(asyncio.CancelledError, Exception):
                await self._pairing
        await self._end_session(say_bye=True)
        Record.delete(self.directory)
        self.record = None
        self._forget_streams()
        self.open_panes.clear()
        self.driving.clear()
        self.status("unpaired", "This computer has forgotten the desktop. Pair again to "
                                "reconnect.")

    # -- the session -----------------------------------------------------------------------------

    def connect(self) -> None:
        if self._pairing and not self._pairing.done():
            return                      # the pairing connects by itself when it succeeds
        self._connect()

    def _connect(self) -> None:
        if self.record is None:
            self.status("unpaired", "This computer is not paired with a desktop yet.")
            return
        if self._supervisor and not self._supervisor.done():
            if self.connected:
                self.status("connected", f"Connected to {self._desktop()}.")
            else:
                self._wake.set()        # waiting out a retry: try now instead
            return
        self._supervisor = asyncio.create_task(self._supervise())

    async def _supervise(self) -> None:
        attempt = 0
        while self.record is not None and not self.stopping:
            record = self.record
            self.status("connecting" if attempt == 0 else "reconnecting",
                        f"Connecting to {self._desktop()}…" if attempt == 0 else
                        f"Reconnecting to {self._desktop()}…")
            client = client_mod.Client(record.rendezvous)
            try:
                welcome = await asyncio.wait_for(client.connect(record.paired), CONNECT_TIMEOUT)
            except asyncio.CancelledError:
                await self._close_client(client)
                raise
            except client_mod.PinMismatch:
                await self._close_client(client)
                reason = (f"{self._desktop()} did not accept this computer. It may be offline, "
                          "or this computer was removed there; pair again if it keeps "
                          "happening.")
                if await self._backoff(attempt, reason):
                    attempt += 1
                    continue
                return
            except Exception:
                await self._close_client(client)
                if await self._backoff(attempt, f"Could not reach {self._desktop()}."):
                    attempt += 1
                    continue
                return
            attempt = 0
            self.client = client
            ended = await self._run_session(client, welcome)
            self.client = None
            self.connected = False
            await self._close_client(client)
            if ended == "revoked":
                self._revoked()
                return
            if self.stopping or self.record is None:
                return
            attempt = 1
            self.status("reconnecting", f"Lost the link to {self._desktop()}; reconnecting…")
            if not await self._backoff(0, "", announce=False):
                return

    async def _backoff(self, attempt: int, why: str, *, announce: bool = True) -> bool:
        if self.stopping or self.record is None:
            return False
        delay = RETRY[min(attempt, len(RETRY) - 1)]
        if announce:
            self.status("offline", f"{why} Trying again in {delay:g} s.")
        self._wake.clear()
        with contextlib.suppress(asyncio.TimeoutError):
            await asyncio.wait_for(self._wake.wait(), delay)
        return not self.stopping and self.record is not None

    async def _run_session(self, client: client_mod.Client, welcome: dict) -> str:
        """Consume the whole stream, in order, until the link ends. Returns why it ended."""
        epoch = welcome.get("hub_epoch")
        self._adopt_epoch(epoch)
        self.capability = str(welcome.get("capability") or "")
        desktop = welcome.get("desktop") or {}
        name = str(desktop.get("name") or "") if isinstance(desktop, dict) else ""
        if name and self.record is not None and name != self.record.desktop_name:
            self.record.desktop_name = name
            with contextlib.suppress(OSError):
                self.record.save(self.directory)
        if not self._paired_announced and self.record is not None:
            self.emit({"t": "paired", "desktop": self._desktop(),
                       "fingerprint": self.record.fingerprint})
            self._paired_announced = True
        self.emit({"t": "welcome", "capability": self.capability,
                   "features": list(welcome.get("features") or []), "desktop": self._desktop(),
                   "device": self.record.paired.device_id if self.record else "",
                   "hub_epoch": epoch})
        self.emit({"t": "message", "message": welcome})
        self.connected = True
        self.status("connected", f"Connected to {self._desktop()}.")

        # Resume first, then re-focus: the ring's replay must reach the Qt side ahead of the
        # fresh snapshot `pane_focus` sends, or an old diff would be painted over a new screen.
        try:
            wanted = {name: seen.high for name, seen in self.streams.items() if seen.high}
            if wanted:
                await self._send({"t": "resume", "streams": wanted, "hub_epoch": epoch})
            for pane in list(self.open_panes):
                await self._focus(pane)
        except Exception:
            return "lost"

        quiet = 0
        while True:
            try:
                message = await asyncio.wait_for(client.inbox.get(), IDLE)
            except asyncio.TimeoutError:
                quiet += 1
                if quiet >= 2:
                    return "lost"
                with contextlib.suppress(Exception):
                    await self._send({"t": "ping", "at": time.time()})
                continue
            quiet = 0
            if not isinstance(message, dict):
                continue
            kind = message.get("t")
            if kind == "revoked":
                self.emit({"t": "message", "message": message})
                return "revoked"
            if kind == "bye":
                self.emit({"t": "message", "message": message})
                return "lost"
            if kind == "ping":
                with contextlib.suppress(Exception):
                    await self._send({"t": "pong", "at": message.get("at")})
            self.forward(message)

    def forward(self, message: dict) -> None:
        """One server message to the Qt side, unless it is a sequenced frame already forwarded."""
        if message.get("t") == "resumed" and message.get("restart"):
            self._adopt_epoch(message.get("hub_epoch"))
        stream = stream_of(message)
        if stream is not None:
            pane = message.get("pane")
            if stream != "panes" and pane not in self.open_panes:
                # A frame for a pane that was closed while it was in flight. It is not
                # remembered either, so opening the pane again starts that stream afresh.
                self.emit({"t": "message", "message": message})
                return
            seen = self.streams.setdefault(stream, Seen())
            if not seen.fresh(message["seq"]):
                return
        envelope = {"t": "message", "message": message}
        if message.get("t") == "control" and isinstance(message.get("pane"), str):
            # app/app.js `onControl`: this device holds the pane only when the holder is the
            # owner **and** the device named is this one; the desktop's own keystroke reads as
            # the owner with no device, and takes the keyboard back.
            pane = message["pane"]
            mine = self.record.paired.device_id if self.record else ""
            held = (str(message.get("holder") or "owner") == "owner"
                    and bool(message.get("device")) and message.get("device") == mine)
            envelope["driving"] = held
            envelope["lost"] = self.driving.get(pane, False) and not held
            self.driving[pane] = held
        self.emit(envelope)

    def _adopt_epoch(self, epoch) -> None:
        """A different ``hub_epoch`` is a restarted desktop: its counters began again at 1, so a
        held ``seq`` would name a frame that no longer exists, or suppress a new one."""
        if self.hub_epoch is not None and epoch != self.hub_epoch:
            self._forget_streams()
        self.hub_epoch = epoch

    def _forget_streams(self, pane: str | None = None) -> None:
        if pane is None:
            self.streams.clear()
            return
        self.streams.pop(f"agent:{pane}", None)
        self.streams.pop(f"screen:{pane}", None)

    def _revoked(self) -> None:
        Record.delete(self.directory)
        self.record = None
        self._forget_streams()
        self.open_panes.clear()
        self.driving.clear()
        self.status("unpaired", "The desktop removed this computer. Pair again to reconnect.")

    async def _send(self, message: dict) -> None:
        client = self.client
        if client is None or client.session is None:
            raise ConnectionError("not connected")
        # One sender at a time: the Noise nonce is a counter, so two frames must reach the
        # socket in the order they were encrypted.
        async with self._send_lock:
            await client.send(message)

    async def _focus(self, pane: str) -> None:
        await self._send({"t": "pane_focus", "pane": pane})
        await self._send({"t": "pane_state_get", "pane": pane})

    async def _close_client(self, client: client_mod.Client | None) -> None:
        if client is None:
            return
        with contextlib.suppress(Exception):
            await client.close()

    async def _end_session(self, *, say_bye: bool = False) -> None:
        supervisor, self._supervisor = self._supervisor, None
        client = self.client
        if say_bye and client is not None and self.connected:
            with contextlib.suppress(Exception):
                await asyncio.wait_for(self._send({"t": "bye", "reason": "closed on the laptop"}),
                                       2)
        if supervisor and not supervisor.done():
            supervisor.cancel()
            with contextlib.suppress(asyncio.CancelledError, Exception):
                await supervisor
        await self._close_client(client)
        self.client = None
        self.connected = False

    # -- panes and input -------------------------------------------------------------------------

    async def open(self, pane: str) -> None:
        if not pane:
            self.error("open needs a pane id.")
            return
        if pane not in self.open_panes:
            self.open_panes.append(pane)
        if self.connected:
            try:
                await self._focus(pane)
            except Exception:
                self.error("The link dropped before the pane opened; it opens when the link is "
                           "back.")

    async def close_pane(self, pane: str) -> None:
        if pane in self.open_panes:
            self.open_panes.remove(pane)
        self._forget_streams(pane)
        self.driving.pop(pane, None)
        if self.connected:
            with contextlib.suppress(Exception):
                await self._send({"t": "pane_blur", "pane": pane})

    async def send(self, message) -> None:
        if not isinstance(message, dict) or not isinstance(message.get("t"), str):
            self.error("send needs a message object with a type.")
            return
        kind = message["t"]
        if kind in wire.NEVER_FROM_CLIENT or kind not in wire.CLIENT_TYPES:
            self.error(f"{kind[:40]!r} cannot be sent to the desktop from here.")
            return
        if kind in SESSION_TYPES:
            self.error(f"{kind!r} is handled by the connection itself; it cannot be sent.")
            return
        if kind == "pane_focus":
            await self.open(str(message.get("pane") or ""))
            return
        if kind == "pane_blur":
            await self.close_pane(str(message.get("pane") or ""))
            return
        if not self.connected:
            # Never queued: a keystroke or a prompt typed blind and delivered a minute later is
            # worse than one refused now, and a password must never wait anywhere (section 6.7).
            self.error("Not connected to the desktop; that was not sent.")
            return
        try:
            await self._send(message)
        except Exception:
            self.error("The link dropped; that was not sent.")
            return
        pane = message.get("pane")
        if isinstance(pane, str):
            # app/app.js sets its own flag as well as sending: the desktop's `control` is what
            # makes it true and follows at once, but nothing should look dead until it arrives.
            if kind == "control_request":
                self.driving[pane] = True
            elif kind == "control_release":
                self.driving[pane] = False

    async def stop(self) -> None:
        self.stopping = True
        if self._pairing and not self._pairing.done():
            self._pairing.cancel()
            with contextlib.suppress(asyncio.CancelledError, Exception):
                await self._pairing
        await self._end_session(say_bye=True)
        self._wake.set()

    def _desktop(self) -> str:
        if self.record is not None and self.record.desktop_name:
            return self.record.desktop_name
        return "the desktop"


async def main_async() -> int:
    viewer = Viewer()
    try:
        await viewer.read_forever()
    finally:
        await viewer.stop()
    return 0


def main() -> int:
    logging.basicConfig(level=logging.WARNING, stream=sys.stderr,
                        format="%(levelname)s %(name)s %(message)s")
    try:
        return asyncio.run(main_async())
    except KeyboardInterrupt:
        return 0


if __name__ == "__main__":
    raise SystemExit(main())
