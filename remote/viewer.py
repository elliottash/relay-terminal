# SPDX-License-Identifier: GPL-3.0-or-later
"""Relay-to-Relay: the process a laptop's Relay runs to open a pane another Relay shares.

It runs in one of two modes, chosen on the command line and never switched while it runs:

* **device** (the default): the laptop pairs as one of the owner's own devices — the same pairing
  a phone does, the same capability grant, the same pinned key. Its record is
  ``data_dir()/paired.json``.
* **guest** (``--guest``): the laptop joins someone else's shared pane with a meeting code and a
  PIN (card #97EG), exactly as a browser does on the join page, and is a *participant* with a
  role (viewer or editor), not a device. Its record is ``data_dir()/guest/guest.json``, what
  ``Client.rejoin`` needs to reconnect until the host ends the access or it expires.

The Qt side (``src/RemotePane``) talks to this process in JSON lines on stdin/stdout, in the
style of ``remote/gui_host.py``; this process holds the Noise session to the desktop through
``remote/client.py`` and never lets a key, the pairing secret, a PIN, an invite link or a frame's
contents reach a log.

The stdio contract. Field names are fixed; fields marked *optional* are additions the Qt side may
ignore.

  Qt → viewer (both modes)
    {"t":"connect"}                 reconnect with the stored record, if any (in guest mode, only
                                    one that has not expired; otherwise `status unjoined`)
    {"t":"open","pane":"<id>"}      pane_focus, then pane_state_get (a guest: pane_focus only);
                                    kept across reconnects
    {"t":"close","pane":"<id>"}     pane_blur
    {"t":"send","message":{…}}      one client → desktop wire message. Refused here when its type
                                    is not in wire.CLIENT_TYPES, is in wire.NEVER_FROM_CLIENT, or
                                    is session plumbing this process owns (hello, resume, …); in
                                    guest mode also when wire.GUEST_TYPES does not give it to this
                                    guest's role (a courtesy: the hub enforces the same rule)
    {"t":"stop"}

  Qt → viewer (device mode)
    {"t":"pair","url":"<pairing link>","name":"<this machine>","platform":"Relay"}
        optional "rendezvous": the rendezvous base, when it is not the link's own origin
    {"t":"forget"}                  delete the stored record (and drop the session)
    (`join` and `leave` are refused with an `error`.)

  Qt → viewer (guest mode)
    {"t":"join","code":"BQRT","pin":"4829","name":"<this person>","platform":"Relay"}
        optional "rendezvous" (default https://join.relay-terminal.ai). The code is trimmed and
        case-insensitive. A stored record is kept until the new share admits this laptop, and is
        then replaced: one guest record at a time.
    {"t":"leave"}                   say `bye` if connected, delete the guest record,
                                    `status unjoined`. `forget` is taken as `leave`.
    (`pair` is refused with an `error`.)

  viewer → Qt (both modes)
    {"t":"status","state":"<state>","message":"<one sentence>"}
        device states: unpaired|pairing|connecting|connected|reconnecting|offline
        guest states:  unjoined|joining|knocking|connecting|connected|reconnecting|offline
    {"t":"code","code":"12345"}     device: the pairing confirmation code the desktop shows;
                                    guest: the knock's code, which the host's dialog shows. Sent
                                    the moment it exists, before the answer.
    {"t":"welcome","capability":"full|…|guest","features":[…]}
        optional "desktop" (its name), "hub_epoch"; device mode "device" (this laptop's device
        id); guest mode "role" (viewer|editor) and "participant" (this guest's id)
    {"t":"message","message":{…}}   every server message, verbatim and in order, with replays
                                    of a sequenced frame this process already forwarded dropped.
                                    In guest mode the first session's greeting is `admitted`,
                                    forwarded here where a reconnect forwards `welcome`.
        optional, on `control` only: "driving" (this laptop holds the pane's keyboard) and
        "lost" (it held it until this message) — app/app.js's `onControl`, computed once here
    {"t":"error","message":"<one sentence>"}
        guest mode, a failed join: also "reason", one of wrong_pin|burned|expired|no_such_code|
        not_admitted|rate_limited|closed|internal, followed by `status unjoined` (or `offline`
        and a reconnect, when an earlier guest record is still stored)

  viewer → Qt (device mode)
    {"t":"paired","desktop":"<name>","fingerprint":"AB12 CD34 EF56"}

  viewer → Qt (guest mode)
    A join says, in order: `status joining` ("Checking the code and PIN…"), `status knocking`
    ("Waiting for the host to let you in."), `code`, then on admission
    {"t":"joined","desktop":"<name>","role":"viewer|editor","panes":["p1",…],
     "expires":<epoch seconds>}
    and the session: `welcome` (capability "guest"), `message` lines, `status connected`.
    {"t":"ended","message":"<one sentence>"}
        the host ended this guest's access (a `bye` carrying ``discard``, a `revoked`, or the
        record's expiry): the record is deleted, `status unjoined` follows, and nothing retries.

Resume (docs/REMOTE-PROTOCOL.md section 7). The streams are the hub's own names — ``panes``,
``agent:<pane>`` and ``screen:<pane>`` (``Host.stream``). For each, this process keeps the highest
``seq`` it has forwarded and a window of the ones below it; on reconnect it sends
``resume {streams, hub_epoch}`` **before** re-focusing the open panes, so the ring replays what was
missed ahead of the fresh snapshot `pane_focus` sends, and anything that arrives twice — the ring
tail `pane_focus` repeats, the `panes` list `hello` sends before `resume` is read — is dropped
rather than forwarded twice. A new ``hub_epoch`` means the desktop restarted: the counters start
again at 1, so everything held is discarded. A guest reconnects through the same loop with
``Client.rejoin_reply`` instead of ``Client.connect``.
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
GUEST_DIR = "guest"
GUEST_RECORD_NAME = "guest.json"
DEFAULT_RENDEZVOUS = "https://join.relay-terminal.ai"

# A failed join's `reason`, and the sentence a person is shown for it.
JOIN_ERRORS = {
    "wrong_pin": "That PIN is not the one on their screen.",
    "burned": "That code was used up after too many wrong PINs; ask for a new one.",
    "expired": "No live share has that code. Codes last ten minutes; ask for a new one.",
    "no_such_code": "No live share has that code. Codes last ten minutes; ask for a new one.",
    "not_admitted": "They did not let you in.",
    "rate_limited": "Too many tries from this network; wait a minute and try again.",
    "closed": "The share closed before you got in; ask for a new code.",
    "internal": "Something went wrong joining that share; ask for a new code and try again.",
}

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
        write_private(self.path(directory), payload)

    @classmethod
    def delete(cls, directory: Path) -> bool:
        try:
            cls.path(directory).unlink()
            return True
        except FileNotFoundError:
            return False


def write_private(path: Path, payload: str) -> None:
    """Replace ``path`` with ``payload``, created 0600 rather than chmod-ed afterwards."""
    temporary = path.with_name(path.name + ".tmp")
    with contextlib.suppress(FileNotFoundError):
        temporary.unlink()
    descriptor = os.open(temporary, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
    with os.fdopen(descriptor, "w") as handle:
        handle.write(payload)
    os.replace(temporary, path)


class GuestRecord:
    """What a laptop admitted as a guest keeps to reconnect: the ``Joined`` record (its static
    private key included), the rendezvous, and the host's name.

    ``data_dir()/guest/guest.json``, the file 0600 and its directory 0700, written exactly as
    :class:`Record` is. The role and the expiry live in ``joined``; the copies at the top of the
    file are for a person reading it.
    """

    def __init__(self, joined: client_mod.Joined, rendezvous: str, desktop_name: str = "",
                 joined_at: float = 0.0):
        self.joined = joined
        self.rendezvous = rendezvous
        self.desktop_name = desktop_name
        self.joined_at = joined_at or time.time()

    @property
    def role(self) -> str:
        return self.joined.role

    @property
    def expires(self) -> float:
        return self.joined.expires

    @property
    def expired(self) -> bool:
        return bool(self.joined.expires) and time.time() >= self.joined.expires

    @staticmethod
    def path(directory: Path) -> Path:
        return directory / GUEST_DIR / GUEST_RECORD_NAME

    @classmethod
    def load(cls, directory: Path) -> "GuestRecord | None":
        try:
            raw = json.loads(cls.path(directory).read_text())
            return cls(client_mod.Joined.from_json(json.dumps(raw["joined"])),
                       str(raw["rendezvous"]), str(raw.get("desktop_name", "")),
                       float(raw.get("joined_at", 0)))
        except FileNotFoundError:
            return None
        except (OSError, ValueError, KeyError, TypeError):
            log.warning("the stored guest record is unreadable; join again")
            return None

    def save(self, directory: Path) -> None:
        payload = json.dumps({"version": 1, "rendezvous": self.rendezvous,
                              "desktop_name": self.desktop_name, "joined_at": self.joined_at,
                              "role": self.role, "expires": self.expires,
                              "joined": json.loads(self.joined.to_json())}, indent=2)
        path = self.path(directory)
        path.parent.mkdir(mode=0o700, exist_ok=True)
        path.parent.chmod(0o700)
        write_private(path, payload)

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


class Ended(Exception):
    """The host ended this guest's access; the argument is the sentence to show."""


