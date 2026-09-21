#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Loopback-only OpenAI-compatible endpoint for the #AGNT step 1 before/after run.

    python3 stub-provider.py 8837

Nothing here calls a provider: the profile points a local model endpoint at this process, so the
terminal pane's agent is this script.  A scene is picked by a **keyword in a user message**, not
by the last message, because the worker appends a user-role message of its own at the end of a
turn and that would take the scene away from the prompt that was typed (the QA-stub gotcha the
#FEJQ run recorded).  Within a scene the step is how many assistant messages with tool calls have
been sent since the message that picked it.

The scenes are the six things the terminal pane must draw exactly as it did before the agent
console was extracted:

  "explain the fold"   reasoning deltas, then prose  -> the ✦ thinking fold, folded and unfolded
  "read the fixture"   a read_file call, then prose  -> a ▸ tool-call row and its fold
  "count slowly"       a long, slow prose stream     -> a turn to queue behind, steer and stop

Everything is **deterministic on purpose**: the same words, the same number of chunks, the same
sleeps, so two runs of the same drive differ only where Relay itself prints a clock.  Anything
else answers one line, so a stray turn cannot hang the run.
"""
import json
import sys
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

REASONING = ("Looking at what the pane has to draw here. "
             "The fold is an OSC 8 run over the rows the block printed, "
             "and the anchor above it is rewritten when the block settles. ")

SCENES = {
    "explain the fold": [
        ("A reasoning block folds under its anchor row, and the anchor says how long it took.",
         [], REASONING),
    ],
    "read the fixture": [
        ("", [{"id": "call_1", "type": "function",
               "function": {"name": "read_file",
                            "arguments": json.dumps({"path": "fixture.txt"})}}], ""),
        ("The file says what the drive wrote into it.", [], ""),
    ],
    "count slowly": [
        ("one two three four five six seven eight nine ten "
         "eleven twelve thirteen fourteen fifteen sixteen seventeen eighteen nineteen twenty",
         [], ""),
    ],
}


def scene(request):
    messages = request.get("messages") or []
    picked, at = None, -1
    for i, message in enumerate(messages):
        if message.get("role") != "user":
            continue
        content = message.get("content")
        if isinstance(content, list):
            content = " ".join(p.get("text", "") for p in content if isinstance(p, dict))
        text = (content or "").lower()
        for key in SCENES:
            if key in text:
                picked, at = key, i
    if picked is None:
        return "Nothing to do here.", [], ""
    step = sum(1 for m in messages[at + 1:]
               if m.get("role") == "assistant" and m.get("tool_calls"))
    steps = SCENES[picked]
    return steps[min(step, len(steps) - 1)]


def pieces(text, n=4):
    words = text.split(" ")
    size = max(1, (len(words) + n - 1) // n)
    return [" ".join(words[i:i + size]) + " " for i in range(0, len(words), size)]


class Handler(BaseHTTPRequestHandler):
    def log_message(self, *args):
        pass

    def do_POST(self):
        length = int(self.headers.get("Content-Length") or 0)
        request = json.loads(self.rfile.read(length) or b"{}")
        prose, calls, reasoning = scene(request)
        model = request.get("model") or "stub"
        finish = "tool_calls" if calls else "stop"
        slow = "count slowly" in json.dumps(request.get("messages") or [])
        if request.get("stream"):
            self.send_response(200)
            self.send_header("Content-Type", "text/event-stream")
            self.end_headers()

            def chunk(delta, finish_reason=None):
                data = {"id": "stub", "object": "chat.completion.chunk", "created": 0,
                        "model": model,
                        "choices": [{"index": 0, "delta": delta, "finish_reason": finish_reason}]}
                self.wfile.write(b"data: " + json.dumps(data).encode() + b"\n\n")
                self.wfile.flush()

            for piece in pieces(reasoning) if reasoning else []:
                time.sleep(0.25)
                chunk({"role": "assistant", "reasoning_content": piece})
            if prose:
                for piece in pieces(prose, 10 if slow else 4):
                    time.sleep(1.2 if slow else 0.25)
                    chunk({"role": "assistant", "content": piece})
            if calls:
                time.sleep(0.25)
                chunk({"role": "assistant", "content": None,
                       "tool_calls": [dict(c, index=i) for i, c in enumerate(calls)]})
            chunk({}, finish)
            self.wfile.write(b"data: [DONE]\n\n")
            return
        message = {"role": "assistant", "content": prose or None}
        if calls:
            message["tool_calls"] = calls
        payload = {"id": "stub", "object": "chat.completion", "created": 0, "model": model,
                   "choices": [{"index": 0, "message": message, "finish_reason": finish}],
                   "usage": {"prompt_tokens": 1, "completion_tokens": 1, "total_tokens": 2}}
        raw = json.dumps(payload).encode()
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(raw)))
        self.end_headers()
        self.wfile.write(raw)

    def do_GET(self):
        raw = json.dumps({"data": [{"id": "stub"}]}).encode()
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(raw)))
        self.end_headers()
        self.wfile.write(raw)


if __name__ == "__main__":
    ThreadingHTTPServer(("127.0.0.1", int(sys.argv[1])), Handler).serve_forever()
