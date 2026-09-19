# SPDX-License-Identifier: AGPL-3.0-or-later
"""SQLite for the gateway: installations, tokens, quotas, spend and seven days of metadata.

Modelled on ``rendezvous/server.py::Store``. An installation's id is derived here from its public
key (``sha256(pubkey)[:32]``), never taken from a request, so an id cannot be squatted or rebound
to another key. Tokens are opaque random strings stored only as hashes and expire on a clock the
config sets: an opaque token needs the database on every request, and the quotas need it anyway.

Every write that a quota depends on happens in one transaction, so two concurrent requests from
the same installation cannot both pass the last thousand tokens. When the database itself fails
the caller fails the request *closed* (503 ``free_unavailable``); this module raises rather than
guessing, and never returns "allowed" it could not record.
"""
from __future__ import annotations

import base64
import hmac
import json
import secrets
import sqlite3
import time
from datetime import datetime, timedelta, timezone
from hashlib import sha256
from pathlib import Path

from remote import noise

CHALLENGE_TTL = 120
METADATA_DAYS = 7
USAGE_DAYS = 35              # per-installation daily totals; spend rows are kept for the ledger
IP_COUNT_HOURS = 2
DEFAULT_PLAN = "free"

# What a store raises when SQLite is unusable. Routes catch this and answer 503.
StoreError = sqlite3.Error

SCHEMA = """
CREATE TABLE IF NOT EXISTS installations (
    installation_id TEXT PRIMARY KEY,
    static_pubkey TEXT NOT NULL,
    token_hash TEXT NOT NULL,
    token_expires REAL NOT NULL,
    plan TEXT NOT NULL,
    created REAL NOT NULL,
    last_seen REAL,
    client_version TEXT
);
CREATE INDEX IF NOT EXISTS installations_token ON installations(token_hash);
CREATE TABLE IF NOT EXISTS challenges (
    challenge TEXT PRIMARY KEY,
    ephemeral_private TEXT NOT NULL,
    created REAL NOT NULL
);
CREATE TABLE IF NOT EXISTS usage (
    installation_id TEXT NOT NULL,
    day TEXT NOT NULL,
    requests INTEGER NOT NULL DEFAULT 0,
    input_tokens INTEGER NOT NULL DEFAULT 0,
    output_tokens INTEGER NOT NULL DEFAULT 0,
    reserved_tokens INTEGER NOT NULL DEFAULT 0,
    cost_micros INTEGER NOT NULL DEFAULT 0,
    PRIMARY KEY (installation_id, day)
);
CREATE TABLE IF NOT EXISTS spend (
    day TEXT NOT NULL,
    provider TEXT NOT NULL,
    role TEXT NOT NULL,
    requests INTEGER NOT NULL DEFAULT 0,
    input_tokens INTEGER NOT NULL DEFAULT 0,
    output_tokens INTEGER NOT NULL DEFAULT 0,
    cost_micros INTEGER NOT NULL DEFAULT 0,
    PRIMARY KEY (day, provider, role)
);
CREATE TABLE IF NOT EXISTS ip_counts (
    ip_hash TEXT NOT NULL,
    hour INTEGER NOT NULL,
    challenges INTEGER NOT NULL DEFAULT 0,
    registrations INTEGER NOT NULL DEFAULT 0,
    PRIMARY KEY (ip_hash, hour)
);
CREATE TABLE IF NOT EXISTS events (
    at REAL NOT NULL,
    kind TEXT NOT NULL,
    installation_hash TEXT,
    detail TEXT
);
CREATE INDEX IF NOT EXISTS events_at ON events(at);
CREATE INDEX IF NOT EXISTS events_kind_install ON events(kind, installation_hash, at);
"""


def derive_installation_id(static_pubkey_b64: str) -> str:
    """The first 128 bits of SHA-256 over the raw public key, or "" for a malformed key."""
    try:
        raw = base64.b64decode(static_pubkey_b64, validate=True)
    except Exception:
        return ""
    if len(raw) != 32:
        return ""
    return sha256(raw).hexdigest()[:32]


def installation_hash(installation_id: str) -> str:
    """What the log and the events table carry: enough to correlate, not the id itself."""
    return sha256(installation_id.encode()).hexdigest()[:12]


def token_hash(token: str) -> str:
    return sha256(token.encode()).hexdigest()


