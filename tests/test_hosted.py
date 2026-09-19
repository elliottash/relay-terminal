# SPDX-License-Identifier: GPL-3.0-or-later
"""Relay Free's client half (protocol 13.9): identity, token cache, the hosted transport, the worker.

A fake gateway on loopback stands in for ``gateway/``: it runs the real challenge/proof check
(``remote.noise``, the same X25519 arithmetic the rendezvous uses), issues opaque tokens, streams a
canned SSE reply with the ``X-Relay-Quota-*`` headers, and can be told to refuse once with 401 or to
report the allowance as used up. Nothing here reaches the network or the desktop keyring:
``RELAY_KEYRING=off`` sends the installation key to a file under a temporary ``XDG_DATA_HOME``.
"""
import base64
import hmac
import json
import os
import secrets
import stat
import subprocess
import sys
import tempfile
import threading
import time
import unittest
from hashlib import sha256
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from unittest import mock

from relay_core import hosted
from relay_core.presets import PRESETS
from relay_core.provider import ChatProvider, HostedChatProvider, ProviderConfig, ProviderError, make_provider

ROOT = Path(__file__).resolve().parents[1]


def sse(*chunks) -> bytes:
    out = b""
    for chunk in chunks:
        out += b"data: " + json.dumps(chunk).encode() + b"\n\n"
    return out + b"data: [DONE]\n\n"


class FakeGateway:
    """The gateway's contract, as the plan fixes it, with knobs the tests turn."""

    def __init__(self):
        from remote import noise
        self.noise = noise
        self.challenges: dict[str, bytes] = {}
        self.tokens: dict[str, float] = {}
        self.registrations = 0
        self.chat_calls = 0
        self.requests: list[dict] = []
        self.expires_in = 3600
        self.quota = {"limit": 250_000, "used": 1_200, "resets_at": int(time.time()) + 3600}
        self.unauthorized_once = False       # answer the next chat call 401, then behave
        self.always_unauthorized = False
        self.exhausted = False               # answer every chat call 429 quota_exhausted
        self.rate_limited_once = False       # answer the next chat call 429 rate_limited, then behave
        self.always_rate_limited = False     # answer every chat call 429 rate_limited
        self.chat_script: list[str] = []     # "unauthorized"/"rate_limited"/"ok" per call, then the knobs
        self.lock = threading.Lock()
        outer = self

        class Handler(BaseHTTPRequestHandler):
            def log_message(self, *args):
                pass

            def _json(self, status, obj, headers=()):
                body = json.dumps(obj).encode()
                self.send_response(status)
                self.send_header("Content-Type", "application/json")
                self.send_header("Content-Length", str(len(body)))
                for name, value in headers:
                    self.send_header(name, value)
                self.end_headers()
                self.wfile.write(body)

            def _refuse(self, status, code, message, headers=()):
                self._json(status, {"error": {"code": code, "message": message,
                                              "resets_at": outer.quota["resets_at"] if code == "quota_exhausted" else None}},
                           headers)

            def _quota_headers(self):
                return [("X-Relay-Quota-Limit", str(outer.quota["limit"])),
                        ("X-Relay-Quota-Used", str(outer.quota["used"])),
                        ("X-Relay-Quota-Resets-At", str(outer.quota["resets_at"]))]

            def _bearer(self):
                header = self.headers.get("Authorization") or ""
                token = header[7:] if header.startswith("Bearer ") else ""
                with outer.lock:
                    expires = outer.tokens.get(token)
                return token if expires and expires > time.time() else None

            def do_GET(self):
                if self.path != "/v1/quota":
                    return self._refuse(404, "bad_request", "no such route")
                if self._bearer() is None:
                    return self._refuse(401, "token_expired", "register again")
                self._json(200, {**outer.quota, "plan": "free"})

            def do_POST(self):
                length = int(self.headers.get("Content-Length") or 0)
                body = json.loads(self.rfile.read(length) or b"{}")
                outer.requests.append({"path": self.path, "body": body,
                                       "authorization": self.headers.get("Authorization")})
                if self.path == "/v1/challenge":
                    private, public = outer.noise.generate_keypair()
                    challenge = secrets.token_urlsafe(24)
                    with outer.lock:
                        outer.challenges[challenge] = private
                    return self._json(200, {"challenge": challenge, "expires_in": 120,
                                            "ephemeral_public": base64.b64encode(public).decode()})
                if self.path == "/v1/register":
                    with outer.lock:
                        private = outer.challenges.pop(body.get("challenge", ""), None)
                    if private is None:
                        return self._refuse(400, "bad_request", "unknown challenge")
                    static = base64.b64decode(body["static_pubkey"])
                    expected = hmac.new(outer.noise.dh(private, static), body["challenge"].encode(), sha256).hexdigest()
                    if not secrets.compare_digest(expected, body.get("proof", "")):
                        return self._refuse(400, "bad_request", "bad proof")
                    token = secrets.token_urlsafe(32)
                    with outer.lock:
                        outer.tokens = {token: time.time() + outer.expires_in}    # rotates: one live token
                        outer.registrations += 1
                    return self._json(200, {"installation_id": sha256(static).hexdigest()[:32], "token": token,
                                            "expires_in": outer.expires_in, "plan": "free",
                                            "quota": dict(outer.quota)})
                if self.path == "/v1/chat/completions":
                    with outer.lock:
                        outer.chat_calls += 1
                        scripted = outer.chat_script.pop(0) if outer.chat_script else ""
                        refuse = outer.always_unauthorized or outer.unauthorized_once
                        outer.unauthorized_once = False
                        limited = outer.rate_limited_once or outer.always_rate_limited
                        outer.rate_limited_once = False
                        if scripted:
                            refuse, limited = scripted == "unauthorized", scripted == "rate_limited"
                    if refuse or self._bearer() is None:
                        return self._refuse(401, "token_expired", "register again")
                    if limited:
                        # The gateway's own shape: the window reopens at resets_at (here, now).
                        return self._json(429, {"error": {"code": "rate_limited",
                                                          "message": "too many requests this minute.",
                                                          "resets_at": int(time.time())}})
                    if outer.exhausted:
                        return self._refuse(429, "quota_exhausted", "allowance used", self._quota_headers())
                    self.send_response(200)
                    self.send_header("Content-Type", "text/event-stream")
                    for name, value in self._quota_headers():
                        self.send_header(name, value)
                    self.end_headers()
                    self.wfile.write(sse({"choices": [{"delta": {"content": "FREE_OK"}}]},
                                         {"choices": [{"delta": {}, "finish_reason": "stop"}]},
                                         {"choices": [], "usage": {"prompt_tokens": 12, "completion_tokens": 3}}))
                    return
                self._refuse(404, "bad_request", "no such route")

        self.server = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
        self.thread = threading.Thread(target=self.server.serve_forever, daemon=True)
        self.thread.start()
        self.base = f"http://127.0.0.1:{self.server.server_port}/v1"

    def close(self):
        self.server.shutdown()
        self.server.server_close()
        self.thread.join()


