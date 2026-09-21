# SPDX-License-Identifier: AGPL-3.0-or-later
"""A client half in Python: what the web app does, in a form tests and scripts can drive.

The browser client (``app/``) is the real one. This exists so the host can be exercised end to end
without a browser, and so the pinning rule has somewhere to be tested: a client **must** refuse a
desktop whose static key is not the one it pinned at pairing, with no "trust this new key" path.
"""
from __future__ import annotations

import asyncio
import contextlib
import hmac
import json
import time
from dataclasses import dataclass
from urllib.parse import quote

from . import noise, pairing, wire, ws

ENC_JSON = 0


class PinMismatch(Exception):
    """The desktop presented a key we did not pin. Re-pair from a fresh QR code; never trust it."""


@dataclass
class Paired:
    desktop_public: bytes
    device_id: str
    capability: str
    static_private: bytes
    desktop_id: str = ""
    # The connect token this device presents at `/v1/connect` (section 8). Minted by the desktop
    # and handed over inside the Noise session, so the rendezvous never sees it before it is used.
    connect_token: str = ""

    def to_json(self) -> str:
        return json.dumps({"desktop_public": pairing.b64(self.desktop_public),
                           "device_id": self.device_id, "capability": self.capability,
                           "static_private": pairing.b64(self.static_private),
                           "desktop_id": self.desktop_id,
                           "connect_token": self.connect_token})

    @classmethod
    def from_json(cls, text: str) -> "Paired":
        raw = json.loads(text)
        return cls(desktop_public=pairing.un64(raw["desktop_public"]), device_id=raw["device_id"],
                   capability=raw["capability"], static_private=pairing.un64(raw["static_private"]),
                   desktop_id=raw.get("desktop_id", ""),
                   connect_token=raw.get("connect_token", ""))


@dataclass
class Joined:
    """What a guest stores after being admitted, and needs to reconnect (section 10.2).

    Deliberately not a :class:`Paired`: there is no ``device_id`` and no ``capability``, because a
    participant is not a device and holds no capability. ``expires`` is when this stops working;
    a `bye` with ``discard`` set, or a handshake the desktop refuses, means throw it away.
    """
    desktop_public: bytes
    participant: str
    role: str
    panes: list[str]
    expires: float
    static_private: bytes
    desktop_id: str = ""
    connect_token: str = ""            # section 8, exactly as a device's

    def to_json(self) -> str:
        return json.dumps({"desktop_public": pairing.b64(self.desktop_public),
                           "participant": self.participant, "role": self.role,
                           "panes": list(self.panes), "expires": self.expires,
                           "static_private": pairing.b64(self.static_private),
                           "desktop_id": self.desktop_id,
                           "connect_token": self.connect_token})

    @classmethod
    def from_json(cls, text: str) -> "Joined":
        raw = json.loads(text)
        return cls(desktop_public=pairing.un64(raw["desktop_public"]),
                   participant=raw["participant"], role=raw["role"],
                   panes=list(raw.get("panes", [])), expires=float(raw.get("expires", 0)),
                   static_private=pairing.un64(raw["static_private"]),
                   desktop_id=raw.get("desktop_id", ""),
                   connect_token=raw.get("connect_token", ""))


