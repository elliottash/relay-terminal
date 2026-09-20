# SPDX-License-Identifier: AGPL-3.0-or-later
"""relay-rendezvous: signaling, a ciphertext-only relay and the device registry.

It routes bytes it cannot read. Every payload it copies is inside a Noise session whose keys were
pinned when a phone scanned a desktop's QR code, so a compromised rendezvous can delay or drop
traffic, and can see who talks to whom and how much — nothing else. That is the whole security
argument, and the code is small on purpose so it can be checked.

What it keeps (docs/REMOTE-PROTOCOL.md section 8): desktop ids and static public keys, pairing
rooms for five minutes, and metadata for seven days. No content, and deliberately **no push
subscription keys** — those are content keys, so they go to the desktop over the Noise session and
never come here.

Run it:  python3 -m rendezvous.server --host 127.0.0.1 --port 8787 --db rendezvous.sqlite3
"""
from __future__ import annotations

import argparse
import asyncio
import base64
import contextlib
import hmac
import json
import logging
import os
import secrets
import sqlite3
import sys
import time
from hashlib import sha256
from pathlib import Path
from urllib.parse import urlsplit

if __package__ in (None, ""):                      # running the file directly
    sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from remote import envelope, httpd, noise, pairing, push, ws

log = logging.getLogger("relay.rendezvous")

ROOM_TTL = 300              # five minutes, matching the QR secret's life
MAX_ROOM_TTL = 7 * 86400    # an invite may live up to a week (section 10)
METADATA_DAYS = 7
MAX_ROOMS_PER_HOUR = 20
MAX_CHANNELS_PER_DESKTOP = 32
# Section 8 counts the concurrent-socket limit **per device**, so that "anyone who learns a
# `desktop_id` [cannot] open every slot and keep the owner's own phone out". A device is named by
# its connect token: minted by the desktop at pairing, delivered inside the Noise session, checked
# here against the secret the desktop registered. Three sockets is a phone reconnecting over a
# flaky link plus a tab it left open, and no more.
MAX_CHANNELS_PER_TOKEN = 3
# The per-address cap stays for the connections that carry no token because they cannot: a room
# (a pairing link, an invite, a meeting code) is reached by whoever holds its id, and the desktop
# is the thing that decides what that connection is worth.
MAX_CHANNELS_PER_PEER = 8
# What one registration may carry, matching `pairing.MAX_REVOKED_TOKENS` on the desktop side.
MAX_REVOKED_TOKENS = 256
MAX_PUSH_PER_HOUR = 600
CHALLENGE_TTL = 120

# Meeting codes (card #97EG): four letters a person can read out, pointing at a room. 23 letters
# with no I, L or O, so nothing on the join page is ambiguous; about 280,000 values, which is why
# lookups are rate-limited per address and why a code only lives ten minutes. The code is public
# and the rendezvous may know it — the PIN that goes with it never comes here.
CODE_ALPHABET = "ABCDEFGHJKMNPQRSTUVWXYZ"
CODE_LENGTH = 4
MAX_CODE_TTL = 600
MAX_LIVE_CODES_PER_DESKTOP = 5
CODE_LOOKUPS_PER_MINUTE = 10

# Where `/v1/push/send` may post. A Web Push endpoint comes from the browser's own push service,
# and there are five of them; everything else is a URL somebody chose, and this process is the one
# with a network on the hosted side. Without this the route is a request-forgery proxy: a paired
# `view` phone sends `push_subscribe {endpoint: "https://169.254.169.254/…"}`, the desktop stores
# it, and the rendezvous makes the request. `RELAY_PUSH_HOSTS` adds to the list, comma separated,
# for a self-hoster running their own push service — and for the tests, which deliver to loopback.
PUSH_SERVICE_HOSTS = (
    "fcm.googleapis.com",                       # Chrome and every Chromium browser
    "android.googleapis.com",                   # the older FCM host, still in circulation
    "updates.push.services.mozilla.com",        # Firefox
    "push.services.mozilla.com",
    "web.push.apple.com",                       # Safari, iOS and macOS
    "push.apple.com",
    "notify.windows.com",                       # Edge / WNS
)


def push_hosts() -> tuple[str, ...]:
    extra = tuple(host.strip().lower() for host in os.environ.get("RELAY_PUSH_HOSTS", "").split(",")
                  if host.strip())
    return PUSH_SERVICE_HOSTS + extra


def push_endpoint_host(endpoint: str) -> str:
    """The host of a push endpoint, or "" when it is not one this server will post to.

    A name is matched against the push services rather than resolved, so there is no DNS lookup
    to race: a rebind between the check and the request would otherwise put the whole check back
    where it started.
    """
    if not endpoint.startswith("https://") or len(endpoint) > 2048:
        return ""
    if any(character.isspace() for character in endpoint):
        return ""
    parts = urlsplit(endpoint)
    if parts.username is not None or parts.password is not None or "@" in (parts.netloc or ""):
        return ""
    host = (parts.hostname or "").lower()
    if not host:
        return ""
    for allowed in push_hosts():
        if host == allowed or host.endswith("." + allowed):
            return host
    return ""


def derive_desktop_id(static_pubkey_b64: str) -> str:
    """A desktop's id is the first 128 bits of SHA-256 over its static public key.

    Binding the two means an id cannot be squatted or rebound to a different key, and a client that
    knows the id learns nothing it could not compute from the key it already pinned.
    """
    try:
        raw = base64.b64decode(static_pubkey_b64, validate=True)
    except Exception:
        return ""
    if len(raw) != 32:
        return ""
    return sha256(raw).hexdigest()[:32]

