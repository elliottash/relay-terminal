"""Every request the desktop sends a rendezvous names itself, because Cloudflare insists.

Found live on 2026-09-18: join.relay-terminal.ai sits behind Cloudflare, which answered 403 to
Python's default `Python-urllib/3.x` User-Agent and 200 to the same request naming itself. The
probe of the hosted address failed, so the share window never offered it, and registering,
opening rooms and making codes would have failed the same way. The stand-in below refuses exactly
what Cloudflare refused.
"""
from __future__ import annotations

import asyncio
import json
import threading
import unittest
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

from remote import gui_host, ws


class _PicklyHandler(BaseHTTPRequestHandler):
    seen: list[str] = []

    def _check(self) -> bool:
        agent = self.headers.get("User-Agent", "")
        type(self).seen.append(agent)
        if not agent.startswith("Relay/"):
            self.send_response(403)
            self.end_headers()
            return False
        return True

    def do_GET(self):                                  # noqa: N802 (http.server's naming)
        if not self._check():
            return
        body = json.dumps({"ok": True}).encode()
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def log_message(self, *args):                      # keep the test output clean
        pass


class UserAgentTests(unittest.TestCase):
    def setUp(self):
        _PicklyHandler.seen = []
        self.server = ThreadingHTTPServer(("127.0.0.1", 0), _PicklyHandler)
        threading.Thread(target=self.server.serve_forever, daemon=True).start()
        self.origin = f"http://127.0.0.1:{self.server.server_address[1]}"

    def tearDown(self):
        self.server.shutdown()
        self.server.server_close()

    def test_the_hosted_probe_names_itself_so_a_pickly_front_end_answers(self):
        self.assertEqual(gui_host.probe_hosted(self.origin), "")
        self.assertEqual(_PicklyHandler.seen, [ws.USER_AGENT])

    def test_the_websocket_handshake_names_itself(self):
        received: list[bytes] = []

        async def main():
            async def handle(reader, writer):
                received.append(await reader.readuntil(b"\r\n\r\n"))
                writer.close()

            server = await asyncio.start_server(handle, "127.0.0.1", 0)
            port = server.sockets[0].getsockname()[1]
            with self.assertRaises(Exception):         # the stand-in never completes the upgrade
                await ws.connect(f"ws://127.0.0.1:{port}/v1/connect", timeout=5)
            server.close()
            await server.wait_closed()

        asyncio.run(main())
        self.assertIn(f"User-Agent: {ws.USER_AGENT}".encode(), received[0])

    def test_a_caller_that_names_itself_is_not_named_twice(self):
        received: list[bytes] = []

        async def main():
            async def handle(reader, writer):
                received.append(await reader.readuntil(b"\r\n\r\n"))
                writer.close()

            server = await asyncio.start_server(handle, "127.0.0.1", 0)
            port = server.sockets[0].getsockname()[1]
            with self.assertRaises(Exception):
                await ws.connect(f"ws://127.0.0.1:{port}/", headers={"User-Agent": "Other/1"},
                                 timeout=5)
            server.close()
            await server.wait_closed()

        asyncio.run(main())
        self.assertEqual(received[0].lower().count(b"user-agent:"), 1)


if __name__ == "__main__":
    unittest.main()
