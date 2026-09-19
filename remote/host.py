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
import secrets
import time
from dataclasses import dataclass
from hashlib import sha256
from typing import Awaitable, Callable

from . import audit as audit_mod, control as control_mod, envelope, guests as guests_mod, \
    identity as identity_mod, meetcode, noise, notify as notify_mod, pairing, panes as panes_mod, \
    push as push_mod, wire, ws

log = logging.getLogger("relay.host")

ENC_JSON = 0
ENC_CBOR = 1
HANDSHAKE_TIMEOUT = 20.0
IDLE_TIMEOUT = 15 * 60
MAX_VOICE_BYTES = 700_000          # what fits a 1 MiB frame once base64 has had its say
SECRET_NONCE_TTL = 45.0            # a password nonce is minted to be used now, not remembered
MAX_SECRET_BYTES = 1024
MAX_HISTORY_ROWS = 200             # section 6.5: one page, so a phone cannot ask for the world
DEFAULT_HISTORY_ROWS = 60
# Section 10.5, "only while I am present": how long the desktop window may be unfocused before the
# share behaves as paused. Short enough to mean something, long enough that alt-tabbing to read a
# stack trace does not take the keyboard off whoever is typing.
PRESENCE_GRACE = 20.0
HOUSEKEEPING = 1.0                 # how often lapsed prompts and control requests are swept

