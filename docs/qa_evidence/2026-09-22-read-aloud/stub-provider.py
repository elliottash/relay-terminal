#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Loopback-only OpenAI-compatible endpoint for the #MDA7 read-aloud drive (copied from #SQ3D's).

    python3 stub-provider.py 8822

Answers every request with one Markdown reply and no tool calls: long enough (several sentences,
a code block, a link and a list) that it is still being read when the drive takes its shots.
"""
import json
import sys
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

REPLY = ("## Result\n\nI read the **three** files you named and found the fault in `src/Pane.h`. "
         "See [the card](https://example.com/card) for the history.\n\n"
         "- The busy line was never refreshed after the turn ended.\n"
         "- The fix calls it once more, from the finished handler.\n\n"
         "```cpp\nrefreshBusyLine();\n```\n\n"
         "This is a longer closing paragraph so that the voice is still talking when the screenshot is "
         "taken, and then it goes on for another sentence or two. It should be stopped long before it "
         "ever reaches this last sentence, which nobody should hear.")


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

            for piece in (REPLY[:40], REPLY[40:]):
                time.sleep(0.2)
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
