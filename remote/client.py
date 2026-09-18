# SPDX-License-Identifier: GPL-3.0-or-later
"""A client half in Python: what the web app does, in a form tests and scripts can drive.

The browser client (``app/``) is the real one. This exists so the host can be exercised end to end
without a browser, and so the pinning rule has somewhere to be tested: a client **must** refuse a
desktop whose static key is not the one it pinned at pairing, with no "trust this new key" path.
"""
from __future__ import annotations

import asyncio
import json
import time
from dataclasses import dataclass

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

    def to_json(self) -> str:
        return json.dumps({"desktop_public": pairing.b64(self.desktop_public),
                           "device_id": self.device_id, "capability": self.capability,
                           "static_private": pairing.b64(self.static_private),
                           "desktop_id": self.desktop_id})

    @classmethod
    def from_json(cls, text: str) -> "Paired":
        raw = json.loads(text)
        return cls(desktop_public=pairing.un64(raw["desktop_public"]), device_id=raw["device_id"],
                   capability=raw["capability"], static_private=pairing.un64(raw["static_private"]),
                   desktop_id=raw.get("desktop_id", ""))


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

    def _url(self, *, room: str = "", desktop: str = "", device: str = "") -> str:
        base = self.rendezvous.replace("https://", "wss://").replace("http://", "ws://")
        if room:
            return f"{base}/v1/connect?room={room}"
        return f"{base}/v1/connect?desktop={desktop}&device={device}"

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
                      desktop_id=reply.get("desktop", {}).get("id", ""))

    async def connect(self, paired: Paired) -> dict:
        """Reconnect as an already-paired device, refusing any key but the pinned one."""
        self.static_private = paired.static_private
        self.static_public = noise.public_of(self.static_private)
        desktop_id = paired.desktop_id or _derive_desktop_id(paired.desktop_public)
        self.socket = await ws.connect(self._url(desktop=desktop_id, device=paired.device_id))
        try:
            await self._handshake(paired.desktop_public)
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