class HostedCase(unittest.TestCase):
    """A fake gateway, a private data directory and a fresh module session per test."""

    @classmethod
    def setUpClass(cls):
        cls.gateway = FakeGateway()

    @classmethod
    def tearDownClass(cls):
        cls.gateway.close()

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.env = mock.patch.dict(os.environ, {"XDG_DATA_HOME": self.temp.name, "RELAY_KEYRING": "off",
                                                hosted.ENV_URL: self.gateway.base})
        self.env.start()
        self.addCleanup(self.env.stop)
        gateway = self.gateway
        with gateway.lock:
            gateway.unauthorized_once = gateway.always_unauthorized = gateway.exhausted = False
            gateway.rate_limited_once = gateway.always_rate_limited = False
            gateway.chat_script.clear()
            gateway.registrations = gateway.chat_calls = 0
            gateway.tokens.clear()
            gateway.requests.clear()
        self.session = hosted.Session()
        hosted.reset(self.session)
        self.addCleanup(hosted.reset, None)

    def config(self):
        return ProviderConfig(self.gateway.base, "relay-main", "", {}, 1024, hosted=True)

    def provider(self):
        return HostedChatProvider(self.config(), session=self.session)

    def complete(self, provider=None):
        events = []
        result = (provider or self.provider()).complete([{"role": "user", "content": "hi"}], [],
                                                        events.append, threading.Event())
        return result, events


