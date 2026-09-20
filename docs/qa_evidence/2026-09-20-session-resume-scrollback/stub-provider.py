#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Loopback-only OpenAI-compatible endpoint for the #0TJ9 resume run.

    python3 stub-provider.py 8823

Two scenes, each keyed on a word in the prompt *typed at the box* — the worker appends
user-role messages of its own at the end of a turn (the completion check), and keying on the
last user message would pick one of those instead:

  "alpha"  a reply whose every line carries ALPHA-MARKER
  "bravo"  the same with BRAVO-MARKER

That is all this run needs: the markers say which conversation a block of replayed terminal
text belongs to, so a file holding one and not the other is the assertion. No tool calls.
"""
import json
import sys
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

# Long enough to fill the pane several times over: the card's symptom was "i couldnt scroll
# back", so a resumed pane has to have more than a screen of text to page into.
SCENES = {
    "alpha": "\n".join(f"- ALPHA-MARKER line {n:02d} of sixty" for n in range(1, 61)),
    "bravo": "\n".join(f"- BRAVO-MARKER line {n:02d} of sixty" for n in range(1, 61)),
}


def scene(request):
    text = ""
    for message in request.get("messages") or []:
        if message.get("role") != "user":
            continue
        content = message.get("content")
        if isinstance(content, list):
            content = " ".join(p.get("text", "") for p in content if isinstance(p, dict))
        content = content or ""
        if any(key in content for key in SCENES):
            text = content
    for key, reply in SCENES.items():
        if key in text:
            return reply
    return "Nothing to say."


def pieces(text):
    words = text.split(" ")
    third = max(1, len(words) // 3)
    return [" ".join(words[:third]) + " ", " ".join(words[third:2 * third]) + " ",
            " ".join(words[2 * third:])]


class Handler(BaseHTTPRequestHandler):
    def log_message(self, *args):
        pass

    def do_POST(self):
        length = int(self.headers.get("Content-Length") or 0)
        request = json.loads(self.rfile.read(length) or b"{}")
        prose = scene(request)
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

            for piece in pieces(prose):
                time.sleep(0.2)
                chunk({"role": "assistant", "content": piece})
            chunk({}, "stop")
            self.wfile.write(b"data: [DONE]\n\n")
            return
        payload = {"id": "stub", "object": "chat.completion", "created": 0, "model": model,
                   "choices": [{"index": 0, "message": {"role": "assistant", "content": prose},
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
