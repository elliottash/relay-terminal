#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Loopback-only OpenAI-compatible endpoint for the task-fold screenshots (card #BDXG).

    python3 stub-provider.py 8816

Every turn is the same two model steps: an `update_todos` tool call, then a one-line answer
once the tool result comes back. Which list it sends depends on how many `update_todos`
results the conversation already carries, so two asks in one session leave two rows with two
different lists — which is what "a row from an earlier turn unfolds to that call's list, not
the current one" needs.

The statuses cover every glyph the fold draws: pending, in_progress, completed, cancelled,
deferred, blocked.
"""
import json
import os
import sys
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

#: Set RELAY_QA_DUMP to a path to record every conversation the app sends (diagnosing a scene).
DUMP = os.environ.get("RELAY_QA_DUMP")

FIRST = [
    {"text": "read the card and the code it names", "status": "completed"},
    {"text": "make the row a fold and draw the list", "status": "in_progress"},
    {"text": "run ctest and the backend suite", "status": "pending"},
    {"text": "ask the owner about the icons", "status": "deferred", "note": "not needed yet"},
    {"text": "wait on the other session's header", "status": "blocked", "note": "their file"},
    {"text": "the old plan", "status": "cancelled", "note": "superseded"},
]
SECOND = [
    {"text": "read the card and the code it names", "status": "completed"},
    {"text": "make the row a fold and draw the list", "status": "completed"},
    {"text": "run ctest and the backend suite", "status": "in_progress"},
    {"text": "write the evidence up", "status": "pending"},
]
ANSWERS = ["The list is up: one in progress, three still open.",
           "Updated again — the fold is written, the tests are running."]


def done_lists(request):
    """How many update_todos results this conversation already carries."""
    return sum(1 for m in request.get("messages") or []
               if m.get("role") == "tool" and "items" in str(m.get("content") or ""))


def tool_call(items):
    return {"tool_calls": [{"index": 0, "id": "call_%d" % len(items), "type": "function",
                            "function": {"name": "update_todos",
                                         "arguments": json.dumps({"items": items})}}]}


def reply(request):
    """(delta, finish_reason) for this step.

    The last message decides: a tool result means the list has just been written, so answer;
    anything else is a fresh ask, so write the list this ask should leave behind.
    """
    messages = request.get("messages") or []
    lists = done_lists(request)
    last = messages[-1] if messages else {}
    text = str(last.get("content") or "")
    # One list per ask, so a turn leaves exactly one row. A fresh ask is the only thing that makes
    # this stub write a list: a tool result means the list has just been written, and Relay's own
    # nudges (the stale-list reminder, the end-of-turn completion check) are user messages too but
    # must not start another one. The first ask of a session carries Relay's context block ahead of
    # it, which also opens "[Relay", so the nudges are matched by their own wording.
    nudge = "[Relay reminder:" in text or "call update_todos to mark" in text
    if last.get("role") == "user" and not nudge:
        return tool_call(SECOND if lists else FIRST), "tool_calls"
    return {"content": ANSWERS[min(max(lists, 1), len(ANSWERS)) - 1]}, "stop"


class Handler(BaseHTTPRequestHandler):
    def log_message(self, *args):
        pass

    def do_POST(self):
        length = int(self.headers.get("Content-Length") or 0)
        request = json.loads(self.rfile.read(length) or b"{}")
        if DUMP:
            with open(DUMP, "a") as handle:
                handle.write(json.dumps(request.get("messages") or [], indent=1) + "\n=====\n")
        delta, finish = reply(request)
        model = request.get("model") or "relay-qa-stub"
        if request.get("stream"):
            self.send_response(200)
            self.send_header("Content-Type", "text/event-stream")
            self.end_headers()
            for part in (delta,):
                data = {"id": "stub", "object": "chat.completion.chunk", "created": 0,
                        "model": model,
                        "choices": [{"index": 0, "delta": part, "finish_reason": None}]}
                self.wfile.write(b"data: " + json.dumps(data).encode() + b"\n\n")
            end = {"id": "stub", "object": "chat.completion.chunk", "created": 0, "model": model,
                   "choices": [{"index": 0, "delta": {}, "finish_reason": finish}]}
            self.wfile.write(b"data: " + json.dumps(end).encode() + b"\n\n")
            self.wfile.write(b"data: [DONE]\n\n")
            return
        message = {"role": "assistant", "content": delta.get("content", "")}
        if "tool_calls" in delta:
            message["tool_calls"] = delta["tool_calls"]
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
