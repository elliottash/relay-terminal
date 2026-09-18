# SPDX-License-Identifier: GPL-3.0-or-later
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
import hmac
import json
import logging
import secrets
import sqlite3
import sys
import time
from hashlib import sha256
from pathlib import Path

if __package__ in (None, ""):                      # running the file directly
    sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from remote import envelope, httpd, noise, push, ws

log = logging.getLogger("relay.rendezvous")

ROOM_TTL = 300              # five minutes, matching the QR secret's life
MAX_ROOM_TTL = 7 * 86400    # an invite may live up to a week (section 10)
METADATA_DAYS = 7
MAX_ROOMS_PER_HOUR = 20
MAX_CHANNELS_PER_DESKTOP = 32
MAX_PUSH_PER_HOUR = 600
CHALLENGE_TTL = 120


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
    last_seen REAL
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
        self.db.row_factory = sqlite3.Row
        self.db.executescript(SCHEMA)
        self.db.commit()

    def close(self) -> None:
        self.db.close()

    # ---- desktops ----------------------------------------------------------------------------

    def register(self, desktop_id: str, static_pubkey: str) -> str | None:
        """Register or re-register. Returns a fresh bearer token, or None on a key mismatch.

        The caller has already proved possession of the private half (``consume_challenge``), and
        ``desktop_id`` is derived from the key, so an id cannot be squatted or rebound.
        """
        row = self.db.execute("SELECT static_pubkey FROM desktops WHERE desktop_id = ?",
                              (desktop_id,)).fetchone()
        if row and row["static_pubkey"] != static_pubkey:
            return None          # the id is taken by a different key; never silently rebind it
        token = secrets.token_urlsafe(32)
        now = time.time()
        if row:
            self.db.execute("UPDATE desktops SET token_hash = ?, last_seen = ? WHERE desktop_id = ?",
                            (token_hash(token), now, desktop_id))
        else:
            self.db.execute(
                "INSERT INTO desktops (desktop_id, static_pubkey, token_hash, created, last_seen)"
                " VALUES (?, ?, ?, ?, ?)", (desktop_id, static_pubkey, token_hash(token), now, now))
        self.db.commit()
        return token

    def authenticate(self, desktop_id: str, token: str) -> bool:
        row = self.db.execute("SELECT token_hash FROM desktops WHERE desktop_id = ?",
                              (desktop_id,)).fetchone()
        return bool(row) and secrets.compare_digest(row["token_hash"], token_hash(token))

    def desktop_exists(self, desktop_id: str) -> bool:
        return bool(self.db.execute("SELECT 1 FROM desktops WHERE desktop_id = ?",
                                    (desktop_id,)).fetchone())

    # ---- rooms -------------------------------------------------------------------------------

    def open_room(self, desktop_id: str, ttl: float = ROOM_TTL) -> str | None:
        now = time.time()
        recent = self.db.execute(
            "SELECT COUNT(*) AS n FROM rooms WHERE desktop_id = ? AND created > ?",
            (desktop_id, now - 3600)).fetchone()["n"]
        if recent >= MAX_ROOMS_PER_HOUR:
            return None
        room = secrets.token_urlsafe(16)
        self.db.execute("INSERT INTO rooms (room, desktop_id, created, expires) VALUES (?, ?, ?, ?)",
                        (room, desktop_id, now, now + max(1.0, min(ttl, MAX_ROOM_TTL))))
        self.db.commit()
        return room

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


class Hub:
    """The live desktop sockets and the client channels attached to each."""

    def __init__(self):
        self.desktops: dict[str, ws.WebSocket] = {}
        self.channels: dict[str, dict[bytes, ws.WebSocket]] = {}

    def attach_desktop(self, desktop_id: str, socket: ws.WebSocket) -> ws.WebSocket | None:
        previous = self.desktops.get(desktop_id)
        self.desktops[desktop_id] = socket
        self.channels.setdefault(desktop_id, {})
        return previous

    def detach_desktop(self, desktop_id: str, socket: ws.WebSocket) -> list[ws.WebSocket]:
        if self.desktops.get(desktop_id) is not socket:
            return []
        self.desktops.pop(desktop_id, None)
        return list(self.channels.pop(desktop_id, {}).values())

    def add_channel(self, desktop_id: str, channel: bytes, socket: ws.WebSocket) -> bool:
        channels = self.channels.setdefault(desktop_id, {})
        if len(channels) >= MAX_CHANNELS_PER_DESKTOP:
            return False
        channels[channel] = socket
        return True

    def drop_channel(self, desktop_id: str, channel: bytes) -> ws.WebSocket | None:
        return self.channels.get(desktop_id, {}).pop(channel, None)

    def client(self, desktop_id: str, channel: bytes) -> ws.WebSocket | None:
        return self.channels.get(desktop_id, {}).get(channel)


def build(store: Store, static_root: Path | None = None) -> httpd.Server:
    server = httpd.Server(static_root=static_root)
    hub = Hub()

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
        token = store.register(desktop_id, pubkey)
        if token is None:
            return httpd.Response.error(409, "that desktop id belongs to a different key.")
        store.note("register", desktop_id)
        return httpd.Response.json({"desktop_id": desktop_id, "token": token})

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
        room = store.open_room(desktop_id, float(fields.get("ttl", ROOM_TTL)))
        if room is None:
            return httpd.Response.error(429, "too many pairing rooms this hour.")
        store.note("room", desktop_id)
        return httpd.Response.json({"room": room, "expires_in": ROOM_TTL})

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
        desktop = hub.desktops.get(desktop_id)
        if desktop is None:
            await socket.close(4404, "that desktop is offline.")
            return

        channel = envelope.new_channel()
        if not hub.add_channel(desktop_id, channel, socket):
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
