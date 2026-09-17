#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""A loopback-only OpenAI-compatible endpoint that makes one noisy tool call.

Collapsed tool output (SWITCHBOARD-DESIGN.md 4.3) can only be seen with a real turn: a real worker,
a real run_command, a real stream of tool_output events. This answers /v1/chat/completions without
touching the network or anybody's API key.

Turn 1 -> a run_command tool call that prints 200 lines.
Turn 2 (the model sees the tool result) -> a short final answer.

    python3 stub-provider.py 8732
"""
import json
import sys
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

COMMAND = "seq 1 200"


def body(request: dict) -> dict:
    used_tool = any(m.get("role") == "tool" for m in request.get("messages") or [])
    if used_tool:
        return {"role": "assistant", "content": "Counted to 200. The output is in the turn pane."}
    return {"role": "assistant", "content": None,
            "tool_calls": [{"id": "call_1", "type": "function",
                            "function": {"name": "run_command",
                                         "arguments": json.dumps({"command": COMMAND})}}]}


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
            # A streamed tool call is addressed by its index in the delta; without it the worker
            # rejects the chunk with "Invalid tool-call index."
            delta = dict(message)
            if delta.get("tool_calls"):
                delta["tool_calls"] = [dict(call, index=i) for i, call in enumerate(delta["tool_calls"])]
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
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 8732
    ThreadingHTTPServer(("127.0.0.1", port), Handler).serve_forever()