class Client:
    def __init__(self, rendezvous: str, *, static_private: bytes | None = None):
        self.rendezvous = rendezvous.rstrip("/")
        self.static_private = static_private or noise.generate_keypair()[0]
        self.static_public = noise.public_of(self.static_private)
        self.socket: ws.WebSocket | None = None
        self.session: noise.Session | None = None
        self.pinned: bytes | None = None
        self.inbox: asyncio.Queue[dict] = asyncio.Queue()
        self._reader: asyncio.Task | None = None
        self.auth_code: str = ""

    def _url(self, *, room: str = "", desktop: str = "", device: str = "",
             connect_token: str = "") -> str:
        base = self.rendezvous.replace("https://", "wss://").replace("http://", "ws://")
        if room:
            return f"{base}/v1/connect?room={room}"
        url = f"{base}/v1/connect?desktop={desktop}&device={device}"
        if connect_token:
            url += f"&ct={quote(connect_token, safe='')}"
        return url

    # ---- connecting --------------------------------------------------------------------------

    async def _handshake(self, desktop_public: bytes) -> None:
        initiator = noise.Initiator(self.static_private, desktop_public)
        await self.socket.send(initiator.write_message_1(b""))
        try:
            reply = await asyncio.wait_for(self.socket.recv(), 20)
        except (ws.ConnectionClosed, asyncio.TimeoutError) as error:
            # The desktop hung up rather than answering: an unknown key, a revoked device, or a
            # desktop that is not the one we pinned. None of them is recoverable here.
            raise PinMismatch("the desktop did not accept this device's key.") from error
        if isinstance(reply, str):
            raise wire.WireError("internal", "the desktop replied with text.")
        _, self.session = initiator.read_message_2(reply)
        # In Noise IK the responder cannot complete the handshake without the private half of the
        # key we pinned, so reaching this line *is* the authentication. A different key fails above.
        self.pinned = desktop_public
        self.auth_code = self._auth_code(self.session.handshake_hash)
        self._reader = asyncio.create_task(self._read())

    @staticmethod
    def _auth_code(handshake_hash: bytes) -> str:
        from hashlib import sha256
        digest = sha256(b"RRP/1 pairing auth" + handshake_hash).digest()
        return f"{int.from_bytes(digest[:4], 'big') % 100000:05d}"

    async def pair(self, url: str, *, name: str, platform: str) -> Paired:
        """Scan a QR code: connect to its room, prove the secret, and pin the desktop key."""
        link = pairing.parse_pair_url(url)
        self.socket = await ws.connect(self._url(room=link["room"]))
        await self._handshake(link["desktop_public"])
        await self.send({"t": "pair_prove", "secret": pairing.b64(link["secret"]),
                         "name": name, "platform": platform})
        reply = await self.expect("paired", timeout=120)
        return Paired(desktop_public=link["desktop_public"], device_id=reply["device_id"],
                      capability=reply["capability"], static_private=self.static_private,
                      desktop_id=reply.get("desktop", {}).get("id", ""),
                      connect_token=str(reply.get("connect_token", "")))

    async def connect(self, paired: Paired) -> dict:
        """Reconnect as an already-paired device, refusing any key but the pinned one."""
        return await self._reconnect(paired.static_private, paired.desktop_public,
                                     paired.desktop_id, paired.device_id,
                                     paired.connect_token)

    async def knock(self, url: str, *, name: str, platform: str) -> Joined:
        """Follow an invite link: connect to its room, knock, and wait to be let in.

        The five-digit code in `knock_pending` is the same derivation pairing uses and is left on
        ``self.auth_code`` for the caller to show. A refusal — or two minutes with no answer —
        arrives as `error not_admitted` and raises, because the channel is closing anyway.
        """
        link = pairing.parse_invite_url(url)
        self.socket = await ws.connect(self._url(room=link["room"]))
        await self._handshake(link["desktop_public"])
        await self.send({"t": "knock", "invite": pairing.b64(link["secret"]),
                         "name": name, "platform": platform})
        await self.expect("knock_pending", timeout=30)
        reply = await self.expect("admitted", timeout=180)
        return Joined(desktop_public=link["desktop_public"], participant=reply["participant"],
                      role=reply["role"], panes=list(reply.get("panes", [])),
                      expires=float(reply.get("expires", 0)),
                      static_private=self.static_private,
                      desktop_id=reply.get("desktop", {}).get("id", ""),
                      connect_token=str(reply.get("connect_token", "")))

    async def knock_admitted(self, url: str, *, name: str,
                             platform: str) -> tuple[Joined, dict]:
        """:meth:`knock`, also returning the desktop's `admitted` message itself.

        The laptop's guest viewer (remote/viewer.py) keeps using this channel as its first
        session, so it needs what `admitted` carries beyond the :class:`Joined` record — the
        desktop's name, ``hub_epoch`` and ``features`` — exactly as a reconnect's `welcome` would.
        """
        link = pairing.parse_invite_url(url)
        self.socket = await ws.connect(self._url(room=link["room"]))
        await self._handshake(link["desktop_public"])
        await self.send({"t": "knock", "invite": pairing.b64(link["secret"]),
                         "name": name, "platform": platform})
        await self.expect("knock_pending", timeout=30)
        reply = await self.expect("admitted", timeout=180)
        joined = Joined(desktop_public=link["desktop_public"], participant=reply["participant"],
                        role=reply["role"], panes=list(reply.get("panes", [])),
                        expires=float(reply.get("expires", 0)),
                        static_private=self.static_private,
                        desktop_id=reply.get("desktop", {}).get("id", ""),
                        connect_token=str(reply.get("connect_token", "")))
        return joined, reply

    async def join_with_code(self, code: str, pin: str, *, app_base: str = "",
                             timeout: float = 20.0) -> str:
        """Type a meeting code and a PIN; get back the link the code phase delivered.

        What the page does before it knocks or pairs: look the code up, run CPace on the PIN in
        the code's room, check the desktop's tag **before** sending ours — a desktop that does not
        know the PIN learns nothing from this side — and open the sealed fragment. A refusal
        raises ``WireError`` with the desktop's reason (``wrong_pin``, ``burned``, ``expired``),
        or ``no_such_code`` when the rendezvous does not know the code, which is also what an
        expired one looks like.

        **The fragment picks its own page** (`pairing.fragment_url`), never the code and never
        the form it was typed into: a share code's invite fragment comes back as a ``/join`` link
        for :meth:`knock` (#97EG), a pairing code's as a ``/pair`` link for :meth:`pair` (#FR1C).
        Nothing else here can tell the two apart, and nothing else here has to.
        """
        from . import meetcode
        from .cpace import CPace, CPaceError
        code = code.strip().upper()
        room = await asyncio.to_thread(self._code_room, code)
        socket = await ws.connect(self._url(room=room))
        try:
            async def receive() -> dict:
                try:
                    data = await asyncio.wait_for(socket.recv(), timeout)
                except ws.ConnectionClosed as error:
                    raise wire.WireError("closed", "the desktop closed the code room.") from error
                message = meetcode.decode(data if isinstance(data, bytes) else data.encode())
                if message["t"] == "meet_error":
                    raise wire.WireError(str(message.get("error", "error")),
                                         "the desktop refused the code.")
                return message

            party = CPace(pin.encode(), meetcode.CI, room.encode(), initiator=True,
                          ad=meetcode.AD_GUEST)
            await socket.send(meetcode.encode({"t": "meet_a", "y": pairing.b64(party.message)}))
            reply = await receive()
            if reply["t"] != "meet_b":
                raise wire.WireError("internal", "the desktop answered out of turn.")
            try:
                yb = meetcode.point(reply)
                isk = party.finish(yb, meetcode.AD_DESKTOP)
            except (meetcode.MeetError, CPaceError) as error:
                raise wire.WireError("wrong_pin", "the desktop's reply does not check out.") \
                    from error
            tag_a, tag_b = meetcode.confirm_tags(isk, party.message, yb)
            try:
                offered = pairing.un64(str(reply.get("tag", "")))
            except Exception:
                offered = b""
            if not hmac.compare_digest(offered, tag_b):
                # Wrong PIN (or not the desktop). Hang up without confirming: the desktop counts
                # the attempt, and has learned nothing it could check a PIN against.
                raise wire.WireError("wrong_pin", "that PIN is not the one on the desktop.")
            await socket.send(meetcode.encode({"t": "meet_confirm", "tag": pairing.b64(tag_a)}))
            sealed = await receive()
            if sealed["t"] != "meet_invite":
                raise wire.WireError("internal", "the desktop answered out of turn.")
            try:
                fragment = meetcode.unseal(isk, code, sealed)
            except meetcode.MeetError as error:
                raise wire.WireError("internal", "the sealed invite did not open.") from error
        finally:
            with contextlib.suppress(Exception):
                await socket.close()
        try:
            return pairing.fragment_url(app_base or self.rendezvous, fragment)
        except ValueError as error:
            raise wire.WireError("internal", str(error)) from error

    async def pair_with_code(self, code: str, pin: str, *, name: str,
                             platform: str) -> Paired:
        """Type a pairing code and a PIN (card #FR1C), then pair on what it delivered.

        The refusal in the middle is the point: a code that delivered an *invite* fragment never
        reaches :meth:`pair`, so a share code typed into the pairing box pairs nothing even
        before the desktop refuses it on the wire.
        """
        url = await self.join_with_code(code, pin)
        if pairing.fragment_kind(url.split("#", 1)[-1]) != "pair":
            raise wire.WireError("not_permitted",
                                 "that code is an invitation to a shared pane, not a pairing "
                                 "code.")
        return await self.pair(url, name=name, platform=platform)

    def _code_room(self, code: str) -> str:
        """``GET /v1/codes/<code>`` → the room, off the event loop like the host's own posts."""
        import urllib.error
        import urllib.request
        from urllib.parse import quote
        try:
            request = urllib.request.Request(f"{self.rendezvous}/v1/codes/{quote(code, safe='')}",
                                             headers={"User-Agent": ws.USER_AGENT})
            with urllib.request.urlopen(request, timeout=20) as response:
                return str(json.loads(response.read())["room"])
        except urllib.error.HTTPError as error:
            if error.code == 429:
                raise wire.WireError("rate_limited", "too many code lookups; wait a minute.") \
                    from error
            raise wire.WireError("no_such_code", "no such meeting code.") from error

    async def rejoin(self, joined: Joined) -> dict:
        """Reconnect as an admitted participant. An expired or removed one gets a `bye` carrying
        ``discard``, which is the client's cue to forget this record rather than retry."""
        return await self._reconnect(joined.static_private, joined.desktop_public,
                                     joined.desktop_id, joined.participant,
                                     joined.connect_token)

    async def rejoin_reply(self, joined: Joined, timeout: float = 20.0) -> dict:
        """:meth:`rejoin`, except that the desktop's goodbye is returned rather than raised.

        :meth:`rejoin` turns a `bye` into ``WireError("closed")``, which cannot be told apart from
        a dropped link; a caller that must forget the record on a `bye` carrying ``discard`` (or a
        `revoked`) reads the message itself here. Returns the `welcome`, or that `bye`/`revoked`;
        a link that closes with no word from the desktop comes back as this client's own
        ``{"t":"bye","reason":"the link closed"}``, which carries no ``discard``.
        """
        self.static_private = joined.static_private
        self.static_public = noise.public_of(self.static_private)
        desktop_id = joined.desktop_id or _derive_desktop_id(joined.desktop_public)
        self.socket = await ws.connect(self._url(desktop=desktop_id, device=joined.participant,
                                                 connect_token=joined.connect_token))
        try:
            await self._handshake(joined.desktop_public)
        except noise.NoiseError as error:
            raise PinMismatch("the desktop's key is not the one this device pinned.") from error
        # A participant whose access ended is sent its goodbye straight after the handshake and
        # the socket closed, so this send may find it gone; the goodbye is in the inbox anyway.
        with contextlib.suppress(Exception):
            await self.send({"t": "hello", "client": "relay-python/1",
                             "proto": wire.PROTOCOL_VERSION})
        deadline = time.monotonic() + timeout
        while True:
            left = deadline - time.monotonic()
            if left <= 0:
                raise asyncio.TimeoutError("waited for welcome")
            message = await asyncio.wait_for(self.inbox.get(), left)
            if message["t"] in ("welcome", "bye", "revoked"):
                return message
            if message["t"] == "error":
                raise wire.WireError(message.get("code", "error"), message.get("message", ""))

    async def _reconnect(self, private: bytes, desktop_public: bytes, desktop_id: str,
                         channel_id: str, connect_token: str = "") -> dict:
        self.static_private = private
        self.static_public = noise.public_of(self.static_private)
        desktop_id = desktop_id or _derive_desktop_id(desktop_public)
        self.socket = await ws.connect(self._url(desktop=desktop_id, device=channel_id,
                                                 connect_token=connect_token))
        try:
            await self._handshake(desktop_public)
        except noise.NoiseError as error:
            raise PinMismatch("the desktop's key is not the one this device pinned.") from error
        await self.send({"t": "hello", "client": "relay-python/1", "proto": wire.PROTOCOL_VERSION})
        return await self.expect("welcome")

    # ---- messages ----------------------------------------------------------------------------

    async def send(self, message: dict) -> None:
        payload = bytes([ENC_JSON]) + wire.encode(message)
        await self.socket.send(self.session.encrypt(payload))

    async def _read(self) -> None:
        try:
            while True:
                frame = await self.socket.recv()
                if isinstance(frame, str):
                    continue
                plaintext = self.session.decrypt(frame)
                if not plaintext or plaintext[0] != ENC_JSON:
                    continue
                await self.inbox.put(wire.decode(plaintext[1:]))
        except (ws.ConnectionClosed, noise.NoiseError, wire.WireError):
            await self.inbox.put({"t": "bye", "reason": "the link closed"})

    async def expect(self, kind: str, timeout: float = 20.0) -> dict:
        """The next message of a kind, raising on an error or a close."""
        deadline = time.monotonic() + timeout
        while True:
            left = deadline - time.monotonic()
            if left <= 0:
                raise asyncio.TimeoutError(f"waited for {kind}")
            message = await asyncio.wait_for(self.inbox.get(), left)
            if message["t"] == kind:
                return message
            if message["t"] == "error":
                raise wire.WireError(message.get("code", "error"), message.get("message", ""))
            if message["t"] in ("bye", "revoked"):
                raise wire.WireError("closed", message.get("reason", "the desktop closed the link."))

    async def transport_switch(self) -> int:
        """Offer to re-bind the session to a new transport; the desktop acks with the exact
        frame count it expects next (docs/REMOTE-PROTOCOL.md section 2)."""
        # The offer itself is a frame, so the count this names includes it: after the ack,
        # exactly this many frames have been applied and the next one starts the new transport.
        await self.send({"t": "transport_switch", "next_seq": self.session.sent + 1})
        reply = await self.expect("transport_switched")
        return int(reply.get("effective", -1))

    async def close(self) -> None:
        if self._reader:
            self._reader.cancel()
        if self.socket:
            await self.socket.close()


def _derive_desktop_id(public_key: bytes) -> str:
    from hashlib import sha256
    return sha256(public_key).hexdigest()[:32]