# ----- identity -----------------------------------------------------------------------------------
class IdentityTests(HostedCase):
    def test_the_installation_key_lives_in_its_own_file_and_is_made_once(self):
        first = hosted.load_identity()
        path = Path(self.temp.name) / "relay" / "hosted" / "identity.key"
        self.assertTrue(path.is_file())
        self.assertEqual(stat.S_IMODE(path.stat().st_mode), 0o600)
        self.assertEqual(stat.S_IMODE(path.parent.stat().st_mode), 0o700)
        self.assertEqual(hosted.load_identity().public, first.public)
        self.assertEqual(first.installation_id, sha256(first.public).hexdigest()[:32])
        # Nothing of the remote identity is touched: separate key, separate directory.
        self.assertFalse((Path(self.temp.name) / "relay" / "remote").exists())

    def test_the_keyring_entry_is_distinct_from_the_remote_identity(self):
        from remote import identity as remote_identity
        cls = hosted.identity_class()
        self.assertTrue(issubclass(cls, remote_identity.Identity))
        self.assertEqual(cls.attribute, "relay-free-identity")
        self.assertNotEqual(cls.attribute, remote_identity.ATTRIBUTE)
        # The base class's own storage is unchanged by the refactor.
        self.assertEqual((remote_identity.Identity.attribute, remote_identity.Identity.filename),
                         ("remote-identity", "identity.key"))

    def test_the_base_url_is_the_preset_unless_the_environment_says_otherwise(self):
        self.assertEqual(hosted.base_url(), self.gateway.base)
        with mock.patch.dict(os.environ, {hosted.ENV_URL: ""}):
            self.assertEqual(hosted.base_url(), PRESETS["relay-free"].base_url)
        # A config at the preset's address follows the override inside the transport; one that
        # names somewhere else does not.
        canonical = ProviderConfig(PRESETS["relay-free"].base_url, "relay-main", "", {}, 1024, hosted=True)
        self.assertEqual(HostedChatProvider(canonical, session=self.session).config.base_url, self.gateway.base)
        elsewhere = ProviderConfig("http://127.0.0.1:9/v1", "relay-main", "", {}, 1024, hosted=True)
        self.assertEqual(HostedChatProvider(elsewhere, session=self.session).config.base_url, "http://127.0.0.1:9/v1")
        # A loopback http gateway is a valid hosted config, so tests and a local gateway both work.
        self.config().validate()
        with self.assertRaises(ValueError):
            ProviderConfig("https://api.relay-terminal.ai/v1", "relay-main", "", {}, 1024).validate()
        ProviderConfig("https://api.relay-terminal.ai/v1", "relay-main", "", {}, 1024, hosted=True).validate()


# ----- the token cache ------------------------------------------------------------------------
class TokenTests(HostedCase):
    def test_a_token_is_taken_once_and_refreshed_near_expiry_or_on_demand(self):
        now = [1_000_000.0]
        session = hosted.Session(clock=lambda: now[0])
        first = session.token()
        self.assertTrue(first)
        self.assertEqual(self.gateway.registrations, 1)
        self.assertEqual(session.token(), first)               # cached
        self.assertEqual(self.gateway.registrations, 1)
        now[0] += 3600 - hosted.REFRESH_MARGIN - 1
        self.assertEqual(session.token(), first)               # still more than five minutes left
        now[0] += 2
        second = session.token()                               # inside the margin: refreshed
        self.assertNotEqual(second, first)
        self.assertEqual(self.gateway.registrations, 2)
        third = session.token(force=True)
        self.assertNotEqual(third, second)
        self.assertEqual(self.gateway.registrations, 3)
        # The register reply carried the quota and the plan.
        self.assertEqual(session.quota(), self.gateway.quota)
        self.assertEqual(session.plan, "free")
        self.assertEqual(session.installation_id, hosted.load_identity().installation_id)

    def test_the_proof_is_the_hmac_over_the_challenge_keyed_by_the_shared_secret(self):
        self.session.token()
        register = next(r for r in self.gateway.requests if r["path"] == "/v1/register")
        identity = hosted.load_identity()
        self.assertEqual(base64.b64decode(register["body"]["static_pubkey"]), identity.public)
        self.assertEqual(register["body"]["client"], {"version": mock.ANY})
        self.assertRegex(register["body"]["proof"], "^[0-9a-f]{64}$")

    def test_the_live_quota_comes_from_get_quota(self):
        quota = self.session.fetch_quota()
        self.assertEqual(quota, self.gateway.quota)
        self.assertEqual(self.session.quota(), quota)
        self.assertEqual(self.session.plan, "free")

    def test_a_gateway_that_cannot_be_reached_is_hosted_unavailable_not_a_crash(self):
        session = hosted.Session(base="http://127.0.0.1:9/v1")
        with self.assertRaises(hosted.HostedUnavailable) as caught:
            session.token()
        self.assertIn("could not reach", str(caught.exception))
        self.assertEqual(caught.exception.code, "")

    def test_a_refusal_at_registration_carries_the_gateways_code(self):
        with mock.patch.object(hosted.Session, "_post", side_effect=hosted.HostedUnavailable(
                "Relay Free is not available right now.", "free_unavailable")):
            with self.assertRaises(hosted.HostedUnavailable) as caught:
                self.session.token()
        self.assertEqual(caught.exception.code, "free_unavailable")

    def test_two_threads_finding_the_token_stale_register_once(self):
        results = []
        threads = [threading.Thread(target=lambda: results.append(self.session.token())) for _ in range(4)]
        for thread in threads:
            thread.start()
        for thread in threads:
            thread.join()
        self.assertEqual(len(set(results)), 1)
        self.assertEqual(self.gateway.registrations, 1)


