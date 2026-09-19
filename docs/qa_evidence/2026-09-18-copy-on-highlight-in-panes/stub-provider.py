#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Loopback-only OpenAI-compatible endpoint, so the screenshots need no API key.

    python3 stub-provider.py 8791

It answers every turn with a few fixed lines of prose -- something to highlight in the panes --
and nothing else: no tool calls, no streaming, no model of its own.
"""
import json
import sys
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

ANSWER = ("The quick brown fox jumps over the lazy dog.\n"
          "Highlighting any of these words should copy them.\n"
          "Copy on highlight is off until the setting is switched on.")


class Handler(BaseHTTPRequestHandler):
    def log_message(self, *args):
        pass

    def do_GET(self):
        if self.path.rstrip("/").endswith("/models"):
            self._send({"object": "list", "data": [{"id": "stub", "object": "model"}]})
        else:
            self.send_error(404)

    def do_POST(self):
        self.rfile.read(int(self.headers.get("Content-Length") or 0))
        self._send({
            "id": "chatcmpl-stub",
            "object": "chat.completion",
            "model": "stub",
            "choices": [{"index": 0, "finish_reason": "stop",
                         "message": {"role": "assistant", "content": ANSWER}}],
            "usage": {"prompt_tokens": 12, "completion_tokens": 24, "total_tokens": 36},
        })

    def _send(self, payload):
        body = json.dumps(payload).encode()
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)


if __name__ == "__main__":
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 8791
    ThreadingHTTPServer(("127.0.0.1", port), Handler).serve_forever()
