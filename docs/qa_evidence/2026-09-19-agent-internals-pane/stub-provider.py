#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Loopback-only OpenAI-compatible endpoint for the agent internals pane shots (card #QT8C).

    python3 stub-provider.py 8831

Every ask is the same turn, which is what the pane has to show and the terminal has to get
back: a streamed reasoning block (~30 lines over ~5 s), then a `run_command` (which sleeps a
few seconds so a scene can close the pane while it runs), then two `read_file` calls that the
terminal merges into one "read 2 files" row, then a one-line answer. Which step this is comes
from the last message: a fresh ask starts the reasoning, a tool result moves to the next
call, and the answer follows the last one.

The reasoning goes out as `reasoning` deltas (which the backend's _reasoning_text reads, like
`reasoning_content`); the tool calls as one `tool_calls` delta; the answer as `content`.
"""
import json
import sys
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

SLEEP = 4   # seconds the run_command takes: long enough to close the pane while it runs


def reasoning():
    yield "## What the ask needs\n\n"
    for n in range(30):
        yield f"{n + 1}. step {n + 1}: check the **gauge**, the feed and the `stable` module\n"
    yield "\nSo: run the check, read the two files, answer.\n"


def chunks(parts, delay, key):
    for part in parts:
        time.sleep(delay)
        yield {key: part}


def tool_call(name, args, index, serial):
    # Ids unique across the session, as a real provider's are: the same id in two turns would
    # make the newer row answer for the older one in any surface that keys rows by it.
    return {"tool_calls": [{"index": 0, "id": f"call_{serial}_{name}_{index}", "type": "function",
                            "function": {"name": name, "arguments": json.dumps(args)}}]}


def steps_done(messages):
    """How many tool results have come back since the last real ask."""
    count = 0
    for m in messages:
        if m.get("role") == "user":
            text = str(m.get("content") or "")
            if "[Relay reminder:" not in text and "call update_todos" not in text:
                count = 0
        elif m.get("role") == "tool":
            count += 1
    return count


def turn(request):
    """Yield (delta, finish_reason) pairs for this step."""
    messages = request.get("messages") or []
    done = steps_done(messages)
    serial = len(messages)
    last = messages[-1] if messages else {}
    if last.get("role") == "user" and done == 0:
        for part in chunks(reasoning(), 0.15, "reasoning"):
            yield part, None
        yield tool_call("run_command", {"command": f"sleep {SLEEP}; printf 'gauge ok\\nfeed ok\\nstable ok\\n'"}, 1, serial), "tool_calls"
        return
    if done == 1:
        yield tool_call("read_file", {"path": "README.md"}, 2, serial), "tool_calls"
        return
    if done == 2:
        yield tool_call("read_file", {"path": "notes.md"}, 3, serial), "tool_calls"
        return
    yield {"content": "Done: the check passed and both files are read."}, "stop"


class Handler(BaseHTTPRequestHandler):
    def log_message(self, *args):
        pass

    def do_POST(self):
        length = int(self.headers.get("Content-Length") or 0)
        request = json.loads(self.rfile.read(length) or b"{}")
        model = request.get("model") or "relay-qa-stub"
        if request.get("stream"):
            self.send_response(200)
            self.send_header("Content-Type", "text/event-stream")
            self.end_headers()
            finish = "stop"
            for delta, reason in turn(request):
                data = {"id": "stub", "object": "chat.completion.chunk", "created": 0, "model": model,
                        "choices": [{"index": 0, "delta": delta, "finish_reason": None}]}
                self.wfile.write(b"data: " + json.dumps(data).encode() + b"\n\n")
                if reason:
                    finish = reason
            end = {"id": "stub", "object": "chat.completion.chunk", "created": 0, "model": model,
                   "choices": [{"index": 0, "delta": {}, "finish_reason": finish}]}
            self.wfile.write(b"data: " + json.dumps(end).encode() + b"\n\n")
            self.wfile.write(b"data: [DONE]\n\n")
            return
        parts = list(turn(request))
        message = {"role": "assistant",
                   "reasoning_content": "".join(d.get("reasoning", "") for d, _ in parts),
                   "content": "".join(d.get("content", "") for d, _ in parts)}
        finish = "stop"
        for delta, reason in parts:
            if "tool_calls" in delta:
                message["tool_calls"] = delta["tool_calls"]
            if reason:
                finish = reason
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
