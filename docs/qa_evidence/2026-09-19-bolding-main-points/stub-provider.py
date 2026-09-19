#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Loopback-only OpenAI-compatible endpoint for the bolding-main-points screenshots (card #CVHT).

    python3 stub-provider.py 8807

Every turn answers with the same Markdown, streamed in small chunks so the pane renders it while
it arrives (the held-prefix path in MarkdownAnsi is exercised, not only whole-text rendering):

    **Done:**    -> the palette's magenta
    **Problem:** -> the palette's red
    **Need:**    -> the palette's blue
    **Bold**     -> plain bold, no role colour

No provider account is touched: the endpoint is 127.0.0.1 and the profile points at it.
"""
import json
import sys
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

REPLY = ("**Done:** the three labels are live.\n"
         "\n"
         "**Problem:** a plain **Bold** word stays plain bold.\n"
         "\n"
         "**Need:** nothing from you right now.\n")


def body(request):
    return {"role": "assistant", "content": REPLY}


class Handler(BaseHTTPRequestHandler):
    def log_message(self, *args):
        pass

    def do_POST(self):
        length = int(self.headers.get("Content-Length") or 0)
        request = json.loads(self.rfile.read(length) or b"{}")
        message = body(request)
        model = request.get("model") or "relay-qa-stub"
        if request.get("stream"):
            self.send_response(200)
            self.send_header("Content-Type", "text/event-stream")
            self.end_headers()
            # Chunks of four characters: the slowest stream the renderer must still get right.
            text = message["content"]
            for at in range(0, len(text), 4):
                delta = {"role": "assistant", "content": text[at:at + 4]}
                chunk = {"index": 0, "delta": delta, "finish_reason": None}
                data = {"id": "stub", "object": "chat.completion.chunk", "created": 0, "model": model,
                        "choices": [chunk]}
                self.wfile.write(b"data: " + json.dumps(data).encode() + b"\n\n")
            last = {"index": 0, "delta": {}, "finish_reason": "stop"}
            data = {"id": "stub", "object": "chat.completion.chunk", "created": 0, "model": model,
                    "choices": [last]}
            self.wfile.write(b"data: " + json.dumps(data).encode() + b"\n\n")
            self.wfile.write(b"data: [DONE]\n\n")
            return
        payload = {"id": "stub", "object": "chat.completion", "created": 0, "model": model,
                   "choices": [{"index": 0, "message": message, "finish_reason": "stop"}],
                   "usage": {"prompt_tokens": 1, "completion_tokens": 1, "total_tokens": 2}}
        raw = json.dumps(payload).encode()
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(raw)))
        self.end_headers()
        self.wfile.write(raw)


if __name__ == "__main__":
    ThreadingHTTPServer(("127.0.0.1", int(sys.argv[1])), Handler).serve_forever()
