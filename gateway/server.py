# SPDX-License-Identifier: GPL-3.0-or-later
"""relay-gateway: the routes, the admission checks and the one log line per request.

Routes (docs/RELAY-FREE.md):

    POST /v1/challenge          -> {challenge, ephemeral_public, expires_in}
    POST /v1/register           -> {installation_id, token, expires_in, plan, quota}
    POST /v1/chat/completions   -> SSE, with X-Relay-Quota-Limit / -Used / -Resets-At
    GET  /v1/quota              -> {limit, used, resets_at, plan}
    GET  /v1/health             -> {ok, roles, open}

Before a request opens an upstream the checks run in this order: token valid → gateway open
(the spend ceilings) → global concurrency → per-install concurrency → per-install requests per
minute → per-install daily tokens. Every refusal is JSON with a stable code
(``quota_exhausted | rate_limited | free_unavailable | token_expired | bad_request``) so the
desktop can word it, and a database failure fails closed as ``free_unavailable``.

The log is metadata: a hash of the installation id, the role, the provider, the status, timing
and token counts. Never a message, never a key. ``GATEWAY_DIAGNOSTIC_BODIES=1`` is the one
temporary exception, off by default and announced at startup when on.

Run it:  python3 -m gateway.server --config /etc/relay-gateway/gateway.json --db gateway.db
"""
from __future__ import annotations

import argparse
import asyncio
import logging
import os
import sys
import time
from pathlib import Path

if __package__ in (None, ""):                      # running the file directly
    sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from remote import httpd, ws

from . import config as config_mod
from . import proxy, validate
from .config import Config
from .store import CHALLENGE_TTL, Installation, Store, StoreError, derive_installation_id, \
    installation_hash

log = logging.getLogger("relay.gateway")

MAX_BODY = config_mod.MAX_REQUEST_BYTES
EXPIRE_EVERY = 600


def error(status: int, code: str, message: str, resets_at: int | None = None,
          headers: dict[str, str] | None = None) -> httpd.Response:
    body = {"error": {"code": code, "message": message}}
    if resets_at is not None:
        body["error"]["resets_at"] = resets_at
    response = httpd.Response.json(body, status=status)
    response.headers = headers
    return response


def quota_headers(usage) -> dict[str, str]:
    return {"X-Relay-Quota-Limit": str(usage.limit), "X-Relay-Quota-Used": str(usage.as_dict()["used"]),
            "X-Relay-Quota-Resets-At": str(usage.resets_at)}


def diagnostic_bodies_enabled() -> bool:
    return os.environ.get("GATEWAY_DIAGNOSTIC_BODIES") == "1"


class Gate:
    """The in-process counters: what is streaming right now, globally and per installation."""

    def __init__(self, config: Config):
        self.config = config
        self.active = 0
        self.per_install: dict[str, int] = {}

    def admit(self, installation_id: str) -> str | None:
        """Take a slot, or say which limit refused: ``global`` or ``install``."""
        if self.active >= self.config.limits.global_concurrency:
            return "global"
        if self.per_install.get(installation_id, 0) >= self.config.quota.concurrency_per_install:
            return "install"
        self.active += 1
        self.per_install[installation_id] = self.per_install.get(installation_id, 0) + 1
        return None

    def release(self, installation_id: str) -> None:
        self.active = max(0, self.active - 1)
        left = self.per_install.get(installation_id, 0) - 1
        if left > 0:
            self.per_install[installation_id] = left
        else:
            self.per_install.pop(installation_id, None)


