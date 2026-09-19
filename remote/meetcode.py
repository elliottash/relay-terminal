# SPDX-License-Identifier: AGPL-3.0-or-later
"""Join with a meeting code and a PIN (card #97EG): the desktop's half of the code phase.

The owner tells a friend two short things, ``BQRT`` and ``4829``. The **code** is public: the
rendezvous hands it out and uses it to find a room (``/v1/codes`` in rendezvous/server.py). The
**PIN** is secret and never leaves the two ends: both run CPace on it (remote/cpace.py), so the
only way to test a PIN is a live attempt against this desktop, which counts them. Three failures
burn the code and the invite behind it; a correct PIN burns the code too, because it is one use.

What a correct PIN earns is small on purpose: an ordinary invite fragment (``v=1&d=…&i=…&r=…``),
sealed under the CPace key, after which the existing path runs unchanged — Noise IK against ``d``,
``knock``, and the owner admitting the knock by hand (docs/REMOTE-PROTOCOL.md section 10). This
module never records a participant and never admits anyone. The invite it delivers is minted
``single_knock``: the first knock claims it and any later one is refused, so the fragment is worth
nothing once used, even to someone who read it over a shoulder.

A code room is a rendezvous room of its own. A connection to one is an :class:`Attempt`, never a
Noise channel: ``Host._open_channel`` asks :meth:`CodeBook.is_code_room` first, and a room stays
known here after its code is used, burned or expired, so a late connection is still answered with
``meet_error`` rather than reaching a hub handler.

Frames on a code room are UTF-8 JSON in **binary** WebSocket messages (the rendezvous relays
nothing else), binary fields unpadded base64url:

    guest → desktop   {"t":"meet_a","y":Ya}
    desktop → guest   {"t":"meet_b","y":Yb,"tag":tag_b}
    guest → desktop   {"t":"meet_confirm","tag":tag_a}        only after tag_b checked out
    desktop → guest   {"t":"meet_invite","nonce":n,"sealed":c}
    desktop → guest   {"t":"meet_error","error":"wrong_pin"|"burned"|"expired"}, then close
"""
from __future__ import annotations

import asyncio
import contextlib
import hmac
import json
import logging
import os
import secrets
import time
from dataclasses import dataclass
from hashlib import sha256

from cryptography.hazmat.primitives.ciphers.aead import AESGCM

from . import envelope, pairing, wire
from .cpace import CPace, CPaceError

log = logging.getLogger("relay.meetcode")

# The wire spec's constants (card #97EG). The browser (app/guest.js) uses the same bytes.
CI = b"relay/meet/v1"
AD_GUEST = b"guest"                  # ADa: the initiator is the guest's browser
AD_DESKTOP = b"desktop"              # ADb: the responder is this desktop
TAG_DESKTOP = b"relay/meet/v1 desktop"
TAG_GUEST = b"relay/meet/v1 guest"
SEAL_LABEL = b"relay/meet/v1 seal"

CODE_LIFETIME = 600.0                # ten minutes, one use
ATTEMPT_TIMEOUT = 30.0               # an attempt that has not confirmed by now has failed
MAX_FAILURES = 3                     # the third failure burns the code and its invite
PIN_DIGITS = 4
MAX_FRAME = 4096                     # the largest code-room frame is a few hundred bytes
FORGET_AFTER = 3600.0                # how long a finished code's room is still recognised

LIVE, USED, BURNED, EXPIRED = "live", "used", "burned", "expired"


class MeetError(Exception):
    """A code-phase refusal, carrying the reason the wire and the audit log name."""

    def __init__(self, reason: str):
        super().__init__(reason)
        self.reason = reason


# ---- the crypto both ends share -----------------------------------------------------------------

def new_pin() -> str:
    """Four digits from the CSPRNG. Leading zeros are digits too."""
    return f"{secrets.randbelow(10 ** PIN_DIGITS):0{PIN_DIGITS}d}"


