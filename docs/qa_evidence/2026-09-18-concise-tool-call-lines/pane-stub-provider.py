#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""A loopback-only OpenAI-compatible endpoint that drives one turn through every tool-call line.

The concise tool-call lines (#TK9C, protocol section 23) can only be seen with a real turn: a real
worker, real `label`s, a real `diff` on the result, real fold requests. This answers
/v1/chat/completions without touching the network or anybody's API key.

One step per model call, in this order, so the pane draws one of each kind of row:

    0  run_command   a long python heredoc      -> "ran python3 script"
    1  run_command   a short command            -> "ran ls -la" (shown whole)
    2  run_command   one that exits 1           -> the failure row, in the error ink
    3  read_file x4  four consecutive reads     -> one merged row, "read 4 files"
    4  edit_file     three changed lines        -> the inline diff, under the row
    5  write_file    a file that is not there   -> "wrote new.py · new"
    6  edit_file     sixty changed lines        -> the diff pane
    7  (no tools)    a short final answer

    python3 pane-stub-provider.py 8733
"""
import json
import sys
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

HEREDOC = """python3 - <<'PY'
total = 0
for row in range(14):
    total += row
    print(f"row {row} of the script's output")
print(total)
PY"""

BIG_OLD = "\n".join(f"line {n}" for n in range(10, 40))
BIG_NEW = "\n".join(f"line {n} (edited by the stub)" for n in range(10, 40))


def call(index, name, args):
    return {"id": f"call_{index}", "type": "function",
            "function": {"name": name, "arguments": json.dumps(args)}}


STEPS = [
    [call(1, "run_command", {"command": HEREDOC})],
    [call(2, "run_command", {"command": "ls -la"})],
    [call(3, "run_command", {"command": "grep -rn 'not-in-this-tree' notes.txt"})],
    [call(4, "read_file", {"path": "alpha.py"}),
     call(5, "read_file", {"path": "beta.py"}),
     call(6, "read_file", {"path": "gamma.py"}),
     call(7, "read_file", {"path": "delta.py"})],
    [call(8, "edit_file", {"path": "alpha.py", "old_string": "SMALL = 1",
                           "new_string": "SMALL = 2\nEXTRA = 3"})],
    [call(9, "write_file", {"path": "brand_new.py", "content": "print('a file that was not here')\n"})],
    [call(10, "edit_file", {"path": "big.txt", "old_string": BIG_OLD, "new_string": BIG_NEW})],
]


def body(request: dict) -> dict:
    # Which step we are on: one per assistant message that asked for tools.
    step = sum(1 for m in request.get("messages") or []
               if m.get("role") == "assistant" and m.get("tool_calls"))
    if step >= len(STEPS):
        return {"role": "assistant", "content": "Done. Every line above folds its own detail open."}
    return {"role": "assistant", "content": None, "tool_calls": STEPS[step]}


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
                delta["tool_calls"] = [dict(c, index=i) for i, c in enumerate(delta["tool_calls"])]
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

    def do_GET(self):
        raw = json.dumps({"data": [{"id": "relay-qa-stub"}]}).encode()
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