def build(store: Store, config: Config) -> httpd.Server:
    server = httpd.Server(max_body=MAX_BODY)
    gate = Gate(config)
    diagnostic = diagnostic_bodies_enabled()
    last_expired = 0.0

    def client_ip(request: ws.Request) -> str:
        if config.client_ip_header:
            forwarded = request.header(config.client_ip_header).strip()
            if forwarded:
                return forwarded.split(",")[0].strip()
        return (request.peer or "").rsplit(":", 1)[0]

    def bearer(request: ws.Request) -> str:
        value = request.header("authorization").strip()
        if value[:7].lower() != "bearer ":
            return ""
        return value[7:].strip()

    def authenticated(request: ws.Request) -> Installation | httpd.Response:
        token = bearer(request)
        installation = store.authenticate(token) if token else None
        if installation is None:
            return error(401, "token_expired", "the token is missing, unknown or expired; "
                         "register again for a new one.")
        return installation

    def housekeeping() -> None:
        nonlocal last_expired
        if time.time() - last_expired > EXPIRE_EVERY:
            last_expired = time.time()
            store.expire()

    def ceilings_hit() -> str | None:
        """Which global spend ceiling is reached, or None while the gateway is open."""
        limits = config.limits
        if store.spend_today() >= limits.spend_per_day_usd:
            return "day"
        if store.spend_month() >= limits.spend_per_month_usd:
            return "month"
        return None

    def open_upstreams(role: config_mod.Role) -> list[config_mod.Upstream]:
        """The role's upstreams whose provider has not hit its own daily ceiling."""
        out = []
        for upstream in role.upstreams:
            ceiling = config.limits.per_provider_per_day_usd.get(upstream.provider)
            if ceiling is not None and store.spend_today(upstream.provider) >= ceiling:
                continue
            out.append(upstream)
        return out

    def log_request(installation_id: str, role: str, outcome: proxy.Outcome) -> None:
        log.info("chat install=%s role=%s provider=%s model=%s status=%d ttft_ms=%s total_ms=%d "
                 "in=%d out=%d usage=%s error=%s fallback=%d truncated=%d",
                 installation_hash(installation_id), role, outcome.provider or "-",
                 outcome.model or "-", outcome.status,
                 "-" if outcome.ttft_ms is None else outcome.ttft_ms, outcome.total_ms,
                 outcome.input_tokens, outcome.output_tokens,
                 "reported" if outcome.usage_reported else "estimated",
                 outcome.error or "-", outcome.fallback, int(outcome.truncated))

    # ---- registration --------------------------------------------------------------------------

    @server.route("POST", "/v1/challenge")
    async def challenge(request: ws.Request, body: bytes) -> httpd.Response:
        """Step one of registering: prove you hold the private half of the key you claim."""
        try:
            housekeeping()
            if store.count_ip(client_ip(request), "challenges") > config.limits.challenges_per_ip_per_hour:
                return error(429, "rate_limited", "too many registration attempts from this "
                             "address this hour.", resets_at=int(time.time() // 3600 + 1) * 3600)
            value, ephemeral = store.new_challenge()
        except StoreError:
            log.exception("challenge: database failure")
            return error(503, "free_unavailable", "Relay Free is unavailable right now.")
        return httpd.Response.json({"challenge": value, "ephemeral_public": ephemeral,
                                    "expires_in": CHALLENGE_TTL})

    @server.route("POST", "/v1/register")
    async def register(request: ws.Request, body: bytes) -> httpd.Response:
        try:
            fields = httpd.json_body(body)
        except ValueError as exc:
            return error(400, "bad_request", str(exc))
        pubkey = str(fields.get("static_pubkey", ""))
        if len(pubkey) > 128:
            return error(400, "bad_request", "static_pubkey is too long.")
        installation_id = derive_installation_id(pubkey)
        if not installation_id:
            return error(400, "bad_request", "static_pubkey must be 32 base64 bytes.")
        client = fields.get("client") if isinstance(fields.get("client"), dict) else {}
        version = str(client.get("version", ""))[:64]
        try:
            if store.count_ip(client_ip(request), "registrations") \
                    > config.limits.registrations_per_ip_per_hour:
                return error(429, "rate_limited", "too many registrations from this address this "
                             "hour.", resets_at=int(time.time() // 3600 + 1) * 3600)
            # The id is derived here, never taken from the request: it cannot be squatted.
            if not store.consume_challenge(str(fields.get("challenge", "")), pubkey,
                                           str(fields.get("proof", ""))):
                return error(401, "bad_request", "that challenge is unknown, spent or unproved.")
            registered = store.register(installation_id, pubkey, config.token_ttl_seconds, version)
            if registered is None:
                return error(409, "bad_request", "that installation id belongs to a different key.")
            token, expires, installation = registered
            usage = store.usage(installation_id, config.quota.tokens_per_day)
            store.note("register", installation_id)
        except StoreError:
            log.exception("register: database failure")
            return error(503, "free_unavailable", "Relay Free is unavailable right now.")
        log.info("register install=%s version=%s", installation_hash(installation_id), version or "-")
        return httpd.Response.json({"installation_id": installation_id, "token": token,
                                    "expires_in": int(expires - time.time()),
                                    "plan": installation.plan, "quota": usage.as_dict()})

    # ---- quota and health ----------------------------------------------------------------------

    @server.route("GET", "/v1/quota")
    async def quota(request: ws.Request, body: bytes) -> httpd.Response:
        try:
            installation = authenticated(request)
            if isinstance(installation, httpd.Response):
                return installation
            usage = store.usage(installation.id, config.quota.tokens_per_day)
        except StoreError:
            log.exception("quota: database failure")
            return error(503, "free_unavailable", "Relay Free is unavailable right now.")
        response = httpd.Response.json({**usage.as_dict(), "plan": installation.plan})
        response.headers = quota_headers(usage)
        return response

    @server.route("GET", "/v1/health")
    async def health(request: ws.Request, body: bytes) -> httpd.Response:
        try:
            is_open = ceilings_hit() is None
        except StoreError:
            return httpd.Response.json({"ok": False, "roles": sorted(config.roles), "open": False},
                                       status=503)
        return httpd.Response.json({"ok": True, "roles": sorted(config.roles), "open": is_open})

    # ---- completions ---------------------------------------------------------------------------

    @server.route("POST", "/v1/chat/completions")
    async def completions(request: ws.Request, body: bytes) -> httpd.Response:
        unavailable = error(503, "free_unavailable", "Relay Free is unavailable right now.")
        try:
            housekeeping()
            installation = authenticated(request)
            if isinstance(installation, httpd.Response):
                return installation
            if ceilings_hit() is not None:
                return error(503, "free_unavailable", "Relay Free has reached its spending limit "
                             "for now; use your own provider key.")
        except StoreError:
            log.exception("completions: database failure")
            return unavailable

        refused = gate.admit(installation.id)
        if refused == "global":
            return error(503, "free_unavailable", "Relay Free is busy; try again in a moment.")
        if refused == "install":
            return error(429, "rate_limited", "this installation already has "
                         f"{config.quota.concurrency_per_install} requests in flight.",
                         resets_at=int(time.time()) + 5)
        # The slot is held from here; every refusal below gives it back, and once a reservation
        # is made the stream's own ``finally`` releases both.
        reserved = False
        try:
            try:
                if store.recent_requests(installation.id) >= config.quota.requests_per_minute:
                    return error(429, "rate_limited", "too many requests this minute.",
                                 resets_at=int(time.time()) + 60)
            except StoreError:
                log.exception("completions: database failure")
                return unavailable
            try:
                validated = validate.validate(body, config)
            except validate.BadRequest as exc:
                return error(400, "bad_request", str(exc))
            role = validated.role
            try:
                upstreams = open_upstreams(role)
                if not upstreams:
                    return error(503, "free_unavailable", f"{role.name} has reached its spending "
                                 "limit for today; use your own provider key.")
                usage = store.reserve(installation.id, config.quota.tokens_per_day,
                                      validated.input_estimate)
                if usage is None:
                    usage = store.usage(installation.id, config.quota.tokens_per_day)
                    return error(429, "quota_exhausted", "Relay Free allowance used for today.",
                                 resets_at=usage.resets_at, headers=quota_headers(usage))
                store.note("chat", installation.id, role=role.name)
                store.touch(installation.id)
            except StoreError:
                log.exception("completions: database failure")
                return unavailable
            reserved = True
        finally:
            if not reserved:
                gate.release(installation.id)

        completion = proxy.Completion(config, validated, upstreams, diagnostic=diagnostic)
        opened = await completion.open()
        if not opened:
            completion.finish()
            gate.release(installation.id)
            try:
                store.settle(installation.id, validated.input_estimate, 0, 0, 0, "", role.name,
                             counted=False)
            except StoreError:
                log.exception("completions: database failure releasing a reservation")
            log_request(installation.id, role.name, completion.outcome)
            return error(completion.outcome.status, "free_unavailable",
                         "Relay Free could not reach a model provider; try again shortly.")

        async def stream():
            outcome = completion.outcome
            outcome.status = 200
            try:
                async for chunk in completion.chunks():
                    yield chunk
            finally:
                gate.release(installation.id)
                try:
                    store.settle(installation.id, validated.input_estimate, outcome.input_tokens,
                                 outcome.output_tokens, completion.cost_micros(),
                                 outcome.provider, role.name)
                except StoreError:
                    log.exception("completions: database failure settling usage")
                log_request(installation.id, role.name, outcome)

        return httpd.Response.stream(stream(), content_type=completion.content_type,
                                     headers=quota_headers(usage))

    return server


async def run(host: str, port: int, db: str, config: Config) -> None:
    store = Store(db)
    server = build(store, config)
    await server.start(host, port)
    log.info("gateway listening on %s:%s (db %s, roles %s)", host, server.port, db,
             ", ".join(sorted(config.roles)))
    try:
        await asyncio.Event().wait()
    finally:
        await server.close()
        store.close()


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="relay-gateway")
    parser.add_argument("--config", required=True, help="path to gateway.json")
    parser.add_argument("--db", default="gateway.sqlite3")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=8790)
    parser.add_argument("--verbose", action="store_true")
    args = parser.parse_args(argv)
    logging.basicConfig(level=logging.DEBUG if args.verbose else logging.INFO,
                        format="%(asctime)s %(levelname)s %(name)s %(message)s")
    try:
        config = config_mod.load(args.config)
    except config_mod.ConfigError as exc:
        log.error("config: %s", exc)
        return 2
    if diagnostic_bodies_enabled():
        log.warning("GATEWAY_DIAGNOSTIC_BODIES=1: request and response bodies are being logged. "
                    "This is a temporary diagnostic mode; unset it as soon as the problem is found.")
    try:
        asyncio.run(run(args.host, args.port, args.db, config))
    except KeyboardInterrupt:
        return 0
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
