#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Loopback-only OpenAI-compatible endpoint for the subagent-badge screenshots (card #YMSR).

    python3 stub-provider.py 8803

Copied from the pane-live-state run (#V8KT) with the spawn modes this card needs. What the first
user message says decides the turn:

  "spawn"      -> starts ONE background subagent, then answers    (badge 1, state Subagents)
  "spawn2"     -> starts TWO, one per tool round, then answers    (badge 2, state Subagents)
  "spawnbusy"  -> starts ONE, then keeps working for 60 s         (badge 1, state Working)
  "SUBTASK"    -> (the subagent itself) waits 90 s, so it stays live while the driver shoots
  anything else-> "Done."                                          (no badge at all)
Every main turn takes 4 s, so it ends while the driver is on another tab.
"""
import json
import sys
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer


def call(name, arguments, n=1):
    return {"role": "assistant", "content": None,
            "tool_calls": [{"id": "call_%s_%d" % (name, i), "type": "function",
                            "function": {"name": name, "arguments": json.dumps(arguments)}}
                           for i in range(n)]}


def agent_call(n):
    return call("agent", {"description": "scan the project", "prompt": "SUBTASK: scan the project",
                          "subagent_type": "general", "background": True}, n)


def first_user(request):
    for m in request.get("messages") or []:
        if m.get("role") == "user":
            content = m.get("content")
            if isinstance(content, list):
                content = " ".join(part.get("text", "") for part in content if isinstance(part, dict))
            return content or ""
    return ""


def body(request):
    text = first_user(request)
    tools_used = sum(1 for m in request.get("messages") or [] if m.get("role") == "tool")
    if "SUBTASK" in text:
        time.sleep(90)
        return {"role": "assistant", "content": "Scanned."}
    if "spawn2" in text:
        if tools_used == 0:
            return agent_call(1)
        if tools_used == 1:
            return agent_call(1)
        time.sleep(4)
        return {"role": "assistant", "content": "Started two agents."}
    if "spawnbusy" in text:
        if tools_used == 0:
            return agent_call(1)
        time.sleep(60)   # still working, with one agent live: badge beside "Relaying…"
        return {"role": "assistant", "content": "Done."}
    if "spawn" in text:
        if tools_used == 0:
            return agent_call(1)
        time.sleep(4)
        return {"role": "assistant", "content": "Started one agent."}
    time.sleep(4)
    return {"role": "assistant", "content": "Done."}


class Handler(BaseHTTPRequestHandler):
    def log_message(self, *args):
        pass

    def do_POST(self):
        length = int(self.headers.get("Content-Length") or 0)
        request = json.loads(self.rfile.read(length) or b"{}")
        message = body(request)
        finish = "tool_calls" if message.get("tool_calls") else "stop"
        model = request.get("model") or "relay-qa-stub"
        if request.get("stream"):
            self.send_response(200)
            self.send_header("Content-Type", "text/event-stream")
            self.end_headers()
            delta = dict(message)
            if delta.get("tool_calls"):
                delta["tool_calls"] = [dict(c, index=i) for i, c in enumerate(delta["tool_calls"])]
            for chunk in ({"index": 0, "delta": delta, "finish_reason": None},
                          {"index": 0, "delta": {}, "finish_reason": finish}):
                data = {"id": "stub", "object": "chat.completion.chunk", "created": 0, "model": model, "choices": [chunk]}
                self.wfile.write(b"data: " + json.dumps(data).encode() + b"\n\n")
            self.wfile.write(b"data: [DONE]\n\n")
            return
        payload = {"id": "stub", "object": "chat.completion", "created": 0, "model": model,
                   "choices": [{"index": 0, "message": message, "finish_reason": finish}],
                   "usage": {"prompt_tokens": 1, "completion_tokens": 1, "total_tokens": 2}}
        raw = json.dumps(payload).encode()
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(raw)))
        self.end_headers()
        self.wfile.write(raw)


if __name__ == "__main__":
    ThreadingHTTPServer(("127.0.0.1", int(sys.argv[1])), Handler).serve_forever()