def confirm_tags(isk: bytes, ya: bytes, yb: bytes) -> tuple[bytes, bytes]:
    """``(tag_a, tag_b)``: the guest's and the desktop's proof that they derived the same ISK.

    Both cover Ya and Yb, so a tag from one attempt cannot be replayed into another, and they are
    labelled apart, so the desktop's tag is never a valid guest tag.
    """
    tag_a = hmac.new(isk, TAG_GUEST + ya + yb, sha256).digest()
    tag_b = hmac.new(isk, TAG_DESKTOP + ya + yb, sha256).digest()
    return tag_a, tag_b


def seal_key(isk: bytes) -> bytes:
    return hmac.new(isk, SEAL_LABEL, sha256).digest()


def seal(isk: bytes, code: str, fragment: str) -> dict:
    """The ``meet_invite`` frame: the fragment under AES-256-GCM, bound to the code."""
    nonce = os.urandom(12)
    sealed = AESGCM(seal_key(isk)).encrypt(nonce, fragment.encode(), code.upper().encode())
    return {"t": "meet_invite", "nonce": pairing.b64(nonce), "sealed": pairing.b64(sealed)}


def unseal(isk: bytes, code: str, frame: dict) -> str:
    """The other half of :func:`seal`. Raises on anything but the sealed fragment."""
    try:
        nonce = pairing.un64(str(frame.get("nonce", "")))
        sealed = pairing.un64(str(frame.get("sealed", "")))
        return AESGCM(seal_key(isk)).decrypt(nonce, sealed, code.upper().encode()).decode()
    except Exception as error:
        raise MeetError("bad_seal") from error


def encode(message: dict) -> bytes:
    return json.dumps(message, separators=(",", ":")).encode()


def decode(data) -> dict:
    """One code-room frame, or MeetError("bad_frame")."""
    if not isinstance(data, (bytes, bytearray)) or len(data) > MAX_FRAME:
        raise MeetError("bad_frame")
    try:
        message = json.loads(bytes(data).decode("utf-8"))
    except (UnicodeDecodeError, ValueError) as error:
        raise MeetError("bad_frame") from error
    if not isinstance(message, dict) or not isinstance(message.get("t"), str):
        raise MeetError("bad_frame")
    return message


def point(message: dict, field: str = "y") -> bytes:
    """A 32-byte curve point from a frame. A malformed one is an invalid point, not a bad frame:
    it is the guest's CPace message that failed."""
    try:
        value = pairing.un64(str(message.get(field, "")))
    except Exception as error:
        raise MeetError("invalid_point") from error
    if len(value) != 32:
        raise MeetError("invalid_point")
    return value


# ---- the desktop's records ----------------------------------------------------------------------

@dataclass
class CodeRecord:
    """One meeting code, in memory only. The PIN and the invite fragment are never written down:
    not to disk, not to the audit log, and the fragment is dropped the moment the code ends."""
    room: str
    invite_id: str
    pin: str
    fragment: str                       # "v=1&d=…&i=…&r=…"; "" once the code is over
    panes: tuple[str, ...] = ()
    code: str = ""                      # "" until the rendezvous has allocated one
    expires: float = 0.0                # absolute, time.time()
    state: str = "pending"              # pending → live → used | burned | expired
    failures: int = 0
    in_flight: int = 0                  # attempts begun and not yet settled

    def seconds_left(self) -> int:
        return max(0, int(self.expires - time.time()))