SCHEMA = """
CREATE TABLE IF NOT EXISTS desktops (
    desktop_id TEXT PRIMARY KEY,
    static_pubkey TEXT NOT NULL,
    token_hash TEXT NOT NULL,
    created REAL NOT NULL,
    last_seen REAL,
    connect_secret TEXT
);
-- Connect-token ids the desktop has revoked (section 8). Ids only, never a token and never a
-- MAC, and they are replaced wholesale at every registration, so they live exactly as long as
-- the desktop's registration does. They are deliberately **not** in `events`: the seven-day
-- metadata log holds no token id, so a copy of it says nothing about which devices a desktop has.
CREATE TABLE IF NOT EXISTS revoked_tokens (
    desktop_id TEXT NOT NULL,
    token_id TEXT NOT NULL,
    PRIMARY KEY (desktop_id, token_id)
);
CREATE TABLE IF NOT EXISTS rooms (
    room TEXT PRIMARY KEY,
    desktop_id TEXT NOT NULL,
    created REAL NOT NULL,
    expires REAL NOT NULL
);
CREATE TABLE IF NOT EXISTS challenges (
    challenge TEXT PRIMARY KEY,
    ephemeral_private TEXT NOT NULL,
    created REAL NOT NULL
);
CREATE TABLE IF NOT EXISTS push_keys (
    singleton INTEGER PRIMARY KEY CHECK (singleton = 1),
    vapid_private TEXT NOT NULL
);
CREATE TABLE IF NOT EXISTS events (
    at REAL NOT NULL,
    kind TEXT NOT NULL,
    desktop_id TEXT,
    detail TEXT
);
CREATE INDEX IF NOT EXISTS events_at ON events(at);
"""


def token_hash(token: str) -> str:
    return sha256(token.encode()).hexdigest()


def push_public_of(private: bytes) -> bytes:
    """The VAPID public key for a private one, uncompressed P-256, for the /v1/push/key route."""
    from cryptography.hazmat.primitives.asymmetric import ec
    from cryptography.hazmat.primitives.serialization import Encoding, PublicFormat
    key = ec.derive_private_key(int.from_bytes(private, "big"), ec.SECP256R1())
    return key.public_key().public_bytes(Encoding.X962, PublicFormat.UncompressedPoint)