def day_of(now: float | None = None) -> str:
    return datetime.fromtimestamp(time.time() if now is None else now, timezone.utc).strftime("%Y-%m-%d")


def month_prefix(now: float | None = None) -> str:
    return day_of(now)[:7]


def next_midnight(now: float | None = None) -> int:
    """When the daily allowance resets, as a UTC epoch second. Quotas run on UTC days so that
    every client, wherever it is, sees the same ``resets_at`` the gateway will act on."""
    moment = datetime.fromtimestamp(time.time() if now is None else now, timezone.utc)
    tomorrow = (moment + timedelta(days=1)).replace(hour=0, minute=0, second=0, microsecond=0)
    return int(tomorrow.timestamp())


class Installation:
    def __init__(self, row: sqlite3.Row):
        self.id: str = row["installation_id"]
        self.plan: str = row["plan"]
        self.token_expires: float = row["token_expires"]
        self.created: float = row["created"]


class Usage:
    """One installation's day so far. ``used`` counts what is settled plus what is reserved for
    requests still streaming, so a burst of concurrent requests cannot all be admitted against
    the same remaining allowance."""

    def __init__(self, limit: int, input_tokens: int = 0, output_tokens: int = 0,
                 reserved: int = 0, requests: int = 0, resets_at: int | None = None):
        self.limit = limit
        self.input_tokens = input_tokens
        self.output_tokens = output_tokens
        self.reserved = reserved
        self.requests = requests
        self.resets_at = next_midnight() if resets_at is None else resets_at

    @property
    def used(self) -> int:
        return self.input_tokens + self.output_tokens + self.reserved

    def as_dict(self) -> dict:
        return {"limit": self.limit, "used": min(self.used, self.limit) if self.limit else self.used,
                "resets_at": self.resets_at}