# ----- the transport ----------------------------------------------------------------------------
class TransportTests(HostedCase):
    def test_a_call_streams_with_a_bearer_token_and_reports_the_quota(self):
        result, events = self.complete()
        self.assertEqual(result["content"], "FREE_OK")
        chat = next(r for r in self.gateway.requests if r["path"] == "/v1/chat/completions")
        self.assertEqual(chat["authorization"], "Bearer " + self.session.token())
        self.assertEqual(chat["body"]["model"], "relay-main")
        self.assertEqual(chat["body"]["stream_options"], {"include_usage": True})
        self.assertNotIn("reasoning_effort", chat["body"])
        quota = next(e for e in events if e["event"] == "hosted_quota")
        self.assertEqual(quota, {"event": "hosted_quota", **self.gateway.quota})
        self.assertEqual(self.session.quota(), self.gateway.quota)
        # Usage still arrives, so the context tracker is exact rather than estimated.
        self.assertEqual(next(e for e in events if e["event"] == "usage")["usage"]["completion_tokens"], 3)

    def test_a_401_refreshes_the_token_and_retries_exactly_once(self):
        self.session.token()
        self.gateway.unauthorized_once = True
        result, _ = self.complete()
        self.assertEqual(result["content"], "FREE_OK")
        self.assertEqual(self.gateway.chat_calls, 2)
        self.assertEqual(self.gateway.registrations, 2)
        # A gateway that keeps refusing gets its one retry and no more.
        self.gateway.always_unauthorized = True
        self.gateway.chat_calls = 0
        with self.assertRaises(ProviderError) as caught:
            self.complete()
        self.assertEqual(caught.exception.code, "token_expired")
        self.assertEqual(self.gateway.chat_calls, 2)

    def test_a_rate_limited_gateway_is_waited_out_and_asked_again(self):
        self.session.token()
        self.gateway.rate_limited_once = True
        result, events = self.complete()
        self.assertEqual(result["content"], "FREE_OK")
        self.assertEqual(self.gateway.chat_calls, 2)
        self.assertTrue(any(e["event"] == "provider_retry" and e.get("reason") == "http"
                            for e in events))

    def test_a_token_refresh_does_not_hand_the_gateway_a_fresh_set_of_retries(self):
        """The 401 retry makes a second HTTP call for the same request; it continues the first
        call's retry count and clock rather than starting six more (review of #VMZP: a gateway
        answering 401 and then 429 was worth 14 requests)."""
        self.session.token()
        self.gateway.chat_script = ["rate_limited", "rate_limited", "unauthorized"]
        self.gateway.always_rate_limited = True
        with self.assertRaises(ProviderError) as caught:
            self.complete()
        self.assertEqual(caught.exception.code, "rate_limited")
        # One first try and six retries in all, wherever the refresh fell.
        self.assertEqual(self.gateway.chat_calls, 1 + HostedChatProvider.HTTP_RETRY_ATTEMPTS + 1)
        self.assertEqual(self.gateway.registrations, 2)

    def test_the_retry_budget_is_shared_across_the_refresh_too(self):
        self.session.token()
        self.gateway.chat_script = ["unauthorized"]
        self.gateway.always_rate_limited = True
        provider = self.provider()
        started = time.monotonic()
        with provider.limit_retry_budget(0.0):     # nothing at all may be waited out
            with self.assertRaises(ProviderError) as caught:
                self.complete(provider)
        self.assertEqual(caught.exception.code, "rate_limited")
        self.assertEqual(self.gateway.chat_calls, 2)      # the 401, the refresh, then no retries
        self.assertLess(time.monotonic() - started, 5.0)

    def test_an_exhausted_allowance_is_a_provider_error_with_its_code_and_reset_time(self):
        self.gateway.exhausted = True
        with self.assertRaises(ProviderError) as caught:
            _, events = self.complete()
        exc = caught.exception
        self.assertEqual(exc.code, "quota_exhausted")
        self.assertEqual(exc.resets_at, self.gateway.quota["resets_at"])
        self.assertIn("allowance is used up", str(exc))
        self.assertIn("API keys", str(exc))
        self.assertNotIn("allowance used", str(exc))     # the gateway's own message is not echoed
        # The refusal's headers still updated the last known quota.
        self.assertEqual(self.session.quota(), self.gateway.quota)
        # A spent allowance lifts at midnight, not in seconds: it is not waited out.
        self.assertEqual(self.gateway.chat_calls, 1)

    def test_error_wording_per_code_and_a_foreign_body_is_dropped(self):
        text, code, resets_at = hosted.describe_error(429, json.dumps(
            {"error": {"code": "rate_limited", "message": "slow down", "resets_at": None}}).encode())
        self.assertEqual(code, "rate_limited")
        self.assertIsNone(resets_at)
        self.assertIn("too many requests", text)
        text, code, _ = hosted.describe_error(503, json.dumps({"error": {"code": "free_unavailable", "message": "x"}}).encode())
        self.assertEqual(code, "free_unavailable")
        self.assertIn("not available right now", text)
        text, code, _ = hosted.describe_error(400, json.dumps({"error": {"code": "bad_request", "message": "m" * 500}}).encode())
        self.assertEqual(code, "bad_request")
        self.assertLessEqual(len(text), hosted.MAX_MESSAGE + 60)       # the message is truncated
        text, code, _ = hosted.describe_error(502, b"<html>SECRET_PROXY_PAGE</html>")
        self.assertEqual(code, "")
        self.assertNotIn("SECRET", text)
        self.assertIn("502", text)
        text, code, _ = hosted.describe_error(401, b"")
        self.assertEqual(code, "token_expired")

    def test_make_provider_dispatches_on_the_hosted_flag(self):
        self.assertIsInstance(make_provider(self.config()), HostedChatProvider)
        plain = make_provider(ProviderConfig("https://api.moonshot.ai/v1", "kimi-k3", "k", {}, 1024))
        self.assertIs(type(plain), ChatProvider)
        # A hosted provider takes its session from the module, so every pane shares one token.
        self.assertIs(make_provider(self.config()).session, self.session)

    def test_without_cryptography_relay_free_is_unavailable_and_says_what_to_install(self):
        unavailable = hosted.HostedUnavailable("Relay Free needs python3-cryptography (the cryptography module).")
        with mock.patch.object(hosted, "_import_remote", side_effect=unavailable):
            self.assertFalse(hosted.available())
            self.assertEqual(hosted.status(), {"available": False, "quota": None})
            with self.assertRaises(ProviderError) as caught:
                self.complete(HostedChatProvider(self.config(), session=hosted.Session()))
        self.assertIn("python3-cryptography", str(caught.exception))
        self.assertEqual(caught.exception.code, "")
        self.assertEqual(self.gateway.chat_calls, 0)

    def test_a_missing_cryptography_module_is_what_makes_it_unavailable(self):
        # The real import path: with `cryptography` gone, `remote.noise` cannot import.
        with mock.patch.dict(sys.modules):
            for name in [n for n in sys.modules if n == "remote" or n.startswith("remote.")
                         or n == "cryptography" or n.startswith("cryptography.")]:
                del sys.modules[name]
            sys.modules["cryptography"] = None
            with mock.patch.object(hosted, "_identity_class", None):
                self.assertFalse(hosted.available())
                with self.assertRaises(hosted.HostedUnavailable) as caught:
                    hosted.identity_class()
        self.assertIn("python3-cryptography", str(caught.exception))
        self.assertTrue(hosted.available())