class CodeBook:
    """The desktop's meeting codes, by code room, and the policy that counts and burns them.

    The **desktop** counts, never the rendezvous: the server is the party this design declines to
    trust, so a count it kept would be a count it could reset.
    """

    def __init__(self, host):
        self.host = host
        self.records: dict[str, CodeRecord] = {}
        self._watchers: list = []

    def on_state(self, callback) -> None:
        """``callback(record)`` whenever a code ends: used, burned or expired."""
        self._watchers.append(callback)

    def _tell(self, record: CodeRecord) -> None:
        for callback in list(self._watchers):
            try:
                callback(record)
            except Exception:
                log.exception("code state callback failed")

    def _prune(self) -> None:
        now = time.time()
        for room in [room for room, record in self.records.items()
                     if record.state not in (LIVE, "pending")
                     and record.expires + FORGET_AFTER < now]:
            del self.records[room]

    def is_code_room(self, room: str) -> bool:
        """Whether a connection on ``room`` belongs to this module rather than to a Noise channel.

        True for a code that is live, and for one that has ended — a connection to a spent code's
        room is still a code-room connection, and is answered here with `meet_error`.
        """
        self._prune()
        return bool(room) and room in self.records

    def by_code(self, code: str) -> CodeRecord | None:
        code = code.strip().upper()
        for record in self.records.values():
            if code and record.code == code:
                return record
        return None

    def live_on(self, panes) -> list[CodeRecord]:
        wanted = set(panes)
        return [record for record in self.records.values()
                if record.state == LIVE and wanted & set(record.panes)]

    async def revoke(self, code: str) -> CodeRecord | None:
        """The owner withdrew a live code: it burns, and its invite with it. None when there is
        no such code, or it has already ended."""
        record = self.by_code(code)
        if record is None or record.state != LIVE:
            return None
        await self.burn(record, reason="revoked")
        return record

    async def create(self, invite, url: str) -> CodeRecord:
        """Open a code room, register it, and have the rendezvous allocate a code for it.

        The room is recognised as a code room **before** a code points at it, so there is no
        moment in which a connection to it could fall through to the Noise channel path.
        """
        host = self.host
        fragment = url.split("#", 1)[1] if "#" in url else ""
        if not fragment:
            raise wire.WireError("internal", "the invite link has no fragment.")
        # One live code per pane: a new one replaces the old, which burns as if withdrawn. Two
        # codes for one pane would be two doors, and the owner is only looking at one of them.
        for older in self.live_on(invite.panes):
            await self.burn(older, reason="replaced")
        try:
            room, granted = await host._open_room(CODE_LIFETIME)
        except Exception:
            host.guests.burn_invite(invite.invite_id)    # nobody will ever hold its link
            raise
        record = CodeRecord(room=room, invite_id=invite.invite_id, pin=new_pin(),
                            fragment=fragment, panes=tuple(invite.panes))
        self.records[room] = record
        try:
            reply = await host._post("/v1/codes", {
                "desktop_id": host.identity.desktop_id, "token": host.token, "room": room,
                "ttl": min(CODE_LIFETIME, granted, float(invite.seconds_left()))})
            code = str(reply["code"]).upper()
            lifetime = float(reply.get("expires_in", CODE_LIFETIME))
        except Exception:
            record.state, record.fragment, record.expires = BURNED, "", time.time()
            host.guests.burn_invite(invite.invite_id)
            raise
        record.code = code
        record.expires = time.time() + lifetime
        host.audit.record("code_create", code=code, invite=invite.invite_id,
                          panes=list(invite.panes), role=invite.role,
                          expires=round(record.expires, 3))
        record.state = LIVE
        asyncio.get_running_loop().call_later(
            lifetime, lambda: host._spawn(self.expire(record)))
        return record

    def attempt(self, channel_id: bytes, meta: dict) -> "Attempt":
        return Attempt(self, self.records[meta.get("room") or ""], channel_id, meta)

    # ---- the outcomes --------------------------------------------------------------------------

    def begin(self, record: CodeRecord) -> str | None:
        """Take one of the code's three tries for a new attempt, or say why there is none.

        A try is taken when the connection arrives, not when it fails: three connections open at
        once must not be three hundred guesses, because a guest learns from ``tag_b`` whether its
        PIN was right before it has to confirm anything.
        """
        if record.state == LIVE and time.time() >= record.expires:
            self.host._spawn(self.expire(record))
            return EXPIRED
        if record.state in (EXPIRED, "pending"):
            return EXPIRED
        if record.state != LIVE:
            return BURNED                   # burned, or used: either way this code is spent
        if record.failures + record.in_flight >= MAX_FAILURES:
            return BURNED
        record.in_flight += 1
        return None

    async def failed(self, record: CodeRecord, reason: str, peer: str) -> bool:
        """Count a failed attempt. True when it was the one that burned the code."""
        record.in_flight = max(0, record.in_flight - 1)
        if record.state != LIVE:
            return record.state == BURNED
        self.host.audit.record("code_attempt", code=record.code, invite=record.invite_id,
                               peer=peer, reason=reason, failures=record.failures + 1)
        record.failures += 1
        if record.failures < MAX_FAILURES:
            return False
        await self.burn(record)
        return True

    async def burn(self, record: CodeRecord, reason: str = "failures") -> None:
        """The code stops resolving and its invite dies with it: three failures, the owner
        withdrawing it (``revoked``), or a newer code for the same pane (``replaced``)."""
        if record.state != LIVE:
            return
        self.host.audit.record("code_burned", code=record.code, invite=record.invite_id,
                               failures=record.failures, reason=reason)
        record.state, record.fragment = BURNED, ""
        self.host.guests.burn_invite(record.invite_id)
        self._tell(record)
        await self._forget_at_rendezvous(record)

    async def used(self, record: CodeRecord, peer: str) -> str:
        """A confirmed PIN: the code is spent, and the fragment goes to the caller, once."""
        record.in_flight = max(0, record.in_flight - 1)
        self.host.audit.record("code_used", code=record.code, invite=record.invite_id,
                               peer=peer, failures=record.failures)
        fragment, record.fragment, record.state = record.fragment, "", USED
        self._tell(record)
        self.host._spawn(self._forget_at_rendezvous(record))
        return fragment

    async def expire(self, record: CodeRecord) -> None:
        """Ten minutes, unused: the code ends, and so does the invite nobody fetched."""
        if record.state != LIVE:
            return
        self.host.audit.record("code_expired", code=record.code, invite=record.invite_id,
                               failures=record.failures)
        record.state, record.fragment = EXPIRED, ""
        self.host.guests.burn_invite(record.invite_id)
        self._tell(record)
        await self._forget_at_rendezvous(record)

    async def _forget_at_rendezvous(self, record: CodeRecord) -> None:
        """``POST /v1/codes/<code>/burn``, so the code stops resolving now rather than at its
        expiry. Best effort: the desktop has already refused the code whatever the server does."""
        if not record.code or self.host.rendezvous is None:
            return
        try:
            await self.host._post(f"/v1/codes/{record.code}/burn",
                                  {"desktop_id": self.host.identity.desktop_id,
                                   "token": self.host.token})
        except Exception as error:
            log.info("burning code at the rendezvous failed: %s", error)


