#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Loopback-only OpenAI-compatible endpoint for the #GH5T run.

    python3 stub-provider.py 8842

Nothing here calls a provider. The run registers this process as a local model endpoint
(`local:stub`), puts it in the helper worker's Options › Models priority list, and the helper —
whose Main is the Claude Code guest preset — falls back onto it and answers out of the scene
below.

The scene is picked by a keyword in a **user** message rather than by the last message: the
worker appends a user-role message of its own at the end of a turn (the completion check), and
keying on the last one would take the scene away from the prompt that was typed. That is the
lesson `2026-09-20-agent-app-control/stub-provider.py` records, and it applies here unchanged.
"""
import json
import sys
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer


def answer(messages):
    text = " ".join(str(m.get("content") or "") for m in messages
                    if m.get("role") == "user").lower()
    if "which card" in text:
        return ("The board has one card: **#FX01 — A fixture card for the helper-on-guest repro**, "
                "in Inbox. I am the helper agent, answering on the stub local endpoint.")
    return "One line, so a stray turn cannot hang the run."


class Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def log_message(self, *args):
        sys.stderr.write("%s %s\n" % (time.strftime("%H:%M:%S"), args[0] % args[1:]))

    def do_POST(self):
        body = json.loads(self.rfile.read(int(self.headers.get("Content-Length") or 0)) or b"{}")
        reply = {"id": "stub", "object": "chat.completion", "created": int(time.time()),
                 "model": body.get("model", "stub"),
                 "choices": [{"index": 0, "finish_reason": "stop",
                              "message": {"role": "assistant",
                                          "content": answer(body.get("messages") or [])}}],
                 "usage": {"prompt_tokens": 10, "completion_tokens": 10, "total_tokens": 20}}
        raw = json.dumps(reply).encode()
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(raw)))
        self.end_headers()
        self.wfile.write(raw)

    def do_GET(self):
        raw = json.dumps({"data": [{"id": "stub-model"}]}).encode()
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(raw)))
        self.end_headers()
        self.wfile.write(raw)


if __name__ == "__main__":
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 8842
    ThreadingHTTPServer(("127.0.0.1", port), Handler).serve_forever()