def sentence_of(text, fallback: str) -> str:
    """A reason the desktop gave, as one sentence for a person: capitalised, full stop, short."""
    text = str(text or "").strip()[:200] or fallback
    if not text.endswith("."):
        text += "."
    return text[:1].upper() + text[1:]


class Viewer:
    def __init__(self, emit=None, directory: Path | None = None, *, guest: bool = False):
        self._emit = emit or self._stdout
        self.directory = directory or data_dir()
        self.guest = guest
        self.record: Record | GuestRecord | None
        if guest:
            self.record = GuestRecord.load(self.directory)
            if self.record is not None and self.record.expired:
                GuestRecord.delete(self.directory)
                self.record = None
        else:
            self.record = Record.load(self.directory)
        self._joining: asyncio.Task | None = None
        self._end_reason = ""
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

    def error(self, message: str, reason: str = "") -> None:
        line = {"t": "error", "message": message}
        if reason:
            line["reason"] = reason
        self.emit(line)

    def announce(self) -> None:
        """Where things stand, said once at start so the Qt side need not guess."""
        if self.record is None:
            self._no_record()
        elif self.guest:
            self.status("offline", f"In {self._desktop()}'s share; not connected yet.")
        else:
            self.status("offline", f"Paired with {self._desktop()}; not connected yet.")

    def _no_record(self, message: str = "") -> None:
        if self.guest:
            self.status("unjoined", message or "Not in anyone's share.")
        else:
            self.status("unpaired", message or "This computer is not paired with a desktop yet.")

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
            if self.guest:
                self.error("This is a guest session; pairing is for one of your own desktops.")
                return
            await self.start_pairing(message)
        elif kind == "join":
            if not self.guest:
                self.error("Joining someone else's share needs a guest session.")
                return
            await self.start_join(message)
        elif kind == "leave" or (kind == "forget" and self.guest):
            if not self.guest:
                self.error("There is no share to leave; use forget to unpair.")
                return
            await self.leave()
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

    # -- joining as a guest ----------------------------------------------------------------------

    async def start_join(self, message: dict) -> None:
        code = str(message.get("code") or "").strip().upper()
        pin = str(message.get("pin") or "").strip()
        if not code or len(code) > 16 or not code.isalnum():
            self._join_failed("no_such_code")
            return
        if len(pin) != 4 or not pin.isdigit():
            # Refused here rather than tried: a PIN that cannot be right would still spend one of
            # the code's three attempts on the host.
            self.error("A PIN is the four digits on their screen.", "wrong_pin")
            self._settle_after_failure()
            return
        rendezvous = str(message.get("rendezvous") or "").strip() or DEFAULT_RENDEZVOUS
        parts = urlsplit(rendezvous)
        if parts.scheme not in ("http", "https") or not parts.netloc:
            self.error("That relay address is not a web address.", "internal")
            self._settle_after_failure()
            return
        rendezvous = f"{parts.scheme}://{parts.netloc}{parts.path.rstrip('/')}"
        await self._cancel_joining()
        # The session to an earlier share stops while this one is tried; its record stays until
        # the new host admits this laptop, and it comes back if they do not.
        await self._end_session()
        name = str(message.get("name") or "Relay")[:80]
        platform = str(message.get("platform") or "Relay")[:40]
        self._joining = asyncio.create_task(self._join(code, pin, rendezvous, name, platform))

    async def _join(self, code: str, pin: str, rendezvous: str, name: str,
                    platform: str) -> None:
        self.status("joining", "Checking the code and PIN…")
        client: client_mod.Client | None = None
        task: asyncio.Task | None = None
        phase = "code"
        try:
            url = await client_mod.Client(rendezvous).join_with_code(code, pin,
                                                                     app_base=rendezvous)
            phase = "knock"
            self.status("knocking", "Waiting for the host to let you in.")
            client = client_mod.Client(rendezvous)
            task = asyncio.create_task(client.knock_admitted(url, name=name, platform=platform))
            del url
            # The code exists once the handshake is done, before the host is asked, and it is
            # what their dialog shows — so it is shown the moment it exists.
            while not client.auth_code and not task.done():
                await asyncio.sleep(0.02)
            if client.auth_code:
                self.emit({"t": "code", "code": client.auth_code})
            joined, admitted = await task
        except asyncio.CancelledError:
            if task is not None:
                task.cancel()
                with contextlib.suppress(BaseException):
                    await task
            await self._close_client(client)
            raise
        except wire.WireError as problem:
            await self._close_client(client)
            reason = problem.code if problem.code in JOIN_ERRORS else (
                "closed" if phase == "knock" else "internal")
            self._join_failed(reason)
            return
        except asyncio.TimeoutError:
            await self._close_client(client)
            self._join_failed("not_admitted" if phase == "knock" else "closed")
            return
        except client_mod.PinMismatch:
            # The host's key did not answer the knock: it is offline, or the invite went.
            await self._close_client(client)
            self._join_failed("closed")
            return
        except Exception:
            await self._close_client(client)
            log.warning("joining failed in the %s phase", phase)
            self._join_failed("internal", "Could not reach the share's relay. Check the "
                                          "connection and try again.")
            return

        desktop = admitted.get("desktop") if isinstance(admitted.get("desktop"), dict) else {}
        host_name = str(desktop.get("name") or admitted.get("desktop_name") or "")[:80]
        self.record = GuestRecord(joined, rendezvous, host_name)
        try:
            self.record.save(self.directory)
        except OSError:
            self.error("Joined, but this computer could not save the share; it will be lost when "
                       "Relay closes.")
        self._forget_streams()
        self.hub_epoch = None
        self.open_panes.clear()
        self.driving.clear()
        self.emit({"t": "joined", "desktop": self._desktop(), "role": joined.role,
                   "panes": list(joined.panes), "expires": joined.expires})
        self._supervisor = asyncio.create_task(self._supervise(first=(client, admitted)))

    def _join_failed(self, reason: str, sentence: str = "") -> None:
        self.error(sentence or JOIN_ERRORS.get(reason, JOIN_ERRORS["internal"]), reason)
        self._settle_after_failure()

    def _settle_after_failure(self) -> None:
        """After a join that did not happen: back to the earlier share if there is one."""
        if self.record is not None:
            self.status("offline", f"Still in {self._desktop()}'s share.")
            self._connect()
        else:
            self._no_record()

    async def _cancel_joining(self) -> None:
        if self._joining and not self._joining.done():
            self._joining.cancel()
            with contextlib.suppress(asyncio.CancelledError, Exception):
                await self._joining
        self._joining = None

    async def leave(self) -> None:
        await self._cancel_joining()
        await self._end_session(say_bye=True, reason="left the share")
        GuestRecord.delete(self.directory)
        self.record = None
        self._forget_streams()
        self.open_panes.clear()
        self.driving.clear()
        self._no_record("You left the share.")

    def _ended(self, sentence: str) -> None:
        """The host ended this guest's access: forget it and say so, and never retry."""
        GuestRecord.delete(self.directory)
        self.record = None
        self._forget_streams()
        self.open_panes.clear()
        self.driving.clear()
        self.emit({"t": "ended", "message": sentence})
        self._no_record()

    # -- the session -----------------------------------------------------------------------------

    def connect(self) -> None:
        if self._pairing and not self._pairing.done():
            return                      # the pairing connects by itself when it succeeds
        if self._joining and not self._joining.done():
            return                      # so does a join
        self._connect()

    def _connect(self) -> None:
        if self.guest and self.record is not None and self.record.expired:
            GuestRecord.delete(self.directory)
            self.record = None
        if self.record is None:
            self._no_record()
            return
        if self._supervisor and not self._supervisor.done():
            if self.connected:
                self.status("connected", f"Connected to {self._desktop()}.")
            else:
                self._wake.set()        # waiting out a retry: try now instead
            return
        self._supervisor = asyncio.create_task(self._supervise())

    async def _supervise(self, first: tuple[client_mod.Client, dict] | None = None) -> None:
        """Keep a session up while there is a record. ``first`` is a session already open — a
        guest's knock channel, which the host keeps as the admitted guest's first session."""
        attempt = 0
        while self.record is not None and not self.stopping:
            record = self.record
            if first is not None:
                (client, welcome), first = first, None
            else:
                if self.guest and record.expired:
                    self._ended(f"Your access to {self._desktop()}'s share has expired.")
                    return
                self.status("connecting" if attempt == 0 else "reconnecting",
                            f"Connecting to {self._desktop()}…" if attempt == 0 else
                            f"Reconnecting to {self._desktop()}…")
                client = client_mod.Client(record.rendezvous)
                try:
                    welcome = await asyncio.wait_for(self._open_session(client, record),
                                                     CONNECT_TIMEOUT)
                except asyncio.CancelledError:
                    await self._close_client(client)
                    raise
                except Ended as ended:
                    await self._close_client(client)
                    self._ended(str(ended))
                    return
                except client_mod.PinMismatch:
                    await self._close_client(client)
                    reason = (f"{self._desktop()} did not answer. It may be offline, or has "
                              "ended this share." if self.guest else
                              f"{self._desktop()} did not accept this computer. It may be "
                              "offline, or this computer was removed there; pair again if it "
                              "keeps happening.")
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
            if ended == "ended":
                self._ended(self._end_reason)
                return
            if ended == "revoked":
                self._revoked()
                return
            if self.stopping or self.record is None:
                return
            attempt = 1
            self.status("reconnecting", f"Lost the link to {self._desktop()}; reconnecting…")
            if not await self._backoff(0, "", announce=False):
                return

    async def _open_session(self, client: client_mod.Client, record) -> dict:
        """The handshake and `hello` for either kind of record; returns the `welcome`."""
        if not self.guest:
            return await client.connect(record.paired)
        reply = await client.rejoin_reply(record.joined)
        if reply.get("t") == "welcome":
            return reply
        if reply.get("t") == "revoked" or reply.get("discard"):
            raise Ended(sentence_of(reply.get("reason"), "The host ended this share."))
        raise ConnectionError("the link closed before the welcome")

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
        self.capability = "guest" if self.guest else str(welcome.get("capability") or "")
        desktop = welcome.get("desktop") or {}
        name = str(desktop.get("name") or "")[:80] if isinstance(desktop, dict) else ""
        changed = False
        if name and self.record is not None and name != self.record.desktop_name:
            self.record.desktop_name = name
            changed = True
        if self.guest and isinstance(self.record, GuestRecord):
            # The host's word on the role and the expiry is the current one: a role changed
            # while this laptop was away shows here first.
            role = welcome.get("role")
            if role in wire.GUEST_ROLES and role != self.record.joined.role:
                self.record.joined.role = role
                changed = True
            expires = welcome.get("expires")
            if isinstance(expires, (int, float)) and float(expires) != self.record.joined.expires:
                self.record.joined.expires = float(expires)
                changed = True
        if changed:
            with contextlib.suppress(OSError):
                self.record.save(self.directory)
        if not self.guest and not self._paired_announced and self.record is not None:
            self.emit({"t": "paired", "desktop": self._desktop(),
                       "fingerprint": self.record.fingerprint})
            self._paired_announced = True
        line = {"t": "welcome", "capability": self.capability,
                "features": list(welcome.get("features") or []), "desktop": self._desktop()}
        if self.guest:
            line["role"] = self.record.role if self.record else str(welcome.get("role") or "")
            line["participant"] = self.record.joined.participant if self.record else ""
        else:
            line["device"] = self.record.paired.device_id if self.record else ""
        line["hub_epoch"] = epoch
        self.emit(line)
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
                if self.guest:
                    self._end_reason = sentence_of(message.get("reason"),
                                                   "The host ended this share.")
                    return "ended"
                return "revoked"
            if kind == "bye":
                self.emit({"t": "message", "message": message})
                if self.guest and message.get("discard"):
                    # The host removed this guest, or the share or its access ended: the record
                    # is dead, and retrying it would only be refused again.
                    self._end_reason = sentence_of(message.get("reason"),
                                                   "The host ended this share.")
                    return "ended"
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
        if self.guest and message.get("t") == "participants":
            self._adopt_role(message)
        if message.get("t") == "control" and isinstance(message.get("pane"), str):
            # app/app.js `onControl`: this device holds the pane only when the holder is the
            # owner **and** the device named is this one; the desktop's own keystroke reads as
            # the owner with no device, and takes the keyboard back. A guest holds it when the
            # holder is `participant:<its own id>`.
            pane = message["pane"]
            if self.guest:
                mine = self.record.joined.participant if isinstance(self.record,
                                                                    GuestRecord) else ""
                held = bool(mine) and message.get("holder") == f"participant:{mine}"
            else:
                mine = self.record.paired.device_id if self.record else ""
                held = (str(message.get("holder") or "owner") == "owner"
                        and bool(message.get("device")) and message.get("device") == mine)
            envelope["driving"] = held
            envelope["lost"] = self.driving.get(pane, False) and not held
            self.driving[pane] = held
        self.emit(envelope)

    def _adopt_role(self, message: dict) -> None:
        """A role change reaches a guest only as its own row (``you``) in `participants`; keep
        the record's role current so `send`'s courtesy check follows a promotion or demotion."""
        if not isinstance(self.record, GuestRecord):
            return
        for item in message.get("items") or []:
            if isinstance(item, dict) and item.get("you"):
                role = item.get("role")
                if role in wire.GUEST_ROLES and role != self.record.joined.role:
                    self.record.joined.role = role
                    with contextlib.suppress(OSError):
                        self.record.save(self.directory)
                return

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
        # A guest never receives pane_state (section 10.1): asking only earns a refusal, which
        # the pane then shows as an error (the live /join drive of 2026-09-18 caught it).
        if not self.guest:
            await self._send({"t": "pane_state_get", "pane": pane})

    async def _close_client(self, client: client_mod.Client | None) -> None:
        if client is None:
            return
        with contextlib.suppress(Exception):
            await client.close()

    async def _end_session(self, *, say_bye: bool = False,
                           reason: str = "closed on the laptop") -> None:
        supervisor, self._supervisor = self._supervisor, None
        client = self.client
        if say_bye and client is not None and self.connected:
            with contextlib.suppress(Exception):
                await asyncio.wait_for(self._send({"t": "bye", "reason": reason}), 2)
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
        if self.guest:
            # A courtesy: the hub judges every guest message by the same table and refuses it
            # anyway, but a refusal here says so before anything is typed into the void.
            needed = wire.GUEST_TYPES.get(kind)
            if needed is None:
                self.error(f"{kind!r} is not open to guests.")
                return
            role = self.record.role if isinstance(self.record, GuestRecord) else ""
            if not wire.role_allows(role, needed):
                self.error(f"Your role in this share does not allow {kind!r}.")
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
        await self._cancel_joining()
        await self._end_session(say_bye=True)
        self._wake.set()

    def _desktop(self) -> str:
        if self.record is not None and self.record.desktop_name:
            return self.record.desktop_name
        return "the host" if self.guest else "the desktop"


async def main_async(guest: bool = False) -> int:
    viewer = Viewer(guest=guest)
    try:
        await viewer.read_forever()
    finally:
        await viewer.stop()
    return 0


def main(argv: list[str] | None = None) -> int:
    import argparse
    parser = argparse.ArgumentParser(prog="remote.viewer",
                                     description="Relay's remote pane viewer (JSON lines on "
                                                 "stdin/stdout).")
    parser.add_argument("--guest", action="store_true",
                        help="join someone else's share with a code and a PIN, as a guest")
    options = parser.parse_args(argv)
    logging.basicConfig(level=logging.WARNING, stream=sys.stderr,
                        format="%(levelname)s %(name)s %(message)s")
    try:
        return asyncio.run(main_async(options.guest))
    except KeyboardInterrupt:
        return 0


if __name__ == "__main__":
    raise SystemExit(main())
