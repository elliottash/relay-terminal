#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Loopback-only OpenAI-compatible endpoint for the fold's "open in pane" link (#7FD3).

    python3 stub-provider.py 8827

The wrong-mode-hints stub (2026-09-17) with this issue's two scenes; what the user's first
message says picks the turn:

  "seq" -> one run_command tool call of `seq 1 40`: the pane draws the fold row
           "ran seq 1 40 · 40 lines · exit 0", whose foot link "open in pane" is the
           one that always answered "detail not available" before 7241a40.
  "six" -> six read_file calls (src/a.py .. src/f.py) in one reply: the pane merges
           them into a "read 6 files" row, whose fold must offer no "open in pane".

Any request that already carries a tool result gets a short final answer, ending the turn.
"""
import json
import sys
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer


def body(request: dict) -> dict:
    if any(m.get("role") == "tool" for m in request.get("messages") or []):
        return {"role": "assistant", "content": "Done."}
    prompt = ""
    for m in reversed(request.get("messages") or []):
        if m.get("role") == "user" and m.get("content"):
            text = m["content"]
            if isinstance(text, list):  # content parts
                text = " ".join(p.get("text", "") for p in text if isinstance(p, dict))
            prompt = text.strip()
            break
    # Relay prepends a labelled context note to the user's turn (relay_core.agent.format_context);
    # a real model reads past it, so the stub must too.
    END = "[End of Relay context]"
    if END in prompt:
        prompt = prompt.split(END, 1)[1].lstrip("\n")
    if "seq" in prompt:
        return {"role": "assistant", "content": None,
                "tool_calls": [{"id": "call_1", "type": "function",
                                "function": {"name": "run_command",
                                             "arguments": json.dumps({"command": "seq 1 40"})}}]}
    if "six" in prompt:
        names = ["a.py", "b.py", "c.py", "d.py", "e.py", "f.py"]
        return {"role": "assistant", "content": None,
                "tool_calls": [{"id": f"call_{i + 1}", "type": "function",
                                "function": {"name": "read_file",
                                             "arguments": json.dumps({"path": f"src/{n}"})}}
                               for i, n in enumerate(names)]}
    return {"role": "assistant", "content": "Done."}


class Handler(BaseHTTPRequestHandler):
    def do_POST(self):
        length = int(self.headers.get("Content-Length") or 0)
        request = json.loads(self.rfile.read(length) or b"{}")
        message = body(request)
        print(f"request: stream={request.get('stream')} tools={len(request.get('tools') or [])} "
              f"messages={[m.get('role') for m in request.get('messages') or []]} -> "
              f"{'tool_calls:' + str(len(message['tool_calls'])) if message.get('tool_calls') else 'answer'}",
              flush=True)
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
                # One delta carrying every call whole: the backend accumulates by index.
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
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 8827
    ThreadingHTTPServer(("127.0.0.1", port), Handler).serve_forever()