class Store:
    def __init__(self, path: str | Path = ":memory:"):
        self.db = sqlite3.connect(path, check_same_thread=False)
        if str(path) != ":memory:":
            # The file holds the VAPID private key (`vapid_pair`), so it is the owner's alone.
            # The sidecar keeps its local registry on disk for exactly that key: drawn fresh at
            # every start, every phone subscribed before a restart was getting no pushes.
            os.chmod(path, 0o600)
        self.db.row_factory = sqlite3.Row
        self.db.executescript(SCHEMA)
        # A database written before connect tokens existed has the desktops table without the
        # column; adding it here rather than recreating the table keeps every registration.
        if "connect_secret" not in {row["name"] for row
                                    in self.db.execute("PRAGMA table_info(desktops)")}:
            self.db.execute("ALTER TABLE desktops ADD COLUMN connect_secret TEXT")
        self.db.commit()
        # Meeting codes, by code: (desktop_id, room, expires). In memory on purpose and never in
        # the database: the code → room map is kept for the code's own ten minutes and not a
        # second longer, where a row in `rooms` or `events` would sit in a file (and, deleted,
        # in its free pages) for the seven days metadata is kept.
        self.codes: dict[str, tuple[str, str, float]] = {}

    def close(self) -> None:
        self.db.close()

    # ---- desktops ----------------------------------------------------------------------------

    def register(self, desktop_id: str, static_pubkey: str,
                 connect_secret: str | None = None) -> str | None:
        """Register or re-register. Returns a fresh bearer token, or None on a key mismatch.

        The caller has already proved possession of the private half (``consume_challenge``), and
        ``desktop_id`` is derived from the key, so an id cannot be squatted or rebound.

        ``connect_secret`` turns per-device connect tokens on for this desktop (section 8): while
        it is set, a client channel must present a token this secret verifies. It is written at
        every registration, so a desktop that stops sending one turns them off again — and a
        rendezvous that restarted has the secret back the moment the desktop re-registers, which
        ``Host._register_again`` does before every reconnect.
        """
        row = self.db.execute("SELECT static_pubkey FROM desktops WHERE desktop_id = ?",
                              (desktop_id,)).fetchone()
        if row and row["static_pubkey"] != static_pubkey:
            return None          # the id is taken by a different key; never silently rebind it
        token = secrets.token_urlsafe(32)
        now = time.time()
        if row:
            self.db.execute("UPDATE desktops SET token_hash = ?, last_seen = ?, connect_secret = ?"
                            " WHERE desktop_id = ?",
                            (token_hash(token), now, connect_secret, desktop_id))
        else:
            self.db.execute(
                "INSERT INTO desktops (desktop_id, static_pubkey, token_hash, created, last_seen,"
                " connect_secret) VALUES (?, ?, ?, ?, ?, ?)",
                (desktop_id, static_pubkey, token_hash(token), now, now, connect_secret))
        self.db.commit()
        return token

    # ---- per-device connect tokens (section 8) -------------------------------------------------

    def connect_secret(self, desktop_id: str) -> bytes | None:
        """The secret to check this desktop's connect tokens against, or None if it has none."""
        row = self.db.execute("SELECT connect_secret FROM desktops WHERE desktop_id = ?",
                              (desktop_id,)).fetchone()
        if not row or not row["connect_secret"]:
            return None
        try:
            return base64.b64decode(row["connect_secret"], validate=True)
        except Exception:
            return None

    def set_revoked_tokens(self, desktop_id: str, token_ids: list[str]) -> int:
        """Replace this desktop's revoked-id set with the one it just sent."""
        wanted = [str(token_id)[:64] for token_id in token_ids
                  if token_id][:MAX_REVOKED_TOKENS]
        self.db.execute("DELETE FROM revoked_tokens WHERE desktop_id = ?", (desktop_id,))
        self.db.executemany("INSERT OR IGNORE INTO revoked_tokens (desktop_id, token_id)"
                            " VALUES (?, ?)", [(desktop_id, token_id) for token_id in wanted])
        self.db.commit()
        return len(wanted)

    def add_revoked_tokens(self, desktop_id: str, token_ids: list[str]) -> list[str]:
        """Add ids to the set, for a revoke that must bite before the next registration."""
        wanted = [str(token_id)[:64] for token_id in token_ids
                  if token_id][:MAX_REVOKED_TOKENS]
        self.db.executemany("INSERT OR IGNORE INTO revoked_tokens (desktop_id, token_id)"
                            " VALUES (?, ?)", [(desktop_id, token_id) for token_id in wanted])
        # Bounded per desktop: repeated revokes between two registrations keep the newest
        # MAX_REVOKED_TOKENS ids, which is all the next registration would carry anyway.
        self.db.execute(
            "DELETE FROM revoked_tokens WHERE desktop_id = ? AND rowid NOT IN ("
            " SELECT rowid FROM revoked_tokens WHERE desktop_id = ? ORDER BY rowid DESC LIMIT ?)",
            (desktop_id, desktop_id, MAX_REVOKED_TOKENS))
        self.db.commit()
        return wanted

    def token_revoked(self, desktop_id: str, token_id: str) -> bool:
        return bool(self.db.execute(
            "SELECT 1 FROM revoked_tokens WHERE desktop_id = ? AND token_id = ?",
            (desktop_id, token_id)).fetchone())

    def authenticate(self, desktop_id: str, token: str) -> bool:
        row = self.db.execute("SELECT token_hash FROM desktops WHERE desktop_id = ?",
                              (desktop_id,)).fetchone()
        return bool(row) and secrets.compare_digest(row["token_hash"], token_hash(token))

    def desktop_exists(self, desktop_id: str) -> bool:
        return bool(self.db.execute("SELECT 1 FROM desktops WHERE desktop_id = ?",
                                    (desktop_id,)).fetchone())

    # ---- rooms -------------------------------------------------------------------------------

    def open_room(self, desktop_id: str, ttl: float = ROOM_TTL) -> tuple[str, float] | None:
        """Open a room and say how long it really lives.

        A room is **not** consumed by the first connection: ``_client_socket`` looks it up and
        leaves it, so an invite with more than one use, and an admitted guest who reconnects,
        both work without a second mechanism here. Single use is a property of the *secret*, not
        of the room — the hub burns a pairing room the moment a secret is proved (``host.py``),
        and an invite counts its own uses in ``remote/guests.py``. Nothing changes about what this
        process can see: a room is an id and a lifetime, and every byte through it is ciphertext.
        """
        now = time.time()
        recent = self.db.execute(
            "SELECT COUNT(*) AS n FROM rooms WHERE desktop_id = ? AND created > ?",
            (desktop_id, now - 3600)).fetchone()["n"]
        if recent >= MAX_ROOMS_PER_HOUR:
            return None
        room = secrets.token_urlsafe(16)
        lifetime = max(1.0, min(float(ttl), MAX_ROOM_TTL))
        self.db.execute("INSERT INTO rooms (room, desktop_id, created, expires) VALUES (?, ?, ?, ?)",
                        (room, desktop_id, now, now + lifetime))
        self.db.commit()
        return room, lifetime

    def room_desktop(self, room: str) -> str | None:
        self.expire()
        row = self.db.execute("SELECT desktop_id FROM rooms WHERE room = ? AND expires > ?",
                              (room, time.time())).fetchone()
        return row["desktop_id"] if row else None

    def burn_room(self, room: str) -> None:
        self.db.execute("DELETE FROM rooms WHERE room = ?", (room,))
        self.db.commit()

    def expire(self) -> None:
        now = time.time()
        self.db.execute("DELETE FROM rooms WHERE expires <= ?", (now,))
        self.db.execute("DELETE FROM events WHERE at < ?", (now - METADATA_DAYS * 86400,))
        self.db.commit()

    # ---- meeting codes ---------------------------------------------------------------------------

    def _sweep_codes(self) -> None:
        now = time.time()
        for code in [code for code, (_, _, expires) in self.codes.items() if expires <= now]:
            del self.codes[code]

    def open_code(self, desktop_id: str, room: str, ttl: float) -> tuple[str, float] | None:
        """A fresh code for one of this desktop's rooms, and how long it lives.

        Never longer than ten minutes, and never longer than the room: a code that outlived its
        room would resolve to a door that no longer opens. None when the desktop already has its
        share of live codes, so one registered key cannot fill the code space.
        """
        self._sweep_codes()
        row = self.db.execute("SELECT expires FROM rooms WHERE room = ? AND desktop_id = ?",
                              (room, desktop_id)).fetchone()
        now = time.time()
        if not row or row["expires"] <= now:
            raise KeyError(room)
        if sum(1 for owner, _, _ in self.codes.values()
               if owner == desktop_id) >= MAX_LIVE_CODES_PER_DESKTOP:
            return None
        lifetime = max(1.0, min(float(ttl), MAX_CODE_TTL, row["expires"] - now))
        for _ in range(64):
            code = "".join(secrets.choice(CODE_ALPHABET) for _ in range(CODE_LENGTH))
            if code not in self.codes:
                self.codes[code] = (desktop_id, room, now + lifetime)
                return code, lifetime
        raise RuntimeError("no free meeting code")    # 280,000 values; not reachable in practice

    def code_room(self, code: str) -> str | None:
        """The room behind a code, case-insensitively, or None — unknown and expired alike."""
        self._sweep_codes()
        entry = self.codes.get(code.strip().upper())
        return entry[1] if entry else None

    def burn_code(self, desktop_id: str, code: str) -> bool:
        """Forget a code at once. Only the desktop that asked for it may."""
        self._sweep_codes()
        code = code.strip().upper()
        entry = self.codes.get(code)
        if entry is None or entry[0] != desktop_id:
            return False
        del self.codes[code]
        return True

    # ---- registration challenges ---------------------------------------------------------------

    def new_challenge(self) -> tuple[str, str]:
        """A challenge plus our ephemeral X25519 public key, for proof of possession."""
        private, public = noise.generate_keypair()
        challenge = secrets.token_urlsafe(24)
        self.db.execute("DELETE FROM challenges WHERE created < ?", (time.time() - CHALLENGE_TTL,))
        self.db.execute("INSERT INTO challenges (challenge, ephemeral_private, created)"
                        " VALUES (?, ?, ?)",
                        (challenge, base64.b64encode(private).decode(), time.time()))
        self.db.commit()
        return challenge, base64.b64encode(public).decode()

    def consume_challenge(self, challenge: str, static_pubkey: str, proof: str) -> bool:
        """Check HMAC(DH(ephemeral, static), challenge). Single use, whatever the outcome."""
        row = self.db.execute(
            "SELECT ephemeral_private, created FROM challenges WHERE challenge = ?",
            (challenge,)).fetchone()
        if not row:
            return False
        self.db.execute("DELETE FROM challenges WHERE challenge = ?", (challenge,))
        self.db.commit()
        if time.time() - row["created"] > CHALLENGE_TTL:
            return False
        try:
            shared = noise.dh(base64.b64decode(row["ephemeral_private"]),
                              base64.b64decode(static_pubkey))
        except Exception:
            return False
        expected = hmac.new(shared, challenge.encode(), sha256).hexdigest()
        return secrets.compare_digest(expected, proof)

    # ---- push --------------------------------------------------------------------------------

    def vapid_pair(self) -> tuple[bytes, bytes]:
        """The one VAPID keypair, made on first use. The private half stays here and signs
        delivery requests; the public half is what a phone subscribes under."""
        row = self.db.execute("SELECT vapid_private FROM push_keys WHERE singleton = 1").fetchone()
        if row:
            private = base64.b64decode(row["vapid_private"])
        else:
            private, _ = push.vapid_generate()
            self.db.execute("INSERT INTO push_keys (singleton, vapid_private) VALUES (1, ?)",
                            (base64.b64encode(private).decode(),))
            self.db.commit()
        return private, push_public_of(private)

    def recent_push_count(self, desktop_id: str) -> int:
        return self.db.execute(
            "SELECT COUNT(*) AS n FROM events WHERE kind = 'push' AND desktop_id = ? AND at > ?",
            (desktop_id, time.time() - 3600)).fetchone()["n"]

    # ---- metadata ----------------------------------------------------------------------------

    def note(self, kind: str, desktop_id: str | None = None, **detail) -> None:
        """Operational metadata only: ids, counts and times. Never content."""
        self.db.execute("INSERT INTO events (at, kind, desktop_id, detail) VALUES (?, ?, ?, ?)",
                        (time.time(), kind, desktop_id, json.dumps(detail) if detail else None))
        self.db.commit()


