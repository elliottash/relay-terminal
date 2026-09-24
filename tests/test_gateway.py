# SPDX-License-Identifier: AGPL-3.0-or-later
"""The Relay Free gateway, end to end in one process: a fake model provider on loopback, the
real gateway on a real port, and a client that registers, streams and gets refused the way the
desktop will.

Fast on purpose: no sleeps beyond a poll, every limit set tiny through the config, one shared
fake upstream and a fresh gateway per test.
"""
import asyncio
import base64
import hmac
import http.client
import json
import os
import threading
import time
import unittest
from hashlib import sha256
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

from gateway import config as config_mod
from gateway import proxy
from gateway import server as server_mod
from gateway import validate as validate_mod
from gateway.store import Store
from remote import noise

KEY_ENV = "GATEWAY_TEST_KEY"
PROMPT = "SECRET_PROMPT_TEXT_the_quick_brown_fox"


def sse(payload: dict) -> bytes:
    return b"data: " + json.dumps(payload).encode() + b"\n\n"


class FakeUpstream:
    """An OpenAI-shaped provider whose behaviour is chosen by the path prefix."""

    def __init__(self):
        self.requests = []
        self.slow_gate = threading.Event()
        outer = self

        class Handler(BaseHTTPRequestHandler):
            protocol_version = "HTTP/1.1"

            def log_message(self, *args):
                pass

            def do_POST(self):
                body = self.rfile.read(int(self.headers["Content-Length"]))
                record = {"path": self.path, "body": json.loads(body),
                          "authorization": self.headers.get("Authorization"),
                          "headers": dict(self.headers)}
                outer.requests.append(record)
                kind = self.path.split("/")[1]
                if kind == "fail503":
                    self.send_response(503)
                    self.send_header("Content-Length", "5")
                    self.end_headers()
                    self.wfile.write(b"ECHO!")
                    return
                if kind == "fail400":
                    self.send_response(400)
                    self.send_header("Content-Length", "0")
                    self.end_headers()
                    return
                if kind == "redirect":
                    self.send_response(307)
                    self.send_header("Location", "http://127.0.0.1:9/steal")
                    self.send_header("Content-Length", "0")
                    self.end_headers()
                    return
                if kind == "json":
                    reply = json.dumps({"choices": [{"finish_reason": "stop", "message": {
                        "role": "assistant", "content": "JSON_REPLY"}}],
                        "usage": {"prompt_tokens": 7, "completion_tokens": 3}}).encode()
                    self.send_response(200)
                    self.send_header("Content-Type", "application/json")
                    self.send_header("Content-Length", str(len(reply)))
                    self.end_headers()
                    self.wfile.write(reply)
                    return
                if kind == "slow":
                    outer.slow_gate.wait(5)
                if self.path.endswith("/images"):
                    # An OpenRouter-images reply: the picture as base64, nothing else.
                    reply = json.dumps({"created": 1, "data": [
                        {"b64_json": base64.b64encode(b"FAKE_IMAGE_BYTES").decode()}]}).encode()
                    self.send_response(200)
                    self.send_header("Content-Type", "application/json")
                    self.send_header("Content-Length", str(len(reply)))
                    self.end_headers()
                    self.wfile.write(reply)
                    return
                self.send_response(200)
                self.send_header("Content-Type", "text/event-stream")
                self.send_header("Connection", "close")
                self.end_headers()
                self.wfile.write(sse({"choices": [{"delta": {"content": "Hello"}, "index": 0}]}))
                self.wfile.flush()
                if kind == "mid":
                    # Bytes have flowed; now the provider dies. No usage, no [DONE].
                    self.connection.close()
                    return
                self.wfile.write(sse({"choices": [{"delta": {"content": " world"}, "index": 0}]}))
                self.wfile.write(sse({"choices": [{"delta": {}, "finish_reason": "stop", "index": 0}]}))
                self.wfile.write(sse({"choices": [], "usage": {"prompt_tokens": 60,
                                                                "completion_tokens": 50}}))
                self.wfile.write(b"data: [DONE]\n\n")
                self.wfile.flush()

        self.server = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
        self.thread = threading.Thread(target=self.server.serve_forever, daemon=True)
        self.thread.start()
        self.base = f"http://127.0.0.1:{self.server.server_port}"

    def stop(self):
        self.server.shutdown()
        self.server.server_close()
        self.thread.join()


