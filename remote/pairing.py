# SPDX-License-Identifier: GPL-3.0-or-later
"""Pairing: the QR link, the one-time secret and the rooms that carry them.

Kept on its own because settings sync (`#05J2`) is meant to reuse exactly this machinery rather
than grow a second one. Nothing here knows about panes or agents; it establishes that two keys
belong together.

The link's secret lives in the URL **fragment**, which browsers never send to a server, so the
rendezvous that hands out the room id never learns the secret that proves possession of it
(docs/REMOTE-PROTOCOL.md section 5).
"""
from __future__ import annotations

import base64
import hmac
import secrets
import time
from dataclasses import dataclass, field
from urllib.parse import parse_qs, unquote, urlsplit

SECRET_BYTES = 16          # 128 bits
ROOM_TTL = 300             # five minutes
MAX_ATTEMPTS = 5           # for the typed word code, section 5.2


def b64(data: bytes) -> str:
    return base64.urlsafe_b64encode(data).decode().rstrip("=")


def un64(text: str) -> bytes:
    return base64.urlsafe_b64decode(text + "=" * (-len(text) % 4))


def fingerprint(public_key: bytes) -> str:
    """A short human-comparable form of a key, shown on both ends before pinning."""
    from hashlib import sha256
    digest = sha256(public_key).hexdigest()[:12].upper()
    return " ".join(digest[i:i + 4] for i in range(0, 12, 4))


def pair_url(app_base: str, desktop_public: bytes, secret: bytes, room: str) -> str:
    """The URL behind the QR code. Everything sensitive is after the '#'."""
    return (f"{app_base.rstrip('/')}/pair"
            f"#v=1&d={b64(desktop_public)}&s={b64(secret)}&r={room}")


def join_url(app_base: str, desktop_public: bytes, secret: bytes, room: str, role: str) -> str:
    """A multiplayer invite (section 10): the same shape, a different path, and `k` names the
    role the invite grants. The `k` in the link is a hint for the person scanning; the desktop's
    own record of the room is what enforcement reads."""
    return (f"{app_base.rstrip('/')}/join"
            f"#v=1&d={b64(desktop_public)}&s={b64(secret)}&r={room}&k={role}")


def parse_pair_url(url: str) -> dict:
    parts = urlsplit(url)
    if not parts.fragment:
        raise ValueError("that link carries no pairing fragment.")
    fragment = parts.fragment
    # Some QR readers and link handlers percent-encode the fragment, which turns the separators
    # into %26 and leaves one field holding the rest of the link.
    if "&" not in fragment and "%26" in fragment.lower():
        fragment = unquote(fragment)
    fields = {name: values[0] for name, values in parse_qs(fragment).items()}
    for required in ("v", "d", "s", "r"):
        if required not in fields:
            raise ValueError(f"the pairing link is missing '{required}'.")
    if fields["v"] != "1":
        raise ValueError(f"unsupported pairing version {fields['v']}.")
    desktop = un64(fields["d"])
    if len(desktop) != 32:
        raise ValueError("the desktop key in the link is not 32 bytes.")
    return {"desktop_public": desktop, "secret": un64(fields["s"]), "room": fields["r"]}


@dataclass
class Room:
    """One pairing opportunity. A pair room is single use; an invite room (section 10) admits
    several joiners while it lives — `uses` counts down and the room dies at zero."""
    room: str
    secret: bytes = field(default_factory=lambda: secrets.token_bytes(SECRET_BYTES))
    opened: float = field(default_factory=time.time)
    ttl: float = ROOM_TTL
    attempts: int = 0
    spent: bool = False
    uses: int = 1                   # a pair room admits one device, full stop
    kind: str = "pair"              # "pair" or "invite"
    role: str = ""                  # the role an invite grants

    @property
    def expired(self) -> bool:
        return self.spent or time.time() > self.opened + self.ttl

    def seconds_left(self) -> int:
        return max(0, int(self.opened + self.ttl - time.time()))

    def check(self, secret: bytes) -> bool:
        """Constant-time check. A failed attempt counts; five burn the room. A success spends
        one use, and the room is gone with the last of them."""
        if self.expired:
            return False
        self.attempts += 1
        if not hmac.compare_digest(self.secret, secret):
            if self.attempts >= MAX_ATTEMPTS:
                self.spent = True     # five wrong guesses and the room is gone
            return False
        self.uses -= 1
        if self.uses <= 0:
            # Spent whether or not the user goes on to allow the device: another proof fails.
            self.spent = True
        return True


class RoomBook:
    """The desktop's open pairing rooms and invites."""

    def __init__(self):
        self.rooms: dict[str, Room] = {}

    def open(self, room_id: str, ttl: float = ROOM_TTL) -> Room:
        self.sweep()
        room = Room(room=room_id, ttl=ttl)
        self.rooms[room_id] = room
        return room

    def open_invite(self, room_id: str, role: str, ttl: float, uses: int) -> Room:
        self.sweep()
        room = Room(room=room_id, ttl=ttl, uses=max(1, uses), kind="invite", role=role)
        self.rooms[room_id] = room
        return room

    def get(self, room_id: str) -> Room | None:
        self.sweep()
        return self.rooms.get(room_id)

    def burn(self, room_id: str) -> None:
        self.rooms.pop(room_id, None)

    def sweep(self) -> None:
        for room_id in [r for r, room in self.rooms.items() if room.expired]:
            self.rooms.pop(room_id, None)