def peer_address(socket: ws.WebSocket) -> str:
    """The address half of a socket's peer, without the port."""
    return (socket.peer or "").rsplit(":", 1)[0]


def client_address(request: ws.Request) -> str:
    """Who is asking, for a per-address limit.

    The socket's peer, unless that peer is loopback: then this is the desktop's own sidecar
    rendezvous behind ``cloudflared`` or ``tailscale serve``, and every request from the internet
    arrives from 127.0.0.1. Those proxies say who they are proxying — Cloudflare in
    ``CF-Connecting-IP``, which it overwrites, and Tailscale by appending to ``X-Forwarded-For``,
    so the last entry is the one the proxy saw. A header is never believed from anywhere else,
    since anyone can send one.
    """
    peer = (request.peer or "").rsplit(":", 1)[0].strip("[]")
    if peer in ("127.0.0.1", "::1", "localhost") or peer.startswith("127."):
        forwarded = request.header("cf-connecting-ip").strip()
        if not forwarded:
            chain = [part.strip() for part in request.header("x-forwarded-for").split(",")]
            forwarded = chain[-1] if chain and chain[-1] else ""
        if forwarded:
            return forwarded[:64]
    return peer


class LookupLimiter:
    """At most ``limit`` lookups per address per minute, remembered in memory only.

    A code is 4 letters, so resolution is the one place a stranger can enumerate. Ten a minute is
    plenty for a person who mistyped and useless for a sweep of 280,000 values.
    """

    def __init__(self, limit: int = CODE_LOOKUPS_PER_MINUTE, window: float = 60.0,
                 clock=time.monotonic):
        self.limit = limit
        self.window = window
        self.clock = clock
        self.seen: dict[str, list[float]] = {}

    def allow(self, address: str) -> bool:
        now = self.clock()
        if len(self.seen) > 10_000:                 # forget idle addresses rather than grow
            self.seen = {key: [t for t in times if now - t < self.window]
                         for key, times in self.seen.items()}
            self.seen = {key: times for key, times in self.seen.items() if times}
        times = [t for t in self.seen.get(address, []) if now - t < self.window]
        if len(times) >= self.limit:
            self.seen[address] = times
            return False
        times.append(now)
        self.seen[address] = times
        return True