# Per-device inbound budget: (messages, seconds). Everything not named gets DEFAULT_LIMIT.
LIMITS = {
    "compose": (20, 60),
    "voice": (10, 60),
    "history_get": (120, 60),
    "tool_output_get": (60, 60),
    "turn_transcript_get": (60, 60),
    "plan_execute": (10, 60),
    "pair_prove": (5, 60),
    "secret_input": (10, 60),        # section 6.7: rate-limited so a full device cannot brute-force
    "push_subscribe": (10, 60),      # a phone subscribes once and then only when the key rotates
    "push_unsubscribe": (10, 60),
    "knock": (5, 60),                # per channel; the per-invite budget is in guests.py
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


@dataclass
class KnockRequest:
    """Someone at the door with an invite link (section 10.2).

    ``participant`` is the id they will keep if the owner lets them in, minted now so the audit
    log's `knock`, `admitted` and `join` lines all name the same person. ``role`` is what the
    invite offers; the owner may answer with a lower one and never a higher.
    """
    participant: str
    name: str
    platform: str
    fingerprint: str
    code: str
    peer: str
    role: str
    panes: list[str]
    invite: str


KnockApprover = Callable[[KnockRequest], Awaitable[tuple[bool, str]]]


async def admit_nobody(request: KnockRequest) -> tuple[bool, str]:
    """The default: a hub nobody wired an owner to admits no guests at all."""
    return False, wire.VIEWER


@dataclass
class PromptRequest:
    """A guest's prompt waiting for the owner (section 10.4).

    The owner sees the name, not only the id, and the **whole** text: approving a prompt is
    approving everything the agent will then do with the owner's keys, so a preview would be the
    wrong thing to decide on.
    """
    prompt_id: str
    participant: str
    name: str
    pane: str
    text: str
    when: str = "now"
    plan_id: str = ""


PromptApprover = Callable[[PromptRequest], Awaitable[bool]]


async def approve_no_prompts(request: PromptRequest) -> bool:
    """The default. A hub with nobody to ask refuses rather than runs: the whole of 10.4 is that
    a guest's words reach the agent only because a person said so."""
    return False


@dataclass
class ControlRequest:
    """An editor asking for the keyboard (section 10.3)."""
    pane: str
    participant: str
    name: str


ControlApprover = Callable[[ControlRequest], Awaitable[bool]]


async def grant_control_to_nobody(request: ControlRequest) -> bool:
    return False


@dataclass
class ShareOptions:
    """The per-share switches of section 10.5, per pane.

    Both are off by default, and both are the owner's alone: they arrive as the `share_options`
    sidecar line, which is in ``wire.OWNER_ONLY``.
    """
    prompts_immediate: bool = False     # 10.4: a guest's prompt runs without being asked about
    present_only: bool = False          # 10.5: an unfocused desktop window behaves as a pause


@dataclass
class SecretNonce:
    """One password prompt's permit: single use, seconds to live, bound to the asking process."""
    value: str
    pane: str
    foreground_pid: int
    generation: int
    expires: float


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
        # A channel belongs to a device **or** to a participant, never both: the handshake looks
        # the static key up in the device store first, and `GuestStore.admit` refuses a key the
        # device store already knows. `who()` is the one name the audit log and the rate limiter
        # use, so a participant is never counted or recorded as a device.
        self.device_id: str | None = None
        self.participant_id: str | None = None
        self.stale_guest = False            # a key we pinned once, whose access has since ended
        self.client_static: bytes | None = None
        self.subscribed: set[str] = set()
        self.visible = True
        self.dedupe = wire.Deduplicator()
        # Encrypting and writing must happen as one step: a Noise cipherstate is a counter, so two
        # concurrent senders can encrypt in one order and reach the socket in another, and the
        # peer then sees a nonce gap and drops the session. Fan-out makes that the common case.
        self.sending = asyncio.Lock()
        self.recv_count = 0                 # frames decrypted: the transport_switch cursor
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

    @property
    def participant(self) -> guests_mod.Participant | None:
        """The live participant record, read through the store on every use, exactly as `device`
        is: a removed guest, an expired one or an ended share must land on this message and not
        at the next handshake. Returns None for an expired or removed record."""
        if self.participant_id is None:
            return None
        return self.host.guests.participant(self.participant_id)

    def role(self) -> str | None:
        participant = self.participant
        return participant.role if participant else None

    def who(self) -> str:
        return self.device_id or self.participant_id or self.id.hex()

    # ---- transport -------------------------------------------------------------------------

    async def send(self, message: dict) -> None:
        if self.session is None or self.closed:
            return
        if self.participant_id is not None:
            # **The** outbound enforcement point for a participant. Every message on its way to a
            # guest — a fan-out, a replay, a direct reply from a handler — passes through here, so
            # a pane they are not on, or an event a guest may not see, cannot leak from anywhere
            # upstream. The scope is read from the live record, never from what was true at join.
            participant = self.participant
            if participant is None:
                # Removed, expired, or the share ended. Nothing about the desktop may still leave;
                # the goodbye that says so must.
                if message.get("t") not in ("bye", "error"):
                    return
            else:
                scoped = self.host.guest_view(participant, message)
                if scoped is None:
                    return
                message = scoped
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
        if self.stale_guest:
            with contextlib.suppress(Exception):
                await self.send({"t": "bye", "reason": "this share has ended, or your access "
                                                       "expired.", "discard": True})
            await self.close("no longer a participant")
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
            # Devices first, then participants, and never both: this order is what makes "a
            # participant record is never a device record" true at the one place that turns a key
            # into an identity (section 10.2).
            device = self.host.devices.by_key(self.client_static)
            if device is not None:
                self.device_id = device.device_id
            else:
                participant = self.host.guests.by_key(self.client_static)
                if participant is None:
                    # A key we pinned once and no longer honour gets the handshake and then a
                    # `bye` saying to discard the record: it is a key this desktop chose, so
                    # answering it leaks nothing, and a guest whose share ended should see that
                    # rather than a socket that closes for no stated reason. An unknown key still
                    # gets nothing at all.
                    if self.host.guests.any_by_key(self.client_static) is None:
                        raise wire.WireError("not_permitted", "this device is not paired.")
                    self.stale_guest = True
                else:
                    self.participant_id = participant.participant_id
        message2, session = responder.write_message_2(b"")
        await self.host._send_envelope(envelope.KIND_DATA, self.id, message2)
        self.session = session
        if self.device_id:
            self.host.devices.touch(self.device_id)
        if self.participant_id:
            self.host.guests.touch(self.participant_id)

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
            self.recv_count += 1
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
        if not self.host.limiter.allow(self.who(), kind):
            await self.send(wire.error("rate_limited", "too many of those; slow down.", request_id))
            return
        if not self.dedupe.fresh(message.get("msg_id")):
            return                                  # already applied; at most once

        if self.participant_id is not None:
            if not await self._guest_may(kind, message, request_id):
                return
            try:
                await self.host.handle(self, kind, message)
            except wire.WireError as error:
                await self.send(wire.error(error.code, error.message, request_id))
            return

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

    async def _guest_may(self, kind: str, message: dict, request_id) -> bool:
        """The inbound gate for a participant (section 10.1). False means it was answered already.

        Four checks, in this order, all against the **live** record:

        1. the record still exists — an expired, removed or ended guest is told and closed;
        2. the type is one section 10.1 names as never for a participant;
        3. the type is in ``GUEST_TYPES`` at or below this participant's role;
        4. any pane the message names is one the invite gave them.
        """
        participant = self.participant
        if participant is None:
            await self.send(wire.error("not_permitted",
                                       "this share has ended or your access expired.", request_id))
            await self.close("no longer a participant")
            return False
        if kind in wire.GUEST_NEVER:
            await self.send(wire.error("not_permitted",
                                       f"a guest never gets that: {wire.GUEST_NEVER[kind]}.",
                                       request_id))
            return False
        needed = wire.GUEST_TYPES.get(kind)
        if needed is None:
            await self.send(wire.error("not_permitted", "a guest may not send that.", request_id))
            return False
        if not wire.role_allows(participant.role, needed):
            await self.send(wire.error("not_permitted",
                                       f"you are a {participant.role} on this pane.", request_id))
            return False
        pane = message.get("pane")
        if isinstance(pane, str) and pane and not participant.may_see(pane):
            # Not `no_such_pane`: whether the desktop has that pane is not a guest's business.
            await self.send(wire.error("not_permitted", "that pane is not shared with you.",
                                       request_id))
            return False
        return True


class Host:
    """The RemoteHub: rendezvous connection, channels, streams and the rules."""

    def __init__(self, identity: identity_mod.Identity, devices: identity_mod.DeviceStore,
                 source: panes_mod.PaneSource, *, app_base: str,
                 approver: Approver = approve_nothing, name: str = "this desktop",
                 guests: guests_mod.GuestStore | None = None,
                 knock_approver: KnockApprover = admit_nobody,
                 prompt_approver: PromptApprover = approve_no_prompts,
                 control_approver: ControlApprover = grant_control_to_nobody,
                 clock: Callable[[], float] = time.monotonic):
        self.identity = identity
        self.devices = devices
        self.source = source
        self.app_base = app_base
        self.approver = approver
        self.name = name
        # Multiplayer (section 10). The guest store is given the device store so it can refuse to
        # pin a key that is already a paired device; the two lists never merge.
        self.guests = guests if guests is not None else \
            guests_mod.GuestStore(devices.directory, devices=devices)
        self.knock_approver = knock_approver
        # The two owner seams of 10.3 and 10.4, shaped exactly like `knock_approver`: a coroutine
        # the GUI (or the CLI, or a test double) answers. A hub nobody wired one to refuses.
        self.prompt_approver = prompt_approver
        self.control_approver = control_approver
        self.prompts = guests_mod.PromptQueue()
        self.controls = guests_mod.ControlQueue()
        # Who is driving each pane: **one** state across the owner's devices, the agent and the
        # participants (section 10.3, remote/control.py).
        self.control = control_mod.ControlBook(self._control_changed)
        # The desktop watches the same handoffs the phones are told about, so its own "alice is
        # typing" indicator cannot drift from theirs.
        self._control_watchers: list[Callable[[str, str, str], None]] = []
        self._state_watchers: list[Callable[[str, bool, str], None]] = []
        self.options: dict[str, ShareOptions] = {}      # per pane; "" is the default for all
        self.paused: set[str] = set()                   # panes the owner paused; "" is all of them
        self._share_state: dict[str, tuple[bool, str]] = {}   # what each pane was last told
        # Which tab each shared pane is in, for panes the owner shared as a whole tab. A guest
        # admitted to a tab gains the panes later added to it and loses the ones that leave.
        self.pane_tabs: dict[str, str] = {}
        # The presence rule of 10.5 reads the **same** `window_active` signal section 9's
        # notifications do; `window_active` below feeds both. The default differs on purpose: the
        # notifier assumes the window is *not* focused (a missed push is worse than a spare one),
        # and the hub assumes it is, because a hub nobody ever tells — `remote.cli share` — must
        # not sit permanently paused.
        self.clock = clock
        self._window_active = True
        self._away_since: float | None = None
        self._housekeeping: asyncio.Task | None = None
        self._in_control_change = False
        self.limiter = Limiter()
        self.audit = audit_mod.AuditLog(devices.directory)   # beside devices.json, 0700/0600
        # Notifications live in remote/notify.py; everything below the "---- push" line is the
        # seam between it and the hub (docs/REMOTE-PROTOCOL.md section 9).
        self.notifier = notify_mod.Notifier(devices, self.push_send, spawn=self._spawn)
        self.secret_nonces: dict[str, SecretNonce] = {}
        self._prompt_generation: dict[str, int] = {}
        self.rooms = pairing.RoomBook()
        # Meeting codes (card #97EG). A connection on a code room is a `meetcode.Attempt`, never a
        # Noise `Channel`; it sits in `channels` so routing and teardown reach it, as nobody.
        self.codes = meetcode.CodeBook(self)
        self.channels: dict[bytes, Channel] = {}
        self.streams: dict[str, wire.Stream] = {}
        self.epoch = base64.urlsafe_b64encode(time.time().hex().encode()).decode().rstrip("=")
        self.socket: ws.WebSocket | None = None
        self.token: str | None = None
        self.rendezvous: str | None = None
        self._rehomed = False
        # Per-device push origins (section 9). `home` is the first rendezvous this hub registered
        # with — the sidecar's own — and is what a device origin of "" means. `tokens` keeps a
        # token for every rendezvous registered with, so a phone subscribed under the local
        # server's VAPID key is still pushed through it while the hub sits at the hosted one.
        self.home: str | None = None
        self.tokens: dict[str, str] = {}
        self._push_unreachable: set[str] = set()
        self._tasks: set[asyncio.Task] = set()
        self._running = False
        self.devices.on_revoke(self._device_changed)
        self.guests.on_change(self._guest_changed)
        self.source.on_panes(self._panes_changed)
        self.source.on_agent(self._agent_event)
        # A source that can produce screen state opts in; an agent-only one simply does not have it.
        self.screens = hasattr(self.source, "on_screen")
        if self.screens:
            self.source.on_screen(self._screen_event)
        # Scrollback is the source's to answer, not the hub's: the bridge and the GUI both hold
        # the emulator that owns it, and an agent-only source has none. Advertised only when true.
        self.scrollback = bool(getattr(self.source, "scrollback", False))
        # pane_state (relay-terminal-71): only a source the GUI drives publishes it, so say so in
        # `welcome` rather than letting every client ask and be refused (section 16).
        self.pane_state = callable(getattr(self.source, "send", None))

    # ---- registration and the rendezvous link --------------------------------------------------

    async def register(self, rendezvous_base: str) -> str:
        """Prove we hold the identity key, then take a bearer token."""
        base = rendezvous_base.rstrip("/")
        self.token = await self._register_at(base)
        self.rendezvous = base
        self.tokens[base] = self.token
        if self.home is None:
            self.home = base
        return self.identity.desktop_id

    def origin(self) -> str:
        """What a device paired or subscribed right now records as its origin: "" at home."""
        return "" if self.rendezvous == self.home else (self.rendezvous or "")

    async def _register_at(self, base: str) -> str:
        """Challenge and register at ``base``; the token. Changes nothing on the hub, so a
        rendezvous that refuses leaves the one already in use untouched (see ``rehome``)."""
        body = await self._post("/v1/challenge", {}, base=base)
        shared = noise.dh(self.identity.private, base64.b64decode(body["ephemeral_public"]))
        proof = hmac.new(shared, body["challenge"].encode(), sha256).hexdigest()
        reply = await self._post("/v1/register", {
            "static_pubkey": base64.b64encode(self.identity.public).decode(),
            "challenge": body["challenge"], "proof": proof}, base=base)
        if reply["desktop_id"] != self.identity.desktop_id:
            raise wire.WireError("internal", "the rendezvous derived a different desktop id.")
        return reply["token"]

    async def rehome(self, rendezvous_base: str) -> None:
        """Move this one hub to another rendezvous: register there, then reconnect the socket.

        The sidecar's hosted address (remote/gui_host.py) uses this to go from the rendezvous it
        runs itself to the public one and back. Registration comes first, so a rendezvous that is
        down or refuses leaves everything as it was. Rooms, invites' rooms and meeting codes live
        at the rendezvous that minted them; a live code is ended as ``expired`` before the move,
        and burned at the old rendezvous with the old token, so nobody is left holding a code
        that resolves to a room this hub no longer listens on.
        """
        base = rendezvous_base.rstrip("/")
        if base == self.rendezvous:
            return
        token = await self._register_at(base)
        for record in [record for record in self.codes.records.values()
                       if record.state == meetcode.LIVE]:
            await self.codes.expire(record)
        self.rendezvous, self.token = base, token
        self.tokens[base] = token
        # Drop the socket; `serve` reconnects at once, to `socket_url()`, which is now the new one.
        self._rehomed = True
        if self.socket is not None:
            with contextlib.suppress(Exception):
                await self.socket.close()

    async def _post(self, path: str, payload: dict, *, base: str | None = None) -> dict:
        import urllib.error
        import urllib.request
        url = f"{base or self.rendezvous}{path}"

        def go() -> dict:
            request = urllib.request.Request(
                url, data=json.dumps(payload).encode(),
                headers={"Content-Type": "application/json", "User-Agent": ws.USER_AGENT}, method="POST")
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
        if self._housekeeping is None:
            # A parked prompt and a parked control request both lapse (10.4, 10.3) and somebody
            # has to notice; the queues' clocks are injected, so a test calls `expire_pending`
            # instead of waiting ten minutes.
            self._housekeeping = asyncio.create_task(self._housekeep())
        delay = 1.0
        while self._running:
            try:
                url = self.socket_url()
                self.socket = await ws.connect(url)
                if url != self.socket_url():
                    # ``rehome`` moved the hub while this was connecting to the old rendezvous.
                    await self.socket.close()
                    continue
                self._rehomed = False
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
            if self._rehomed:              # moved on purpose (``rehome``): no back-off
                self._rehomed = False
                continue
            await asyncio.sleep(delay)
            delay = min(delay * 2, 30.0)

    async def stop(self) -> None:
        self._running = False
        if self._housekeeping is not None:
            self._housekeeping.cancel()
            self._housekeeping = None
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
        if self.codes.is_code_room(meta.get("room") or ""):
            # A meeting code's room: CPace and a sealed invite, then close. It never reaches the
            # Noise handshake or any handler below, before or after the invite (remote/meetcode.py).
            channel = self.codes.attempt(frame.channel, meta)
        else:
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
            if channel.device_id:
                self.control.drop_device(channel.device_id)
            if self.screens and channel.device_id:
                self.source.release_device(channel.device_id)
            if channel.participant_id:
                await self._participant_left(channel)

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

    async def _open_room(self, ttl: float) -> tuple[str, float]:
        """A rendezvous room and the lifetime it was actually granted."""
        reply = await self._post("/v1/rooms", {"desktop_id": self.identity.desktop_id,
                                               "token": self.token, "ttl": ttl})
        return reply["room"], float(reply.get("expires_in", ttl))

    # ---- streams -------------------------------------------------------------------------------

    def stream(self, name: str, limit: int = 2000) -> wire.Stream:
        if name not in self.streams:
            self.streams[name] = wire.Stream(name, limit=limit)
        return self.streams[name]

    def _panes_changed(self) -> None:
        message = self.stream("panes", limit=64).add({"t": "panes", "items": self._items()})
        live = {item.get("id") for item in message["items"]}
        for pane in [p for p in self.control.holders if p not in live]:
            self.control.forget(pane)       # a pane that is gone is driven by nobody
        self._fan_out(message, needed=wire.VIEW)
        self.notifier.on_panes(message["items"])

    def _items(self) -> list[dict]:
        """The pane list, with a password nonce minted for any pane at a prompt (section 6.7).

        The nonce is desktop-minted, single use and bound to (pane, foreground pid, prompt
        generation); it expires in seconds and a spent or stale one is refused below. A replayed
        `panes` message carrying an old nonce is harmless for the same reason.
        """
        items = [dict(item) for item in self.source.snapshot()]
        for item in items:
            # One control state (section 10.3): the source's own answer is folded in — the agent
            # taking a program is the desktop's business and reaches us this way — and what every
            # client then reads is the book's, in section 6.3's spelling.
            pane = item.get("id", "")
            self.control.observe(pane, str(item.get("control") or "human"))
            item["control"] = self.control.field(pane)
        now = time.monotonic()
        self.secret_nonces = {pane: nonce for pane, nonce in self.secret_nonces.items()
                              if nonce.expires > now}
        for item in items:
            pane = item.get("id", "")
            state = self.source.secret_state(pane) if item.get("status") == "password" else None
            if state is None:
                # The prompt this pane's permit was minted for has ended. **Burn it**, and move
                # the generation on, so that the next prompt from the same process gets a permit
                # of its own: section 6.7 binds the nonce to a prompt generation, and a permit
                # that outlives its prompt binds only the pane. The attack is two prompts from one
                # process — ssh asking for a key passphrase and then for a password — where the
                # phone is still holding the first one's permit and would answer the second on the
                # strength of a decision the person made about the first.
                if self.secret_nonces.pop(pane, None) is not None:
                    self._prompt_generation[pane] = self._prompt_generation.get(pane, 0) + 1
                continue
            generation = self._prompt_generation.get(pane, 0)
            nonce = self.secret_nonces.get(pane)
            if nonce and nonce.generation == generation and nonce.foreground_pid == \
                    state.get("foreground_pid", 0) and nonce.expires > now:
                item["secret_nonce"] = nonce.value
                continue
            self._prompt_generation[pane] = generation + 1
            nonce = SecretNonce(value=secrets.token_urlsafe(16), pane=pane,
                                foreground_pid=state.get("foreground_pid", 0),
                                generation=generation + 1, expires=now + SECRET_NONCE_TTL)
            self.secret_nonces[pane] = nonce
            self.audit.record("prompt_detected", pane=pane,
                              foreground_pid=nonce.foreground_pid or None)
            item["secret_nonce"] = nonce.value
        return items

    def _screen_event(self, pane: str, message: dict) -> None:
        # Screen frames are large and only interesting to whoever is looking at that pane, so they
        # are not kept in a long ring: a client that falls behind asks for a fresh snapshot.
        stream = self.stream(f"screen:{pane}", limit=8)
        self._fan_out(stream.add(message), needed=wire.VIEW, pane=pane)

    def _agent_event(self, pane: str, event: dict) -> None:
        name = event.get("event", "")
        self.notifier.on_agent(pane, event)
        if not wire.may_forward(name):
            return                      # denied by default; see remote/wire.py
        message = self.stream(f"agent:{pane}").add(
            {"t": "agent", "pane": pane, "event": self._scrub(event)})
        # Most events are for anyone watching the pane; the ones naming other conversations are
        # the owner's level, the same rule pane_state's `sessions` block follows (section 16).
        self._fan_out(message, needed=wire.floor_for(name), pane=pane)

    @staticmethod
    def _scrub(event: dict) -> dict:
        """Worker errors interpolate local paths; a phone gets the shape, not the filesystem."""
        if event.get("event") == "error" and isinstance(event.get("message"), str):
            event = dict(event)
            event["message"] = event["message"].split(":")[0][:200]
        return event

    def _fan_out(self, message: dict, *, needed: str, pane: str | None = None) -> None:
        """Send to every channel allowed to see it. Capability — and, for a guest, the role and
        the pane scope — is re-read per channel, so a downgrade, a revoke or a removal that
        happened a moment ago takes effect on this very message."""
        for channel in list(self.channels.values()):
            if channel.participant_id is not None:
                participant = channel.participant
                if participant is None:
                    continue
                if pane is not None and not participant.may_see(pane):
                    continue
                if pane is not None and pane not in channel.subscribed:
                    continue
                # `Channel.send` filters again; this is the cheap early exit, not the rule.
                self._spawn(channel.send(message))
                continue
            capability = channel.capability()
            if capability is None or not wire.allows(capability, needed):
                continue
            if pane is not None and pane not in channel.subscribed:
                continue
            self._spawn(channel.send(message))

    # ---- what a participant is allowed to be sent (section 10.1) -------------------------------

    def guest_view(self, participant: guests_mod.Participant | None, message: dict) -> dict | None:
        """The form of ``message`` a participant may receive, or None to withhold it entirely.

        Called on **every** outbound message on a participant's channel (``Channel.send``), which
        is why the rules are here rather than at each sender:

        * the **type** is in ``GUEST_SERVER_TYPES`` — outbound is an allow-list too (section
          10.1). This used to be the other way round, everything passing unless it was named, and
          that made every desktop→client message somebody adds later a guest-visible leak on the
          day it lands rather than the day somebody decided it;
        * a `panes` list is cut down to the panes of their invite, and the desktop-minted password
          nonce is stripped — a guest is never offered the password field (section 10.3);
        * anything naming a pane they are not on is dropped;
        * an `agent` message is dropped unless its event is in ``GUEST_EVENTS``, which is a strict
          subset of what a device may see;
        * a queue event keeps only the prompts this guest wrote themselves: `queued` carries the
          text and `queue_changed` carries a preview of every waiting item, so a share with two
          editors would otherwise show each of them what the other asked for, and both of them
          what the owner is asking (section 10.4);
        * everything else — `welcome`, `admitted`, `participants`, `control`, `share_state`,
          `prompt_decided`, `error`, `pong`, `bye` — passes.
        """
        if participant is None:
            return None
        kind = message.get("t")
        if not wire.may_send_to_guest(kind if isinstance(kind, str) else ""):
            return None
        if kind == "panes":
            items = [{name: value for name, value in item.items() if name != "secret_nonce"}
                     for item in message.get("items", []) if participant.may_see(item.get("id"))]
            return {**message, "items": items}
        pane = message.get("pane")
        if isinstance(pane, str) and pane and not participant.may_see(pane):
            return None
        if kind == "control" and "device" in message:
            # Which of the owner's devices holds the pane is the owner's business: to a guest the
            # holder is "owner", with no device id in it (section 10.3).
            return {name: value for name, value in message.items() if name != "device"}
        if kind == "agent":
            event = (message.get("event") or {}).get("event", "")
            if not wire.may_forward_to_guest(event):
                return None
            scrubbed = self._guest_queue_view(participant, message.get("event") or {})
            if scrubbed is not message.get("event"):
                return {**message, "event": scrubbed}
        return message

    def _guest_queue_view(self, participant: guests_mod.Participant, event: dict) -> dict:
        """A queue event with other people's prompt text taken out, and the author named.

        The queue is the one guest-visible event that carries what somebody *typed*: `queued` has
        the prompt and `queue_changed` a preview of each waiting item. A guest may see that a row
        exists, whose it is and what happens to it — that is what makes a shared queue legible —
        but the words belong to whoever wrote them. Their own rows are left whole, matched on the
        `guest:<id>` origin the hub itself puts on an approved prompt.
        """
        name = event.get("event")
        if name not in ("queued", "queue_changed"):
            return event
        mine = f"guest:{participant.participant_id}"

        def author(origin) -> str:
            origin = origin if isinstance(origin, str) else ""
            if origin == mine:
                return "you"
            if origin.startswith("guest:"):
                other = self.guests.participants.get(origin.split(":", 1)[1])
                return other.name if other is not None else "a guest"
            return "the owner"

        # The fields that hold what somebody typed, whichever event they arrive on. `queued` used
        # to have its own shorter list than the rows below, which made the rule depend on which
        # field the producer happened to use: `backend/relay_core/queue.py` puts the words in
        # `preview` on a row and the demo source puts them in `text` on a `queued`, and a
        # producer that put a `preview` on a `queued` would have handed every guest the owner's
        # prompt. One list, applied in both places.
        words = ("text", "prompt", "preview", "label")
        if name == "queued":
            if event.get("origin") == mine:
                return {**event, "author": "you"}
            out = {key: value for key, value in event.items() if key not in words}
            out["author"] = author(event.get("origin"))
            return out
        out = dict(event)
        for field in ("items", "steering"):
            rows = event.get(field)
            if not isinstance(rows, list):
                continue
            kept = []
            for row in rows:
                if not isinstance(row, dict):
                    continue
                origin = row.get("origin")
                if origin == mine:
                    kept.append({**row, "author": "you"})
                    continue
                kept.append({key: value for key, value in row.items() if key not in words}
                            | {"author": author(origin)})
            out[field] = kept
        return out

    # ---- presence (section 10.3) ----------------------------------------------------------------

    def participants_on(self, pane: str) -> list[dict]:
        """Who is on a pane, shaped for the `participants` message (section 10.3).

        ``driving`` is read from the one control book, so it cannot disagree with the `control`
        message or with the pane item's ``control`` field — they are three spellings of one state.
        A participant who is connected right now is marked ``online``: the record outlives the
        socket, and presence is about the socket.
        """
        holder = self.control_holder(pane)
        online = {channel.participant_id for channel in self.channels.values()
                  if channel.participant_id and not channel.closed}
        return [{"id": p.participant_id, "name": p.name, "role": p.role,
                 "driving": holder == p.participant_id,
                 "online": p.participant_id in online}
                for p in self.guests.on_pane(pane)]

    def control_holder(self, pane: str) -> str | None:
        """The participant id currently driving ``pane``, or None for the owner or the agent."""
        return self.control.participant(pane)

    def holder_name(self, pane: str) -> str:
        """What to show beside `control`: the desktop's name for the owner, the guest's for a
        guest. A device id is never sent — an owner's own phone driving *is* the owner."""
        holder = self.control.holder(pane)
        if holder.kind == control_mod.PARTICIPANT:
            participant = self.guests.participant(holder.who)
            return participant.name if participant else holder.name
        if holder.kind == control_mod.AGENT:
            return "the agent"
        return self.name

    def control_message(self, pane: str) -> dict:
        """The `control` of section 10.3, plus one field a guest never sees.

        ``holder`` is ``owner`` for the desktop **and** for the owner's own phone, because an
        owner's paired device driving *is* the owner driving, and a guest is told no more than
        that. But the phone itself has to know whether the hand on the keyboard is its own — it
        cannot flip its drive UI to "Watching" otherwise — so the message carries ``device``, the
        id of the owner's device holding the pane, and :meth:`guest_view` takes it off on the way
        to a participant.
        """
        holder = self.control.holder(pane)
        message = {"t": "control", "pane": pane, "holder": holder.label,
                   "name": self.holder_name(pane)}
        if holder.kind == control_mod.OWNER and holder.who:
            message["device"] = holder.who
        return message

    def send_participants(self, pane: str) -> None:
        """Tell **everyone on a pane** who is on it — section 10.3's word, which includes the
        owner's own paired devices. Called on join, leave, role change, removal and every handoff.

        A guest's copy marks their own row `you`; a device of the owner's is not a participant, so
        every row of its copy is somebody else and `you` is false on all of them. Nothing else
        differs: `guest_view` is about what a *participant* may be told, and the owner's phone is
        the owner.
        """
        for channel in list(self.channels.values()):
            if channel.participant_id is not None:
                participant = channel.participant
                if participant is None or not participant.may_see(pane):
                    continue
            else:
                capability = channel.capability()
                if capability is None or not wire.allows(capability, wire.VIEW):
                    continue
                if pane not in channel.subscribed:
                    continue
            self._spawn(channel.send(self.participants_message(pane, channel)))

    def participants_message(self, pane: str, channel: Channel) -> dict:
        """One channel's copy of the list. A device is not a participant, so `you` is false on
        every row of its copy; a guest's own row is the one marked."""
        return {"t": "participants", "pane": pane,
                "items": [{**item, "you": item["id"] == channel.participant_id}
                          for item in self.participants_on(pane)]}

    def _to_pane(self, pane: str, message: dict) -> None:
        """Everyone on a pane: every participant scoped to it, and every device watching it.

        Not :meth:`_fan_out`, because a participant is *on* the pane whether or not they have
        sent `pane_focus` — the pane is the whole of what they were invited to — while a device
        sees only what it subscribed to.
        """
        for channel in list(self.channels.values()):
            if channel.participant_id is not None:
                participant = channel.participant
                if participant is None or not participant.may_see(pane):
                    continue
                self._spawn(channel.send(message))
                continue
            capability = channel.capability()
            if capability is None or not wire.allows(capability, wire.VIEW):
                continue
            if pane not in channel.subscribed:
                continue
            self._spawn(channel.send(message))

    # ---- control handoff (section 10.3) ---------------------------------------------------------

    def on_control(self, callback: Callable[[str, str, str], None]) -> None:
        """``callback(pane, holder, name)`` on every handoff, for the desktop's own display."""
        self._control_watchers.append(callback)

    def on_share_state(self, callback: Callable[[str, bool, str], None]) -> None:
        """``callback(pane, paused, reason)`` whenever a pane's guests stop or start being able
        to act — including the presence rule deciding the owner has been away long enough."""
        self._state_watchers.append(callback)

    def _control_changed(self, pane: str, old: control_mod.Holder,
                         new: control_mod.Holder) -> None:
        """One handoff, told to everyone once. Called by the book, never by hand."""
        if self.screens and old.who:
            # The source keeps its own driver — a device id, or `guest:<id>` for a participant —
            # and letting it go is part of the handoff rather than a second state that has to be
            # kept in step with this one.
            was = old.who if old.kind == control_mod.OWNER else f"guest:{old.who}"
            with contextlib.suppress(Exception):
                self.source.release(pane, was)
        self._to_pane(pane, self.control_message(pane))
        self.send_participants(pane)
        for watcher in list(self._control_watchers):
            with contextlib.suppress(Exception):
                watcher(pane, self.control.label(pane), self.holder_name(pane))
        if not self._in_control_change:
            # The pane item's `control` field is derived from the book (section 6.3), so the pane
            # list is stale the moment a handoff happens. The guard is because `_items` observes
            # the source's own field and can therefore land back here.
            self._in_control_change = True
            try:
                self._panes_changed()
            finally:
                self._in_control_change = False

    def grant_control(self, pane: str, participant_id: str) -> bool:
        """Hand a guest the keyboard. The owner's decision, from the desktop only."""
        participant = self.guests.participant(participant_id)
        if participant is None or not wire.role_allows(participant.role, wire.EDITOR):
            return False
        if not participant.may_see(pane):
            return False
        if self.share_state(pane)[0]:
            return False                      # a paused share hands the keyboard to nobody
        self.audit.record("control_grant", participant=participant_id, pane=pane,
                          was=self.control.label(pane))
        self.control.grant(pane, participant_id, participant.name)
        self.controls.drop(pane, participant_id)
        return True

    def take_control(self, pane: str, *, by: str = "keystroke") -> bool:
        """The owner's physical keystroke in the pane (section 10.3): control comes back with
        nobody asked. Whoever was typing is told with `control`, and anything of theirs already
        in flight is refused with `not_driving` by the ordinary gate."""
        holder = self.control.holder(pane)
        if holder == control_mod.THE_OWNER:
            return False
        self.audit.record("control_take", pane=pane, by=by, was=self.control.label(pane),
                          participant=self.control.participant(pane))
        return self.control.take(pane)

    def revoke_control(self, pane: str) -> bool:
        """The owner taking it back from the desktop's sharing panel rather than by typing."""
        holder = self.control.holder(pane)
        if holder.kind != control_mod.PARTICIPANT:
            return False
        self.audit.record("control_revoke", pane=pane, participant=holder.who)
        return self.control.take(pane)

    async def ask_owner_about_control(self, channel: Channel,
                                      participant: guests_mod.Participant,
                                      pane: str) -> guests_mod.PendingControl:
        """Park a guest's request for the keyboard, tell them it is waiting, and ask the owner
        (section 10.3). The answer is `control_answer {pane, participant, grant}`; sixty seconds
        with no answer is a refusal, which :meth:`expire_pending` applies.
        """
        item = self.controls.park(participant.participant_id, pane)
        self.audit.record("control_request", participant=participant.participant_id, pane=pane)
        await channel.send({"t": "control_pending", "pane": pane})
        self._spawn(self._decide_control(item, participant.name))
        return item

    async def _decide_control(self, item: guests_mod.PendingControl, name: str) -> None:
        request = ControlRequest(pane=item.pane, participant=item.participant, name=name)
        try:
            granted = bool(await asyncio.wait_for(self.control_approver(request),
                                                  self.controls.lifetime))
        except asyncio.TimeoutError:
            granted = False
        except Exception:
            log.exception("the owner's answer about control failed")
            granted = False
        if self.controls.get(item.pane, item.participant) is not item:
            return              # it lapsed, or they were removed: the answer is too late to apply
        self.controls.drop(item.pane, item.participant)
        if granted and self.grant_control(item.pane, item.participant):
            # The handoff itself told them, and told everyone else on the pane the same thing.
            # A second, private "yes" would be one more message saying what they can already see.
            return
        self.audit.record("control_refused", participant=item.participant, pane=item.pane)
        self._tell_one(item.participant, {**self.control_message(item.pane),
                                          "reason": "refused"})

    def _tell_one(self, participant_id: str, message: dict) -> None:
        for channel in list(self.channels.values()):
            if channel.participant_id == participant_id:
                self._spawn(channel.send(message))

    # ---- pause and "only while I am present" (section 10.5) -------------------------------------

    def options_for(self, pane: str) -> ShareOptions:
        """This pane's switches, falling back to the ones set for the whole share."""
        return self.options.get(pane) or self.options.get("") or ShareOptions()

    def set_share_options(self, pane: str, **changes) -> ShareOptions:
        """``share_options {pane, prompts_immediate, present_only}``. A pane of "" is the default
        for every shared pane, which is how a desktop with one switch in its sharing panel sets
        it."""
        current = self.options.get(pane) or ShareOptions()
        options = ShareOptions(
            prompts_immediate=bool(changes.get("prompts_immediate",
                                               current.prompts_immediate)),
            present_only=bool(changes.get("present_only", current.present_only)))
        self.audit.record("share_options", pane=pane or None,
                          prompts_immediate=options.prompts_immediate,
                          present_only=options.present_only)
        self.options[pane] = options
        self.refresh_presence()
        return options

    def share_pause(self, pane: str, on: bool) -> None:
        """``share_pause {pane?, on}``: refuse every participant's input and prompt while the
        screen keeps streaming. A paused holder keeps nothing — control goes back to the owner,
        because "paused" that left somebody able to type would not be a pause."""
        self.audit.record("share_pause", pane=pane or None, on=bool(on))
        if on:
            self.paused.add(pane)
        else:
            self.paused.discard(pane)
        for name in self._shared_panes():
            if self.share_state(name)[0]:
                self.take_control(name, by="pause")
        self.refresh_presence()

    def present(self) -> bool:
        """Is the owner at the desktop? False only once the grace period has run out."""
        if self._window_active or self._away_since is None:
            return True
        return self.clock() - self._away_since < PRESENCE_GRACE

    def window_active(self, active: bool) -> None:
        """The `window_active` line of section 9, shared by the notifications rule and 10.5's
        "only while I am present". One signal, two readers; there is no second line."""
        self.notifier.window_active(active)
        self._window_active = bool(active)
        self._away_since = None if active else self.clock()
        if not active:
            # Nothing is paused yet; something may be once the grace has passed, and the guests
            # are owed the `share_state` that says so. `_housekeep` would find it a second later
            # anyway; this is so the phone hears at the moment it becomes true.
            with contextlib.suppress(RuntimeError):
                asyncio.get_running_loop().call_later(PRESENCE_GRACE + 0.05,
                                                      self.refresh_presence)
        self.refresh_presence()

    def share_state(self, pane: str) -> tuple[bool, str]:
        """Whether a pane's guests may act, and why not (section 10.5).

        ``("owner")`` is the pause switch; ``("away")`` is *guests can act only while I am
        present* with the desktop window unfocused for longer than the grace period.
        """
        if "" in self.paused or pane in self.paused:
            return True, "owner"
        if self.options_for(pane).present_only and not self.present():
            return True, "away"
        return False, ""

    def _shared_panes(self) -> list[str]:
        panes = {pane for p in self.guests.live() for pane in p.panes}
        panes.update(self.control.holders)
        panes.update(self.options)
        panes.discard("")
        return sorted(panes)

    def refresh_presence(self) -> None:
        """Tell the guests of every pane whose state changed, and drop a holder who may no longer
        drive. Idempotent, so it is safe on a timer and safe to call from anywhere."""
        for pane in self._shared_panes():
            paused, reason = self.share_state(pane)
            known = self._share_state.get(pane)
            if known == (paused, reason):
                continue
            self._share_state[pane] = (paused, reason)
            if known is None and not paused:
                continue        # the first look at an ordinary pane is not news; a pause is
            if paused:
                self.take_control(pane, by=f"pause:{reason}")
            self._to_pane(pane, {"t": "share_state", "pane": pane, "paused": paused,
                                 "reason": reason})
            for watcher in list(self._state_watchers):
                with contextlib.suppress(Exception):
                    watcher(pane, paused, reason)

    # ---- lapses (10.3's minute and 10.4's ten) ---------------------------------------------------

    def expire_pending(self) -> None:
        """Drop what has run out of time and tell whoever was waiting.

        Driven by the queues' own injected clocks, so a test advances a number rather than
        sleeping for ten minutes, and by :meth:`_housekeep` in a running hub.
        """
        for item in self.prompts.expire():
            self.audit.record("prompt_decided", participant=item.participant, pane=item.pane,
                              prompt=item.prompt_id, approved=False, reason="lapsed")
            self._tell_one(item.participant, {"t": "prompt_decided", "id": item.prompt_id,
                                              "pane": item.pane, "approved": False,
                                              "reason": "lapsed"})
        for item in self.controls.expire():
            self._tell_one(item.participant, {**self.control_message(item.pane),
                                              "reason": "lapsed"})
        # A holder who is no longer a live editor — expired, removed, demoted — drives nothing.
        for pane in list(self.control.holders):
            participant_id = self.control.participant(pane)
            if participant_id is None:
                continue
            participant = self.guests.participant(participant_id)
            if participant is None or not participant.may_see(pane) or \
                    not wire.role_allows(participant.role, wire.EDITOR):
                self.audit.record("control_revoke", pane=pane, participant=participant_id,
                                  reason="no longer an editor here")
                self.control.take(pane)
        self.refresh_presence()

    async def _housekeep(self) -> None:
        while self._running:
            await asyncio.sleep(HOUSEKEEPING)
            with contextlib.suppress(Exception):
                self.expire_pending()

    async def _participant_left(self, channel: Channel) -> None:
        participant = self.guests.participants.get(channel.participant_id or "")
        self.audit.record("leave", participant=channel.participant_id)
        # A dropped socket is a lost keyboard: the next person to ask should not be told somebody
        # who is not here is driving (section 10.3).
        for pane in self.control.drop_participant(channel.participant_id or ""):
            self.audit.record("control_release", participant=channel.participant_id, pane=pane,
                              reason="disconnected")
        for item in self.controls.for_participant(channel.participant_id or ""):
            self.controls.drop(item.pane, item.participant)
        if participant is not None:
            for pane in list(participant.panes):
                self.send_participants(pane)

    def _guest_changed(self, participant_id: str) -> None:
        """A removal, an expiry or a role change reaches the live session now.

        A record that is gone closes the channel; one that is merely different stays open and the
        next message is judged by the new role, because every check reads the store.
        """
        participant = self.guests.participants.get(participant_id)
        for channel in list(self.channels.values()):
            if channel.participant_id != participant_id:
                continue
            if participant is None or not participant.live:
                self._spawn(self._drop_participant(channel))
        # Removed, expired or demoted to viewer: they hold no keyboard and no prompt of theirs
        # runs. Sections 10.3 and 10.4 both say this, and `expire_pending` is the same rule on a
        # timer for the case nothing announced — an expiry.
        gone = participant is None or not participant.live
        demoted = participant is not None and not wire.role_allows(participant.role, wire.EDITOR)
        if gone or demoted:
            for pane in self.control.drop_participant(participant_id):
                self.audit.record("control_revoke", participant=participant_id, pane=pane,
                                  reason="removed" if gone else "no longer an editor")
            for item in self.controls.for_participant(participant_id):
                self.controls.drop(item.pane, item.participant)
            for item in self.prompts.for_participant(participant_id):
                self.prompts.drop(item.prompt_id)
                self.audit.record("prompt_decided", participant=participant_id, pane=item.pane,
                                  prompt=item.prompt_id, approved=False,
                                  reason="removed" if gone else "no longer an editor")
                # A guest who is merely demoted is still connected and still watching their
                # prompt spin; one who was removed is already being closed, and `Channel.send`
                # drops everything but the goodbye for them.
                self._tell_one(participant_id, {"t": "prompt_decided", "id": item.prompt_id,
                                                "pane": item.pane, "approved": False,
                                                "reason": "removed"})
        if participant is not None:
            for pane in list(participant.panes):
                self.send_participants(pane)

    async def _drop_participant(self, channel: Channel) -> None:
        with contextlib.suppress(Exception):
            await channel.send({"t": "bye", "reason": "this share has ended, or your access "
                                                      "expired.", "discard": True})
        await channel.close("no longer a participant")

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
                self.audit.record("revoke", device=device_id)
                self.control.drop_device(device_id)
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
        if channel.participant_id is not None:
            await self._welcome_participant(channel)
            return
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
                         + (["screen", "takeover"] if self.screens else [])
                         + (["history"] if self.scrollback else [])
                         + (["pane_state"] if self.pane_state else [])),
            "server_time": time.time(),
        })
        await channel.send(self.stream("panes", limit=64).add(
            {"t": "panes", "items": self._items()}))

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
        if self.guests.invite_by_room(channel.room) is not None:
            # An invite room never produces a device record. This is the structural half of
            # "a participant record is never a device record": there is no message on an invite
            # channel that reaches `DeviceStore.pair` (section 10.2).
            raise wire.WireError("not_permitted",
                                 "that is an invite link, not a pairing code; knock instead.")
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

        if self.guests.by_key(channel.client_static) is not None:
            # The other half of `GuestStore.admit`'s refusal, and section 10.2's "a key is a
            # device *or* a participant and never both". Only one direction was enforced: admit
            # refuses a key the device store knows, but nothing stopped a key the **guest** store
            # knows from being paired — and since the handshake looks devices up first, the next
            # connection would then be that guest holding a capability rather than a role. A
            # guest watching a shared pane can see the pairing QR the moment the owner opens it,
            # which is all this needs to be reachable rather than theoretical.
            raise wire.WireError("not_permitted",
                                 "that key is already a guest here; remove them from the share "
                                 "before pairing this device.")

        code = auth_code(channel.session.handshake_hash)
        request = PairRequest(name=identity_mod.clean_label(message.get("name", "")),
                              platform=identity_mod.clean_label(message.get("platform", ""), 24),
                              fingerprint=pairing.fingerprint(channel.client_static),
                              code=code, peer=channel.peer)
        allowed, capability = await self.approver(request)
        if not allowed or capability not in wire.CAPABILITIES:
            raise wire.WireError("not_permitted", "the desktop refused this device.")
        device = self.devices.pair(channel.client_static, request.name, request.platform,
                                   capability, origin=self.origin())
        channel.device_id = device.device_id
        self.audit.record("pair", device=device.device_id, name=request.name,
                          capability=capability, peer=request.peer)
        log.info("paired %s (%s) as %s", device.name, device.fingerprint, capability)
        await channel.send({"t": "paired", "device_id": device.device_id,
                            "capability": capability, "code": code,
                            "desktop": {"id": self.identity.desktop_id, "name": self.name,
                                        "fingerprint": self.identity.fingerprint}})

    # -- multiplayer: invites, knocking and the owner's controls (section 10) ---------------------

    async def invite_create(self, panes: list[str], role: str = wire.VIEWER, *,
                            expires_in: float = guests_mod.DEFAULT_EXPIRY,
                            uses: int = 1,
                            single_knock: bool = False,
                            tab: str = "") -> tuple[guests_mod.Invite, str]:
        """Open a room, mint an invite for it, and return the invite and the link.

        The room's ``ttl`` is the invite's lifetime, so a week-long invite does not point at a
        room the rendezvous forgot after five minutes (section 8). The plaintext secret exists
        only here, in the URL this returns: the record holds a hash.
        """
        for pane in panes:
            if not self.source.has_pane(pane):
                raise wire.WireError("no_such_pane", "no such pane.")
        if tab:
            # A whole-tab invite names every pane the tab holds now; later ones join by pane_tab.
            panes = list(panes) + sorted(pane for pane, where in self.pane_tabs.items()
                                         if where == tab and pane not in panes)
        lifetime = max(60.0, min(float(expires_in or guests_mod.DEFAULT_EXPIRY),
                                 guests_mod.MAX_EXPIRY))
        room, granted = await self._open_room(lifetime)
        invite, secret = self.guests.create_invite(list(panes), role, room,
                                                   expires_in=min(lifetime, granted), uses=uses,
                                                   single_knock=single_knock, tab=tab)
        self.audit.record("invite_create", invite=invite.invite_id, panes=invite.panes, tab=invite.tab,
                          role=invite.role, uses=invite.uses_left,
                          expires=round(invite.expires, 3))
        url = pairing.invite_url(self.app_base, self.identity.public, secret, room)
        return invite, url

    async def code_create(self, panes: list[str],
                          role: str = wire.VIEWER, *, tab: str = "") -> meetcode.CodeRecord:
        """A meeting code and a PIN for a pane (card #97EG).

        Behind the code is an ordinary invite — one use, the code's ten minutes, and
        ``single_knock`` so the fragment the code phase hands over admits one knock and no more —
        and a second room that only the code phase listens on. The invite's link is kept in
        memory in the code's record and nowhere else; the GUI is given the code, the PIN and the
        invite's id, never the link.
        """
        invite, url = await self.invite_create(list(panes), role,
                                               expires_in=meetcode.CODE_LIFETIME, uses=1,
                                               single_knock=True, tab=tab)
        return await self.codes.create(invite, url)

    def invite_revoke(self, invite_id: str) -> bool:
        invite = self.guests.invite(invite_id)
        if invite is None:
            return False
        self.audit.record("invite_revoke", invite=invite_id)
        return self.guests.burn_invite(invite_id)

    async def role_set(self, participant_id: str, role: str) -> bool:
        """Change what a participant may do. Live: the next message is judged by the new role."""
        participant = self.guests.participants.get(participant_id)
        if participant is None or role not in wire.GUEST_ROLES:
            return False
        self.audit.record("role_set", participant=participant_id, role=role,
                          was=participant.role)
        return self.guests.set_role(participant_id, role)

    async def participant_remove(self, participant_id: str) -> bool:
        """Cut one guest off: their session closes, their invite burns, their record goes."""
        participant = self.guests.participants.get(participant_id)
        if participant is None:
            return False
        self.audit.record("participant_remove", participant=participant_id,
                          invite=participant.invite, panes=list(participant.panes))
        for item in self.prompts.for_participant(participant_id):
            self.prompts.drop(item.prompt_id)
        for pane in list(participant.panes):
            self.controls.drop(pane, participant_id)
        self.guests.remove(participant_id)          # closes the live session via _guest_changed
        self.control.drop_participant(participant_id)
        return True

    def pane_tab(self, pane: str, tab: str) -> None:
        """The desktop says which tab a shared pane is in ("" for a pane shared on its own).

        Called **before** the pane list goes out, so the `panes` each guest is sent already
        reflects the change. A pane joining a tab that is shared whole joins the scope of that
        tab's guests; a pane leaving one (moved to another tab) leaves it. Both are audited before
        the store changes, one row per participant (section 10.6).
        """
        old = self.pane_tabs.get(pane, "")
        if tab:
            self.pane_tabs[pane] = tab
        else:
            self.pane_tabs.pop(pane, None)
        if old == tab:
            # Already in this tab — but an invite minted since may not name it yet; add_to_tab
            # is a no-op for rows that already hold it.
            self._grow(tab, pane)
            return
        if old:
            self._shrink(old, pane, reason="moved")
        if tab:
            self._grow(tab, pane)

    def pane_gone(self, pane: str) -> None:
        """A shared pane closed or stopped being shared: it leaves every tab scope it was in."""
        old = self.pane_tabs.pop(pane, "")
        if old:
            self._shrink(old, pane, reason="closed")

    def _grow(self, tab: str, pane: str) -> None:
        invites, people = self.guests.tab_rows(tab)
        for invite in invites:
            if pane not in invite.panes:
                self.audit.record("scope_grown", invite=invite.invite_id, tab=tab, pane=pane)
        for participant in people:
            if not participant.may_see(pane):
                self.audit.record("scope_grown", participant=participant.participant_id,
                                  tab=tab, pane=pane)
        for participant in self.guests.add_to_tab(tab, pane):
            for shown in participant.panes:
                self.send_participants(shown)

    def _shrink(self, tab: str, pane: str, *, reason: str) -> None:
        _, people = self.guests.tab_rows(tab)
        leaving = [p.participant_id for p in people if p.may_see(pane)]
        for participant_id in leaving:
            self.audit.record("scope_shrunk", participant=participant_id, tab=tab, pane=pane,
                              reason=reason)
            # Whatever they held on that pane goes with it: the keyboard, a pending ask for it,
            # and prompts still waiting for the owner.
            self.control.release(pane, control_mod.PARTICIPANT, participant_id)
            self.controls.drop(pane, participant_id)
            for item in self.prompts.for_participant(participant_id):
                if item.pane == pane:
                    self.prompts.drop(item.prompt_id)
        self.guests.remove_from_tab(tab, pane)
        self.send_participants(pane)

    async def share_end(self, pane: str) -> int:
        """Stop sharing a pane: every participant on it goes, and its invites burn."""
        self.audit.record("share_end", pane=pane,
                          participants=[p.participant_id for p in self.guests.on_pane(pane)])
        for item in self.prompts.for_pane(pane):
            self.prompts.drop(item.prompt_id)
        for item in self.controls.for_pane(pane):
            self.controls.drop(pane, item.participant)
        gone = self.guests.end_share(pane)
        # The pane is not shared any more: nobody remote drives it, it is not paused, and its
        # switches go with it rather than surprising the next share of the same pane.
        self.control.forget(pane)
        self.paused.discard(pane)
        self.options.pop(pane, None)
        self._share_state.pop(pane, None)
        return len(gone)

    async def _on_knock(self, channel: Channel, message: dict) -> None:
        """A guest at the door (section 10.2). Their first message after the handshake.

        Order: the channel must be an invite channel, the secret must check (five wrong burn the
        invite), the knock must fit the invite's budget, the owner is asked with the same
        five-digit code pairing uses, and only an admission spends a use.
        """
        if channel.device_id or channel.participant_id:
            raise wire.WireError("not_permitted", "this channel is already known here.")
        if not channel.room:
            raise wire.WireError("not_permitted", "a knock needs an invite link.")
        invite = self.guests.invite_by_room(channel.room)
        if invite is None:
            raise wire.WireError("not_permitted", "that invite has expired or been revoked.")
        offered = message.get("invite", "")
        try:
            secret = pairing.un64(offered) if isinstance(offered, str) else b""
        except Exception:
            secret = b""
        if not invite.check(secret):
            self.guests.save()                      # the failed attempt is counted on disk
            self.audit.record("knock_refused", invite=invite.invite_id, peer=channel.peer,
                              reason="wrong or spent secret")
            raise wire.WireError("not_permitted", "that invite link is wrong or spent.")
        if not self.guests.may_knock(invite.invite_id):
            raise wire.WireError("rate_limited", "too many people are knocking; try shortly.")
        if not self.guests.claim_knock(invite, channel.client_static):
            # A meeting code's invite takes one knock (card #97EG): this fragment was forwarded,
            # replayed or read after the code phase, and the first knock already claimed it.
            self.audit.record("knock_refused", invite=invite.invite_id, peer=channel.peer,
                              reason="a code's invite accepts one knock")
            raise wire.WireError("not_permitted", "that invite link is wrong or spent.")

        participant_id = secrets.token_hex(8)
        code = auth_code(channel.session.handshake_hash)
        request = KnockRequest(participant=participant_id,
                               name=identity_mod.clean_label(message.get("name", "")),
                               platform=identity_mod.clean_label(message.get("platform", ""), 24),
                               fingerprint=pairing.fingerprint(channel.client_static),
                               code=code, peer=channel.peer, role=invite.role,
                               panes=list(invite.panes), invite=invite.invite_id)
        self.audit.record("knock", participant=participant_id, invite=invite.invite_id,
                          name=request.name, peer=request.peer, role=invite.role)
        await channel.send({"t": "knock_pending", "code": code})

        self.guests.waiting(invite.invite_id, +1)
        try:
            admitted, role = await asyncio.wait_for(self.knock_approver(request),
                                                    guests_mod.KNOCK_TIMEOUT)
        except asyncio.TimeoutError:
            # Two minutes with no answer is a refusal, not a question still open.
            admitted, role = False, wire.VIEWER
        except Exception:
            log.exception("the owner's answer to a knock failed")
            admitted, role = False, wire.VIEWER
        finally:
            self.guests.waiting(invite.invite_id, -1)

        if role not in wire.GUEST_ROLES:
            role = invite.role
        if not wire.role_allows(invite.role, role):
            # Admitting *below* the invite is the owner's to do; above it is not. An answer that
            # asks for more than the link offered is taken as the link's own role.
            role = invite.role
        if not admitted:
            self.audit.record("refused", participant=participant_id, invite=invite.invite_id)
            if invite.single_knock:
                self.guests.burn_invite(invite.invite_id)   # its one knock was the owner's "no"
            await channel.send(wire.error("not_admitted", "the desktop did not let you in.",
                                          message.get("id")))
            await channel.close("not admitted")
            return

        # Section 10.6 asks for the line before the action. For everything the owner does —
        # revoke, remove, role change, share end — it is, above. Admission is the one place it
        # cannot be: `admit` is where a key that is already a paired device is refused, so a line
        # written first would record an admission that did not happen. It is written the moment
        # the record exists and before the guest is told anything.
        participant = self.guests.admit(invite, channel.client_static, request.name,
                                        request.platform, role, participant_id)
        channel.participant_id = participant.participant_id
        self.audit.record("admitted", participant=participant.participant_id,
                          invite=invite.invite_id, role=participant.role,
                          panes=list(participant.panes), uses_left=invite.uses_left)
        self.audit.record("join", participant=participant.participant_id, peer=channel.peer)
        log.info("admitted %s (%s) as %s on %s", participant.name, participant.fingerprint,
                 participant.role, ", ".join(participant.panes))
        await channel.send({"t": "admitted", "participant": participant.participant_id,
                            "role": participant.role, "desktop_name": self.name,
                            "panes": list(participant.panes),
                            "expires": round(participant.expires, 3),
                            "hub_epoch": self.epoch, "code": code,
                            "features": self.guest_features(),
                            "desktop": {"id": self.identity.desktop_id, "name": self.name,
                                        "fingerprint": self.identity.fingerprint}})
        await channel.send(self.stream("panes", limit=64).add(
            {"t": "panes", "items": self._items()}))
        await self._tell_pane_state(channel, participant)

    async def _welcome_participant(self, channel: Channel) -> None:
        """A guest reconnecting with an ordinary `hello` (section 10.2)."""
        participant = channel.participant
        if participant is None:
            await channel.send(wire.error("not_permitted", "your access here has ended."))
            await channel.close("no longer a participant")
            return
        self.guests.touch(participant.participant_id)
        self.audit.record("join", participant=participant.participant_id, peer=channel.peer)
        await channel.send({
            "t": "welcome",
            "desktop": {"id": self.identity.desktop_id, "name": self.name,
                        "fingerprint": self.identity.fingerprint},
            "proto": wire.PROTOCOL_VERSION,
            "participant": participant.participant_id,
            "role": participant.role,
            "panes": list(participant.panes),
            "expires": round(participant.expires, 3),
            "hub_epoch": self.epoch,
            # No `capability` and no `password_entry`: those belong to a device record, and a
            # guest has none. A client that looks for them finds nothing, which is the point.
            "features": self.guest_features(),
            "server_time": time.time(),
        })
        await channel.send(self.stream("panes", limit=64).add(
            {"t": "panes", "items": self._items()}))
        await self._tell_pane_state(channel, participant)

    def guest_features(self) -> list[str]:
        """What a participant's client may rely on, for `admitted` and for `welcome` alike.

        Both greetings compute it here rather than each writing its own list: a guest admitted
        for the first time would otherwise have to assume a screen and scrollback exist until
        their first reconnect told them otherwise.
        """
        return (["panes", "agent"]
                + (["screen"] if self.screens else [])
                + (["history"] if self.scrollback else []))

    async def _tell_pane_state(self, channel: Channel,
                               participant: guests_mod.Participant) -> None:
        """Presence, who is driving and whether typing is on — for every pane they are on.

        A guest who has just joined, or come back after a tunnel, must not have to guess any of
        the three: `participants` says who is here, `control` who has the keyboard, `share_state`
        why nothing they type would land.
        """
        for pane in participant.panes:
            self.send_participants(pane)
            paused, reason = self.share_state(pane)
            await channel.send(self.control_message(pane))
            await channel.send({"t": "share_state", "pane": pane, "paused": paused,
                                "reason": reason})

    # -- guest prompts (section 10.4) --------------------------------------------------------------

    async def ask_owner_about_prompt(self, channel: Channel,
                                     participant: guests_mod.Participant, pane: str, text: str, *,
                                     when: str = "now",
                                     plan_id: str = "") -> guests_mod.PendingPrompt:
        """Park a guest's prompt, tell them it is waiting, and ask the owner (section 10.4).

        The owner answers `prompt_answer {id, approve}`; ten minutes with no answer is a refusal,
        applied by :meth:`expire_pending`. An approved prompt goes to the pane's **agent** and
        never to the router, whatever the text says, carrying `origin: guest:<id>`.
        """
        if self.share_state(pane)[0]:
            raise wire.WireError("paused", "this share is paused; the owner is not taking "
                                           "prompts right now.")
        item = self.prompts.park(participant.participant_id, pane, text, when=when,
                                 plan_id=plan_id)
        self.audit.record("guest_prompt", participant=participant.participant_id, pane=pane,
                          prompt=item.prompt_id, text=text, plan=plan_id or None,
                          immediate=self.options_for(pane).prompts_immediate or None)
        await channel.send({"t": "prompt_pending", "id": item.prompt_id, "pane": pane})
        if self.options_for(pane).prompts_immediate:
            # The owner's "guest prompts run immediately" (10.5). Still parked, still audited,
            # still agent-only: what it skips is the question, not the record or the routing.
            self._spawn(self._run_prompt(item, participant.name, approved=True,
                                         reason="immediate"))
        else:
            self._spawn(self._decide_prompt(item, participant.name))
        return item

    async def _decide_prompt(self, item: guests_mod.PendingPrompt, name: str) -> None:
        request = PromptRequest(prompt_id=item.prompt_id, participant=item.participant, name=name,
                                pane=item.pane, text=item.text, when=item.when,
                                plan_id=item.plan_id)
        try:
            approved = bool(await asyncio.wait_for(self.prompt_approver(request),
                                                   self.prompts.lifetime))
        except asyncio.TimeoutError:
            approved = False
        except Exception:
            log.exception("the owner's answer to a guest prompt failed")
            approved = False
        await self._run_prompt(item, name, approved=approved, reason="refused")

    async def _run_prompt(self, item: guests_mod.PendingPrompt, name: str, *, approved: bool,
                          reason: str) -> None:
        """Apply the owner's answer, if the prompt is still there to apply it to.

        A prompt whose guest was removed, demoted or whose share ended is **gone** from the queue
        by then, and a decision about it does nothing: that is the one rule that keeps "approve"
        from running something the owner took the right to ask for away five minutes ago.
        """
        if self.prompts.get(item.prompt_id) is not item:
            return
        self.prompts.drop(item.prompt_id)
        if approved and self.share_state(item.pane)[0]:
            approved, reason = False, "paused"
        self.audit.record("prompt_decided", participant=item.participant, pane=item.pane,
                          prompt=item.prompt_id, approved=approved,
                          reason=None if approved else reason)
        if not approved:
            self._tell_one(item.participant, {"t": "prompt_decided", "id": item.prompt_id,
                                              "pane": item.pane, "approved": False,
                                              "reason": reason})
            return
        self._tell_one(item.participant, {"t": "prompt_decided", "id": item.prompt_id,
                                          "pane": item.pane, "approved": True})
        origin = f"guest:{item.participant}"
        try:
            if item.plan_id:
                # A guest's `plan_execute` is a prompt (10.4), and an approved one is still the
                # plan flow: the id is resolved against the desktop's own table, never a path.
                await self.source.plan_execute(item.pane, item.plan_id, origin=origin)
            else:
                await self.source.compose(item.pane, item.text, to_agent=True, when=item.when,
                                          origin=origin, origin_name=name)
        except wire.WireError as error:
            self._tell_one(item.participant, wire.error(error.code, error.message))
        except Exception:
            log.exception("running an approved guest prompt on %s", item.pane)
            self._tell_one(item.participant,
                           wire.error("internal", "the desktop could not run that prompt."))

    def decide_prompt(self, prompt_id: str, approve: bool) -> bool:
        """``prompt_answer {id, approve}`` from the desktop, for a hub driven by hand or by the
        sidecar's own queue rather than by an awaited approver."""
        item = self.prompts.get(prompt_id)
        if item is None:
            return False
        participant = self.guests.participant(item.participant)
        self._spawn(self._run_prompt(item, participant.name if participant else "",
                                     approved=approve, reason="refused"))
        return True

    def guest_drives(self, channel: Channel, pane: str) -> bool:
        """Whether this guest may type into ``pane`` right now: the one control book says so, and
        the record behind the channel is still a live editor's."""
        participant = channel.participant
        if participant is None or not wire.role_allows(participant.role, wire.EDITOR):
            return False
        return self.control_holder(pane) == channel.participant_id

    # -- panes -----------------------------------------------------------------------------------

    async def _on_panes_get(self, channel: Channel, message: dict) -> None:
        await channel.send({"t": "panes", "items": self._items(),
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
        # Who is driving and who is here, the moment one of the owner's devices opens the pane —
        # the same two things a joining guest is told (`_tell_pane_state`). Without them a phone
        # opening a pane a guest is already typing in believes the keyboard is free until the
        # next handoff. Sent to this channel alone: nobody else's view of the pane changed.
        if channel.participant_id is None:
            await channel.send(self.control_message(pane))
            await channel.send(self.participants_message(pane, channel))

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
        participant = channel.participant
        if participant is not None:
            # A guest's prompt is never passed on. `agent: false` — the composer's shell route —
            # is refused whatever their role (section 10.1), and the rest waits for the owner.
            if message.get("agent") is False:
                raise wire.WireError("not_permitted",
                                     "a guest's prompt always goes to the agent, never the shell.")
            await self.ask_owner_about_prompt(channel, participant, pane, text, when=when)
            return
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
        # Three spellings of one id, because section 16 renamed it: `row` is what `pane_state`
        # calls a queue row and what `app/pane.js` sends, `item`/`item_id` are what the older
        # composer sent. Reading only `item_id` meant the phone's Remove button asked the desktop
        # to withdraw the empty string — the row stayed, and nothing said why. Whichever arrives,
        # it is an id this desktop minted and the GUI resolves it against its own table.
        item = next((message[name] for name in ("row", "item_id", "item")
                     if isinstance(message.get(name), str) and message[name]), "")
        await self.source.queue_remove(self._pane_of(message), item)

    async def _on_recap_request(self, channel: Channel, message: dict) -> None:
        await self.source.recap_request(self._pane_of(message))

    async def _on_plan_execute(self, channel: Channel, message: dict) -> None:
        plan_id = message.get("plan_id")
        if not isinstance(plan_id, str) or not plan_id.isalnum() or len(plan_id) > 32:
            # Never a path: the id must be one this desktop minted in a plan_written event.
            raise wire.WireError("unknown_type", "plan_execute needs a plan_id.")
        pane = self._pane_of(message)
        participant = channel.participant
        if participant is not None:
            # Section 10.4: a guest's plan_execute is treated as a prompt, because executing a
            # plan is asking the agent to do everything in it.
            await self.ask_owner_about_prompt(channel, participant, pane,
                                              f"Execute the plan {plan_id}", plan_id=plan_id)
            return
        await self.source.plan_execute(pane, plan_id, origin=f"remote:{channel.device_id}")

    async def _on_voice(self, channel: Channel, message: dict) -> None:
        pane = self._pane_of(message)
        audio_format = message.get("format", "webm")
        if audio_format not in ("webm", "ogg", "wav", "mp3", "m4a"):
            raise wire.WireError("unknown_type", "unsupported audio format.")
        audio = wire.decode_bytes(message.get("data"), MAX_VOICE_BYTES * 4 // 3 + 8, "the clip")
        request_id = message.get("id")

        # Transcribing is a network round trip on the desktop, seconds long. Awaiting it here
        # would stop reading this device's other messages until it finished, so the phone would
        # freeze for the whole clip; the reply carries the request's id, so it can come back
        # whenever it is ready. The per-device rate limit above still caps how many are in flight.
        async def answer() -> None:
            try:
                text = await self.source.transcribe(pane, audio, audio_format)
            except wire.WireError as error:
                await channel.send(wire.error(error.code, error.message, request_id))
                return
            except Exception:                       # the phone gets an answer either way
                log.exception("transcribing a clip for %s", pane)
                await channel.send(wire.error("internal", "the clip could not be transcribed.",
                                              request_id))
                return
            await channel.send({"t": "agent", "pane": pane,
                                "event": {"event": "transcribed", "text": text},
                                "id": request_id})

        self._spawn(answer())

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

    def _screen_pane(self, channel: Channel, message: dict) -> str:
        """A pane whose terminal this desktop is streaming. Reading it needs no control."""
        if not self.screens:
            raise wire.WireError("not_permitted", "this desktop is not sharing a terminal.")
        return self._pane_of(message)

    def _typing_pane(self, channel: Channel, message: dict, *, claim: str = "typing") -> str:
        """The pane this input may reach, or the refusal that says why not.

        For a participant, in this order: the share is not paused (10.5), they hold the pane's
        control token (10.3), and only then anything about whether this desktop streams a
        terminal — a guest who is not driving is refused for that reason and not for some detail
        of the desktop's plumbing.

        For one of the owner's own devices, ``claim`` says what this message is:

        * ``"typing"`` — `keys`, `line`, `paste`. Typing *is* taking control (section 6.6 has no
          separate claim), but only while the keyboard is the device's to take: section 10.3
          refuses input from anyone who is not the holder, and that includes the owner's phone
          once the owner has taken the pane back at the desktop or handed it to a guest;
        * ``"take"`` — `control_request`, which is the asking, so it always claims;
        * ``"none"`` — `control_release`, which is about giving it up and needs no claim at all.
        """
        if channel.participant_id is not None:
            pane = self._pane_of(message)
            paused, reason = self.share_state(pane)
            if paused:
                raise wire.WireError("paused", self._pause_message(reason))
            if not self.guest_drives(channel, pane):
                raise wire.WireError("not_driving", "you are not driving this pane.")
            self._check_not_secret(pane)
            return self._screen_pane(channel, message)
        pane = self._screen_pane(channel, message)
        self._check_not_secret(pane)
        if channel.device_id and claim != "none":
            name = channel.device.name if channel.device else ""
            if claim != "take" and not self.control.may_type(pane, channel.device_id):
                raise wire.WireError("not_driving", "you are not driving this pane.")
            self.control.claim_device(pane, channel.device_id, name=name)
        return pane

    def _check_not_secret(self, pane: str) -> None:
        """Ordinary input is refused while the pane is at a password prompt (section 6.6).

        The sources refuse it again at the write, from a fresh termios read, which is the check
        that matters; this one is here so the rule holds for **every** source rather than for the
        two that happen to implement it, and so a participant holding the keyboard meets it in
        the same place a `full` device does.
        """
        if self.source.secret_prompt(pane):
            raise wire.WireError("not_permitted",
                                 "that pane is at a password prompt; ordinary input is refused.")

    @staticmethod
    def _pause_message(reason: str) -> str:
        if reason == "away":
            return "the owner is away from the desktop; typing resumes when they are back."
        return "this share is paused."

    def _typist(self, channel: Channel) -> str:
        """Who to name in the audit line and to the source: a device id, or a guest's."""
        if channel.participant_id is not None:
            return f"guest:{channel.participant_id}"
        return channel.device_id or ""

    def _audit_input(self, channel: Channel, kind: str, pane: str, **fields) -> None:
        """Section 10.6, written before the write. A participant's line is recorded with its
        text; keys and pastes as byte counts, because a password typed at a prompt this desktop
        did not detect must not be sitting in the log in full."""
        who = ({"participant": channel.participant_id} if channel.participant_id
               else {"device": channel.device_id})
        self.audit.record(kind, pane=pane, **who, **fields)

    async def _on_keys(self, channel: Channel, message: dict) -> None:
        pane = self._typing_pane(channel, message)
        raw = wire.decode_bytes(message.get("bytes"), 8192, "keys")
        self._audit_input(channel, "keys", pane, bytes=len(raw))
        await self.source.send_keys(pane, raw, device=self._typist(channel))

    async def _on_line(self, channel: Channel, message: dict) -> None:
        pane = self._typing_pane(channel, message)
        text = message.get("text")
        if not isinstance(text, str) or len(text) > 4096:
            raise wire.WireError("unknown_type", "line needs text.")
        self._audit_input(channel, "line", pane, text=text)
        await self.source.send_line(pane, text, device=self._typist(channel))

    async def _on_paste(self, channel: Channel, message: dict) -> None:
        pane = self._typing_pane(channel, message)
        text = message.get("text")
        if not isinstance(text, str) or len(text) > 64_000:
            raise wire.WireError("unknown_type", "paste needs text.")
        self._audit_input(channel, "paste", pane, bytes=len(text.encode()))
        await self.source.paste(pane, text, device=self._typist(channel))

    async def _on_control_request(self, channel: Channel, message: dict) -> None:
        participant = channel.participant
        if participant is not None:
            # For a participant this is a request, not a grant (section 10.3): it parks, the
            # guest is told it is waiting, and the owner is asked.
            pane = self._pane_of(message)
            paused, reason = self.share_state(pane)
            if paused:
                raise wire.WireError("paused", self._pause_message(reason))
            if self.control_holder(pane) == channel.participant_id:
                await channel.send(self.control_message(pane))
                return
            await self.ask_owner_about_control(channel, participant, pane)
            return
        pane = self._typing_pane(channel, message, claim="take")
        await self.source.send_keys(pane, b"", device=channel.device_id)
        await channel.send({"t": "agent", "pane": pane,
                            "event": {"event": "status", "text": "You have the keyboard."}})

    async def _on_control_release(self, channel: Channel, message: dict) -> None:
        if channel.participant_id is not None:
            pane = self._pane_of(message)
            self.controls.drop(pane, channel.participant_id)
            if self.control_holder(pane) == channel.participant_id:
                self.audit.record("control_release", participant=channel.participant_id,
                                  pane=pane)
                self.control.release(pane, control_mod.PARTICIPANT, channel.participant_id)
            return
        pane = self._typing_pane(channel, message, claim="none")
        self.audit.record("control_release", device=channel.device_id, pane=pane)
        self.control.release(pane, control_mod.OWNER, channel.device_id or "")
        self.source.release(pane, channel.device_id)

    async def _on_screen_get(self, channel: Channel, message: dict) -> None:
        pane = self._screen_pane(channel, message)
        await channel.send(self.source.screen_snapshot(pane))

    async def _on_history_get(self, channel: Channel, message: dict) -> None:
        """A page of scrollback (section 6.5). `view` is enough: it is reading, not typing.

        The cursor is absolute. `before_row` is the row the page ends just below, so a phone
        that holds rows `[R, ...)` asks for `before_row: R` and gets exactly the rows above them,
        however much the shell printed in between; omitting it asks for the newest page. A
        newest-relative cursor would shift under live output and leave a hole or a repeat in the
        middle of what the person is reading.

        Answered off the read loop, like `voice`: a wedged GUI must time out on its own request
        rather than stop this device's other messages, and two devices paging at once each get
        their own answer back by id.
        """
        pane = self._pane_of(message)
        if not self.scrollback:
            raise wire.WireError("not_permitted", "this desktop does not share scrollback.")
        count = message.get("count", DEFAULT_HISTORY_ROWS)
        if isinstance(count, bool) or not isinstance(count, int):
            raise wire.WireError("unknown_type", "history_get needs an integer count.")
        count = max(1, min(count, MAX_HISTORY_ROWS))
        before = message.get("before_row", -1)
        if before is None:
            before = -1
        if isinstance(before, bool) or not isinstance(before, int):
            raise wire.WireError("unknown_type", "before_row must be a scrollback row.")
        before = min(before, 1 << 31)
        request_id = message.get("id")

        async def answer() -> None:
            try:
                page = await self.source.history(pane, before, count)
            except wire.WireError as error:
                await channel.send(wire.error(error.code, error.message, request_id))
                return
            except Exception:                       # the phone gets an answer either way
                log.exception("paging scrollback for %s", pane)
                await channel.send(wire.error("internal", "that page could not be read.",
                                              request_id))
                return
            await channel.send({"t": "history", "pane": pane, "id": request_id,
                                "from_row": int(page.get("from_row", 0)),
                                "total": int(page.get("total", 0)),
                                "more": bool(page.get("more")),
                                "lines": page.get("lines") or []})

        self._spawn(answer())

    # -- not in P1 ---------------------------------------------------------------------------------

    async def _on_transport_switch(self, channel: Channel, message: dict) -> None:
        """The explicit re-binding of the Noise stream to a new transport (section 2).

        ``next_seq`` must name exactly the next frame the receiver expects: everything before it
        was applied, everything after it arrives on the new path only. A gap or a replay is
        refused with ``stale_seq`` and the session stays where it is, so a second writer cannot
        split the nonce space — which is the whole point of writing the handshake down before
        a WebRTC transport exists rather than after.
        """
        wanted = message.get("next_seq")
        if not isinstance(wanted, int) or wanted < 0:
            raise wire.WireError("unknown_type", "transport_switch needs next_seq.")
        if wanted != channel.recv_count:
            raise wire.WireError("stale_seq",
                                 "next_seq must be exactly the next frame this side expects.")
        await channel.send({"t": "transport_switched", "effective": wanted})

    async def _on_secret_input(self, channel: Channel, message: dict) -> None:
        """A password line (section 6.7). Nothing here trusts the client's view of the pane.

        Order: the per-device switch (off by default), then the nonce — known, unspent, unexpired,
        bound to this pane, this asking process and this prompt generation — then a fresh prompt
        check, and only then the write, which re-checks a final time at the moment it happens.
        The bytes are wiped on the way out; the audit line records that this happened, not what.
        """
        device = channel.device
        if device is None or not device.password_entry:
            raise wire.WireError("not_permitted", "password entry is off for this device.")
        pane = self._pane_of(message)
        # A bytearray, not bytes: the copy this process holds is zeroed below. CPython still makes
        # transient copies while decoding (the wire itself is inside Noise), so this is best
        # effort here and a hard guarantee only on the C++ side, which wipes with
        # relay::input::Secret; the spec says as much (section 6.7, "wiped in place").
        raw = bytearray(wire.decode_bytes(message.get("bytes"), MAX_SECRET_BYTES, "the password"))
        offered = message.get("nonce")
        nonce = self.secret_nonces.get(pane)
        state = self.source.secret_state(pane) if nonce else None
        if (not isinstance(offered, str) or nonce is None or state is None
                or nonce.value != offered or nonce.foreground_pid != state.get("foreground_pid", 0)
                or nonce.expires <= time.monotonic()):
            raise wire.WireError("not_permitted",
                                 "that password prompt has ended; reopen it and try again.")
        if not self.source.secret_prompt(pane):
            raise wire.WireError("not_permitted",
                                 "that pane is no longer at a password prompt.")
        self.secret_nonces.pop(pane, None)          # single use, whatever happens next
        self.audit.record("secret_input", pane=pane, device=channel.device_id)
        try:
            await self.source.send_secret(pane, bytes(raw), device=channel.device_id)
        finally:
            for index in range(len(raw)):
                raw[index] = 0
        await channel.send({"t": "agent", "pane": pane,
                            "event": {"event": "status", "text": "Password sent."}})

    # ---- push ------------------------------------------------------------------------------------
    # Section 9. The decisions — which events are worth a buzz, the presence rule, the per-pane
    # cooldown and what a body may say — are in remote/notify.py; what is here is the wire: two
    # client messages, and the one call that hands the rendezvous an endpoint and ciphertext.

    async def _on_push_subscribe(self, channel: Channel, message: dict) -> None:
        """A phone's Web Push subscription, which never goes near the rendezvous.

        ``endpoint``, ``p256dh`` and ``auth`` are the subscription the browser was given; ``key``
        is the per-device seal key the service worker keeps in IndexedDB. Together they are enough
        to construct a notification this phone will show, which is exactly why the rendezvous is
        told none of it (docs/REMOTE-PROTOCOL.md section 8).

        ``kinds`` is which notifications this device wants. Sending this message again replaces
        the whole list, which is how the checkboxes on the phone change their minds without the
        browser asking for permission a second time. The reply carries what is now stored, so a
        client that reloads learns its own settings from the desktop rather than guessing.
        """
        if channel.device_id is None:
            raise wire.WireError("not_permitted", "pair first.")
        subscription = notify_mod.clean_subscription(message)
        # The phone subscribed under the VAPID key of the rendezvous it reached us through, which
        # is the one the hub is on now: its pushes go out there from here on (section 9).
        self.devices.set_push(channel.device_id, subscription, origin=self.origin())
        self.audit.record("push_subscribe", device=channel.device_id)
        await channel.send({"t": "push_state", "subscribed": True,
                            "kinds": subscription["kinds"], "id": message.get("id")})

    async def _on_push_unsubscribe(self, channel: Channel, message: dict) -> None:
        if channel.device_id is None:
            raise wire.WireError("not_permitted", "pair first.")
        self.devices.set_push(channel.device_id, None)
        self.audit.record("push_unsubscribe", device=channel.device_id)
        await channel.send({"t": "push_state", "subscribed": False, "kinds": [],
                            "id": message.get("id")})

    async def push_send(self, endpoint: str, payload: bytes, origin: str = "") -> dict:
        """Post one encrypted payload through the device's own rendezvous, which cannot read it.

        ``origin`` is the device's (``""`` is home, the local rendezvous): the server whose VAPID
        key its subscription was made under, whichever rendezvous the hub is on at the moment. A
        rendezvous this hub holds no token for is registered with first. One that cannot be
        reached drops the push — never queued — and is logged once, by origin, never by endpoint.
        """
        base = (origin or self.home or "").rstrip("/")
        if not base or not self.token:
            raise wire.WireError("internal", "this hub is not registered with a rendezvous.")
        fields = {"desktop_id": self.identity.desktop_id, "endpoint": endpoint,
                  "ciphertext": base64.b64encode(payload).decode(),
                  "ttl": push_mod.PUSH_TTL, "urgency": "normal"}
        try:
            for attempt in (0, 1):
                token = self.tokens.get(base)
                if token is None:
                    token = self.tokens[base] = await self._register_at(base)
                try:
                    reply = await self._post("/v1/push/send", {**fields, "token": token},
                                             base=base)
                    break
                except wire.WireError as error:
                    # A token that server no longer takes (it restarted): register once more.
                    if attempt or "failed: 401" not in error.message:
                        raise
                    self.tokens.pop(base, None)
        except OSError as error:                   # URLError, refused, timed out
            if base not in self._push_unreachable:
                self._push_unreachable.add(base)
                log.info("push dropped: rendezvous %s is unreachable (%s)", base,
                         type(error).__name__)
            return {"delivered": False, "status": None, "drop": False}
        self._push_unreachable.discard(base)
        return reply

    # ---- pane_state (relay-terminal-71) ----------------------------------------------------------
    # One pane model, two views (docs/REMOTE-PROTOCOL.md section 16). The GUI publishes each shared
    # pane's state as a `pane_state` line; the hub cleans it (remote/pane_state.py), keeps the
    # latest, and sends each device watching the pane its own capability's view of it. The client
    # actions below forward to the GUI as sidecar lines of the same names, carrying only ids the
    # desktop minted; the GUI resolves them against its own tables and re-checks that the action is
    # one the row offers right now.
    #
    # Never to a participant: `_send_pane_state` refuses a guest's channel before anything else,
    # and every client type here is in wire.GUEST_NEVER, so a guest can neither ask for a state nor
    # act on one. The latest state is kept outside `self.streams` on purpose — a `resume` replays a
    # stream's ring as stored, which would skip both the capability filter and that rule.

    def _pane_state_book(self) -> pane_state_mod.Book:
        book = self.__dict__.get("_pane_states")
        if book is None:
            book = self.__dict__["_pane_states"] = pane_state_mod.Book()
        return book

    def pane_state_from_gui(self, message: dict) -> None:
        """A `pane_state` or `queue_edit_text` line from the GUI (remote/gui_host.py)."""
        kind = message.get("t")
        if kind == "pane_state":
            self.pane_state_published(message)
        elif kind == "queue_edit_text":
            self._queue_edit_answered(message)

    def pane_state_published(self, message: dict) -> None:
        state = pane_state_mod.clean(message)
        if state is None:
            return
        book = self._pane_state_book()
        if not self.source.has_pane(state["pane"]):
            book.forget(state["pane"])
            return
        stamped = book.store(state)
        waiting = book.take_waiting(state["pane"])
        asked = {id(channel) for channel, _ in waiting}
        for channel, request_id in waiting:
            self._spawn(self._send_pane_state(channel, stamped, asked=True, request_id=request_id))
        for channel in list(self.channels.values()):
            if id(channel) not in asked:
                self._spawn(self._send_pane_state(channel, stamped))

    async def _send_pane_state(self, channel: Channel, state: dict, *, asked: bool = False,
                               request_id=None) -> None:
        """One device's view of one state. The capability is read here, as it is sent."""
        if channel.participant_id is not None or channel.closed:
            return                              # never to a guest, whatever asked for it
        if not asked and state["pane"] not in channel.subscribed:
            return
        view = pane_state_mod.for_capability(state, channel.capability())
        if view is None:
            return
        view["seq"] = state["seq"]
        if request_id is not None:
            view["id"] = request_id
        await channel.send(view)

    def _pane_state_line(self, message: dict) -> None:
        """Hand a line to the GUI. Only a source the GUI drives publishes pane state."""
        send = getattr(self.source, "send", None)
        if not callable(send):
            raise wire.WireError("not_permitted", "this desktop does not publish pane state.")
        send(message)

    def _pane_state_origin(self, channel: Channel) -> dict:
        device = channel.device
        return {"origin": f"remote:{channel.device_id}",
                "device_name": device.name if device is not None else ""}

    def _pane_state_pane(self, channel: Channel, message: dict) -> str:
        if channel.participant_id is not None:
            # GUEST_NEVER already refused every type that lands here; this is the second lock.
            raise wire.WireError("not_permitted", "a guest never gets that.")
        return self._pane_of(message)

    async def _on_pane_state_get(self, channel: Channel, message: dict) -> None:
        pane = self._pane_state_pane(channel, message)
        book = self._pane_state_book()
        latest = book.latest.get(pane)
        if latest is not None:
            await self._send_pane_state(channel, latest, asked=True, request_id=message.get("id"))
            return
        # Nothing published yet: ask the GUI, and answer this channel when the state arrives.
        if not book.wait(pane, channel, message.get("id")):
            raise wire.WireError("busy", "that pane's state has been asked for already.")
        self._pane_state_line({"t": "pane_state_get", "pane": pane})

    async def _on_queue_move(self, channel: Channel, message: dict) -> None:
        pane = self._pane_state_pane(channel, message)
        row, to = pane_state_mod.row_of(message), pane_state_mod.move_of(message)
        self._pane_state_line({"t": "queue_move", "pane": pane, "row": row, "to": to,
                               **self._pane_state_origin(channel)})

    async def _on_queue_send_now(self, channel: Channel, message: dict) -> None:
        pane = self._pane_state_pane(channel, message)
        row = pane_state_mod.row_of(message)
        self._pane_state_line({"t": "queue_send_now", "pane": pane, "row": row,
                               **self._pane_state_origin(channel)})

    async def _on_queue_edit(self, channel: Channel, message: dict) -> None:
        """Take a row back to edit it on the phone. The GUI withdraws the row and answers with
        its text, which goes to this device alone — never fanned out, never to anyone else."""
        pane = self._pane_state_pane(channel, message)
        row = pane_state_mod.row_of(message)
        book = self._pane_state_book()
        edit_id = book.new_edit(channel, message.get("id"), pane, row)
        if edit_id is None:
            raise wire.WireError("busy", "too many edits waiting on the desktop.")
        try:
            self._pane_state_line({"t": "queue_edit", "pane": pane, "row": row, "id": edit_id,
                                   **self._pane_state_origin(channel)})
        except wire.WireError:
            book.take_edit(edit_id)
            raise

        async def lapse() -> None:
            await asyncio.sleep(QUEUE_EDIT_TIMEOUT)
            pending = book.take_edit(edit_id)
            if pending is not None:
                await channel.send(wire.error("internal", "the desktop did not answer.",
                                              pending.request_id))

        self._spawn(lapse())

    def _queue_edit_answered(self, message: dict) -> None:
        pending = self._pane_state_book().take_edit(message.get("id"))
        if pending is None:
            return                              # not an id this hub minted, or it lapsed
        if message.get("pane") != pending.pane or message.get("row") != pending.row:
            self._spawn(pending.channel.send(wire.error(
                "internal", "the desktop answered a different row.", pending.request_id)))
            return
        ok, text = pane_state_mod.edit_answer(message)
        channel = pending.channel
        if channel.participant_id is not None:
            return
        if not ok:
            self._spawn(channel.send(wire.error("not_permitted", text, pending.request_id)))
            return
        reply = {"t": "queue_edit_text", "pane": pending.pane, "row": pending.row, "text": text}
        if pending.request_id is not None:
            reply["id"] = pending.request_id
        self._spawn(channel.send(reply))

    async def _on_model_pick(self, channel: Channel, message: dict) -> None:
        pane = self._pane_state_pane(channel, message)
        choice = pane_state_mod.choice_of(message)
        self._pane_state_line({"t": "model_pick", "pane": pane, "choice": choice,
                               **self._pane_state_origin(channel)})

    async def _on_conversation_new(self, channel: Channel, message: dict) -> None:
        pane = self._pane_state_pane(channel, message)
        self._pane_state_line({"t": "conversation_new", "pane": pane,
                               **self._pane_state_origin(channel)})

    async def _on_conversation_open(self, channel: Channel, message: dict) -> None:
        """Open one of this pane's past conversations (owner level, section 16).

        The session is named by a token from a `pane_state` this desktop sent, and the pane
        resolves it against the list it published — a path or a session file name from the wire
        would read somebody else's conversation into the pane.
        """
        pane = self._pane_state_pane(channel, message)
        session = pane_state_mod.session_of(message)
        self._pane_state_line({"t": "conversation_open", "pane": pane, "session": session,
                               **self._pane_state_origin(channel)})


# pane_state (relay-terminal-71): imported here, below the class, so this module's shared import
# block stays untouched while other sessions edit it; nothing above runs before the module is done.
from . import pane_state as pane_state_mod  # noqa: E402

QUEUE_EDIT_TIMEOUT = 15.0          # a wedged GUI is an error on the phone, not a spinner
LIMITS.update({
    "model_pick": (20, 60),        # each one reconfigures the pane's provider
    "conversation_new": (10, 60),
    "conversation_open": (20, 60),   # reading past conversations, not writing anything
    "queue_edit": (60, 60),
})
