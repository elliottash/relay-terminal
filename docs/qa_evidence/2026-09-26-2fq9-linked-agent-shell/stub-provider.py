#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Loopback-only OpenAI-compatible endpoint for card #2FQ9's live drive.

    python3 stub-provider.py 8841

It answers every request with what the conversation it was handed already holds: every
`MARK-<WORD>` the person's messages contain, in order, and how many of them there are. A reply
that names a marker typed *before* the agent was popped out, sent *after* it was docked back, is
the evidence that the same conversation survived both moves.
"""
import json
import re
import sys
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer


def texts(request):
    out = []
    for message in request.get("messages") or []:
        if message.get("role") != "user":
            continue
        content = message.get("content")
        if isinstance(content, list):
            content = " ".join(part.get("text", "") for part in content if isinstance(part, dict))
        out.append(content or "")
    return out


def answer(request):
    markers = []
    for text in texts(request):
        for marker in re.findall(r"MARK-[A-Z]+", text):
            if marker not in markers:
                markers.append(marker)
    return ("STUB REPLY. Markers this conversation holds, oldest first: "
            + (", ".join(markers) if markers else "none") + ".")


class Handler(BaseHTTPRequestHandler):
    def log_message(self, *args):
        pass

    def do_POST(self):
        length = int(self.headers.get("Content-Length") or 0)
        request = json.loads(self.rfile.read(length) or b"{}")
        with open(sys.argv[2] if len(sys.argv) > 2 else "/dev/null", "a") as log:
            log.write(json.dumps({"user_texts": [t[-200:] for t in texts(request)]}) + "\n")
        prose = answer(request)
        model = request.get("model") or "stub"
        if request.get("stream"):
            self.send_response(200)
            self.send_header("Content-Type", "text/event-stream")
            self.end_headers()

            def chunk(delta, finish=None):
                data = {"id": "stub", "object": "chat.completion.chunk", "created": 0, "model": model,
                        "choices": [{"index": 0, "delta": delta, "finish_reason": finish}]}
                self.wfile.write(b"data: " + json.dumps(data).encode() + b"\n\n")
                self.wfile.flush()

            try:
                chunk({"role": "assistant", "content": prose})
                chunk({}, "stop")
                self.wfile.write(b"data: " + json.dumps(
                    {"id": "stub", "object": "chat.completion.chunk", "created": 0, "model": model,
                     "choices": [], "usage": {"prompt_tokens": 900, "completion_tokens": 30,
                                              "total_tokens": 930}}).encode() + b"\n\n")
                self.wfile.write(b"data: [DONE]\n\n")
                self.wfile.flush()
            except BrokenPipeError:
                pass
            return
        payload = {"id": "stub", "object": "chat.completion", "created": 0, "model": model,
                   "choices": [{"index": 0, "message": {"role": "assistant", "content": prose},
                                "finish_reason": "stop"}],
                   "usage": {"prompt_tokens": 900, "completion_tokens": 30, "total_tokens": 930}}
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