class _Routes(dict):
    """httpd's exact-path route table, plus the two routes whose last segment is a code.

    ``GET /v1/codes/<CODE>`` and ``POST /v1/codes/<CODE>/burn`` are registered under a ``*`` and
    found here; the handler reads the code back out of the path. Everything else is looked up
    exactly as before.
    """

    def get(self, key, default=None):
        found = super().get(key)
        if found is not None:
            return found
        method, path = key
        if path.startswith("/v1/codes/"):
            rest = path[len("/v1/codes/"):]
            if rest.endswith("/burn") and "/" not in rest[:-len("/burn")]:
                return super().get((method, "/v1/codes/*/burn"), default)
            if rest and "/" not in rest:
                return super().get((method, "/v1/codes/*"), default)
        return default


def code_from_path(path: str) -> str:
    return path[len("/v1/codes/"):].split("/", 1)[0].strip().upper()


class Hub:
    """The live desktop sockets and the client channels attached to each."""

    def __init__(self):
        self.desktops: dict[str, ws.WebSocket] = {}
        self.channels: dict[str, dict[bytes, ws.WebSocket]] = {}
        # The connect token each channel was opened on, so the budget can be counted per device
        # and a revoked token's live channels can be closed. Ids only, and in memory only.
        self.tokens: dict[str, dict[bytes, str]] = {}

    def attach_desktop(self, desktop_id: str, socket: ws.WebSocket) -> ws.WebSocket | None:
        previous = self.desktops.get(desktop_id)
        self.desktops[desktop_id] = socket
        self.channels.setdefault(desktop_id, {})
        return previous

    def detach_desktop(self, desktop_id: str, socket: ws.WebSocket) -> list[ws.WebSocket]:
        if self.desktops.get(desktop_id) is not socket:
            return []
        self.desktops.pop(desktop_id, None)
        self.tokens.pop(desktop_id, None)
        return list(self.channels.pop(desktop_id, {}).values())

    def add_channel(self, desktop_id: str, channel: bytes, socket: ws.WebSocket,
                    token_id: str = "") -> bool:
        """Take a slot, if there is one for this desktop, this device and this address.

        With a connect token the budget is counted **per device**, which is the rule section 8
        states: a stranger who knows a `desktop_id` has no token, gets no channel at all, and
        cannot reach the owner's share of the budget however many addresses they have. Without
        one — a room, which is a pairing link, an invite or a meeting code — the per-address share
        is what stops one client taking the desktop's whole budget with a loop.
        """
        channels = self.channels.setdefault(desktop_id, {})
        tokens = self.tokens.setdefault(desktop_id, {})
        if len(channels) >= MAX_CHANNELS_PER_DESKTOP:
            return False
        if token_id:
            if sum(1 for other in tokens.values()
                   if other == token_id) >= MAX_CHANNELS_PER_TOKEN:
                return False
        else:
            peer = peer_address(socket)
            if peer and sum(1 for other in channels.values()
                            if peer_address(other) == peer) >= MAX_CHANNELS_PER_PEER:
                return False
        channels[channel] = socket
        if token_id:
            tokens[channel] = token_id
        return True

    def drop_channel(self, desktop_id: str, channel: bytes) -> ws.WebSocket | None:
        self.tokens.get(desktop_id, {}).pop(channel, None)
        return self.channels.get(desktop_id, {}).pop(channel, None)

    def channels_on_tokens(self, desktop_id: str,
                           token_ids: set[str]) -> list[tuple[bytes, ws.WebSocket]]:
        """The live channels a revoke has just killed — closed while the phone is mid-session."""
        tokens = self.tokens.get(desktop_id, {})
        channels = self.channels.get(desktop_id, {})
        return [(channel, channels[channel]) for channel, token_id in list(tokens.items())
                if token_id in token_ids and channel in channels]

    def client(self, desktop_id: str, channel: bytes) -> ws.WebSocket | None:
        return self.channels.get(desktop_id, {}).get(channel)