class Attempt:
    """One connection to a code room: CPace, key confirmation, and the sealed invite.

    It sits in ``Host.channels`` beside the Noise channels, so the rendezvous link's routing, a
    lost link and shutdown all reach it unchanged. Everywhere else the hub looks at a channel it
    is **nobody**: no device, no participant, no capability, nothing subscribed, and ``send`` of a
    hub message does nothing. The only bytes that leave on a code room are this class's own
    frames, written by :meth:`_send`.
    """

    device_id = None
    participant_id = None
    client_static = None
    session = None
    stale_guest = False
    visible = False

    def __init__(self, book: CodeBook, record: CodeRecord, channel_id: bytes, meta: dict):
        self.book = book
        self.host = book.host
        self.record = record
        self.id = channel_id
        self.meta = meta
        self.room = record.room
        self.peer = meta.get("peer", "?")
        self.inbox: asyncio.Queue[bytes | None] = asyncio.Queue(maxsize=8)
        self.subscribed: set[str] = set()
        self.closed = False
        self.opened = time.monotonic()
        self.last_seen = self.opened

    # ---- what the hub may ask of any channel ---------------------------------------------------

    @property
    def device(self):
        return None

    @property
    def participant(self):
        return None

    def capability(self) -> None:
        return None

    def role(self) -> None:
        return None

    def who(self) -> str:
        return self.id.hex()

    async def send(self, message: dict) -> None:
        """A hub message never goes out on a code room, whatever sent it."""
        return

    async def close(self, reason: str = "") -> None:
        if self.closed:
            return
        self.closed = True
        with contextlib.suppress(Exception):
            await self.host._send_envelope(envelope.KIND_CLOSE, self.id,
                                           json.dumps({"reason": reason}).encode())
        with contextlib.suppress(asyncio.QueueFull):
            self.inbox.put_nowait(None)

    # ---- the attempt ---------------------------------------------------------------------------

    async def _send(self, message: dict) -> None:
        if self.closed:
            return
        await self.host._send_envelope(envelope.KIND_DATA, self.id, encode(message))

    async def _next(self) -> dict:
        data = await self.inbox.get()
        if data is None:
            raise MeetError("closed")
        return decode(data)

    async def _exchange(self) -> tuple[bytes, bytes, bytes]:
        """meet_a → meet_b → meet_confirm. Returns the ISK once the guest's tag checks out."""
        first = await self._next()
        if first["t"] != "meet_a":
            raise MeetError("bad_frame")
        ya = point(first)
        try:
            party = CPace(self.record.pin.encode(), CI, self.room.encode(), initiator=False,
                          ad=AD_DESKTOP)
            isk = party.finish(ya, AD_GUEST)
        except CPaceError as error:
            raise MeetError("invalid_point") from error
        yb = party.message
        tag_a, tag_b = confirm_tags(isk, ya, yb)
        await self._send({"t": "meet_b", "y": pairing.b64(yb), "tag": pairing.b64(tag_b)})
        second = await self._next()
        if second["t"] != "meet_confirm":
            raise MeetError("bad_frame")
        try:
            offered = pairing.un64(str(second.get("tag", "")))
        except Exception:
            offered = b""
        if not hmac.compare_digest(offered, tag_a):
            raise MeetError("wrong_pin")
        return isk

    def _link_is_ours(self) -> bool:
        """False when the desktop itself is going away: that ends an attempt, but it is not the
        guest's failure and does not count against the code."""
        return self.host.socket is not None and self.host._running

    async def run(self) -> None:
        record = self.record
        refusal = self.book.begin(record)
        if refusal is not None:
            with contextlib.suppress(Exception):
                await self._send({"t": "meet_error", "error": refusal})
            await self.close(refusal)
            return
        try:
            isk = await asyncio.wait_for(self._exchange(), ATTEMPT_TIMEOUT)
        except asyncio.CancelledError:
            record.in_flight = max(0, record.in_flight - 1)
            raise
        except (asyncio.TimeoutError, MeetError, wire.WireError) as error:
            reason = error.reason if isinstance(error, MeetError) else \
                "timeout" if isinstance(error, asyncio.TimeoutError) else "closed"
            if not self._link_is_ours():
                record.in_flight = max(0, record.in_flight - 1)
                await self.close("the desktop went away")
                return
            burned = await self.book.failed(record, reason, self.peer)
            with contextlib.suppress(Exception):
                await self._send({"t": "meet_error", "error": BURNED if burned else "wrong_pin"})
            await self.close("code attempt failed")
            return
        if record.state == LIVE and time.time() >= record.expires:
            await self.book.expire(record)          # the timer has not fired yet; the clock has
        if record.state != LIVE:
            # Right PIN, but the code ended while this attempt was in the air: expired, or used by
            # the attempt that confirmed first.
            record.in_flight = max(0, record.in_flight - 1)
            with contextlib.suppress(Exception):
                await self._send({"t": "meet_error",
                                  "error": EXPIRED if record.state == EXPIRED else BURNED})
            await self.close("code no longer live")
            return
        fragment = await self.book.used(record, self.peer)
        with contextlib.suppress(Exception):
            await self._send(seal(isk, record.code, fragment))
        await self.close("invite delivered")
