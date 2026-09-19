#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Loopback-only OpenAI-compatible endpoint for the open-task-list screenshots (card #TKS9).

    python3 stub-provider.py 8805

Copied from the subagent-badge run (#YMSR) with the task scripts this card needs. What the first
user message says decides the turn:

  "tasks3"   -> update_todos with 3 items (T1 in_progress, T2/T3 pending), then answers.
                Three open tasks, no subagents: the strip full width, no overflow line.
  "tasks7"   -> update_todos with 7 items, T1-T4 completed, T5 in_progress, T6/T7 pending.
                The marginal task is at index 4 of 7, so the window is clamp(4-2, 0, 2) = 2:
                rows T3..T7 with T5 third and T1/T2 off the top. That is the proof the window
                slid — a marginal at index 2 would have given T1..T5 and proved nothing.
  "both7"    -> the same 7 items, then a background subagent on T5 and a second on T6, so the
                strip has two linked pairs to align across its two columns.
  "keys7"    -> the same 7 items and ONE background subagent, on T5: the driver then has a task
                with a subagent (Enter opens its tab) and plain, delegable ones below it (S).
  "SUBTASK"  -> (the subagent itself) waits 90 s, so it stays live while the driver shoots.
  anything else -> "Done." with no todo list at all: the strip must be absent.

Every main turn ends 4 s after its last tool call, so the tasks outlive the turn that wrote them.
"""
import json
import sys
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

# T1-T4 done, T5 running, T6-T7 still to come: seven tasks, the marginal one at index 4.
SEVEN = [("set up the sandbox", "completed"),
         ("read the protocol doc", "completed"),
         ("write the layout function", "completed"),
         ("unit-test the window", "completed"),
         ("paint the two columns", "in_progress"),
         ("wire the pane callbacks", "pending"),
         ("record the evidence", "pending")]

THREE = [("read the protocol doc", "in_progress"),
         ("write the layout function", "pending"),
         ("record the evidence", "pending")]


def call(name, arguments):
    return {"role": "assistant", "content": None,
            "tool_calls": [{"id": "call_%s_%d" % (name, int(time.time() * 1000) % 100000), "type": "function",
                            "function": {"name": name, "arguments": json.dumps(arguments)}}]}


def todos_call(items):
    return call("update_todos", {"items": [{"text": text, "status": status} for text, status in items]})


def agent_call(todo_id, description):
    return call("agent", {"description": description, "prompt": "SUBTASK: " + description,
                          "subagent_type": "general", "background": True, "todo_id": todo_id})


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
    if "tasks3" in text:
        if tools_used == 0:
            return todos_call(THREE)
        time.sleep(4)
        return {"role": "assistant", "content": "Three tasks listed."}
    if "both7" in text:
        if tools_used == 0:
            return todos_call(SEVEN)
        if tools_used == 1:
            return agent_call("T5", "paint the two columns")
        if tools_used == 2:
            return agent_call("T6", "wire the pane callbacks")
        time.sleep(4)
        return {"role": "assistant", "content": "Two agents on T5 and T6."}
    if "keys7" in text:
        if tools_used == 0:
            return todos_call(SEVEN)
        if tools_used == 1:
            return agent_call("T5", "paint the two columns")
        time.sleep(4)
        return {"role": "assistant", "content": "One agent on T5."}
    if "tasks7" in text:
        if tools_used == 0:
            return todos_call(SEVEN)
        time.sleep(4)
        return {"role": "assistant", "content": "Seven tasks listed."}
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