def build(store: Store, static_root: Path | None = None) -> httpd.Server:
    server = httpd.Server(static_root=static_root)
    server.routes = _Routes(server.routes)
    hub = Hub()
    lookups = LookupLimiter()

    @server.route("POST", "/v1/challenge")
    async def challenge(request: ws.Request, body: bytes) -> httpd.Response:
        """Step one of registering: prove you hold the private half of the key you claim."""
        value, ephemeral = store.new_challenge()
        return httpd.Response.json({"challenge": value, "ephemeral_public": ephemeral,
                                    "expires_in": CHALLENGE_TTL})

    @server.route("POST", "/v1/register")
    async def register(request: ws.Request, body: bytes) -> httpd.Response:
        try:
            fields = httpd.json_body(body)
        except ValueError as error:
            return httpd.Response.error(400, str(error))
        pubkey = str(fields.get("static_pubkey", ""))
        if len(pubkey) > 128:
            return httpd.Response.error(400, "static_pubkey is too long.")
        desktop_id = derive_desktop_id(pubkey)
        if not desktop_id:
            return httpd.Response.error(400, "static_pubkey must be 32 base64 bytes.")
        # The id is derived here, never taken from the request: it cannot be squatted.
        if not store.consume_challenge(str(fields.get("challenge", "")), pubkey,
                                       str(fields.get("proof", ""))):
            return httpd.Response.error(401, "that challenge is unknown, spent or unproved.")
        # Per-device connect tokens (section 8). The secret is what `/v1/connect` checks a
        # client's token against; the ids are the tokens that must stop working. Both are sent at
        # every registration, so this server has them again after a restart without a round trip
        # of its own, and a desktop that sends neither is one with tokens off.
        secret = fields.get("connect_secret")
        connect_secret = None
        if isinstance(secret, str) and secret:
            try:
                raw = base64.b64decode(secret, validate=True)
            except Exception:
                return httpd.Response.error(400, "connect_secret must be base64.")
            if len(raw) != 32:
                return httpd.Response.error(400, "connect_secret must be 32 bytes.")
            connect_secret = secret
        revoked = fields.get("revoked_tokens") or []
        if not isinstance(revoked, list) or len(revoked) > MAX_REVOKED_TOKENS:
            return httpd.Response.error(400, "revoked_tokens must be a list of at most "
                                             f"{MAX_REVOKED_TOKENS} ids.")
        token = store.register(desktop_id, pubkey, connect_secret)
        if token is None:
            return httpd.Response.error(409, "that desktop id belongs to a different key.")
        store.set_revoked_tokens(desktop_id, [str(value) for value in revoked])
        # Metadata is ids, counts and times (section 8). The count of revoked tokens is a count;
        # the ids themselves are never written here, so the seven-day log names no device.
        store.note("register", desktop_id, revoked=len(revoked), tokens=bool(connect_secret))
        return httpd.Response.json({"desktop_id": desktop_id, "token": token,
                                    "connect_tokens": bool(connect_secret)})

    @server.route("POST", "/v1/revoke")
    async def revoke(request: ws.Request, body: bytes) -> httpd.Response:
        """A desktop says which connect-token ids are dead, and their channels close now.

        Registration carries the whole list, but a device revoked at four in the afternoon must
        stop working at four in the afternoon — including for a phone that is mid-session, whose
        channel this closes — rather than at the desktop's next reconnect.
        """
        try:
            fields = httpd.json_body(body)
        except ValueError as error:
            return httpd.Response.error(400, str(error))
        desktop_id = str(fields.get("desktop_id", ""))
        if not store.authenticate(desktop_id, str(fields.get("token", ""))):
            return httpd.Response.error(401, "unknown desktop or bad token.")
        ids = fields.get("token_ids") or []
        if not isinstance(ids, list) or len(ids) > MAX_REVOKED_TOKENS:
            return httpd.Response.error(400, "token_ids must be a list of at most "
                                             f"{MAX_REVOKED_TOKENS} ids.")
        added = store.add_revoked_tokens(desktop_id, [str(value) for value in ids])
        closed = 0
        for channel, socket in hub.channels_on_tokens(desktop_id, set(added)):
            hub.drop_channel(desktop_id, channel)
            closed += 1
            with contextlib.suppress(Exception):
                await socket.close(4403, "that device was revoked.")
        store.note("revoke", desktop_id, count=len(added), closed=closed)
        return httpd.Response.json({"revoked": len(added), "closed": closed})

    @server.route("POST", "/v1/rooms")
    async def rooms(request: ws.Request, body: bytes) -> httpd.Response:
        try:
            fields = httpd.json_body(body)
        except ValueError as error:
            return httpd.Response.error(400, str(error))
        desktop_id = str(fields.get("desktop_id", ""))
        token = str(fields.get("token", ""))
        if not store.authenticate(desktop_id, token):
            return httpd.Response.error(401, "unknown desktop or bad token.")
        try:
            wanted = float(fields.get("ttl", ROOM_TTL))
        except (TypeError, ValueError):
            wanted = ROOM_TTL
        opened = store.open_room(desktop_id, wanted)
        if opened is None:
            return httpd.Response.error(429, "too many pairing rooms this hour.")
        room, lifetime = opened
        store.note("room", desktop_id)
        # The lifetime actually granted, not the default: an invite may ask for up to a week
        # (section 10.2) and the desktop sizes its own record from what comes back.
        return httpd.Response.json({"room": room, "expires_in": lifetime})

    # ---- meeting codes (card #97EG) -------------------------------------------------------------
    # The rendezvous learns a code and the room it names, which it could see anyway: a room id is
    # what every client connects with. It never learns the PIN — that stays between the two ends,
    # inside CPace (remote/meetcode.py) — so resolving a code is only ever the start of an online
    # guess against the desktop, which counts and burns.

    @server.route("POST", "/v1/codes")
    async def codes(request: ws.Request, body: bytes) -> httpd.Response:
        try:
            fields = httpd.json_body(body)
        except ValueError as error:
            return httpd.Response.error(400, str(error))
        desktop_id = str(fields.get("desktop_id", ""))
        if not store.authenticate(desktop_id, str(fields.get("token", ""))):
            return httpd.Response.error(401, "unknown desktop or bad token.")
        try:
            wanted = float(fields.get("ttl", MAX_CODE_TTL))
        except (TypeError, ValueError):
            wanted = MAX_CODE_TTL
        try:
            opened = store.open_code(desktop_id, str(fields.get("room", "")), wanted)
        except KeyError:
            return httpd.Response.error(404, "no such room for this desktop.")
        if opened is None:
            return httpd.Response.error(429, "too many live meeting codes.")
        code, lifetime = opened
        # Never the code itself: the event table is kept for seven days, the code for ten minutes.
        store.note("code", desktop_id)
        return httpd.Response.json({"code": code, "expires_in": lifetime})

    @server.route("GET", "/v1/codes/*")
    async def code_lookup(request: ws.Request, body: bytes) -> httpd.Response:
        # CORS-open like /v1/push/key: in production the join page's origin is not this one.
        cors = {"Access-Control-Allow-Origin": "*", "Access-Control-Allow-Methods": "GET"}
        if not lookups.allow(client_address(request)):
            return httpd.Response(status=429, headers=cors, body=json.dumps(
                {"error": "too many lookups; wait a minute."}).encode())
        room = store.code_room(code_from_path(request.path))
        if room is None:
            # Unknown, expired, burned and malformed all answer the same, byte for byte, so a
            # probe cannot tell a code that existed from one that never did.
            return httpd.Response(status=404, headers=cors, body=json.dumps(
                {"error": "no such meeting code."}).encode())
        return httpd.Response(headers=cors, body=json.dumps({"room": room}).encode())

    @server.route("POST", "/v1/codes/*/burn")
    async def code_burn(request: ws.Request, body: bytes) -> httpd.Response:
        try:
            fields = httpd.json_body(body)
        except ValueError as error:
            return httpd.Response.error(400, str(error))
        desktop_id = str(fields.get("desktop_id", ""))
        if not store.authenticate(desktop_id, str(fields.get("token", ""))):
            return httpd.Response.error(401, "unknown desktop or bad token.")
        # Burning a code that has already gone is not an error: the desktop wanted it gone.
        store.burn_code(desktop_id, code_from_path(request.path))
        return httpd.Response.json({})

    @server.route("GET", "/v1/push/key")
    async def push_key(request: ws.Request, body: bytes) -> httpd.Response:
        # Public by definition: it is the key a browser subscribes under. CORS-open because in
        # production the app origin (app.relay-terminal.ai) and this one differ.
        return httpd.Response(
            body=json.dumps({"vapid": push.b64url(store.vapid_pair()[1])}).encode(),
            headers={"Access-Control-Allow-Origin": "*",
                     "Access-Control-Allow-Methods": "GET"})

    @server.route("POST", "/v1/push/send")
    async def push_send(request: ws.Request, body: bytes) -> httpd.Response:
        """The desktop posts an opaque payload and the endpoint to post it to.

        There is deliberately no subscribe endpoint. A subscription's ``p256dh`` and ``auth`` are
        content keys: a phone sends them to its **desktop** inside the Noise session, and we only
        ever see the endpoint URL the push service published. Otherwise a compromised rendezvous
        could forge a "password prompt" notification, which is the whole game.
        """
        try:
            fields = httpd.json_body(body)
        except ValueError as error:
            return httpd.Response.error(400, str(error))
        desktop_id = str(fields.get("desktop_id", ""))
        if not store.authenticate(desktop_id, str(fields.get("token", ""))):
            return httpd.Response.error(401, "unknown desktop or bad token.")
        if store.recent_push_count(desktop_id) >= MAX_PUSH_PER_HOUR:
            return httpd.Response.error(429, "too many pushes this hour.")
        endpoint = str(fields.get("endpoint", ""))
        if not endpoint.startswith("https://") or len(endpoint) > 2048:
            return httpd.Response.error(400, "endpoint must be an https URL.")
        if not push_endpoint_host(endpoint):
            # Not a push service, so not somewhere this process posts to. The desktop refuses the
            # same subscription when the phone offers it; this is the refusal that binds, because
            # registering a desktop here proves possession of a key and nothing else.
            return httpd.Response.error(400, "that endpoint is not a Web Push service.")
        ciphertext = str(fields.get("ciphertext", ""))
        try:
            payload = base64.b64decode(ciphertext, validate=True)
        except (ValueError, TypeError):
            return httpd.Response.error(400, "ciphertext must be base64.")
        if not payload or len(payload) > 4096:
            return httpd.Response.error(400, "ciphertext is required.")
        urgency = str(fields.get("urgency", "normal"))
        if urgency not in ("very-low", "low", "normal", "high"):
            urgency = "normal"
        ttl = min(max(int(fields.get("ttl", 60) or 60), 0), 24 * 3600)
        store.note("push", desktop_id)
        # Delivery: we sign with the VAPID key and post bytes we cannot read. The body arrives
        # encrypted twice over — RFC 8291 to the subscription keys, and inside that a seal to a
        # key that travelled to the desktop inside the Noise session — so this process never
        # holds anything that can open it, and cannot forge one either.
        delivered, status, drop = await deliver(store.vapid_pair()[0], endpoint, payload,
                                                ttl, urgency)
        return httpd.Response.json({"delivered": delivered, "status": status, "drop": drop})

    async def deliver(vapid_private: bytes, endpoint: str, payload: bytes, ttl: int,
                      urgency: str) -> tuple[bool, int | None, bool]:
        import urllib.error
        import urllib.request
        authorization = push.vapid_authorization(vapid_private, endpoint)

        def go() -> tuple[bool, int | None, bool]:
            request = urllib.request.Request(
                endpoint, data=payload, method="POST", headers={
                    "Authorization": authorization,
                    "TTL": str(ttl),
                    "Urgency": urgency,
                    "Content-Type": "application/octet-stream",
                    # RFC 8291 section 4: an aes128gcm payload says so, or the push service has
                    # no way to know what it is holding and answers 400. Invisible to a test that
                    # only talks to a service written alongside it, fatal on a real phone.
                    "Content-Encoding": "aes128gcm",
                })
            try:
                with urllib.request.urlopen(request, timeout=10) as response:
                    return response.status in (200, 201), response.status, False
            except urllib.error.HTTPError as error:
                # 404/410: the subscription is gone; the desktop is told to drop it.
                return False, error.code, error.code in (404, 410)
            except (urllib.error.URLError, OSError, ValueError) as error:
                log.info("push to %s failed: %s", endpoint[:48], error)
                return False, None, False
        return await asyncio.to_thread(go)

    @server.route("GET", "/v1/health")
    async def health(request: ws.Request, body: bytes) -> httpd.Response:
        return httpd.Response.json({"ok": True, "desktops": len(hub.desktops),
                                    "channels": sum(len(c) for c in hub.channels.values())})

    @server.socket("/v1/connect")
    async def connect(socket: ws.WebSocket) -> None:
        query = socket.request.query if socket.request else {}
        if "token" in query:
            await _desktop_socket(socket, query)
        else:
            await _client_socket(socket, query)

    async def _desktop_socket(socket: ws.WebSocket, query: dict) -> None:
        desktop_id = query.get("desktop", "")
        if not store.authenticate(desktop_id, query.get("token", "")):
            await socket.close(4401, "unknown desktop or bad token.")
            return
        previous = hub.attach_desktop(desktop_id, socket)
        if previous is not None:
            await previous.close(4409, "replaced by a newer connection.")
        store.note("desktop_online", desktop_id)
        log.info("desktop %s online from %s", desktop_id[:8], socket.peer)
        try:
            while True:
                message = await socket.recv()
                if isinstance(message, str):
                    continue                      # the desktop link is binary only
                try:
                    frame = envelope.unpack(message)
                except envelope.EnvelopeError as error:
                    log.info("bad envelope from desktop %s: %s", desktop_id[:8], error)
                    continue
                client = hub.client(desktop_id, frame.channel)
                if client is None:
                    continue
                if frame.kind == envelope.KIND_CLOSE:
                    hub.drop_channel(desktop_id, frame.channel)
                    await client.close(4403, "the desktop closed this channel.")
                    continue
                try:
                    await client.send(frame.payload)
                except ws.ConnectionClosed:
                    hub.drop_channel(desktop_id, frame.channel)
        except ws.ConnectionClosed:
            pass
        finally:
            for client in hub.detach_desktop(desktop_id, socket):
                await client.close(4404, "the desktop went offline.")
            store.note("desktop_offline", desktop_id)
            log.info("desktop %s offline", desktop_id[:8])

    async def _client_socket(socket: ws.WebSocket, query: dict) -> None:
        room = query.get("room", "")
        desktop_id = store.room_desktop(room) if room else query.get("desktop", "")
        if not desktop_id or not store.desktop_exists(desktop_id):
            await socket.close(4404, "no such desktop or the pairing code expired.")
            return
        # Per-device connect tokens (section 8). A desktop that registered a secret is one whose
        # devices carry tokens, and a channel to it without a valid one is refused **with the
        # same answer an unknown desktop gets**: a stranger holding a `desktop_id` computed from
        # a link they once saw learns nothing from the refusal, and takes no slot on the way.
        # A room carries no token by design — a pairing link, an invite or a meeting code is
        # reached by whoever holds the room id, and the desktop decides what that is worth.
        token_id = ""
        if not room:
            secret = store.connect_secret(desktop_id)
            if secret is not None:
                token_id = pairing.check_connect_token(
                    secret, desktop_id, query.get("device", ""), query.get("ct", ""))
                if not token_id or store.token_revoked(desktop_id, token_id):
                    await socket.close(4404, "no such desktop or the pairing code expired.")
                    return
        desktop = hub.desktops.get(desktop_id)
        if desktop is None:
            await socket.close(4404, "that desktop is offline.")
            return

        channel = envelope.new_channel()
        if not hub.add_channel(desktop_id, channel, socket, token_id):
            await socket.close(4429, "too many connections to that desktop.")
            return
        meta = {"room": room} if room else {"device": query.get("device", "")}
        meta["peer"] = socket.peer.rsplit(":", 1)[0]
        try:
            await desktop.send(envelope.pack_json(envelope.KIND_OPEN, channel, meta))
        except ws.ConnectionClosed:
            hub.drop_channel(desktop_id, channel)
            await socket.close(4404, "that desktop went offline.")
            return
        store.note("channel_open", desktop_id, room=bool(room))

        try:
            while True:
                message = await socket.recv()
                if isinstance(message, str):
                    continue
                if len(message) > ws.MAX_FRAME:
                    await socket.close(4413, "frame too large.")
                    break
                try:
                    await desktop.send(envelope.pack(envelope.KIND_DATA, channel, message))
                except ws.ConnectionClosed:
                    await socket.close(4404, "that desktop went offline.")
                    break
        except ws.ConnectionClosed:
            pass
        finally:
            hub.drop_channel(desktop_id, channel)
            live = hub.desktops.get(desktop_id)
            if live is not None:
                try:
                    await live.send(envelope.pack_json(envelope.KIND_CLOSE, channel,
                                                       {"reason": "client disconnected"}))
                except ws.ConnectionClosed:
                    pass

    return server


async def run(host: str, port: int, db: str, static_root: Path | None = None) -> None:
    store = Store(db)
    server = build(store, static_root=static_root)
    await server.start(host, port)
    log.info("rendezvous listening on %s:%s (db %s)", host, server.port, db)
    try:
        await asyncio.Event().wait()
    finally:
        await server.close()
        store.close()


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="relay-rendezvous")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=8787)
    parser.add_argument("--db", default="rendezvous.sqlite3")
    parser.add_argument("--static", help="also serve a directory (development only)")
    parser.add_argument("--verbose", action="store_true")
    args = parser.parse_args(argv)
    logging.basicConfig(level=logging.DEBUG if args.verbose else logging.INFO,
                        format="%(asctime)s %(levelname)s %(name)s %(message)s")
    try:
        asyncio.run(run(args.host, args.port, args.db,
                        Path(args.static) if args.static else None))
    except KeyboardInterrupt:
        return 0
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
