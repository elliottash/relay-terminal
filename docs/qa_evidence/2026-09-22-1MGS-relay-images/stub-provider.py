#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Loopback-only OpenAI-compatible endpoint for the #1MGS relay-side drive.

    python3 stub-provider.py 8821

Answers every request with a reply holding Markdown images, streamed in 7-character deltas,
and no tool calls.
"""
import json
import sys
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

REPLY = ("Here is the chart:\n\n![the chart](chart.png)\n\nand inline ![wide](~/project/wide.png) then "
         "[a link](notes.md) after it, and a web one ![logo](https://example.com/l.png) and a missing "
         "![gone](nope.png).\n")


class Handler(BaseHTTPRequestHandler):
    def log_message(self, *args):
        pass

    def do_POST(self):
        length = int(self.headers.get("Content-Length") or 0)
        request = json.loads(self.rfile.read(length) or b"{}")
        model = request.get("model") or "stub"
        if request.get("stream"):
            self.send_response(200)
            self.send_header("Content-Type", "text/event-stream")
            self.end_headers()

            def chunk(delta, finish_reason=None):
                data = {"id": "stub", "object": "chat.completion.chunk", "created": 0, "model": model,
                        "choices": [{"index": 0, "delta": delta, "finish_reason": finish_reason}]}
                self.wfile.write(b"data: " + json.dumps(data).encode() + b"\n\n")
                self.wfile.flush()

            for piece in [REPLY[i:i + 7] for i in range(0, len(REPLY), 7)]:
                time.sleep(0.03)
                chunk({"role": "assistant", "content": piece})
            chunk({}, "stop")
            self.wfile.write(b"data: [DONE]\n\n")
            return
        payload = {"id": "stub", "object": "chat.completion", "created": 0, "model": model,
                   "choices": [{"index": 0, "message": {"role": "assistant", "content": REPLY},
                                "finish_reason": "stop"}],
                   "usage": {"prompt_tokens": 1, "completion_tokens": 1, "total_tokens": 2}}
        raw = json.dumps(payload).encode()
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(raw)))
        self.end_headers()
        self.wfile.write(raw)

    def do_GET(self):
        raw = json.dumps({"data": [{"id": "stub"}]}).encode()
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(raw)))
        self.end_headers()
        self.wfile.write(raw)


if __name__ == "__main__":
    ThreadingHTTPServer(("127.0.0.1", int(sys.argv[1])), Handler).serve_forever()
