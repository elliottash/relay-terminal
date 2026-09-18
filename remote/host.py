# SPDX-License-Identifier: GPL-3.0-or-later
"""The desktop end of RRP/1: one hub, many phones.

The hub keeps one outbound WebSocket to the rendezvous and runs an independent Noise session per
client channel. It is the only place that decides what a device may do, so the rules the security
review asked for live here rather than in the callers:

* capability is read from the **live** device record on every message and on every fan-out, so a
  revoke or a downgrade lands immediately instead of at the next handshake;
* a client never names a worker request type, a file path or a plan path — ids are resolved against
  tables the desktop itself minted;
* a remote ``compose`` always goes to the agent unless the device is ``full``, because the
  composer's own routing would otherwise let an ``agent`` device run a shell command directly;
* pairing shows the same five-digit code on both screens, derived from the handshake hash, so
  approving a device is not just approving a name the device chose for itself.

docs/REMOTE-PROTOCOL.md sections 4 to 7.
"""
from __future__ import annotations

import asyncio
import base64
import contextlib
import hmac
import json
import logging
import time
from dataclasses import dataclass
from hashlib import sha256
from typing import Awaitable, Callable

from . import envelope, identity as identity_mod, noise, pairing, panes as panes_mod, wire, ws

log = logging.getLogger("relay.host")

ENC_JSON = 0
ENC_CBOR = 1
HANDSHAKE_TIMEOUT = 20.0
IDLE_TIMEOUT = 15 * 60
MAX_VOICE_BYTES = 700_000          # what fits a 1 MiB frame once base64 has had its say

# Per-device inbound budget: (messages, seconds). Everything not named gets DEFAULT_LIMIT.
LIMITS = {
    "compose": (20, 60),
    "voice": (10, 60),
    "history_get": (120, 60),
    "tool_output_get": (60, 60),
    "turn_transcript_get": (60, 60),
    "plan_execute": (10, 60),
    "pair_prove": (5, 60),
}
DEFAULT_LIMIT = (240, 60)


def auth_code(handshake_hash: bytes) -> str:
    """The five digits shown on both screens at pairing.

    Derived from the handshake hash, which neither side controls alone, so a device that talked to
    an impostor shows a different number from the one the desktop is displaying.
    """
    digest = sha256(b"RRP/1 pairing auth" + handshake_hash).digest()
    return f"{int.from_bytes(digest[:4], 'big') % 100000:05d}"


@dataclass
class PairRequest:
    name: str
    platform: str
    fingerprint: str
    code: str
    peer: str


Approver = Callable[[PairRequest], Awaitable[tuple[bool, str]]]


async def approve_nothing(request: PairRequest) -> tuple[bool, str]:
    return False, wire.VIEW


class Limiter:
    def __init__(self):
        self.hits: dict[tuple[str, str], list[float]] = {}

    def allow(self, who: str, kind: str) -> bool:
        count, window = LIMITS.get(kind, DEFAULT_LIMIT)
        now = time.monotonic()
        recent = [at for at in self.hits.get((who, kind), []) if at > now - window]
        if len(recent) >= count:
            self.hits[(who, kind)] = recent
            return False
        recent.append(now)
        self.hits[(who, kind)] = recent
        return True


