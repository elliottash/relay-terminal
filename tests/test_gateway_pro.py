# SPDX-License-Identifier: AGPL-3.0-or-later
"""Pro access over real HTTP, with actual captured upstream requests and a local operator CLI."""
import contextlib
import io
import json
import os
import sqlite3
import tempfile
import threading
import time
import unittest
from unittest.mock import patch

from gateway import config, pro
from gateway.store import Store
from tests.test_gateway import FakeUpstream, Gateway, KEY_ENV, config_for


class ProTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.upstream = FakeUpstream()
        cls.env = patch.dict(os.environ, {KEY_ENV: "upstream-secret"})
        cls.env.start()

    @classmethod
    def tearDownClass(cls):
        cls.upstream.stop()
        cls.env.stop()

    def setUp(self):
        self.upstream.requests.clear()
        self.upstream.slow_gate.clear()

    def gateway(self, enabled=True, pro_kind="ok", **overrides):
        data = config_for(self.upstream.base, **overrides)
        if enabled:
            for name in config.PRO_ROLES:
                data["roles"][name] = {"upstreams": [{"provider": pro_kind, "model": (
                    "glm5.3flash" if name.endswith("flash") else "glm5.3")}],
                    "max_output_tokens": 300, "max_input_chars": 4000,
                    "effort": "high", "max_effort": "high"}
            # Deliberately synthetic fixture prices, not operator recommendations.
            data["providers"][pro_kind]["price_per_mtok"].update({
                "glm5.3": list(overrides.get("prices", (0.5, 1.5))),
                "glm5.3flash": list(overrides.get("prices", (0.5, 1.5)))})
        gateway = Gateway(data)
        self.addCleanup(gateway.close)
        return gateway

    def chat(self, gateway, token, code=None, model="relay-pro-main", **fields):
        return gateway.call("POST", "/v1/chat/completions", {
            "model": model, "messages": [{"role": "user", "content": "hi"}], **fields},
            token=token, headers={"X-Relay-Pro-Code": code} if code is not None else {})

    def entitlement(self, gateway, token, code=None):
        return gateway.call_json("GET", "/v1/pro", token=token,
                                 headers={"X-Relay-Pro-Code": code} if code is not None else {})

    def denied(self, reply):
        self.assertEqual(reply[0], 403, reply)
        body = json.loads(reply[2]) if isinstance(reply[2], bytes) else reply[2]
        self.assertEqual(body["error"]["code"], "pro_access_denied")

    def test_valid_roles_proxy_and_no_secret_logging(self):
        gateway = self.gateway()
        token, registered = gateway.token()
        code = gateway.store.issue_pro_code("alice")
        status, _, result = self.entitlement(gateway, token, code)
        self.assertEqual(status, 200)
        self.assertEqual(result, {"active": True, "models": list(config.PRO_ROLES)})
        with self.assertLogs("relay.gateway", level="INFO") as logs:
            for role in config.PRO_ROLES:
                status, headers, body = self.chat(gateway, token, code, role, reasoning_effort="max")
                self.assertEqual(status, 200, body)
                self.assertTrue(body.endswith(b"data: [DONE]\n\n"))
                self.assertIn("X-Relay-Quota-Used", headers)
                sent = self.upstream.requests[-1]
                self.assertEqual(sent["body"]["model"],
                                 "glm5.3flash" if role.endswith("flash") else "glm5.3")
                self.assertEqual(sent["body"]["reasoning"], {"effort": "high"})
                self.assertEqual(sent["authorization"], "Bearer upstream-secret")
                self.assertNotIn("x-relay-pro-code", {k.lower() for k in sent["headers"]})
                self.assertNotIn(code, json.dumps(sent))
        self.assertNotIn(code, "\n".join(logs.output))
        self.assertNotIn(token, "\n".join(logs.output))
        self.assertEqual(registered["plan"], "free")
        self.assertEqual(gateway.call_json("GET", "/v1/quota", token=token)[2]["used"], 330)

    def test_missing_bad_revoked_and_independent_people(self):
        gateway = self.gateway()
        token, _ = gateway.token()
        alice = gateway.store.issue_pro_code("alice")
        bob = gateway.store.issue_pro_code("bob")
        self.assertEqual(self.chat(gateway, token, alice)[0], 200)
        self.assertTrue(gateway.store.revoke_pro_code("alice"))
        for code in (None, "", "forged", alice):
            self.denied(self.entitlement(gateway, token, code))
            for role in config.PRO_ROLES:
                self.denied(self.chat(gateway, token, code, role))
        self.assertEqual(len(self.upstream.requests), 1)
        self.assertEqual(self.chat(gateway, token, bob)[0], 200)
        self.assertEqual(self.entitlement(gateway, token, bob)[0], 200)

    def test_code_never_replaces_installation_identity(self):
        gateway = self.gateway()
        code = gateway.store.issue_pro_code("alice")
        for token in (None, "forged-installation", code):
            self.assertEqual(self.entitlement(gateway, token, code)[0], 401)
            self.assertEqual(self.chat(gateway, token, code)[0], 401)
        self.assertEqual(self.upstream.requests, [])

    def test_forged_plan_and_body_code_do_not_grant_access(self):
        gateway = self.gateway()
        token, _ = gateway.token()
        code = gateway.store.issue_pro_code("alice")
        self.denied(self.chat(gateway, token, plan="pro", pro_code=code))
        self.denied(self.chat(gateway, token, model="relay-pro-ultra"))
        self.denied(self.chat(gateway, token, code, model="relay-pro-ultra"))
        self.assertEqual(self.chat(gateway, token, code, model="glm5.3")[0], 400)
        self.assertEqual(self.upstream.requests, [])

    def test_unconfigured_pro_and_free_unchanged(self):
        for enabled in (False, True):
            gateway = self.gateway(enabled)
            token, _ = gateway.token()
            code = gateway.store.issue_pro_code("alice")
            if not enabled:
                self.denied(self.entitlement(gateway, token, code))
                self.denied(self.chat(gateway, token, code))
            gateway.store.revoke_pro_code("alice")
            for role in ("relay-main", "relay-lite"):
                self.assertEqual(self.chat(gateway, token, code, role)[0], 200)
                self.assertEqual(self.chat(gateway, token, None, role)[0], 200)

    def test_pro_uses_shared_quota_rate_and_spend(self):
        for overrides, expected_status, expected_code in (
            ({"tokens_per_day": 100}, 429, "quota_exhausted"),
            ({"requests_per_minute": 1}, 429, "rate_limited"),
            ({"prices": (1_000_000, 1_000_000)}, 503, "free_unavailable"),
        ):
            gateway = self.gateway(**overrides)
            token, _ = gateway.token()
            code = gateway.store.issue_pro_code("alice")
            self.assertEqual(self.chat(gateway, token, code)[0], 200)
            for role in ("relay-pro-main", "relay-main"):
                status, _, body = self.chat(gateway, token, code, role)
                self.assertEqual(status, expected_status)
                self.assertEqual(json.loads(body)["error"]["code"], expected_code)

    def test_pro_and_free_share_concurrency(self):
        gateway = self.gateway(pro_kind="slow", concurrency_per_install=1)
        token, _ = gateway.token()
        code = gateway.store.issue_pro_code("alice")
        results = []
        worker = threading.Thread(target=lambda: results.append(self.chat(gateway, token, code)))
        worker.start()
        try:
            deadline = time.monotonic() + 3
            while not self.upstream.requests and time.monotonic() < deadline:
                time.sleep(0.01)
            self.assertTrue(self.upstream.requests)
            for role in ("relay-pro-main", "relay-main"):
                status, _, body = self.chat(gateway, token, code, role)
                self.assertEqual(status, 429)
                self.assertEqual(json.loads(body)["error"]["code"], "rate_limited")
        finally:
            self.upstream.slow_gate.set()
            worker.join(5)
        self.assertEqual(results[0][0], 200)
        self.assertEqual(self.chat(gateway, token, code)[0], 200)

    def test_database_failure_fails_closed(self):
        gateway = self.gateway()
        token, _ = gateway.token()
        code = gateway.store.issue_pro_code("alice")
        with patch.object(gateway.store, "pro_code_active", side_effect=sqlite3.OperationalError):
            with self.assertLogs("relay.gateway", level="ERROR"):
                self.assertEqual(self.entitlement(gateway, token, code)[0], 503)
                self.assertEqual(self.chat(gateway, token, code)[0], 503)
        self.assertEqual(self.upstream.requests, [])

    def test_reserved_config_and_prices(self):
        data = config_for(self.upstream.base)
        data["roles"]["relay-pro-ultra"] = data["roles"]["relay-main"]
        with self.assertRaises(config.ConfigError):
            config.parse(data, {KEY_ENV: "k"})
        data["roles"]["relay-pro-main"] = data["roles"].pop("relay-pro-ultra")
        data["roles"]["relay-pro-main"]["upstreams"] = [{"provider": "ok", "model": "glm5.3"}]
        with self.assertRaisesRegex(config.ConfigError, "no price"):
            config.parse(data, {KEY_ENV: "k"})


