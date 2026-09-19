#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Loopback-only OpenAI-compatible endpoint that records the tool list of every turn (#JN7X).

    python3 stub-provider.py 8811 /path/to/tools.log

Every request is answered "Done." after a moment, and one line is appended to the log:

    turn=<first user message> tools=<comma-separated tool names> board_tools=<n> policy=<yes|no>

That is the evidence for "the pane's agent has no board tools before the tab is attached and has
them afterwards, without a new conversation": the tool list and the Switchboard block of the
system prompt are what the worker actually sent to the model, and `messages=<n>` shows the
conversation growing rather than restarting.
"""
import json
import sys
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

LOG = sys.argv[2] if len(sys.argv) > 2 else "/dev/stderr"


def first_user(request):
    for m in request.get("messages") or []:
        if m.get("role") == "user":
            content = m.get("content")
            if isinstance(content, list):
                content = " ".join(p.get("text", "") for p in content if isinstance(p, dict))
            return (content or "").strip().splitlines()[0][:60]
    return ""


def system_text(request):
    for m in request.get("messages") or []:
        if m.get("role") == "system":
            content = m.get("content")
            if isinstance(content, list):
                content = " ".join(p.get("text", "") for p in content if isinstance(p, dict))
            return content or ""
    return ""


def record(request):
    names = sorted(t.get("function", {}).get("name", "?") for t in (request.get("tools") or []))
    board = [n for n in names if n.startswith("board_")]
    system = system_text(request)
    with open(LOG, "a", encoding="utf-8") as handle:
        handle.write("turn=%-24s messages=%-3d board_tools=%d [%s] switchboard_prompt=%s all=%s\n"
                     % (first_user(request), len(request.get("messages") or []), len(board),
                        ",".join(board) or "-",
                        "yes" if "Switchboard" in system else "no", ",".join(names)))


class Handler(BaseHTTPRequestHandler):
    def log_message(self, *args):
        pass

    def do_POST(self):
        length = int(self.headers.get("Content-Length") or 0)
        request = json.loads(self.rfile.read(length) or b"{}")
        record(request)
        time.sleep(1)
        message = {"role": "assistant", "content": "Done."}
        model = request.get("model") or "relay-qa-stub"
        if request.get("stream"):
            self.send_response(200)
            self.send_header("Content-Type", "text/event-stream")
            self.end_headers()
            for chunk in ({"index": 0, "delta": message, "finish_reason": None},
                          {"index": 0, "delta": {}, "finish_reason": "stop"}):
                data = {"id": "stub", "object": "chat.completion.chunk", "created": 0,
                        "model": model, "choices": [chunk]}
                self.wfile.write(b"data: " + json.dumps(data).encode() + b"\n\n")
            self.wfile.write(b"data: [DONE]\n\n")
            return
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