class Store:
    def __init__(self, path: str | Path = ":memory:"):
        # Every method below runs on the server's event-loop thread, and the ``with self.db``
        # blocks are real transactions (the module's default deferred mode), so a read-check-write
        # such as ``reserve`` cannot interleave with another request's.
        self.db = sqlite3.connect(path, check_same_thread=False)
        self.db.row_factory = sqlite3.Row
        # WAL keeps a reader (the operator's sqlite3 query) from blocking the writer, and the
        # busy timeout keeps that operator's query from failing a request outright.
        if str(path) != ":memory:":
            self.db.execute("PRAGMA journal_mode = WAL")
        self.db.execute("PRAGMA busy_timeout = 5000")
        self.db.executescript(SCHEMA)
        self.db.commit()

    def close(self) -> None:
        self.db.close()

    # ---- registration --------------------------------------------------------------------------

    def new_challenge(self) -> tuple[str, str]:
        """A challenge plus our ephemeral X25519 public key, for proof of possession."""
        private, public = noise.generate_keypair()
        challenge = secrets.token_urlsafe(24)
        with self.db:
            self.db.execute("DELETE FROM challenges WHERE created < ?", (time.time() - CHALLENGE_TTL,))
            self.db.execute("INSERT INTO challenges (challenge, ephemeral_private, created)"
                            " VALUES (?, ?, ?)",
                            (challenge, base64.b64encode(private).decode(), time.time()))
        return challenge, base64.b64encode(public).decode()

    def consume_challenge(self, challenge: str, static_pubkey: str, proof: str) -> bool:
        """Check HMAC(DH(ephemeral, static), challenge). Single use, whatever the outcome."""
        with self.db:
            row = self.db.execute(
                "SELECT ephemeral_private, created FROM challenges WHERE challenge = ?",
                (challenge,)).fetchone()
            if not row:
                return False
            self.db.execute("DELETE FROM challenges WHERE challenge = ?", (challenge,))
        if time.time() - row["created"] > CHALLENGE_TTL:
            return False
        try:
            shared = noise.dh(base64.b64decode(row["ephemeral_private"]),
                              base64.b64decode(static_pubkey))
        except Exception:
            return False
        expected = hmac.new(shared, challenge.encode(), sha256).hexdigest()
        return secrets.compare_digest(expected, proof)

    def register(self, installation_id: str, static_pubkey: str, ttl: int,
                 client_version: str = "") -> tuple[str, float, Installation] | None:
        """Register or re-register; every call rotates the token.

        Returns ``(token, expires, installation)``, or None when the id already belongs to a
        different key (which can only be a SHA-256 collision or a forged id, and is refused
        either way rather than silently rebound).
        """
        now = time.time()
        token = secrets.token_urlsafe(32)
        expires = now + ttl
        with self.db:
            row = self.db.execute("SELECT static_pubkey FROM installations WHERE installation_id = ?",
                                  (installation_id,)).fetchone()
            if row and row["static_pubkey"] != static_pubkey:
                return None
            if row:
                self.db.execute(
                    "UPDATE installations SET token_hash = ?, token_expires = ?, last_seen = ?,"
                    " client_version = ? WHERE installation_id = ?",
                    (token_hash(token), expires, now, client_version[:64], installation_id))
            else:
                self.db.execute(
                    "INSERT INTO installations (installation_id, static_pubkey, token_hash,"
                    " token_expires, plan, created, last_seen, client_version)"
                    " VALUES (?, ?, ?, ?, ?, ?, ?, ?)",
                    (installation_id, static_pubkey, token_hash(token), expires, DEFAULT_PLAN, now,
                     now, client_version[:64]))
            fresh = self.db.execute("SELECT * FROM installations WHERE installation_id = ?",
                                    (installation_id,)).fetchone()
        return token, expires, Installation(fresh)

    def authenticate(self, token: str) -> Installation | None:
        """The installation a bearer token belongs to, or None when unknown **or expired**."""
        if not token or len(token) > 128:
            return None
        row = self.db.execute("SELECT * FROM installations WHERE token_hash = ?",
                              (token_hash(token),)).fetchone()
        if not row or not secrets.compare_digest(row["token_hash"], token_hash(token)):
            return None
        if row["token_expires"] <= time.time():
            return None
        return Installation(row)

    def touch(self, installation_id: str) -> None:
        with self.db:
            self.db.execute("UPDATE installations SET last_seen = ? WHERE installation_id = ?",
                            (time.time(), installation_id))

    def set_plan(self, installation_id: str, plan: str) -> bool:
        with self.db:
            cursor = self.db.execute("UPDATE installations SET plan = ? WHERE installation_id = ?",
                                     (plan, installation_id))
        return cursor.rowcount > 0

    # ---- per-installation quota ---------------------------------------------------------------

    def usage(self, installation_id: str, limit: int) -> Usage:
        row = self.db.execute(
            "SELECT requests, input_tokens, output_tokens, reserved_tokens FROM usage"
            " WHERE installation_id = ? AND day = ?", (installation_id, day_of())).fetchone()
        if not row:
            return Usage(limit)
        return Usage(limit, row["input_tokens"], row["output_tokens"], row["reserved_tokens"],
                     row["requests"])

    def reserve(self, installation_id: str, limit: int, estimate: int) -> Usage | None:
        """Admit a request against today's allowance, holding ``estimate`` tokens until
        ``settle``. Returns the usage *after* the reservation, or None when it does not fit.

        The estimate is the prompt's size (chars/4): the reply's length is unknown until it has
        streamed, and refusing every request that *could* reach the cap would refuse most of
        them. An installation may therefore overrun by at most one reply, which ``settle``
        then counts, so the next request is refused.
        """
        day = day_of()
        with self.db:
            self.db.execute(
                "INSERT OR IGNORE INTO usage (installation_id, day) VALUES (?, ?)",
                (installation_id, day))
            row = self.db.execute(
                "SELECT requests, input_tokens, output_tokens, reserved_tokens FROM usage"
                " WHERE installation_id = ? AND day = ?", (installation_id, day)).fetchone()
            used = row["input_tokens"] + row["output_tokens"] + row["reserved_tokens"]
            if used >= limit or used + estimate > limit:
                return None
            self.db.execute(
                "UPDATE usage SET reserved_tokens = reserved_tokens + ?"
                " WHERE installation_id = ? AND day = ?", (estimate, installation_id, day))
        return Usage(limit, row["input_tokens"], row["output_tokens"],
                     row["reserved_tokens"] + estimate, row["requests"])

    def settle(self, installation_id: str, estimate: int, input_tokens: int, output_tokens: int,
               cost_micros: int, provider: str, role: str, counted: bool = True) -> None:
        """Replace a reservation with what the call actually used, and add it to the spend
        ledger. ``counted`` is false when nothing reached upstream, so only the hold is released."""
        day = day_of()
        with self.db:
            self.db.execute(
                "UPDATE usage SET reserved_tokens = MAX(0, reserved_tokens - ?),"
                " input_tokens = input_tokens + ?, output_tokens = output_tokens + ?,"
                " requests = requests + ?, cost_micros = cost_micros + ?"
                " WHERE installation_id = ? AND day = ?",
                (estimate, input_tokens, output_tokens, 1 if counted else 0, cost_micros,
                 installation_id, day))
            if counted:
                self.db.execute(
                    "INSERT INTO spend (day, provider, role, requests, input_tokens, output_tokens,"
                    " cost_micros) VALUES (?, ?, ?, 1, ?, ?, ?)"
                    " ON CONFLICT (day, provider, role) DO UPDATE SET"
                    " requests = requests + 1, input_tokens = input_tokens + excluded.input_tokens,"
                    " output_tokens = output_tokens + excluded.output_tokens,"
                    " cost_micros = cost_micros + excluded.cost_micros",
                    (day, provider, role, input_tokens, output_tokens, cost_micros))

    def recent_requests(self, installation_id: str, seconds: int = 60) -> int:
        return self.db.execute(
            "SELECT COUNT(*) AS n FROM events WHERE kind = 'chat' AND installation_hash = ?"
            " AND at > ?", (installation_hash(installation_id), time.time() - seconds)).fetchone()["n"]

    # ---- spend ceilings -------------------------------------------------------------------------

    def spend_today(self, provider: str | None = None) -> float:
        """Today's spend in USD, for one provider or all of them."""
        if provider is None:
            row = self.db.execute("SELECT COALESCE(SUM(cost_micros), 0) AS c FROM spend WHERE day = ?",
                                  (day_of(),)).fetchone()
        else:
            row = self.db.execute(
                "SELECT COALESCE(SUM(cost_micros), 0) AS c FROM spend WHERE day = ? AND provider = ?",
                (day_of(), provider)).fetchone()
        return row["c"] / 1_000_000

    def spend_month(self) -> float:
        row = self.db.execute(
            "SELECT COALESCE(SUM(cost_micros), 0) AS c FROM spend WHERE day LIKE ?",
            (month_prefix() + "-%",)).fetchone()
        return row["c"] / 1_000_000

    # ---- per-address limits ---------------------------------------------------------------------

    def count_ip(self, address: str, what: str) -> int:
        """Count one more challenge or registration from an address this hour and return the
        new total. The address is stored hashed: the limit needs equality, not the address."""
        assert what in ("challenges", "registrations")
        hour = int(time.time() // 3600)
        key = sha256(address.encode()).hexdigest()[:16]
        with self.db:
            self.db.execute("INSERT OR IGNORE INTO ip_counts (ip_hash, hour) VALUES (?, ?)", (key, hour))
            self.db.execute(f"UPDATE ip_counts SET {what} = {what} + 1 WHERE ip_hash = ? AND hour = ?",
                            (key, hour))
            row = self.db.execute(f"SELECT {what} AS n FROM ip_counts WHERE ip_hash = ? AND hour = ?",
                                  (key, hour)).fetchone()
        return row["n"]

    # ---- metadata -------------------------------------------------------------------------------

    def note(self, kind: str, installation_id: str | None = None, **detail) -> None:
        """Operational metadata only: hashed ids, counts and times. Never content."""
        with self.db:
            self.db.execute(
                "INSERT INTO events (at, kind, installation_hash, detail) VALUES (?, ?, ?, ?)",
                (time.time(), kind, installation_hash(installation_id) if installation_id else None,
                 json.dumps(detail) if detail else None))

    def expire(self) -> None:
        now = time.time()
        with self.db:
            self.db.execute("DELETE FROM events WHERE at < ?", (now - METADATA_DAYS * 86400,))
            self.db.execute("DELETE FROM challenges WHERE created < ?", (now - CHALLENGE_TTL,))
            self.db.execute("DELETE FROM ip_counts WHERE hour < ?",
                            (int(now // 3600) - IP_COUNT_HOURS,))
            self.db.execute("DELETE FROM usage WHERE day < ?", (day_of(now - USAGE_DAYS * 86400),))

    def active_installations(self, days: int = 1) -> int:
        return self.db.execute("SELECT COUNT(*) AS n FROM installations WHERE last_seen > ?",
                               (time.time() - days * 86400,)).fetchone()["n"]
