#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Loopback-only OpenAI-compatible endpoint for the website screenshots.

The landing page shows a real agent turn: a real worker, a real run_command, a real write_file
diff. This answers /v1/chat/completions on 127.0.0.1 without touching the network or anybody's
API key, so the screenshots can be taken with no provider account.

    python3 stub-provider.py 8791

Turn 1 -> run_command "make"          (the build really fails in the demo workspace)
Turn 2 -> write_file greet.c          (the fix, shown as a diff)
Turn 3 -> a short final answer.
"""
import json
import sys
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

FIXED = '''#include <stdio.h>

int main(void)
{
    printf("hello from relay\\n");
    return 0;
}
'''

ANSWER = ("greet.c was missing the semicolon after the printf call, so cc stopped there. "
          "I added it; make now builds greet cleanly.")


def call(name: str, arguments: dict) -> dict:
    return {"role": "assistant", "content": None,
            "tool_calls": [{"id": "call_" + name, "type": "function",
                            "function": {"name": name, "arguments": json.dumps(arguments)}}]}


def body(request: dict) -> dict:
    tools_used = sum(1 for m in request.get("messages") or [] if m.get("role") == "tool")
    if tools_used == 0:
        return call("run_command", {"command": "make"})
    if tools_used == 1:
        return call("write_file", {"path": "greet.c", "content": FIXED})
    return {"role": "assistant", "content": ANSWER}


class Handler(BaseHTTPRequestHandler):
    def do_POST(self):
        length = int(self.headers.get("Content-Length") or 0)
        request = json.loads(self.rfile.read(length) or b"{}")
        message = body(request)
        finish = "tool_calls" if message.get("tool_calls") else "stop"
        payload = {"id": "stub", "object": "chat.completion", "created": 0,
                   "model": request.get("model") or "relay-qa-stub",
                   "choices": [{"index": 0, "message": message, "finish_reason": finish}],
                   "usage": {"prompt_tokens": 1, "completion_tokens": 1, "total_tokens": 2}}
        if request.get("stream"):
            self.send_response(200)
            self.send_header("Content-Type", "text/event-stream")
            self.end_headers()
            delta = dict(message)
            if delta.get("tool_calls"):
                delta["tool_calls"] = [dict(c, index=i) for i, c in enumerate(delta["tool_calls"])]
            chunk = {"id": "stub", "object": "chat.completion.chunk", "created": 0,
                     "model": payload["model"],
                     "choices": [{"index": 0, "delta": delta, "finish_reason": None}]}
            self.wfile.write(b"data: " + json.dumps(chunk).encode() + b"\n\n")
            done = {"id": "stub", "object": "chat.completion.chunk", "created": 0,
                    "model": payload["model"],
                    "choices": [{"index": 0, "delta": {}, "finish_reason": finish}]}
            self.wfile.write(b"data: " + json.dumps(done).encode() + b"\n\n")
            self.wfile.write(b"data: [DONE]\n\n")
            return
        raw = json.dumps(payload).encode()
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(raw)))
        self.end_headers()
        self.wfile.write(raw)

    def log_message(self, *args):
        pass


if __name__ == "__main__":
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 8791
    ThreadingHTTPServer(("127.0.0.1", port), Handler).serve_forever()
