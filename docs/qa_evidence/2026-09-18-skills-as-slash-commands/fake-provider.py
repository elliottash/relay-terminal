#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""An OpenAI-compatible stand-in for the skills-as-/commands run: no key, no credits.

Logs every tool-carrying request's last user message in full, one JSON line each, so the run can
show that the SKILL.md travelled with `/clean-commit`. Usage: fake-provider.py <port> <log file>
"""
import json
import sys
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

PORT, LOG = int(sys.argv[1]), sys.argv[2]


class Handler(BaseHTTPRequestHandler):
    def _send(self, obj):
        data = json.dumps(obj).encode()
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def do_GET(self):
        self._send({"object": "list", "data": [{"id": "fake", "object": "model"}]})

    def do_POST(self):
        body = json.loads(self.rfile.read(int(self.headers.get("Content-Length", 0))) or b"{}")
        messages = body.get("messages", [])
        user = next((m.get("content") for m in reversed(messages) if m.get("role") == "user"), "")
        if body.get("tools"):
            with open(LOG, "a", encoding="utf-8") as log:
                log.write(json.dumps({"user": user}) + "\n")
        if "use the" in str(user):
            time.sleep(3)   # a slow answer, so the run can see the hint toast before "Ready" replaces it
        text = "Following the clean-commit skill: FAKE-OK." if "CLEAN-COMMIT-MARKER" in str(user) else "No skill came with that."
        self._send({"id": "fake", "object": "chat.completion", "model": body.get("model"),
                    "choices": [{"index": 0, "message": {"role": "assistant", "content": text}, "finish_reason": "stop"}],
                    "usage": {"prompt_tokens": 10, "completion_tokens": 5, "total_tokens": 15}})

    def log_message(self, *args):
        pass


ThreadingHTTPServer(("127.0.0.1", PORT), Handler).serve_forever()