def config_for(upstream_base: str, *, main=("ok",), prices=(0.5, 1.5), tokens_per_day=100000,
               requests_per_minute=100, concurrency_per_install=2, spend_per_day=20.0,
               registrations_per_ip=10, extra_role=None, connect_timeout=2.0,
               effort_style="reasoning", images_per_day=0, image_price=0.01,
               image_upstream=("ok",)) -> dict:
    """A config whose providers are the fake upstream's behaviours, one provider per path."""
    kinds = ("ok", "fail503", "fail400", "redirect", "json", "slow", "mid")
    providers = {kind: {"base_url": f"{upstream_base}/{kind}", "key_env": KEY_ENV,
                        "effort_style": effort_style,
                        "price_per_mtok": {"fake-model": list(prices)},
                        "price_per_image": {"fake-image-model": image_price}}
                 for kind in kinds}
    roles = {
        "relay-main": {"upstreams": [{"provider": kind, "model": "fake-model"} for kind in main],
                       "effort": "low", "max_effort": "medium",
                       "max_output_tokens": 300, "max_input_chars": 4000},
        "relay-lite": {"upstreams": [{"provider": "ok", "model": "fake-model",
                                      "extra": {"temperature": 0}}],
                       "effort": "minimal", "max_effort": "medium",
                       "max_output_tokens": 50, "max_input_chars": 2000},
    }
    if images_per_day:
        roles["relay-image"] = {
            "kind": "images",
            "upstreams": [{"provider": kind, "model": "fake-image-model"}
                          for kind in image_upstream],
            "resolutions": ["64x64", "128x64"],
            "aspect_ratios": ["1:1", "2:1"],
            "max_input_chars": 4000}
    if extra_role:
        roles.update(extra_role)
    return {"roles": roles, "providers": providers,
            "quota": {"tokens_per_day": tokens_per_day, "requests_per_minute": requests_per_minute,
                      "concurrency_per_install": concurrency_per_install,
                      "images_per_day": images_per_day},
            "limits": {"global_concurrency": 8, "spend_per_day_usd": spend_per_day,
                       "spend_per_month_usd": 1000, "per_provider_per_day_usd": {},
                       "registrations_per_ip_per_hour": registrations_per_ip,
                       "challenges_per_ip_per_hour": 100},
            "token_ttl_seconds": 3600, "allow_insecure_loopback": True,
            "upstream_connect_timeout": connect_timeout, "upstream_stall_timeout": 5}


class Gateway:
    """A gateway on its own event loop in a background thread, so tests are plain functions."""

    def __init__(self, config_dict: dict):
        self.config = config_mod.parse(config_dict, {KEY_ENV: "upstream-secret"})
        self.store = Store(":memory:")
        self.server = server_mod.build(self.store, self.config)
        self.loop = asyncio.new_event_loop()
        self.thread = threading.Thread(target=self.loop.run_forever, daemon=True)
        self.thread.start()
        asyncio.run_coroutine_threadsafe(self.server.start("127.0.0.1", 0), self.loop).result(5)
        self.host, self.port = "127.0.0.1", self.server.port

    def close(self):
        asyncio.run_coroutine_threadsafe(self.server.close(), self.loop).result(5)
        self.loop.call_soon_threadsafe(self.loop.stop)
        self.thread.join(5)
        self.loop.close()
        try:
            self.store.close()
        except Exception:
            pass

    # ---- a client --------------------------------------------------------------------------------

    def call(self, method: str, path: str, body=None, token: str | None = None,
             headers: dict | None = None):
        connection = http.client.HTTPConnection(self.host, self.port, timeout=10)
        sent = {"Content-Type": "application/json", **(headers or {})}
        if token:
            sent["Authorization"] = "Bearer " + token
        data = None if body is None else (body if isinstance(body, bytes) else json.dumps(body).encode())
        connection.request(method, path, body=data, headers=sent)
        response = connection.getresponse()
        raw = response.read()
        connection.close()
        return response.status, dict(response.getheaders()), raw

    def call_json(self, method, path, body=None, token=None, headers=None):
        status, response_headers, raw = self.call(method, path, body, token, headers)
        return status, response_headers, json.loads(raw)

    def register(self, keypair=None, version="0.1-test"):
        private, public = keypair or noise.generate_keypair()
        status, _, challenge = self.call_json("POST", "/v1/challenge", {})
        assert status == 200, challenge
        shared = noise.dh(private, base64.b64decode(challenge["ephemeral_public"]))
        proof = hmac.new(shared, challenge["challenge"].encode(), sha256).hexdigest()
        return self.call_json("POST", "/v1/register", {
            "static_pubkey": base64.b64encode(public).decode(),
            "challenge": challenge["challenge"], "proof": proof, "client": {"version": version}})

    def token(self):
        status, _, reply = self.register()
        assert status == 200, reply
        return reply["token"], reply

    def chat(self, token, model="relay-main", **fields):
        body = {"model": model, "messages": [{"role": "user", "content": PROMPT}], **fields}
        return self.call("POST", "/v1/chat/completions", body, token=token)


class GatewayTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.upstream = FakeUpstream()
        os.environ[KEY_ENV] = "upstream-secret"

    @classmethod
    def tearDownClass(cls):
        cls.upstream.stop()
        os.environ.pop(KEY_ENV, None)

    def setUp(self):
        self.upstream.requests.clear()
        self.upstream.slow_gate.clear()
        self.gateways = []

    def tearDown(self):
        for gateway in self.gateways:
            gateway.close()

    def gateway(self, **overrides) -> Gateway:
        gateway = Gateway(config_for(self.upstream.base, **overrides))
        self.gateways.append(gateway)
        return gateway

    # ---- registration ------------------------------------------------------------------------

    def test_register_then_stream_with_quota_headers(self):
        gateway = self.gateway()
        status, _, reply = gateway.register()
        self.assertEqual(status, 200)
        self.assertEqual(reply["plan"], "free")
        self.assertEqual(len(reply["installation_id"]), 32)
        self.assertEqual(reply["quota"], {"limit": 100000, "used": 0, "resets_at": reply["quota"]["resets_at"]})
        self.assertGreater(reply["expires_in"], 3000)

        status, headers, raw = gateway.chat(reply["token"])
        self.assertEqual(status, 200, raw)
        self.assertEqual(headers["Content-Type"], "text/event-stream")
        self.assertEqual(headers["Transfer-Encoding"], "chunked")
        self.assertEqual(headers["X-Relay-Quota-Limit"], "100000")
        self.assertIn("X-Relay-Quota-Resets-At", headers)
        self.assertIn(b'"content": "Hello"', raw)
        self.assertTrue(raw.endswith(b"data: [DONE]\n\n"))

        # The usage chunk settled the quota: 60 in + 50 out, and the reservation is gone.
        status, headers, quota = gateway.call_json("GET", "/v1/quota", token=reply["token"])
        self.assertEqual(status, 200)
        self.assertEqual(quota["used"], 110)
        self.assertEqual(quota["plan"], "free")
        self.assertEqual(headers["X-Relay-Quota-Used"], "110")

        # What reached upstream: the operator's key, the role's model and extra, no client secret.
        sent = self.upstream.requests[-1]
        self.assertEqual(sent["authorization"], "Bearer upstream-secret")
        self.assertEqual(sent["path"], "/ok/chat/completions")
        self.assertEqual(sent["body"]["model"], "fake-model")
        self.assertEqual(sent["body"]["reasoning"], {"effort": "low"})
        self.assertTrue(sent["body"]["stream"])
        self.assertEqual(sent["body"]["stream_options"], {"include_usage": True})
        self.assertEqual(sent["body"]["max_tokens"], 300)

    def test_re_registering_rotates_the_token(self):
        gateway = self.gateway()
        keypair = noise.generate_keypair()
        _, _, first = gateway.register(keypair)
        _, _, second = gateway.register(keypair)
        self.assertEqual(first["installation_id"], second["installation_id"])
        self.assertNotEqual(first["token"], second["token"])
        status, _, reply = gateway.call_json("GET", "/v1/quota", token=first["token"])
        self.assertEqual(status, 401)
        self.assertEqual(reply["error"]["code"], "token_expired")
        self.assertEqual(gateway.call_json("GET", "/v1/quota", token=second["token"])[0], 200)

    def test_a_bad_proof_is_refused(self):
        gateway = self.gateway()
        _, public = noise.generate_keypair()
        status, _, challenge = gateway.call_json("POST", "/v1/challenge", {})
        status, _, reply = gateway.call_json("POST", "/v1/register", {
            "static_pubkey": base64.b64encode(public).decode(),
            "challenge": challenge["challenge"], "proof": "00" * 32})
        self.assertEqual(status, 401)
        status, _, reply = gateway.call_json("POST", "/v1/register", {"static_pubkey": "nope"})
        self.assertEqual(status, 400)
        self.assertEqual(reply["error"]["code"], "bad_request")

    def test_per_ip_registration_limit(self):
        gateway = self.gateway(registrations_per_ip=2)
        self.assertEqual(gateway.register()[0], 200)
        self.assertEqual(gateway.register()[0], 200)
        status, _, reply = gateway.register()
        self.assertEqual(status, 429)
        self.assertEqual(reply["error"]["code"], "rate_limited")
        self.assertIn("resets_at", reply["error"])

    def test_expired_token(self):
        gateway = self.gateway()
        token, reply = gateway.token()
        gateway.store.db.execute("UPDATE installations SET token_expires = ?", (time.time() - 1,))
        gateway.store.db.commit()
        status, _, body = gateway.chat(token)
        self.assertEqual(status, 401)
        self.assertEqual(json.loads(body)["error"]["code"], "token_expired")
        status, _, body = gateway.chat("not-a-token")
        self.assertEqual(status, 401)
        status, _, body = gateway.call("POST", "/v1/chat/completions", {"model": "relay-main"})
        self.assertEqual(status, 401)

    # ---- validation --------------------------------------------------------------------------

    def test_unknown_model_and_disallowed_field(self):
        gateway = self.gateway()
        token, _ = gateway.token()
        status, _, body = gateway.chat(token, model="gpt-4o")
        self.assertEqual(status, 400)
        self.assertEqual(json.loads(body)["error"]["code"], "bad_request")
        status, _, body = gateway.chat(token, model="fake-model")     # an upstream name is not a role
        self.assertEqual(status, 400)
        status, _, body = gateway.chat(token, n=3)
        self.assertEqual(status, 400)
        self.assertIn("n", json.loads(body)["error"]["message"])
        status, _, body = gateway.call("POST", "/v1/chat/completions", b"not json", token=token)
        self.assertEqual(status, 400)
        status, _, body = gateway.call("POST", "/v1/chat/completions",
                                       {"model": "relay-main", "messages": [{"role": "user",
                                                                             "content": "x",
                                                                             "hidden": 1}]},
                                       token=token)
        self.assertEqual(status, 400)
        self.assertEqual(self.upstream.requests, [])

    def test_max_tokens_clamped_and_the_roles_extra_wins(self):
        gateway = self.gateway()
        token, _ = gateway.token()
        status, _, _ = gateway.chat(token, model="relay-lite", max_tokens=5000,
                                    temperature=1.5, stream=False)
        self.assertEqual(status, 200)
        sent = self.upstream.requests[-1]["body"]
        self.assertEqual(sent["max_tokens"], 50)
        self.assertEqual(sent["temperature"], 0)               # the role's extra wins
        self.assertTrue(sent["stream"])
        self.assertEqual(set(sent), {"model", "messages", "stream", "stream_options",
                                     "max_tokens", "temperature", "reasoning"})

    def test_effort_is_clamped_to_the_roles_ceiling(self):
        gateway = self.gateway()                                # main: default low, cap medium
        token, _ = gateway.token()

        def sent_with(**fields):
            status, _, body = gateway.chat(token, **fields)
            self.assertEqual(status, 200, body)
            return self.upstream.requests[-1]["body"]

        # Relay's own transport asks with reasoning_effort; "max" comes out as the cap, silently.
        self.assertEqual(sent_with(reasoning_effort="max")["reasoning"], {"effort": "medium"})
        self.assertEqual(sent_with(reasoning_effort="high")["reasoning"], {"effort": "medium"})
        self.assertEqual(sent_with(reasoning_effort="low")["reasoning"], {"effort": "low"})
        self.assertEqual(sent_with(reasoning_effort="none")["reasoning"], {"effort": "none"})
        # The OpenRouter dialect is accepted on the way in too.
        self.assertEqual(sent_with(reasoning={"effort": "xhigh"})["reasoning"], {"effort": "medium"})
        # No ask: the role's default, per role.
        self.assertEqual(sent_with()["reasoning"], {"effort": "low"})
        self.assertEqual(sent_with(model="relay-lite")["reasoning"], {"effort": "minimal"})
        # An unknown value, a non-effort reasoning object and the thinking switch are refused.
        for bad in (dict(reasoning_effort="turbo"), dict(reasoning_effort=3),
                    dict(reasoning={"enabled": True}), dict(thinking={"type": "enabled"})):
            status, _, body = gateway.chat(token, **bad)
            self.assertEqual(status, 400, bad)
            self.assertEqual(json.loads(body)["error"]["code"], "bad_request")

    def test_effort_style_is_the_providers_dialect(self):
        gateway = self.gateway(effort_style="reasoning_effort")
        token, _ = gateway.token()
        self.assertEqual(gateway.chat(token, reasoning_effort="max")[0], 200)
        sent = self.upstream.requests[-1]["body"]
        self.assertEqual(sent["reasoning_effort"], "medium")
        self.assertNotIn("reasoning", sent)

        gateway = self.gateway(effort_style="none")
        token, _ = gateway.token()
        self.assertEqual(gateway.chat(token, reasoning_effort="high")[0], 200)
        sent = self.upstream.requests[-1]["body"]
        self.assertNotIn("reasoning", sent)
        self.assertNotIn("reasoning_effort", sent)

    def test_oversized_request_is_refused_per_role(self):
        gateway = self.gateway()
        token, _ = gateway.token()
        status, _, body = gateway.call("POST", "/v1/chat/completions",
                                       {"model": "relay-lite", "messages": [
                                           {"role": "user", "content": "x" * 3000}]}, token=token)
        self.assertEqual(status, 400)
        self.assertEqual(json.loads(body)["error"]["code"], "bad_request")

    # ---- quotas and ceilings -----------------------------------------------------------------

    def test_daily_quota_exhausted(self):
        gateway = self.gateway(tokens_per_day=100)
        token, _ = gateway.token()
        self.assertEqual(gateway.chat(token)[0], 200)          # settles at 110 > 100
        status, headers, body = gateway.chat(token)
        self.assertEqual(status, 429)
        reply = json.loads(body)
        self.assertEqual(reply["error"]["code"], "quota_exhausted")
        self.assertGreater(reply["error"]["resets_at"], time.time())
        self.assertEqual(headers["X-Relay-Quota-Used"], "100")   # capped at the limit
        self.assertEqual(len(self.upstream.requests), 1)
        # Another installation is unaffected.
        other, _ = gateway.token()
        self.assertEqual(gateway.chat(other)[0], 200)

    def test_requests_per_minute(self):
        gateway = self.gateway(requests_per_minute=2)
        token, _ = gateway.token()
        self.assertEqual(gateway.chat(token)[0], 200)
        self.assertEqual(gateway.chat(token)[0], 200)
        status, _, body = gateway.chat(token)
        self.assertEqual(status, 429)
        self.assertEqual(json.loads(body)["error"]["code"], "rate_limited")

    def test_global_spend_ceiling_closes_the_gateway(self):
        # A dollar a token: the first call costs $110, past the $20 day ceiling.
        gateway = self.gateway(prices=(1_000_000, 1_000_000))
        self.assertEqual(gateway.call_json("GET", "/v1/health")[2],
                         {"ok": True, "roles": ["relay-lite", "relay-main"], "open": True,
                          "images": []})
        token, _ = gateway.token()
        self.assertEqual(gateway.chat(token)[0], 200)
        self.assertAlmostEqual(gateway.store.spend_today(), 110.0)
        status, _, body = gateway.chat(token)
        self.assertEqual(status, 503)
        self.assertEqual(json.loads(body)["error"]["code"], "free_unavailable")
        self.assertEqual(gateway.call_json("GET", "/v1/health")[2]["open"], False)
        self.assertEqual(len(self.upstream.requests), 1)

    def test_per_install_concurrency(self):
        gateway = self.gateway(main=("slow",), concurrency_per_install=2)
        token, _ = gateway.token()
        results = []
        workers = [threading.Thread(target=lambda: results.append(gateway.chat(token)[0]))
                   for _ in range(2)]
        for worker in workers:
            worker.start()
        for _ in range(100):                                   # both are now waiting upstream
            if len(self.upstream.requests) == 2:
                break
            time.sleep(0.01)
        status, _, body = gateway.chat(token)
        self.assertEqual(status, 429)
        self.assertEqual(json.loads(body)["error"]["code"], "rate_limited")
        self.upstream.slow_gate.set()
        for worker in workers:
            worker.join(5)
        self.assertEqual(results, [200, 200])
        self.assertEqual(gateway.chat(token)[0], 200)          # slots were given back

    # ---- upstream behaviour ------------------------------------------------------------------

    def test_failover_before_the_first_byte_only(self):
        gateway = self.gateway(main=("fail503", "ok"),
                               extra_role={"relay-mid": {"upstreams": [
                                   {"provider": "mid", "model": "fake-model"},
                                   {"provider": "ok", "model": "fake-model"}],
                                   "max_output_tokens": 100, "max_input_chars": 4000}})
        token, _ = gateway.token()
        with self.assertLogs("relay.gateway", level="INFO") as logs:
            status, _, raw = gateway.chat(token)
        self.assertEqual(status, 200)
        self.assertTrue(raw.endswith(b"data: [DONE]\n\n"))
        self.assertEqual([r["path"] for r in self.upstream.requests],
                         ["/fail503/chat/completions", "/ok/chat/completions"])
        line = [record for record in logs.output if "chat install=" in record][-1]
        self.assertIn("fallback=1", line)
        self.assertIn("provider=ok", line)

        self.upstream.requests.clear()
        status, _, raw = gateway.chat(token, model="relay-mid")
        self.assertEqual(status, 200)
        self.assertIn(b"Hello", raw)
        self.assertNotIn(b"[DONE]", raw)                      # truncated, and not retried
        self.assertEqual([r["path"] for r in self.upstream.requests], ["/mid/chat/completions"])
        # No usage chunk arrived, so the quota was charged from the estimate, never left at zero.
        status, _, quota = gateway.call_json("GET", "/v1/quota", token=token)
        self.assertGreater(quota["used"], 110)

    def test_all_upstreams_down_is_free_unavailable(self):
        gateway = self.gateway(main=("fail503",))
        token, _ = gateway.token()
        status, _, body = gateway.chat(token)
        self.assertEqual(status, 503)
        self.assertEqual(json.loads(body)["error"]["code"], "free_unavailable")
        self.assertNotIn("ECHO", body.decode())                # the upstream body is not relayed
        # The reservation was released: the allowance is untouched.
        self.assertEqual(gateway.call_json("GET", "/v1/quota", token=token)[2]["used"], 0)
        gateway = self.gateway(main=("fail400",))
        token, _ = gateway.token()
        status, _, body = gateway.chat(token)
        self.assertEqual(status, 502)
        self.assertEqual(json.loads(body)["error"]["code"], "free_unavailable")

    def test_a_refusal_says_how_many_upstreams_it_already_tried(self):
        # Owner, 2026-09-19: the gateway owns the upstream retries, and says so, so the desktop
        # transport does not run its six over the same chain (protocol 15.2).
        gateway = self.gateway(main=("fail503", "fail400"))
        token, _ = gateway.token()
        status, _, body = gateway.chat(token)
        self.assertEqual(status, 502)
        error = json.loads(body)["error"]
        self.assertEqual(error["code"], "free_unavailable")
        self.assertEqual(error["retried"], 1)                 # two upstreams asked, one a retry
        self.assertEqual([r["path"] for r in self.upstream.requests],
                         ["/fail503/chat/completions", "/fail400/chat/completions"])
        # One upstream is no retry at all, so the refusal carries no mark and the client may ask
        # again exactly as it always did.
        gateway = self.gateway(main=("fail503",))
        token, _ = gateway.token()
        status, _, body = gateway.chat(token)
        self.assertEqual(status, 503)
        self.assertNotIn("retried", json.loads(body)["error"])
        # Nor do the gateway's own refusals, whose window the client still waits out.
        gateway = self.gateway(main=("ok",), requests_per_minute=1)
        token, _ = gateway.token()
        self.assertEqual(gateway.chat(token)[0], 200)
        status, _, body = gateway.chat(token)
        self.assertEqual((status, json.loads(body)["error"]["code"]), (429, "rate_limited"))
        self.assertNotIn("retried", json.loads(body)["error"])

    def test_the_retryable_statuses_are_the_ones_the_client_retries(self):
        """One set, two layers. A status the gateway calls transient and the client calls final
        would be retried twice over or nowhere at all; they cannot share a module, because the box
        runs `gateway/` and `remote/` only (gateway/README.md), so this is the seam."""
        from relay_core.provider import ChatProvider
        self.assertEqual(set(proxy.RETRYABLE_STATUSES), set(ChatProvider.HTTP_RETRY_STATUSES))
        self.assertNotIn(501, proxy.RETRYABLE_STATUSES)       # as final as a 404
        self.assertNotIn(505, proxy.RETRYABLE_STATUSES)
        self.assertIn(529, proxy.RETRYABLE_STATUSES)          # Anthropic's "overloaded"

    def test_redirects_are_not_followed(self):
        gateway = self.gateway(main=("redirect", "ok"))
        token, _ = gateway.token()
        status, _, raw = gateway.chat(token)
        self.assertEqual(status, 502)
        self.assertEqual([r["path"] for r in self.upstream.requests], ["/redirect/chat/completions"])

    def test_json_reply_is_passed_through(self):
        gateway = self.gateway(main=("json",))
        token, _ = gateway.token()
        status, headers, raw = gateway.chat(token)
        self.assertEqual(status, 200)
        self.assertEqual(headers["Content-Type"], "application/json")
        self.assertEqual(json.loads(raw)["choices"][0]["message"]["content"], "JSON_REPLY")
        self.assertEqual(gateway.call_json("GET", "/v1/quota", token=token)[2]["used"], 10)

    # ---- operational -------------------------------------------------------------------------

    def test_log_line_has_no_prompt_and_no_key(self):
        gateway = self.gateway()
        token, reply = gateway.token()
        with self.assertLogs("relay.gateway", level="INFO") as logs:
            self.assertEqual(gateway.chat(token)[0], 200)
        joined = "\n".join(logs.output)
        self.assertNotIn(PROMPT, joined)
        self.assertNotIn("upstream-secret", joined)
        self.assertNotIn(token, joined)
        self.assertNotIn(reply["installation_id"], joined)
        line = [record for record in logs.output if "chat install=" in record][-1]
        for field in ("role=relay-main", "provider=ok", "status=200", "ttft_ms=", "total_ms=",
                      "in=60", "out=50", "usage=reported", "error=-", "fallback=0"):
            self.assertIn(field, line)

    def test_database_failure_fails_closed(self):
        gateway = self.gateway()
        token, _ = gateway.token()
        gateway.store.db.close()
        with self.assertLogs("relay.gateway", level="ERROR") as logs:
            status, _, body = gateway.chat(token)
            self.assertEqual(status, 503)
            self.assertEqual(json.loads(body)["error"]["code"], "free_unavailable")
            status, _, body = gateway.call("POST", "/v1/challenge", {})
            self.assertEqual(status, 503)
        self.assertEqual(len(logs.output), 2)
        self.assertEqual(gateway.call("GET", "/v1/health")[0], 503)
        self.assertEqual(self.upstream.requests, [])

    # ---- images ---------------------------------------------------------------------------------

    def image(self, gateway, token, prompt=PROMPT, model="relay-image", **fields):
        return gateway.call("POST", "/v1/images",
                            {"model": model, "prompt": prompt, **fields}, token=token)

    def test_image_serves_counts_and_quotes(self):
        gateway = self.gateway(images_per_day=2, image_price=0.01)
        token, _ = gateway.token()
        status, headers, raw = self.image(gateway, token)
        self.assertEqual(status, 200, raw)
        reply = json.loads(raw)
        self.assertEqual(base64.b64decode(reply["data"][0]["b64_json"]), b"FAKE_IMAGE_BYTES")
        self.assertEqual(headers["X-Relay-Image-Limit"], "2")
        self.assertEqual(headers["X-Relay-Image-Used"], "1")

        # What reached upstream: the operator's key, the model, the default resolution,
        # and the prompt the client sent — the gateway adds nothing.
        sent = self.upstream.requests[-1]
        self.assertEqual(sent["path"], "/ok/images")
        self.assertEqual(sent["authorization"], "Bearer upstream-secret")
        self.assertEqual(sent["body"]["model"], "fake-image-model")
        self.assertEqual(sent["body"]["prompt"], PROMPT)
        self.assertEqual(sent["body"]["resolution"], "64x64")

        # The image is counted against its own allowance, not the token one...
        status, _, quota = gateway.call_json("GET", "/v1/quota", token=token)
        self.assertEqual(quota["used"], 0)
        self.assertEqual(quota["images"], {"limit": 2, "used": 1,
                                           "resets_at": quota["images"]["resets_at"]})
        # ...and its price is settled into the operator's spend ledger.
        self.assertEqual(gateway.store.spend_today(), 0.01)

        status, headers, _ = self.image(gateway, token)
        self.assertEqual((status, headers["X-Relay-Image-Used"]), (200, "2"))
        status, headers, body = self.image(gateway, token)
        self.assertEqual(status, 429)
        self.assertEqual(json.loads(body)["error"]["code"], "quota_exhausted")
        self.assertEqual(headers["X-Relay-Image-Used"], "2")
        self.assertIn("resets_at", json.loads(body)["error"])

    def test_image_health_lists_the_role(self):
        gateway = self.gateway(images_per_day=2, image_price=0.01)
        status, _, health = gateway.call_json("GET", "/v1/health")
        self.assertEqual(status, 200)
        self.assertEqual(health["images"], [{"role": "relay-image", "model": "fake-image-model",
                                             "price_usd": 0.01, "resolutions": ["64x64", "128x64"],
                                             "aspect_ratios": ["1:1", "2:1"]}])

    def test_image_validation(self):
        gateway = self.gateway(images_per_day=2)
        token, _ = gateway.token()
        for body, why in (
                ({"model": "relay-main", "prompt": PROMPT}, "chat role"),
                ({"model": "nope", "prompt": PROMPT}, "unknown role"),
                ({"model": "relay-image"}, "no prompt"),
                ({"model": "relay-image", "prompt": ""}, "empty prompt"),
                ({"model": "relay-image", "prompt": "x" * 4001}, "too long"),
                ({"model": "relay-image", "prompt": PROMPT, "resolution": "999x999"}, "resolution"),
                ({"model": "relay-image", "prompt": PROMPT, "aspect_ratio": "9:9"}, "aspect ratio")):
            status, _, raw = gateway.call("POST", "/v1/images", body, token=token)
            self.assertEqual(status, 400, (why, raw))
            self.assertEqual(json.loads(raw)["error"]["code"], "bad_request", why)
        # A named aspect ratio and resolution pass through as sent.
        status, _, _ = self.image(gateway, token, resolution="128x64", aspect_ratio="2:1")
        self.assertEqual(status, 200)
        self.assertEqual(self.upstream.requests[-1]["body"]["resolution"], "128x64")
        self.assertEqual(self.upstream.requests[-1]["body"]["aspect_ratio"], "2:1")
        # None of it reached upstream.
        self.assertEqual(len(self.upstream.requests), 1)

    def test_image_failover_and_refusal(self):
        gateway = self.gateway(images_per_day=2, image_upstream=("fail503", "ok"))
        token, _ = gateway.token()
        status, _, raw = self.image(gateway, token)
        self.assertEqual(status, 200, raw)                        # failed over to the second upstream
        self.assertEqual(self.upstream.requests[-1]["path"], "/ok/images")

        gateway = self.gateway(images_per_day=2, image_upstream=("fail400",))
        token, _ = gateway.token()
        with self.assertLogs("relay.gateway", level="INFO") as logs:
            status, _, raw = self.image(gateway, token)
            self.assertEqual(status, 502)
            self.assertEqual(json.loads(raw)["error"]["code"], "free_unavailable")
            self.assertNotIn("retried", json.loads(raw)["error"])   # one upstream, no failover
        self.assertTrue(any("error=upstream_refused" in line for line in logs.output))
        self.assertFalse(any(PROMPT in line for line in logs.output))
        # The refused picture is not counted.
        self.assertEqual(self._image_used(gateway, token), 0)

    def _image_used(self, gateway, token):
        """Read the installation's image count through /v1/quota."""
        status, _, quota = gateway.call_json("GET", "/v1/quota", token=token)
        self.assertEqual(status, 200)
        return quota["images"]["used"]

    def test_image_spending_ceiling_refuses_before_the_call(self):
        gateway = self.gateway(images_per_day=2, image_price=0.01, spend_per_day=0.005)
        token, _ = gateway.token()
        status, _, raw = self.image(gateway, token)
        self.assertEqual(status, 503)
        self.assertEqual(json.loads(raw)["error"]["code"], "free_unavailable")
        self.assertEqual(self._image_used(gateway, token), 0)
        self.assertEqual(gateway.store.spend_today(), 0.0)
        self.assertEqual(self.upstream.requests, [])              # nothing reached upstream