class Channel:
    """One client: a Noise session, a subscription set and whatever it is allowed to do."""

    def __init__(self, host: "Host", channel_id: bytes, meta: dict):
        self.host = host
        self.id = channel_id
        self.meta = meta
        self.room = meta.get("room") or ""
        self.peer = meta.get("peer", "?")
        self.inbox: asyncio.Queue[bytes | None] = asyncio.Queue(maxsize=64)
        self.session: noise.Session | None = None
        self.device_id: str | None = None
        self.client_static: bytes | None = None
        self.subscribed: set[str] = set()
        self.visible = True
        self.dedupe = wire.Deduplicator()
        # Encrypting and writing must happen as one step: a Noise cipherstate is a counter, so two
        # concurrent senders can encrypt in one order and reach the socket in another, and the
        # peer then sees a nonce gap and drops the session. Fan-out makes that the common case.
        self.sending = asyncio.Lock()
        self.opened = time.monotonic()
        self.last_seen = time.monotonic()
        self.closed = False

    # ---- device and capability -----------------------------------------------------------------

    @property
    def device(self) -> identity_mod.Device | None:
        """Always read through to the store: a revoke must not be shadowed by a cached copy."""
        if self.device_id is None:
            return None
        return self.host.devices.get(self.device_id)

    def capability(self) -> str | None:
        device = self.device
        return device.capability if device else None

    # ---- transport -------------------------------------------------------------------------

    async def send(self, message: dict) -> None:
        if self.session is None or self.closed:
            return
        payload = bytes([ENC_JSON]) + wire.encode(message)
        async with self.sending:
            if self.session is None or self.closed:
                return
            await self.host._send_envelope(envelope.KIND_DATA, self.id,
                                           self.session.encrypt(payload))

    async def close(self, reason: str = "") -> None:
        if self.closed:
            return
        self.closed = True
        with contextlib.suppress(Exception):
            await self.host._send_envelope(envelope.KIND_CLOSE, self.id,
                                           json.dumps({"reason": reason}).encode())
        await self.inbox.put(None)

    # ---- the channel's life ------------------------------------------------------------------

    async def run(self) -> None:
        try:
            await asyncio.wait_for(self._handshake(), HANDSHAKE_TIMEOUT)
        except asyncio.TimeoutError:
            log.info("channel %s: handshake timed out", self.id.hex()[:8])
            await self.close("handshake timed out")
            return
        except (noise.NoiseError, wire.WireError) as error:
            log.info("channel %s: handshake refused (%s)", self.id.hex()[:8], error)
            await self.close("handshake refused")
            return
        try:
            await self._messages()
        except (noise.NoiseError, wire.WireError) as error:
            log.info("channel %s: dropped (%s)", self.id.hex()[:8], error)
        finally:
            await self.close("done")

    async def _handshake(self) -> None:
        first = await self.inbox.get()
        if first is None:
            raise wire.WireError("internal", "channel closed before the handshake.")
        responder = noise.Responder(self.host.identity.private)
        responder.read_message_1(first)
        self.client_static = responder.client_static

        if not self.room:
            device = self.host.devices.by_key(self.client_static)
            if device is None:
                # Unknown or revoked key on a non-pairing channel: nothing to talk about.
                raise wire.WireError("not_permitted", "this device is not paired.")
            self.device_id = device.device_id
        message2, session = responder.write_message_2(b"")
        await self.host._send_envelope(envelope.KIND_DATA, self.id, message2)
        self.session = session
        if self.device_id:
            self.host.devices.touch(self.device_id)

    async def _messages(self) -> None:
        while not self.closed:
            try:
                frame = await asyncio.wait_for(self.inbox.get(), IDLE_TIMEOUT)
            except asyncio.TimeoutError:
                await self.close("idle")
                return
            if frame is None:
                return
            plaintext = self.session.decrypt(frame)
            if not plaintext:
                continue
            if plaintext[0] != ENC_JSON:
                # RRP/1 clients speak JSON. Refusing CBOR keeps that decoder away from the wire.
                raise wire.WireError("unknown_type", "clients must use the JSON encoding.")
            message = wire.decode(plaintext[1:])
            self.last_seen = time.monotonic()
            await self._dispatch(message)

    async def _dispatch(self, message: dict) -> None:
        kind = message["t"]
        request_id = message.get("id")
        if kind in wire.NEVER_FROM_CLIENT:
            await self.send(wire.error("not_permitted", "that is never accepted remotely.", request_id))
            return
        if kind not in wire.CLIENT_TYPES:
            await self.send(wire.error("unknown_type", f"unknown message type {kind!r}.", request_id))
            return
        who = self.device_id or self.id.hex()
        if not self.host.limiter.allow(who, kind):
            await self.send(wire.error("rate_limited", "too many of those; slow down.", request_id))
            return
        if not self.dedupe.fresh(message.get("msg_id")):
            return                                  # already applied; at most once

        needed = wire.CLIENT_TYPES[kind]
        if needed is not None:
            capability = self.capability()
            if capability is None:
                await self.send(wire.error("not_permitted", "this device is not paired.", request_id))
                await self.close("not paired")
                return
            if not wire.allows(capability, needed):
                await self.send(wire.error("not_permitted",
                                           f"this device is paired for {capability}.", request_id))
                return
        try:
            await self.host.handle(self, kind, message)
        except wire.WireError as error:
            await self.send(wire.error(error.code, error.message, request_id))


