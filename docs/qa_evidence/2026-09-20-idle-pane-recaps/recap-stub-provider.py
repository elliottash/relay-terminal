#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""A loopback-only OpenAI-compatible endpoint for the #D54R live check.

Unchanged copy of the #MVGR stub (docs/qa_evidence/2026-09-20-recap-finished-at/
recap-stub-provider.py): the recap sidecall gets JSON, a normal turn answers at once.

    python3 recap-stub-provider.py 8768
"""
import json
import sys
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer


def body(request: dict) -> dict:
    messages = request.get("messages") or []
    system = next((m.get("content") for m in messages if m.get("role") == "system"), "") or ""
    if "You write a recap" in system:
        return {"role": "assistant",
                "content": json.dumps({"summary": "Three stub turns ran while the pane sat "
                                                  "idle and unwatched. Nothing is unverified.",
                                       "next_action": None})}
    if "You predict the next short request" in system:
        return {"role": "assistant", "content": json.dumps({"prompt": ""})}
    return {"role": "assistant", "content": "Turn done."}


class Handler(BaseHTTPRequestHandler):
    def do_POST(self):
        length = int(self.headers.get("Content-Length") or 0)
        request = json.loads(self.rfile.read(length) or b"{}")
        message = body(request)
        payload = {"id": "stub", "object": "chat.completion", "created": 0,
                   "model": request.get("model") or "relay-qa-stub",
                   "choices": [{"index": 0, "message": message, "finish_reason": "stop"}],
                   "usage": {"prompt_tokens": 1, "completion_tokens": 1, "total_tokens": 2}}
        raw = json.dumps(payload).encode()
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(raw)))
        self.end_headers()
        self.wfile.write(raw)

    def do_GET(self):
        raw = json.dumps({"data": [{"id": "relay-qa-stub"}]}).encode()
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(raw)))
        self.end_headers()
        self.wfile.write(raw)

    def log_message(self, *args):
        pass


if __name__ == "__main__":
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 8768
    ThreadingHTTPServer(("127.0.0.1", port), Handler).serve_forever()