class OperatorTests(unittest.TestCase):
    def test_cli_digest_only_and_live_connection_revocation(self):
        with tempfile.TemporaryDirectory() as directory:
            path = os.path.join(directory, "gateway.db")
            store = Store(path)
            self.addCleanup(store.close)

            def cli(*args):
                out, err = io.StringIO(), io.StringIO()
                with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
                    status = pro.main(["--db", path, *args])
                return status, out.getvalue(), err.getvalue()

            status, output, _ = cli("issue", "alice")
            self.assertEqual(status, 0)
            code = output.strip()
            self.assertEqual(len(code), 46)
            self.assertTrue(store.pro_code_active(code))
            self.assertEqual(cli("issue", "alice")[0], 1)
            status, listing, _ = cli("list")
            self.assertEqual(status, 0)
            self.assertNotIn(code, listing)
            self.assertEqual(json.loads(listing)[0]["person"], "alice")
            self.assertNotIn(code, "\n".join(store.db.iterdump()))
            bob = cli("issue", "bob")[1].strip()
            self.assertNotEqual(code, bob)
            self.assertEqual(cli("revoke", "alice")[0], 0)
            self.assertFalse(store.pro_code_active(code))
            self.assertTrue(store.pro_code_active(bob))
            replacement = cli("issue", "alice")[1].strip()
            self.assertNotEqual(replacement, code)
            self.assertTrue(store.pro_code_active(replacement))
            self.assertFalse(store.pro_code_active(code))


if __name__ == "__main__":
    unittest.main()