class ConfigTests(unittest.TestCase):
    def test_example_config_loads(self):
        path = os.path.join(os.path.dirname(__file__), "..", "gateway", "gateway.example.json")
        env = {"GATEWAY_OPENROUTER_KEY": "k"}
        config = config_mod.load(path, env)
        self.assertEqual(sorted(config.roles),
                         ["relay-flash", "relay-image", "relay-lite", "relay-main"])
        self.assertEqual(config.roles["relay-lite"].max_output_tokens, 512)
        image = config.roles["relay-image"]
        self.assertEqual(image.kind, "images")
        self.assertEqual(image.resolutions, ("1024x1024", "1536x1024", "1024x1536"))
        self.assertEqual(image.resolutions[0], "1024x1024")
        self.assertEqual(config.providers["openrouter"].image_cost_micros(image.upstreams[0].model),
                         6000)
        self.assertEqual([(r.effort, r.max_effort) for r in config.roles.values()],
                         [("medium", "medium"), ("low", "medium"), ("minimal", "medium"),
                          ("medium", "medium")])
        self.assertEqual(config.providers["openrouter"].effort_style, "reasoning")
        self.assertEqual(config.providers["deepseek"].effort_style, "reasoning_effort")
        self.assertEqual(config.client_ip_header, "cf-connecting-ip")
        with self.assertRaises(config_mod.ConfigError):
            config_mod.load(path, {})                          # the key is missing

    def test_image_role_needs_a_price_and_an_allowance(self):
        """A served image model with no price is refused at startup, exactly like a chat model —
        and an image role with no per-install allowance is refused too: never a route that
        spends with nothing counting it."""
        data = config_for("http://127.0.0.1:1", images_per_day=2)
        data["providers"]["ok"].pop("price_per_image")
        with self.assertRaises(config_mod.ConfigError):
            config_mod.parse(data, {KEY_ENV: "k"})

        data = config_for("http://127.0.0.1:1", images_per_day=2)
        data["providers"]["ok"]["price_per_image"] = {"other-model": 0.01}
        with self.assertRaises(config_mod.ConfigError):
            config_mod.parse(data, {KEY_ENV: "k"})

        data = config_for("http://127.0.0.1:1", images_per_day=0)
        data["roles"]["relay-image"] = {"kind": "images",
                                        "upstreams": [{"provider": "ok",
                                                       "model": "fake-image-model"}],
                                        "resolutions": ["64x64"]}
        with self.assertRaises(config_mod.ConfigError):
            config_mod.parse(data, {KEY_ENV: "k"})

        data = config_for("http://127.0.0.1:1", images_per_day=2)
        del data["roles"]["relay-image"]["resolutions"]
        with self.assertRaises(config_mod.ConfigError):
            config_mod.parse(data, {KEY_ENV: "k"})

    def test_effort_settings_are_checked(self):
        data = config_for("http://127.0.0.1:1")
        data["roles"]["relay-main"]["effort"] = "high"          # above its own max_effort
        with self.assertRaises(config_mod.ConfigError):
            config_mod.parse(data, {KEY_ENV: "k"})
        data = config_for("http://127.0.0.1:1")
        data["roles"]["relay-main"]["max_effort"] = "ultra"
        with self.assertRaises(config_mod.ConfigError):
            config_mod.parse(data, {KEY_ENV: "k"})
        data = config_for("http://127.0.0.1:1", effort_style="thinking")
        with self.assertRaises(config_mod.ConfigError):
            config_mod.parse(data, {KEY_ENV: "k"})
        data = config_for("http://127.0.0.1:1")
        data["roles"]["relay-main"]["upstreams"][0]["extra"] = {"reasoning": {"effort": "high"}}
        with self.assertRaises(config_mod.ConfigError):        # the role owns effort
            config_mod.parse(data, {KEY_ENV: "k"})
        data = config_for("http://127.0.0.1:1")
        for role in data["roles"].values():
            del role["effort"], role["max_effort"]              # defaults: medium, capped at medium
        config = config_mod.parse(data, {KEY_ENV: "k"})
        self.assertEqual((config.roles["relay-main"].effort, config.roles["relay-main"].max_effort),
                         ("medium", "medium"))

    def test_a_served_model_without_a_price_is_a_startup_error(self):
        data = config_for("http://127.0.0.1:1")
        del data["providers"]["ok"]["price_per_mtok"]["fake-model"]
        with self.assertRaises(config_mod.ConfigError) as caught:
            config_mod.parse(data, {KEY_ENV: "k"})
        self.assertIn("price", str(caught.exception))

    def test_plain_http_needs_loopback_and_the_flag(self):
        data = config_for("http://127.0.0.1:1")
        data["allow_insecure_loopback"] = False
        with self.assertRaises(config_mod.ConfigError):
            config_mod.parse(data, {KEY_ENV: "k"})
        config_mod.parse(data, {KEY_ENV: "k", "GATEWAY_ALLOW_HTTP_LOOPBACK": "1"})
        data = config_for("http://example.com")
        with self.assertRaises(config_mod.ConfigError):
            config_mod.parse(data, {KEY_ENV: "k"})

    def test_cost(self):
        config = config_mod.parse(config_for("http://127.0.0.1:1"), {KEY_ENV: "k"})
        self.assertEqual(config.providers["ok"].cost_micros("fake-model", 1_000_000, 1_000_000),
                         2_000_000)


