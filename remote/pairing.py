# SPDX-License-Identifier: AGPL-3.0-or-later
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
from hashlib import sha256
from urllib.parse import parse_qs, unquote, urlsplit

SECRET_BYTES = 16          # 128 bits
ROOM_TTL = 300             # five minutes
MAX_ATTEMPTS = 5           # for the typed word code, section 5.2

# ---- per-device connect tokens (docs/REMOTE-PROTOCOL.md section 8) -----------------------------
# A `desktop_id` is SHA-256 of the static key, and that key is in the fragment of every pairing
# and invite link, so anyone who ever held a link can compute the id and ask the rendezvous for a
# channel to that desktop. A connect token is what makes `/v1/connect` answer *this device*
# rather than *that desktop*: the desktop mints one per device inside the Noise session, the
# rendezvous checks it with a secret the desktop registered, and channels are counted per token.
CONNECT_TOKEN_CONTEXT = b"relay/connect-token/v1"
CONNECT_TOKEN_ID_BYTES = 9      # 72 bits: an id, not a secret — the MAC is what authenticates
CONNECT_MAC_BYTES = 16          # 128 bits of HMAC-SHA-256, truncated
MAX_REVOKED_TOKENS = 256        # what one registration carries; see `Host.revoked_connect_tokens`


def b64(data: bytes) -> str:
    return base64.urlsafe_b64encode(data).decode().rstrip("=")


def un64(text: str) -> bytes:
    return base64.urlsafe_b64decode(text + "=" * (-len(text) % 4))


def fingerprint(public_key: bytes) -> str:
    """A short human-comparable form of a key, shown on both ends before pinning."""
    from hashlib import sha256
    digest = sha256(public_key).hexdigest()[:12].upper()
    return " ".join(digest[i:i + 4] for i in range(0, 12, 4))


def connect_secret(identity_private: bytes) -> bytes:
    """The per-desktop secret connect tokens are keyed on, derived from the identity key.

    Derived rather than stored, so it survives a restart, needs no new file beside the identity,
    and is the same at whatever rendezvous this desktop registers with — which is what lets a
    phone keep its token when the desktop moves from its own rendezvous to the hosted one
    (`Host.rehome`). The rendezvous is handed this secret and can therefore mint tokens of its
    own; that grants a channel slot and nothing else, and a rendezvous that wanted to deny its
    own channels can simply refuse them. It is not a content key and opens no session: every byte
    through a channel is still sealed to the pinned Noise static key.
    """
    return hmac.new(identity_private, CONNECT_TOKEN_CONTEXT, sha256).digest()


def new_connect_token_id() -> str:
    """A fresh token id, kept on the device (or participant) record so it can be revoked."""
    return b64(secrets.token_bytes(CONNECT_TOKEN_ID_BYTES))


def connect_mac(secret: bytes, desktop_id: str, device_id: str, token_id: str) -> str:
    """HMAC over the three things a token is only ever valid for, each length-prefixed, so no
    two different triples can read as the same message whatever characters they hold."""
    message = b"".join(len(field).to_bytes(2, "big") + field
                       for field in (str(desktop_id).encode()[:65535],
                                     str(device_id).encode()[:65535],
                                     str(token_id).encode()[:65535]))
    return b64(hmac.new(secret, message, sha256).digest()[:CONNECT_MAC_BYTES])


def mint_connect_token(secret: bytes, desktop_id: str, device_id: str, token_id: str) -> str:
    """`<token id>.<mac>` — what the client presents as `ct` on `/v1/connect`."""
    if not token_id:
        return ""
    return f"{token_id}.{connect_mac(secret, desktop_id, device_id, token_id)}"


