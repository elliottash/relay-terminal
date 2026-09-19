#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Loopback-only OpenAI-compatible endpoint for the Ctrl+Enter queue run (#N8VK).

    python3 stub-provider.py 8823

Copied from the thinking-fold stub (issue T8CN, itself from #YMSR) with this run's changes:
every chat request is logged to $RELAY_STUB_LOG (epoch seconds and the first user message) so
the evidence can show *when* the worker's ask arrived, next to when the driver pressed the keys.
The reply is a short streamed answer; a prompt containing "slow" streams for ~6 s instead, for
the busy-interrupt scene.
"""
import json
import os
import sys
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer


def turns(text):
    if "slow" in text:
        for part in ("Pondering. ", "Still pondering. ", "Nearly there. ", "Almost done. ",
                     "One more look. ", "Wrapping up.\n"):
            time.sleep(1.0)
            yield {"content": part}
        yield {"content": "Slow turn finished."}
        return
    time.sleep(0.3)
    yield {"content": "Received. "}
    yield {"content": "Done."}


def first_user(request):
    for m in request.get("messages") or []:
        if m.get("role") == "user":
            content = m.get("content")
            if isinstance(content, list):
                content = " ".join(p.get("text", "") for p in content if isinstance(p, dict))
            return content or ""
    return ""


class Handler(BaseHTTPRequestHandler):
    def log_message(self, *args):
        pass

    def do_POST(self):
        length = int(self.headers.get("Content-Length") or 0)
        request = json.loads(self.rfile.read(length) or b"{}")
        text = first_user(request)
        log = os.environ.get("RELAY_STUB_LOG")
        if log:
            with open(log, "a", encoding="utf-8") as f:
                f.write(f"{time.time():.3f} ask {text[:70]!r}\n")
        parts = turns(text)   # lazy: the sleeps pace the stream, not the setup
        model = request.get("model") or "relay-qa-stub"
        if request.get("stream"):
            self.send_response(200)
            self.send_header("Content-Type", "text/event-stream")
            self.end_headers()
            for part in parts:
                data = {"id": "stub", "object": "chat.completion.chunk", "created": 0,
                        "model": model,
                        "choices": [{"index": 0, "delta": part, "finish_reason": None}]}
                self.wfile.write(b"data: " + json.dumps(data).encode() + b"\n\n")
            done = {"id": "stub", "object": "chat.completion.chunk", "created": 0, "model": model,
                    "choices": [{"index": 0, "delta": {}, "finish_reason": "stop"}]}
            self.wfile.write(b"data: " + json.dumps(done).encode() + b"\n\n")
            self.wfile.write(b"data: [DONE]\n\n")
            return
        message = {"role": "assistant", "content": ""}
        message["content"] = "".join(p.get("content", "") for p in parts)
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
