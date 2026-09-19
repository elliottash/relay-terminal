#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Loopback-only OpenAI-compatible endpoint for the #HQ2B left/normal-weight screenshots.

    python3 stub-provider.py 8805

Copied from the 2026-09-19 relaying-status-language run (#4E13); what the first user message
says decides the turn:
  "slow"    -> waits 60 s, then answers                     (working: "Relaying thinking…")
  "spawn"   -> starts a background subagent, then answers   (subagents: "Relaying waiting…")
  "stuck"   -> calls ask_user and never answers past it     (needs you: amber "waiting for
                                                              your answer…" while the card is up)
  "longrun" -> calls run_command with a long-running shell  (working: "Relaying running …",
                                                              a line long enough to elide in a
                                                              narrow pane)
  "SUBTASK" -> (the subagent itself) waits 90 s
Every ordinary turn takes 4 s, so it ends while the driver is shooting.
"""
import json
import sys
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer


def call(name, arguments):
    return {"role": "assistant", "content": None,
            "tool_calls": [{"id": "call_" + name, "type": "function",
                            "function": {"name": name, "arguments": json.dumps(arguments)}}]}


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
    if "spawn" in text and tools_used == 0:
        return call("agent", {"description": "scan the project", "prompt": "SUBTASK: scan the project",
                              "subagent_type": "general", "background": True})
    if "stuck" in text and tools_used == 0:
        # The card is the point (#MQ9C): the worker blocks on the answer, the turn clock keeps
        # ticking, and the busy line above the prompt goes amber "waiting for your answer…".
        time.sleep(4)
        return call("ask_user", {"questions": [{"header": "Direction",
                                                "question": "Should the busy line stay left-aligned?"}]}
                    )
    if "longrun" in text and tools_used == 0:
        # A command long enough (in its label, capped at 40 chars by tool_labels) that the busy
        # line elides from the middle in a pane ~420 px wide, and long enough running (120 s)
        # to be captured. The padding keeps `command_label` from shrinking to "sleep 120".
        return call("run_command", {"command": "sleep 120 && echo padding-padding-padding-padding"})
    if "slow" in text:
        time.sleep(60)
    elif tools_used == 0:
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
