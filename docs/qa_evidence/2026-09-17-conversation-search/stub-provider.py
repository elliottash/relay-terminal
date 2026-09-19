#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""A loopback-only OpenAI-compatible endpoint for the live QA run.

The conversation-search QA needs real agent turns (real worker, real autosave, real indexing) but
must not reach the network or use anybody's API key. This answers /v1/chat/completions from a
canned table, so the conversations in the screenshots are produced by Relay's own code path.

    python3 stub-provider.py 8731
"""
import json
import sys
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

REPLIES = [
    ("pelican", "Noted: the pelican is the bird you asked me to remember. Pelicans carry their catch in a throat pouch."),
    ("aardvark", "The aardvark in the basement is nocturnal and eats ants; I would leave it alone."),
    ("capybara", "Capybaras are the largest rodents and very calm company."),
]


class Handler(BaseHTTPRequestHandler):
    def do_POST(self):
        length = int(self.headers.get("Content-Length") or 0)
        request = json.loads(self.rfile.read(length) or b"{}")
        last = ""
        for message in reversed(request.get("messages") or []):
            if message.get("role") == "user":
                last = str(message.get("content") or "")
                break
        text = "I read your message and kept it in this conversation."
        for needle, reply in REPLIES:
            if needle in last.lower():
                text = reply
                break
        body = json.dumps({"id": "qa", "object": "chat.completion", "model": request.get("model", "stub"),
                           "choices": [{"index": 0, "finish_reason": "stop",
                                        "message": {"role": "assistant", "content": text}}],
                           "usage": {"prompt_tokens": 20, "completion_tokens": 20, "total_tokens": 40}}).encode()
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def log_message(self, *args):
        pass


if __name__ == "__main__":
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 8731
    ThreadingHTTPServer(("127.0.0.1", port), Handler).serve_forever()
