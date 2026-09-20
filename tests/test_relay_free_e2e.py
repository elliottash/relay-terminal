# SPDX-License-Identifier: AGPL-3.0-or-later
"""Relay Free end to end: the desktop's own client against the real gateway in one process.

``tests/test_hosted.py`` proves the client against a fake gateway and ``tests/test_gateway.py``
proves the gateway against a fake provider. This file joins the two real halves, so the contract
they were each written to (the proof, the derived installation id, the quota headers, the error
codes) is checked where it matters: a ``HostedChatProvider`` registering, streaming, being told
its allowance, refreshing a rotated token and being refused.
"""
import logging
import os
import tempfile
import threading
import time
import unittest
from unittest import mock

from relay_core import hosted
from relay_core.provider import HostedChatProvider, ProviderConfig, ProviderError

from tests.test_gateway import KEY_ENV, PROMPT, FakeUpstream, Gateway, config_for


class RelayFreeEndToEnd(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.upstream = FakeUpstream()
        os.environ[KEY_ENV] = "upstream-secret"

    @classmethod
    def tearDownClass(cls):
        cls.upstream.stop()
        os.environ.pop(KEY_ENV, None)

    def start(self, **overrides) -> Gateway:
        gateway = Gateway(config_for(self.upstream.base, **overrides))
        self.addCleanup(gateway.close)
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        env = mock.patch.dict(os.environ, {"XDG_DATA_HOME": self.temp.name, "RELAY_KEYRING": "off",
                                           hosted.ENV_URL: f"http://{gateway.host}:{gateway.port}/v1"})
        env.start()
        self.addCleanup(env.stop)
        self.upstream.requests.clear()
        return gateway

    @staticmethod
    def provider(session, model="relay-main"):
        canonical = hosted.PRESETS[hosted.PRESET_ID].base_url
        return HostedChatProvider(ProviderConfig(canonical, model, "", {}, 1024, hosted=True),
                                  session=session)

    @staticmethod
    def complete(provider):
        events = []
        result = provider.complete([{"role": "user", "content": PROMPT}], [], events.append,
                                   threading.Event())
        return result, events

    def test_a_fresh_install_registers_streams_and_learns_its_allowance(self):
        gateway = self.start()
        session = hosted.Session()
        with self.assertLogs("relay.gateway", level="INFO") as logged:
            result, events = self.complete(self.provider(session))
            # The gateway settles the quota and writes its line after the last chunk is written,
            # which is after the client has returned; wait for that settlement, briefly.
            deadline = time.monotonic() + 5
            while session.fetch_quota()["used"] < 110 and time.monotonic() < deadline:
                time.sleep(0.02)
        self.assertEqual(result["content"], "Hello world")
        self.assertIn("delta", [event["event"] for event in events])
        quotas = [event for event in events if event["event"] == "hosted_quota"]
        self.assertEqual(len(quotas), 1)
        self.assertEqual(quotas[0]["limit"], 100000)
        self.assertGreater(quotas[0]["resets_at"], 0)
        # The id the gateway derived is the one the client derived: a different gateway is refused.
        self.assertEqual(session.installation_id, gateway.store.authenticate(session.token()).id)
        # The gateway saw the client's message but rebuilt the request: its own key, its own model.
        sent = self.upstream.requests[-1]
        self.assertEqual(sent["authorization"], "Bearer upstream-secret")
        self.assertEqual(sent["body"]["model"], "fake-model")
        self.assertEqual(sent["body"]["reasoning"], {"effort": "low"})
        # After the stream the used count reflects the provider's usage chunk (60 + 50).
        self.assertEqual(session.fetch_quota()["used"], 110)
        # And the gateway's log carries the outcome, never the prompt.
        text = "\n".join(logged.output)
        self.assertIn("status=200", text)
        self.assertNotIn(PROMPT, text)

    def test_an_exhausted_allowance_is_a_coded_refusal_and_the_terminal_is_not_involved(self):
        self.start(tokens_per_day=120)
        session = hosted.Session()
        self.complete(self.provider(session))        # 110 of 120 used
        with self.assertRaises(ProviderError) as caught:
            self.complete(self.provider(session))
        self.assertEqual(caught.exception.code, "quota_exhausted")
        self.assertIsNotNone(caught.exception.resets_at)
        self.assertIn("Options › Models", str(caught.exception))

    def test_a_model_that_is_not_a_role_is_refused_by_the_gateway_not_the_upstream(self):
        self.start()
        with self.assertRaises(ProviderError) as caught:
            self.complete(self.provider(hosted.Session(), model="gpt-4o"))
        self.assertEqual(caught.exception.code, "bad_request")
        self.assertEqual(self.upstream.requests, [])

    def test_a_rotated_token_is_refreshed_once_without_the_caller_noticing(self):
        gateway = self.start()
        first = hosted.Session()
        self.complete(self.provider(first))
        stale = first.token()
        # The same installation registering again (a second worker, say) rotates the token.
        second = hosted.Session()
        second.token()
        self.assertIsNone(gateway.store.authenticate(stale))
        result, _ = self.complete(self.provider(first))
        self.assertEqual(result["content"], "Hello world")
        self.assertNotEqual(first.token(), stale)
        rows = gateway.store.db.execute("SELECT COUNT(*) AS n FROM installations").fetchone()["n"]
        self.assertEqual(rows, 1)


if __name__ == "__main__":
    unittest.main()