class Host:
    """The RemoteHub: rendezvous connection, channels, streams and the rules."""

    def __init__(self, identity: identity_mod.Identity, devices: identity_mod.DeviceStore,
                 source: panes_mod.PaneSource, *, app_base: str,
                 approver: Approver = approve_nothing, name: str = "this desktop"):
        self.identity = identity
        self.devices = devices
        self.source = source
        self.app_base = app_base
        self.approver = approver
        self.name = name
        self.limiter = Limiter()
        self.rooms = pairing.RoomBook()
        self.channels: dict[bytes, Channel] = {}
        self.streams: dict[str, wire.Stream] = {}
        self.epoch = base64.urlsafe_b64encode(time.time().hex().encode()).decode().rstrip("=")
        self.socket: ws.WebSocket | None = None
        self.token: str | None = None
        self.rendezvous: str | None = None
        self._tasks: set[asyncio.Task] = set()
        self._running = False
        self.devices.on_revoke(self._device_changed)
        self.source.on_panes(self._panes_changed)
        self.source.on_agent(self._agent_event)
        # A source that can produce screen state opts in; an agent-only one simply does not have it.
        self.screens = hasattr(self.source, "on_screen")
        if self.screens:
            self.source.on_screen(self._screen_event)

    # ---- registration and the rendezvous link --------------------------------------------------

    async def register(self, rendezvous_base: str) -> str:
        """Prove we hold the identity key, then take a bearer token."""
        self.rendezvous = rendezvous_base.rstrip("/")
        body = await self._post("/v1/challenge", {})
        shared = noise.dh(self.identity.private, base64.b64decode(body["ephemeral_public"]))
        proof = hmac.new(shared, body["challenge"].encode(), sha256).hexdigest()
        reply = await self._post("/v1/register", {
            "static_pubkey": base64.b64encode(self.identity.public).decode(),
            "challenge": body["challenge"], "proof": proof})
        self.token = reply["token"]
        if reply["desktop_id"] != self.identity.desktop_id:
            raise wire.WireError("internal", "the rendezvous derived a different desktop id.")
        return reply["desktop_id"]

    async def _post(self, path: str, payload: dict) -> dict:
        import urllib.error
        import urllib.request
        url = f"{self.rendezvous}{path}"

        def go() -> dict:
            request = urllib.request.Request(
                url, data=json.dumps(payload).encode(),
                headers={"Content-Type": "application/json"}, method="POST")
            try:
                with urllib.request.urlopen(request, timeout=20) as response:
                    return json.loads(response.read())
            except urllib.error.HTTPError as error:
                detail = error.read().decode("utf-8", "replace")[:200]
                raise wire.WireError("internal", f"{path} failed: {error.code} {detail}") from error
        return await asyncio.to_thread(go)

    def socket_url(self) -> str:
        base = self.rendezvous.replace("https://", "wss://").replace("http://", "ws://")
        return f"{base}/v1/connect?desktop={self.identity.desktop_id}&token={self.token}"

    async def serve(self) -> None:
        """Hold the rendezvous socket open, reconnecting until ``stop`` is called."""
        self._running = True
        delay = 1.0
        while self._running:
            try:
                self.socket = await ws.connect(self.socket_url())
                log.info("connected to the rendezvous as %s", self.identity.desktop_id[:8])
                delay = 1.0
                await self._read_socket()
            except (ws.WebSocketError, OSError) as error:
                log.info("rendezvous link down (%s); retrying in %.0fs", error, delay)
            finally:
                self.socket = None
                for channel in list(self.channels.values()):
                    await channel.close("the link went down")
                self.channels.clear()
            if not self._running:
                break
            await asyncio.sleep(delay)
            delay = min(delay * 2, 30.0)

    async def stop(self) -> None:
        self._running = False
        for channel in list(self.channels.values()):
            await channel.close("shutting down")
        if self.socket is not None:
            await self.socket.close()
        for task in list(self._tasks):
            task.cancel()

    async def _read_socket(self) -> None:
        while True:
            message = await self.socket.recv()
            if isinstance(message, str):
                continue
            try:
                frame = envelope.unpack(message)
            except envelope.EnvelopeError as error:
                log.info("bad envelope from the rendezvous: %s", error)
                continue
            if frame.kind == envelope.KIND_OPEN:
                await self._open_channel(frame)
            elif frame.kind == envelope.KIND_CLOSE:
                channel = self.channels.pop(frame.channel, None)
                if channel:
                    await channel.close("the client went away")
            elif frame.kind == envelope.KIND_DATA:
                channel = self.channels.get(frame.channel)
                if channel is None:
                    continue
                try:
                    channel.inbox.put_nowait(frame.payload)
                except asyncio.QueueFull:
                    await channel.close("too far behind")
                    self.channels.pop(frame.channel, None)

    async def _open_channel(self, frame: envelope.Envelope) -> None:
        try:
            meta = frame.meta()
        except envelope.EnvelopeError:
            meta = {}
        channel = Channel(self, frame.channel, meta)
        self.channels[frame.channel] = channel
        task = asyncio.create_task(self._run_channel(channel))
        self._tasks.add(task)
        task.add_done_callback(self._tasks.discard)

    async def _run_channel(self, channel: Channel) -> None:
        try:
            await channel.run()
        finally:
            self.channels.pop(channel.id, None)
            if self.screens and channel.device_id:
                self.source.release_device(channel.device_id)

    async def _send_envelope(self, kind: int, channel: bytes, payload: bytes) -> None:
        if self.socket is None:
            raise wire.WireError("internal", "the rendezvous link is down.")
        await self.socket.send(envelope.pack(kind, channel, payload))

    # ---- pairing -------------------------------------------------------------------------------

    async def open_pairing(self, ttl: float = pairing.ROOM_TTL) -> tuple[str, pairing.Room]:
        """Ask for a room, mint a secret, and return the URL for the QR code."""
        reply = await self._post("/v1/rooms", {"desktop_id": self.identity.desktop_id,
                                               "token": self.token, "ttl": ttl})
        room = self.rooms.open(reply["room"], ttl=min(ttl, reply.get("expires_in", ttl)))
        return pairing.pair_url(self.app_base, self.identity.public, room.secret, room.room), room

    # ---- streams -------------------------------------------------------------------------------

    def stream(self, name: str, limit: int = 2000) -> wire.Stream:
        if name not in self.streams:
            self.streams[name] = wire.Stream(name, limit=limit)
        return self.streams[name]

    def _panes_changed(self) -> None:
        message = self.stream("panes", limit=64).add({"t": "panes", "items": self.source.snapshot()})
        self._fan_out(message, needed=wire.VIEW)

    def _screen_event(self, pane: str, message: dict) -> None:
        # Screen frames are large and only interesting to whoever is looking at that pane, so they
        # are not kept in a long ring: a client that falls behind asks for a fresh snapshot.
        stream = self.stream(f"screen:{pane}", limit=8)
        self._fan_out(stream.add(message), needed=wire.VIEW, pane=pane)

    def _agent_event(self, pane: str, event: dict) -> None:
        name = event.get("event", "")
        if not wire.may_forward(name):
            return                      # denied by default; see remote/wire.py
        message = self.stream(f"agent:{pane}").add(
            {"t": "agent", "pane": pane, "event": self._scrub(event)})
        self._fan_out(message, needed=wire.VIEW, pane=pane)

    @staticmethod
    def _scrub(event: dict) -> dict:
        """Worker errors interpolate local paths; a phone gets the shape, not the filesystem."""
        if event.get("event") == "error" and isinstance(event.get("message"), str):
            event = dict(event)
            event["message"] = event["message"].split(":")[0][:200]
        return event

    def _fan_out(self, message: dict, *, needed: str, pane: str | None = None) -> None:
        """Send to every channel allowed to see it. Capability is re-read per channel, so a
        downgrade or a revoke that happened a moment ago takes effect on this very message."""
        for channel in list(self.channels.values()):
            capability = channel.capability()
            if capability is None or not wire.allows(capability, needed):
                continue
            if pane is not None and pane not in channel.subscribed:
                continue
            self._spawn(channel.send(message))

    def _spawn(self, coroutine) -> None:
        task = asyncio.create_task(coroutine)
        self._tasks.add(task)
        task.add_done_callback(self._tasks.discard)

    def _device_changed(self, device_id: str) -> None:
        """A revoke or a downgrade must reach live sessions now, not at the next handshake."""
        device = self.devices.devices.get(device_id)
        for channel in list(self.channels.values()):
            if channel.device_id != device_id:
                continue
            if device is None or device.revoked:
                if self.screens:
                    self.source.release_device(device_id)
                self._spawn(self._revoke_channel(channel))

    async def _revoke_channel(self, channel: Channel) -> None:
        with contextlib.suppress(Exception):
            await channel.send({"t": "revoked", "reason": "this device was revoked"})
        await channel.close("revoked")

    # ---- message handling ----------------------------------------------------------------------

    async def handle(self, channel: Channel, kind: str, message: dict) -> None:
        handler = getattr(self, f"_on_{kind}", None)
        if handler is None:
            await channel.send(wire.error("unknown_type", f"{kind} is not implemented yet.",
                                          message.get("id")))
            return
        await handler(channel, message)

    # -- session ---------------------------------------------------------------------------------

    async def _on_hello(self, channel: Channel, message: dict) -> None:
        if channel.device_id is None:
            await channel.send(wire.error("not_permitted", "pair first."))
            return
        device = channel.device
        await channel.send({
            "t": "welcome",
            "desktop": {"id": self.identity.desktop_id, "name": self.name,
                        "fingerprint": self.identity.fingerprint},
            "proto": wire.PROTOCOL_VERSION,
            "capability": device.capability,
            "password_entry": device.password_entry,
            "hub_epoch": self.epoch,
            "features": (["panes", "agent", "compose", "voice"]
                         + (["screen", "takeover"] if self.screens else [])),
            "server_time": time.time(),
        })
        await channel.send(self.stream("panes", limit=64).add(
            {"t": "panes", "items": self.source.snapshot()}))

    async def _on_ping(self, channel: Channel, message: dict) -> None:
        await channel.send({"t": "pong", "at": message.get("at"), "server_time": time.time()})

    async def _on_pong(self, channel: Channel, message: dict) -> None:
        return

    async def _on_bye(self, channel: Channel, message: dict) -> None:
        await channel.close("the client said goodbye")

    async def _on_client_state(self, channel: Channel, message: dict) -> None:
        channel.visible = bool(message.get("visible", True))

    async def _on_resume(self, channel: Channel, message: dict) -> None:
        wanted = message.get("streams")
        if not isinstance(wanted, dict):
            raise wire.WireError("unknown_type", "resume needs a streams object.")
        if message.get("hub_epoch") not in (None, self.epoch):
            await channel.send({"t": "resumed", "streams": {}, "hub_epoch": self.epoch,
                                "restart": True})
            return
        replayed: dict[str, int] = {}
        for name, seq in list(wanted.items())[:64]:
            stream = self.streams.get(str(name))
            if stream is None or not isinstance(seq, int):
                continue
            missed = stream.since(seq)
            if missed is None:
                replayed[str(name)] = -1        # too old: the client must take a fresh snapshot
                continue
            replayed[str(name)] = len(missed)
            for item in missed:
                await channel.send(item)
        await channel.send({"t": "resumed", "streams": replayed, "hub_epoch": self.epoch})

    # -- pairing ---------------------------------------------------------------------------------

    async def _on_pair_prove(self, channel: Channel, message: dict) -> None:
        if not channel.room:
            raise wire.WireError("not_permitted", "this channel is not a pairing channel.")
        room = self.rooms.get(channel.room)
        if room is None:
            raise wire.WireError("not_permitted", "that pairing code has expired.")
        secret = message.get("secret", "")
        try:
            offered = pairing.un64(secret) if isinstance(secret, str) else b""
        except Exception:
            offered = b""
        if not room.check(offered):
            self.rooms.burn(channel.room)
            raise wire.WireError("not_permitted", "that pairing code is wrong or spent.")
        self.rooms.burn(channel.room)

        code = auth_code(channel.session.handshake_hash)
        request = PairRequest(name=identity_mod.clean_label(message.get("name", "")),
                              platform=identity_mod.clean_label(message.get("platform", ""), 24),
                              fingerprint=pairing.fingerprint(channel.client_static),
                              code=code, peer=channel.peer)
        allowed, capability = await self.approver(request)
        if not allowed or capability not in wire.CAPABILITIES:
            raise wire.WireError("not_permitted", "the desktop refused this device.")
        device = self.devices.pair(channel.client_static, request.name, request.platform, capability)
        channel.device_id = device.device_id
        log.info("paired %s (%s) as %s", device.name, device.fingerprint, capability)
        await channel.send({"t": "paired", "device_id": device.device_id,
                            "capability": capability, "code": code,
                            "desktop": {"id": self.identity.desktop_id, "name": self.name,
                                        "fingerprint": self.identity.fingerprint}})

    # -- panes -----------------------------------------------------------------------------------

    async def _on_panes_get(self, channel: Channel, message: dict) -> None:
        await channel.send({"t": "panes", "items": self.source.snapshot(),
                            "seq": self.stream("panes", limit=64).seq})

    def _pane_of(self, message: dict) -> str:
        pane = message.get("pane")
        if not isinstance(pane, str) or not self.source.has_pane(pane):
            raise wire.WireError("no_such_pane", "no such pane.")
        return pane

    async def _on_pane_focus(self, channel: Channel, message: dict) -> None:
        pane = self._pane_of(message)
        channel.subscribed.add(pane)
        stream = self.stream(f"agent:{pane}")
        for item in list(stream.ring)[-40:]:
            await channel.send(item)
        if self.screens:
            await channel.send(self.source.screen_snapshot(pane))

    async def _on_pane_blur(self, channel: Channel, message: dict) -> None:
        pane = message.get("pane", "")
        channel.subscribed.discard(pane)
        if self.screens and channel.device_id:
            self.source.release(pane, channel.device_id)

    # -- agent -----------------------------------------------------------------------------------

    async def _on_compose(self, channel: Channel, message: dict) -> None:
        pane = self._pane_of(message)
        text = message.get("text")
        if not isinstance(text, str) or not text.strip():
            raise wire.WireError("unknown_type", "compose needs text.")
        if len(text) > 32_000:
            raise wire.WireError("unknown_type", "that prompt is too long.")
        when = message.get("when", "now")
        if when not in ("now", "queue"):
            raise wire.WireError("unknown_type", 'when must be "now" or "queue".')
        # A remote prompt goes to the agent unless the device is trusted with the shell. The
        # composer's own routing would otherwise run `git push --force` for an `agent` device.
        capability = channel.capability()
        to_agent = True
        if message.get("agent") is False:
            if capability != wire.FULL:
                raise wire.WireError("not_permitted",
                                     "only a full device may send a line straight to the shell.")
            to_agent = False
        await self.source.compose(pane, text, to_agent=to_agent, when=when,
                                  origin=f"remote:{channel.device_id}")

    async def _on_agent_stop(self, channel: Channel, message: dict) -> None:
        await self.source.agent_stop(self._pane_of(message))

    async def _on_queue_remove(self, channel: Channel, message: dict) -> None:
        item = message.get("item_id")
        await self.source.queue_remove(self._pane_of(message),
                                       item if isinstance(item, str) else "")

    async def _on_recap_request(self, channel: Channel, message: dict) -> None:
        await self.source.recap_request(self._pane_of(message))

    async def _on_plan_execute(self, channel: Channel, message: dict) -> None:
        plan_id = message.get("plan_id")
        if not isinstance(plan_id, str) or not plan_id.isalnum() or len(plan_id) > 32:
            # Never a path: the id must be one this desktop minted in a plan_written event.
            raise wire.WireError("unknown_type", "plan_execute needs a plan_id.")
        await self.source.plan_execute(self._pane_of(message), plan_id,
                                       origin=f"remote:{channel.device_id}")

    async def _on_voice(self, channel: Channel, message: dict) -> None:
        pane = self._pane_of(message)
        audio_format = message.get("format", "webm")
        if audio_format not in ("webm", "ogg", "wav", "mp3", "m4a"):
            raise wire.WireError("unknown_type", "unsupported audio format.")
        audio = wire.decode_bytes(message.get("data"), MAX_VOICE_BYTES * 4 // 3 + 8, "the clip")
        text = await self.source.transcribe(pane, audio, audio_format)
        await channel.send({"t": "agent", "pane": pane,
                            "event": {"event": "transcribed", "text": text},
                            "id": message.get("id")})

    async def _on_turn_transcript_get(self, channel: Channel, message: dict) -> None:
        pane = self._pane_of(message)
        turn = message.get("turn_id")
        payload = self.source.turn_transcript(pane, turn if isinstance(turn, str) else "")
        await channel.send({"t": "agent", "pane": pane, "id": message.get("id"),
                            "event": {"event": "turn_transcript", "turn": payload}})

    async def _on_tool_output_get(self, channel: Channel, message: dict) -> None:
        pane = self._pane_of(message)
        tool = message.get("tool_id")
        payload = self.source.tool_output(pane, tool if isinstance(tool, str) else "")
        await channel.send({"t": "agent", "pane": pane, "id": message.get("id"),
                            "event": {"event": "tool_output", "output": payload}})

    # -- take over ---------------------------------------------------------------------------------
    # All of these need `full`, which wire.CLIENT_TYPES enforces before we are called. The source
    # refuses them again if the pane is at a password prompt, read fresh from the tty.

    def _typing_pane(self, channel: Channel, message: dict) -> str:
        if not self.screens:
            raise wire.WireError("not_permitted", "this desktop is not sharing a terminal.")
        return self._pane_of(message)

    async def _on_keys(self, channel: Channel, message: dict) -> None:
        pane = self._typing_pane(channel, message)
        raw = wire.decode_bytes(message.get("bytes"), 8192, "keys")
        await self.source.send_keys(pane, raw, device=channel.device_id)

    async def _on_line(self, channel: Channel, message: dict) -> None:
        pane = self._typing_pane(channel, message)
        text = message.get("text")
        if not isinstance(text, str) or len(text) > 4096:
            raise wire.WireError("unknown_type", "line needs text.")
        await self.source.send_line(pane, text, device=channel.device_id)

    async def _on_paste(self, channel: Channel, message: dict) -> None:
        pane = self._typing_pane(channel, message)
        text = message.get("text")
        if not isinstance(text, str) or len(text) > 64_000:
            raise wire.WireError("unknown_type", "paste needs text.")
        await self.source.paste(pane, text, device=channel.device_id)

    async def _on_control_request(self, channel: Channel, message: dict) -> None:
        pane = self._typing_pane(channel, message)
        await self.source.send_keys(pane, b"", device=channel.device_id)
        await channel.send({"t": "agent", "pane": pane,
                            "event": {"event": "status", "text": "You have the keyboard."}})

    async def _on_control_release(self, channel: Channel, message: dict) -> None:
        pane = self._typing_pane(channel, message)
        self.source.release(pane, channel.device_id)

    async def _on_screen_get(self, channel: Channel, message: dict) -> None:
        pane = self._typing_pane(channel, message)
        await channel.send(self.source.screen_snapshot(pane))

    # -- not in P1 ---------------------------------------------------------------------------------

    async def _on_history_get(self, channel: Channel, message: dict) -> None:
        # Needs a const VtCore::historyLines in both cores; see the design doc section 12.1.
        raise wire.WireError("not_permitted", "scrollback paging is not implemented yet.")

    async def _on_secret_input(self, channel: Channel, message: dict) -> None:
        # Deliberately refused until the desktop can re-read termios at write time and mint a
        # single-use nonce bound to the prompt (docs/REMOTE-PROTOCOL.md section 6.7).
        raise wire.WireError("not_permitted", "password entry from a phone is not implemented.")
