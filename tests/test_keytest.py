# SPDX-License-Identifier: AGPL-3.0-or-later
"""The keys modal's Test button against a provider that says "not now" (protocol 13.8).

The Test button shows a spinner and nothing else: its thread is not cancellable and there is no
status line for "asking again in 60 s". So the one thing checked here is that a refusal the
transport would otherwise wait out six times over becomes an answer inside ``keytest.TIMEOUT_S``.
Everything else about the Test button lives in tests/test_keystore.py.
"""
import threading
import time
import unittest
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

from relay_core import keytest
from relay_core.provider import ChatProvider, ProviderConfig


class RefusingServer:
    """A loopback endpoint that answers every chat call 429 with a minute-long Retry-After."""

    def __init__(self):
        self.calls = 0
        outer = self

        class Handler(BaseHTTPRequestHandler):
            def log_message(self, *args):
                pass

            def do_POST(self):
                self.rfile.read(int(self.headers.get("Content-Length") or 0))
                outer.calls += 1
                body = b'{"error": "rate limit"}'
                self.send_response(429)
                self.send_header("Retry-After", "60")
                self.send_header("Content-Length", str(len(body)))
                self.end_headers()
                self.wfile.write(body)

        self.server = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
        self.thread = threading.Thread(target=self.server.serve_forever, daemon=True)
        self.thread.start()
        self.base = f"http://127.0.0.1:{self.server.server_port}/v1"

    def close(self):
        self.server.shutdown()
        self.server.server_close()
        self.thread.join()


class KeyTestBudgetTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.endpoint = RefusingServer()

    @classmethod
    def tearDownClass(cls):
        cls.endpoint.close()

    def factory(self, preset_id, key):
        # A real transport, so the real retry loop runs; only the address is the fake one.
        return ChatProvider(ProviderConfig(self.endpoint.base, "test-model", "", {}, keytest.MAX_TOKENS))

    def test_a_rate_limited_provider_answers_the_button_inside_the_budget(self):
        started = time.monotonic()
        result = keytest.check("openrouter", "k", self.factory)
        elapsed = time.monotonic() - started
        self.assertFalse(result["ok"])
        self.assertIn("429", result["error"])
        # Six retries honouring Retry-After: 60 would be six minutes of spinner. The budget is
        # shorter than the wait the provider named, so the refusal is the answer straight away.
        self.assertEqual(self.endpoint.calls, 1)
        self.assertLess(elapsed, keytest.TIMEOUT_S)
        self.assertLess(result["elapsed_ms"], keytest.TIMEOUT_S * 1000)

    def test_the_budget_applies_to_the_thread_the_button_starts_too(self):
        events = []
        thread = keytest.run("openrouter", events.append, "req-1", lookup=lambda pid: "k",
                             factory=self.factory)
        thread.join(keytest.TIMEOUT_S)
        self.assertFalse(thread.is_alive())
        self.assertEqual(len(events), 1)
        self.assertEqual(events[0]["event"], "key_tested")
        self.assertFalse(events[0]["ok"])


if __name__ == "__main__":
    unittest.main()
