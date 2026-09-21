# SPDX-License-Identifier: AGPL-3.0-or-later
"""Join with a meeting code and a PIN (card #97EG): the desktop's half of the code phase.

The owner tells a friend two short things, ``BQRT`` and ``4829``. The **code** is public: the
rendezvous hands it out and uses it to find a room (``/v1/codes`` in rendezvous/server.py). The
**PIN** is secret and never leaves the two ends: both run CPace on it (remote/cpace.py), so the
only way to test a PIN is a live attempt against this desktop, which counts them. Three failures
burn the code and the invite behind it; a correct PIN burns the code too, because it is one use.

What a correct PIN earns is small on purpose: **one fragment**, sealed under the CPace key, after
which the existing path runs unchanged. There are two kinds of code and the difference is in the
fragment, never in the code or the page it was typed on:

* ``KIND_INVITE`` (card #97EG) delivers an invite fragment (``v=1&d=…&i=…&r=…``): Noise IK against
  ``d``, ``knock``, and the owner admitting the knock by hand (docs/REMOTE-PROTOCOL.md section 10).
  The invite is minted ``single_knock``: the first knock claims it and any later one is refused, so
  the fragment is worth nothing once used, even to someone who read it over a shoulder.
* ``KIND_PAIR`` (card #FR1C) delivers the desktop's **pairing** fragment (``v=1&d=…&s=…&r=…``) —
  what the pairing QR holds — for the owner's own phone, which has no camera pointed at the
  desktop and nowhere to paste a 140-character link. From there section 5 runs unchanged: Noise IK
  against ``d``, ``pair_prove``, the five digits compared on both screens, the capability the owner
  allows, and the connect token in ``paired``. The pairing room is minted for that code alone and
  is in no QR, so a scan and a typed code can never race for the same room.

The two are **not interchangeable**, and neither end has to be told which it is holding: the
fragment's own shape decides (``pairing.fragment_kind``), and the desktop refuses the mismatch a
second time whichever room it is offered on — ``Host._on_knock`` has no invite for a pairing room,
``Host._on_pair_prove`` refuses an invite room by name.

This module never records a participant, never pairs a device and never admits anyone: everything
it hands over is checked again by the handler it is handed to.

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

# What the code is a door to. The word is the one `pairing.fragment_kind` returns for the fragment
# the code phase delivers, so a record and its fragment can never disagree about which it is. In
# the audit log it is written as `code_kind`, because every line's own type is already its `kind`.
KIND_INVITE, KIND_PAIR = "invite", "pair"


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
    """One meeting code, in memory only. The PIN and the fragment are never written down: not to
    disk, not to the audit log, and the fragment is dropped the moment the code ends."""
    room: str
    invite_id: str                      # the invite behind an invite code; "" for a pairing code
    pin: str
    fragment: str                       # what a correct PIN earns; "" once the code is over
    panes: tuple[str, ...] = ()
    kind: str = KIND_INVITE             # KIND_INVITE or KIND_PAIR
    pair_room: str = ""                 # the pairing room behind a pairing code; "" otherwise
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

    def live_pairs(self) -> list[CodeRecord]:
        """The live pairing codes. There is at most one: a pairing code is about this desktop
        rather than about a pane, so the pane rule below has nothing to key on and "one at a
        time" takes its place."""
        return [record for record in self.records.values()
                if record.state == LIVE and record.kind == KIND_PAIR]

    def _burn_behind(self, record: CodeRecord) -> None:
        """What the code was a door to dies with it: a pairing code's room (nobody can prove a
        secret in a room that is gone), an invite code's invite."""
        if record.kind == KIND_PAIR:
            if record.pair_room:
                self.host.rooms.burn(record.pair_room)
        elif record.invite_id:
            self.host.guests.burn_invite(record.invite_id)

    async def revoke(self, code: str, kind: str = "") -> CodeRecord | None:
        """The owner withdrew a live code: it burns, and whatever it was a door to with it. None
        when there is no such code, or it has already ended.

        ``kind`` is the caller saying which sort of code it means — the sharing panel's
        ``code_revoke`` withdraws a share code, the pairing dialog's ``pair_code_revoke`` a
        pairing code — so neither surface can reach past its own codes into the other's.
        """
        record = self.by_code(code)
        if record is None or record.state != LIVE or (kind and record.kind != kind):
            return None
        await self.burn(record, reason="revoked")
        return record

    async def create(self, invite, url: str) -> CodeRecord:
        """A meeting code for a share invitation (card #97EG)."""
        # One live code per pane: a new one replaces the old, which burns as if withdrawn. Two
        # codes for one pane would be two doors, and the owner is only looking at one of them.
        for older in self.live_on(invite.panes):
            await self.burn(older, reason="replaced")
        return await self._create(self._fragment(url, "invite"), kind=KIND_INVITE,
                                  invite_id=invite.invite_id, panes=tuple(invite.panes),
                                  role=invite.role, behind_ttl=float(invite.seconds_left()))

    async def create_pair(self, url: str, room) -> CodeRecord:
        """A pairing code for the owner's own phone (card #FR1C): what a correct PIN earns is the
        pairing fragment of ``url``, the one behind the QR.

        ``room`` is the :class:`pairing.Room` that fragment points at, opened for this code and
        for nothing else — a room that were also in a QR would let a scan and a typed code race
        for its one use. One live pairing code at a time, for the same reason a pane has one.
        """
        for older in self.live_pairs():
            await self.burn(older, reason="replaced")
        return await self._create(self._fragment(url, "pairing"), kind=KIND_PAIR,
                                  pair_room=room.room, behind_ttl=float(room.seconds_left()))

    @staticmethod
    def _fragment(url: str, what: str) -> str:
        fragment = url.split("#", 1)[1] if "#" in url else ""
        if not fragment:
            raise wire.WireError("internal", f"the {what} link has no fragment.")
        return fragment

    async def _create(self, fragment: str, *, kind: str, behind_ttl: float,
                      invite_id: str = "", pair_room: str = "", panes: tuple = (),
                      role: str = "") -> CodeRecord:
        """Open a code room, register it, and have the rendezvous allocate a code for it.

        The room is recognised as a code room **before** a code points at it, so there is no
        moment in which a connection to it could fall through to the Noise channel path. A code
        that cannot be allocated takes what it was a door to down with it: nobody will ever hold
        that fragment, and leaving the room open would leave a way in with no way to withdraw it.
        """
        host = self.host
        record = CodeRecord(room="", invite_id=invite_id, pin=new_pin(), fragment=fragment,
                            panes=panes, kind=kind, pair_room=pair_room)
        try:
            room, granted = await host._open_room(CODE_LIFETIME)
        except Exception:
            self._burn_behind(record)
            raise
        record.room = room
        self.records[room] = record
        try:
            reply = await host._post("/v1/codes", {
                "desktop_id": host.identity.desktop_id, "token": host.token, "room": room,
                "ttl": min(CODE_LIFETIME, granted, behind_ttl)})
            code = str(reply["code"]).upper()
            lifetime = float(reply.get("expires_in", CODE_LIFETIME))
        except Exception:
            record.state, record.fragment, record.expires = BURNED, "", time.time()
            self._burn_behind(record)
            raise
        record.code = code
        record.expires = time.time() + lifetime
        host.audit.record("code_create", code_kind=kind, code=code, invite=invite_id or None,
                          panes=list(panes), role=role or None,
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
        self.host.audit.record("code_attempt", code_kind=record.kind, code=record.code,
                               invite=record.invite_id or None, peer=peer, reason=reason,
                               failures=record.failures + 1)
        record.failures += 1
        if record.failures < MAX_FAILURES:
            return False
        await self.burn(record)
        return True

    async def burn(self, record: CodeRecord, reason: str = "failures") -> None:
        """The code stops resolving and what it opened dies with it: three failures, the owner
        withdrawing it (``revoked``), or a newer code in its place (``replaced``)."""
        if record.state != LIVE:
            return
        self.host.audit.record("code_burned", code_kind=record.kind, code=record.code,
                               invite=record.invite_id or None, failures=record.failures,
                               reason=reason)
        record.state, record.fragment = BURNED, ""
        self._burn_behind(record)
        self._tell(record)
        await self._forget_at_rendezvous(record)

    async def used(self, record: CodeRecord, peer: str) -> str:
        """A confirmed PIN: the code is spent, and the fragment goes to the caller, once."""
        record.in_flight = max(0, record.in_flight - 1)
        self.host.audit.record("code_used", code_kind=record.kind, code=record.code,
                               invite=record.invite_id or None, peer=peer,
                               failures=record.failures)
        fragment, record.fragment, record.state = record.fragment, "", USED
        self._tell(record)
        self.host._spawn(self._forget_at_rendezvous(record))
        return fragment

    async def expire(self, record: CodeRecord) -> None:
        """Ten minutes, unused: the code ends, and so does the door nobody went through."""
        if record.state != LIVE:
            return
        self.host.audit.record("code_expired", code_kind=record.kind, code=record.code,
                               invite=record.invite_id or None, failures=record.failures)
        record.state, record.fragment = EXPIRED, ""
        self._burn_behind(record)
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
    """One connection to a code room: CPace, key confirmation, and the sealed fragment.

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
        await self.close("fragment delivered")