class ValidateTests(unittest.TestCase):
    def setUp(self):
        self.config = config_mod.parse(config_for("http://127.0.0.1:1"), {KEY_ENV: "k"})

    def check(self, **fields):
        return validate_mod.validate(json.dumps({"model": "relay-main", "messages": [
            {"role": "user", "content": "hi"}], **fields}).encode(), self.config)

    def test_shapes(self):
        good = self.check(tools=[{"type": "function", "function": {"name": "f", "parameters": {}}}],
                          tool_choice="auto", top_p=0.5, response_format={"type": "json_object"})
        self.assertEqual(good.body["tool_choice"], "auto")
        self.assertEqual(good.upstream_body("m", {"temperature": 0})["model"], "m")
        self.assertEqual(good.upstream_body("m", {}, "reasoning")["reasoning"], {"effort": "low"})
        self.assertEqual(good.upstream_body("m", {}, "reasoning_effort")["reasoning_effort"], "low")
        self.assertNotIn("reasoning", good.upstream_body("m", {}, "none"))
        for bad in (dict(tools=[{"type": "web_search"}]), dict(tool_choice="always"),
                    dict(top_p=2), dict(temperature="hot"), dict(max_tokens=0),
                    dict(response_format={"type": "xml"}), dict(user="me"), dict(seed=1)):
            with self.assertRaises(validate_mod.BadRequest, msg=bad):
                self.check(**bad)

    def test_message_shapes(self):
        def messages(*items):
            return validate_mod.validate(json.dumps({"model": "relay-main",
                                                     "messages": list(items)}).encode(), self.config)
        messages({"role": "system", "content": "s"},
                 {"role": "user", "content": [{"type": "text", "text": "t"},
                                              {"type": "image_url", "image_url": {"url": "data:x"}}]},
                 {"role": "assistant", "content": None, "tool_calls": [
                     {"id": "c1", "type": "function", "function": {"name": "f", "arguments": "{}"}}]},
                 {"role": "tool", "tool_call_id": "c1", "content": "done"})
        for bad in ([{"role": "boss", "content": "x"}], [{"role": "user"}],
                    [{"role": "user", "content": [{"type": "video"}]}],
                    [{"role": "tool", "content": "x"}],
                    [{"role": "user", "content": "x", "tool_calls": []}], []):
            with self.assertRaises(validate_mod.BadRequest, msg=bad):
                messages(*bad)


if __name__ == "__main__":
    unittest.main()
