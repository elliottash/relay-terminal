#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Loopback-only OpenAI-compatible endpoint for the `ask_user` screenshots (#MQ9C).

    python3 stub-provider.py 8799

A planner that asks before it plans, which is the whole card: the first model call answers with an
`ask_user` call carrying two questions (one single-choice with a recommendation, one multiple), the
next one — which only happens once the user has answered, because the tool blocks until then —
calls `write_plan` quoting the answers back, and the last replies in two sentences.
"""
import json
import sys
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

QUESTIONS = [
    {"header": "Scope",
     "question": "How far should the rename go?",
     "options": [
         {"label": "This file only", "description": "Leave every caller as it is.", "recommended": True},
         {"label": "The whole package", "description": "Rename the callers too, in one commit."},
         {"label": "Package and its tests", "description": "Also rewrite the fixtures that name it."}]},
    {"header": "Checks",
     "question": "Which checks should the plan run at the end?",
     "multiple": True,
     "options": [
         {"label": "ctest", "description": "The C++ suite."},
         {"label": "scripts/test.sh", "description": "Backend and Bash."},
         {"label": "A live pane", "description": "Drive the app under Xvfb."}]},
    # No options: an open question (owner, 2026-09-19). The pane takes whatever is typed.
    {"header": "Wording",
     "question": "What should the deprecation note on the old name say?"},
]


def call(name, arguments):
    return {"role": "assistant", "content": None,
            "tool_calls": [{"id": "call_" + name, "type": "function",
                            "function": {"name": name, "arguments": json.dumps(arguments)}}]}


def answers(request) -> str:
    """What came back from the pane, as the tool result put it."""
    for message in reversed(request.get("messages") or []):
        if message.get("role") == "tool" and "answers" in str(message.get("content") or ""):
            return str(message.get("content"))
    return ""


def body(request):
    tool_results = [m for m in request.get("messages") or [] if m.get("role") == "tool"]
    if not tool_results:
        return call("ask_user", {"questions": QUESTIONS})
    if len(tool_results) == 1:
        return call("write_plan", {
            "title": "Rename the parser entry point",
            "content": "## Goal\n\nRename `parse()` to `parseDocument()`.\n\n"
                       f"## What you told me\n\n```\n{answers(request)[:600]}\n```\n\n"
                       "## Steps\n\n1. Rename the definition.\n2. Fix the callers the answer covers.\n"
                       "3. Run the checks the answer named.\n"})
    return {"role": "assistant", "content": "The plan is written and follows what you chose. "
                                            "Open it to edit anything before it runs."}


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
                data = {"id": "stub", "object": "chat.completion.chunk", "created": 0, "model": model,
                        "choices": [chunk]}
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
