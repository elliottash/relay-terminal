#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""A loopback-only OpenAI-compatible endpoint for the wrong-mode hints check.

The agent-mode hint only fires when the agent's own run_command of the submitted
command fails, so the stub behaves like a model that reproduces terminal commands:
turn 1 answers with a run_command tool call of `cd <tmp> && <the user's prompt>`,
turn 2 (after the tool result) gives a short final answer with no relay-run block.

    python3 stub-provider.py 8733
"""
import json
import re
import sys
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

SANDBOX = "/tmp/relay-qa-wrong-mode"


def body(request: dict) -> dict:
    used_tool = any(m.get("role") == "tool" for m in request.get("messages") or [])
    if used_tool:
        return {"role": "assistant", "content": "Ran it."}
    prompt = ""
    for m in reversed(request.get("messages") or []):
        if m.get("role") == "user" and m.get("content"):
            text = m["content"]
            if isinstance(text, list):  # content parts
                text = " ".join(p.get("text", "") for p in text if isinstance(p, dict))
            prompt = text.strip()
            break
    # Relay prepends a labelled context note to the user's turn (relay_core.agent.format_context).
    # A real model reads the note and runs the user's command, so the stub must likewise skip
    # past [End of Relay context] before taking the first line as the command to reproduce.
    END = "[End of Relay context]"
    if END in prompt:
        prompt = prompt.split(END, 1)[1].lstrip("\n")
    command = "cd {0} && {1}".format(SANDBOX, prompt.splitlines()[0] if prompt else "true")
    return {"role": "assistant", "content": None,
            "tool_calls": [{"id": "call_1", "type": "function",
                            "function": {"name": "run_command",
                                         "arguments": json.dumps({"command": command})}}]}


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
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 8733
    ThreadingHTTPServer(("127.0.0.1", port), Handler).serve_forever()