# ----- the worker -----------------------------------------------------------------------------
class WorkerTests(HostedCase):
    """configure {preset: relay-free} + ask streams through the fake gateway; presets and hosted_quota."""

    def worker(self, messages, wait_for, timeout=20):
        env = {**os.environ, "PYTHONPATH": str(ROOT / "backend"), "HOME": self.temp.name}
        proc = subprocess.Popen([sys.executable, "-S", str(ROOT / "backend/worker.py")],
                                stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                text=True, cwd=ROOT, env=env)
        events = []
        try:
            for message in messages:
                proc.stdin.write(json.dumps(message) + "\n")
            proc.stdin.flush()
            deadline = time.monotonic() + timeout
            while time.monotonic() < deadline:
                line = proc.stdout.readline()
                if not line:
                    break
                event = json.loads(line)
                events.append(event)
                if event.get("event") in wait_for:
                    break
            proc.stdin.write(json.dumps({"type": "shutdown"}) + "\n")
            proc.stdin.flush()
            out, err = proc.communicate(timeout=timeout)
        finally:
            if proc.poll() is None:
                proc.kill()
        events += [json.loads(line) for line in out.splitlines() if line.strip()]
        return events, err

    def test_the_presets_row_and_the_hosted_quota_request(self):
        events, err = self.worker([{"type": "presets", "id": "p1"}, {"type": "hosted_quota", "id": "q1"}],
                                  wait_for={"hosted_quota"})
        presets = next(e for e in events if e["event"] == "presets")
        row = next(p for p in presets["presets"] if p["id"] == "relay-free")
        self.assertEqual({k: row[k] for k in ("has_stored_key", "key_source", "hosted", "local", "available", "quota")},
                         {"has_stored_key": False, "key_source": "included", "hosted": True, "local": False,
                          "available": True, "quota": None})
        self.assertEqual(row["group"], "included")
        # Every other row is as before: no `available`, no `quota`, and never `hosted`.
        kimi = next(p for p in presets["presets"] if p["id"] == "kimi")
        self.assertFalse(kimi["hosted"])
        self.assertNotIn("available", kimi)
        quota = next(e for e in events if e["event"] == "hosted_quota")
        self.assertEqual(quota, {"event": "hosted_quota", "id": "q1", **self.gateway.quota})
        self.assertNotIn("error", [e["event"] for e in events], events)
        self.assertNotIn(self.session.token() if self.session.quota() else "NEVER", err)

    def test_configure_relay_free_and_ask_streams_a_reply_and_the_quota(self):
        events, err = self.worker([{"type": "configure", "preset": "relay-free", "use_stored_key": True,
                                    "workspace": self.temp.name, "max_tokens": 1024,
                                    "route_assist": False, "suggestions": False},
                                   {"type": "ask", "text": "hello", "id": "a1"}],
                                  wait_for={"done", "error"})
        configured = next(e for e in events if e["event"] == "configured")
        self.assertEqual(configured["model"], "relay-main")
        self.assertEqual(configured["roles"]["chores"]["model"], "relay-lite")
        self.assertEqual(configured["roles"]["route_assist"]["model"], "relay-lite")
        self.assertEqual(configured["tiers"]["flash"]["model"], "relay-flash")
        kinds = [e["event"] for e in events]
        self.assertIn("done", kinds, (kinds, err[-2000:]))
        self.assertEqual("".join(e["text"] for e in events if e["event"] == "delta"), "FREE_OK")
        quota = next(e for e in events if e["event"] == "hosted_quota")
        self.assertEqual((quota["limit"], quota["used"]), (self.gateway.quota["limit"], self.gateway.quota["used"]))
        # The token never appears in the log or on the pipe.
        token = next(iter(self.gateway.tokens))
        self.assertNotIn(token, err)
        self.assertNotIn(token, json.dumps(events))

    def test_an_exhausted_allowance_ends_the_turn_with_its_code(self):
        self.gateway.exhausted = True
        events, _ = self.worker([{"type": "configure", "preset": "relay-free", "use_stored_key": True,
                                  "workspace": self.temp.name, "max_tokens": 1024,
                                  "route_assist": False, "suggestions": False},
                                 {"type": "ask", "text": "hello", "id": "a1"}],
                                wait_for={"done", "error"})
        error = next(e for e in events if e["event"] == "error")
        self.assertEqual(error["code"], "quota_exhausted")
        self.assertEqual(error["resets_at"], self.gateway.quota["resets_at"])
        self.assertIn("allowance is used up", error["text"])


if __name__ == "__main__":
    unittest.main()