def check_connect_token(secret: bytes, desktop_id: str, device_id: str, token: str) -> str:
    """The token id a valid token carries, or "" — which the caller must treat as no token.

    The MAC covers the desktop, the device id the client is asking to be and the token id, so a
    token minted for desktop A does not open desktop B, and one minted for a device cannot be
    replayed under another device's name. Nothing here reads a database: the check is one HMAC
    against the secret the desktop registered, so the rendezvous keeps no per-device state and
    needs no round trip to the desktop at connect time.
    """
    if not isinstance(token, str) or token.count(".") != 1:
        return ""
    token_id, mac = token.split(".", 1)
    if not token_id or not mac or len(token_id) > 64 or len(mac) > 64:
        return ""
    if not hmac.compare_digest(connect_mac(secret, desktop_id, device_id, token_id), mac):
        return ""
    return token_id


def pair_url(app_base: str, desktop_public: bytes, secret: bytes, room: str) -> str:
    """The URL behind the QR code. Everything sensitive is after the '#'."""
    return (f"{app_base.rstrip('/')}/pair"
            f"#v=1&d={b64(desktop_public)}&s={b64(secret)}&r={room}")


def invite_url(app_base: str, desktop_public: bytes, secret: bytes, room: str) -> str:
    """A multiplayer invite (section 10.2): the same shape as a pairing link, a different path,
    and `i` rather than `s` so a link cannot be fed to the wrong handler by accident.

    The role the invite grants is deliberately **not** in the link. Enforcement reads the invite
    record on the desktop; a role in the fragment would only be a claim the holder could edit,
    and a number the person scanning might believe.
    """
    return (f"{app_base.rstrip('/')}/join"
            f"#v=1&d={b64(desktop_public)}&i={b64(secret)}&r={room}")


def _fragment_fields(url: str, what: str) -> dict:
    """The fields of a link's fragment, tolerating the escaping QR readers do to it."""
    parts = urlsplit(url)
    if not parts.fragment:
        raise ValueError(f"that link carries no {what} fragment.")
    fragment = parts.fragment
    # Some QR readers and link handlers percent-encode the fragment, which turns the separators
    # into %26 and leaves one field holding the rest of the link.
    if "&" not in fragment and "%26" in fragment.lower():
        fragment = unquote(fragment)
    return {name: values[0] for name, values in parse_qs(fragment).items()}


def _link(url: str, secret_field: str, what: str) -> dict:
    fields = _fragment_fields(url, what)
    for required in ("v", "d", secret_field, "r"):
        if required not in fields:
            raise ValueError(f"the {what} link is missing '{required}'.")
    if fields["v"] != "1":
        raise ValueError(f"unsupported {what} version {fields['v']}.")
    desktop = un64(fields["d"])
    if len(desktop) != 32:
        raise ValueError("the desktop key in the link is not 32 bytes.")
    return {"desktop_public": desktop, "secret": un64(fields[secret_field]), "room": fields["r"]}


def parse_pair_url(url: str) -> dict:
    return _link(url, "s", "pairing")


def parse_invite_url(url: str) -> dict:
    """The other half of :func:`invite_url`, tolerant of the same percent-encoding."""
    return _link(url, "i", "invite")


@dataclass
class Room:
    """One pairing opportunity, single use: `uses` counts down and the room dies at zero.

    An invite (section 10) is *not* a room: it lives in ``remote/guests.py`` with its own secret,
    role, pane scope, use count and expiry, and a rendezvous room is only the address its link
    points at. Keeping the two apart is what stops a link that admits a guest from also being a
    link that pairs a device.
    """
    room: str
    secret: bytes = field(default_factory=lambda: secrets.token_bytes(SECRET_BYTES))
    opened: float = field(default_factory=time.time)
    ttl: float = ROOM_TTL
    attempts: int = 0
    spent: bool = False
    uses: int = 1                   # a pair room admits one device, full stop

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
    """The desktop's open pairing rooms."""

    def __init__(self):
        self.rooms: dict[str, Room] = {}

    def open(self, room_id: str, ttl: float = ROOM_TTL) -> Room:
        self.sweep()
        room = Room(room=room_id, ttl=ttl)
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
