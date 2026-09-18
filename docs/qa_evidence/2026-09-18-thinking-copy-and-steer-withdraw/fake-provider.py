#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""A scripted, streaming OpenAI-compatible server for the thinking-copy and steer-withdraw runs.

Relay sees it as a local model endpoint, so no key and no credits. With tools in the request:

  first step   streams reasoning_content slowly (a numbered sentence every 0.4 s, about 20 s),
               then calls run_command `sleep 30; echo done` - a long wait at the next tool call
  after a tool streams a short reasoning and answers "Finished."
  no tools     a short summary (titles and other side calls)

Every request is logged as one JSON line with the start of each user message the model was sent,
which is how the run proves a withdrawn steer never reached the model.
Usage: fake-provider.py <port> <log file>
"""
import json
import sys
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

PORT, LOG = int(sys.argv[1]), sys.argv[2]


def chunk(delta, finish=None):
    return {"id": "fake", "object": "chat.completion.chunk", "model": "fake",
            "choices": [{"index": 0, "delta": delta, "finish_reason": finish}]}


class Handler(BaseHTTPRequestHandler):
    def do_GET(self):
        data = json.dumps({"object": "list", "data": [{"id": "fake", "object": "model"}]}).encode()
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def event(self, obj):
        self.wfile.write(b"data: " + json.dumps(obj).encode() + b"\n\n")
        self.wfile.flush()

    def do_POST(self):
        body = json.loads(self.rfile.read(int(self.headers.get("Content-Length", 0))) or b"{}")
        messages = body.get("messages", [])
        users = [m.get("content") for m in messages if m.get("role") == "user"]
        with open(LOG, "a", encoding="utf-8") as log:
            log.write(json.dumps({"tools": bool(body.get("tools")), "messages": len(messages),
                                  "last_role": messages[-1].get("role") if messages else None,
                                  "users": [(u if isinstance(u, str) else json.dumps(u))[-60:] for u in users]}) + "\n")
        self.send_response(200)
        self.send_header("Content-Type", "text/event-stream")
        self.end_headers()
        if not body.get("tools"):
            self.event(chunk({"role": "assistant", "content": "Thinking copy test"}))
            self.event(chunk({}, "stop"))
            self.wfile.write(b"data: [DONE]\n\n")
            return
        if messages and messages[-1].get("role") == "tool":
            self.event(chunk({"role": "assistant", "reasoning_content": "The command finished. "}))
            self.event(chunk({"content": "Finished."}))
            self.event(chunk({}, "stop"))
            self.wfile.write(b"data: [DONE]\n\n")
            return
        self.event(chunk({"role": "assistant", "reasoning_content": ""}))
        for n in range(1, 51):
            self.event(chunk({"reasoning_content": f"Reasoning sentence number {n} about the plan. "}))
            time.sleep(0.4)
        call = {"index": 0, "id": "call_1", "type": "function",
                "function": {"name": "run_command", "arguments": json.dumps({"command": "sleep 30; echo done"})}}
        self.event(chunk({"tool_calls": [call]}))
        self.event(chunk({}, "tool_calls"))
        self.wfile.write(b"data: [DONE]\n\n")

    def log_message(self, *args):
        pass


ThreadingHTTPServer(("127.0.0.1", PORT), Handler).serve_forever()
