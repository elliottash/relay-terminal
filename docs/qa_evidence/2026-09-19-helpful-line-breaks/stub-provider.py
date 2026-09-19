#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Loopback-only OpenAI-compatible endpoint for the transcript-gap screenshots (#5AWD).

    python3 stub-provider.py 8816

The last user message picks the turn; within it, one step per assistant message that asked
for tools since that user message:

  "walk"   0  prose, then two reads          -> ✦ line · gap · ▸ model · prose · gap · two rows
           1  a run_command                  -> a third row, single-spaced under the reads
           2  prose, then an edit            -> gap · prose · gap · the edit row (+ its diff)
           3  the final prose                -> gap · prose
  "tools"  0  a run_command with no prose    -> ▸ model directly on top of the row (no gap)
           1  the final prose                -> gap · prose
  else        "Done."                        -> the second ✦ line gets a gap from the turn above

Prose streams as `content` deltas in three pieces, then the tool calls as one delta with
indexed entries (the worker refuses an unindexed streamed tool call).
"""
import json
import sys
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer


def call(index, name, args):
    return {"id": f"call_{index}", "type": "function",
            "function": {"name": name, "arguments": json.dumps(args)}}


WALK = [
    ("Let me look at the two files first, then list the directory.",
     [call(1, "read_file", {"path": "alpha.py"}), call(2, "read_file", {"path": "beta.py"})]),
    ("", [call(3, "run_command", {"command": "ls -la"})]),
    ("The listing is what I expected. **alpha.py** sets `SMALL = 1`; the edit bumps it and adds a second constant.",
     [call(4, "edit_file", {"path": "alpha.py", "old_string": "SMALL = 1",
                            "new_string": "SMALL = 2\nEXTRA = 3"})]),
    ("Done. `alpha.py` now sets SMALL to 2 and defines EXTRA.", []),
]
TOOLS = [
    ("", [call(5, "run_command", {"command": "echo hello from the stub"})]),
    ("Done: the command printed one line.", []),
]


def scene(request):
    messages = request.get("messages") or []
    # The last user message *typed at the prompt*: the worker appends user-role messages of its
    # own at the end of a turn (the completion check), which must not pick the scene.
    last_user, text = -1, ""
    for i, m in enumerate(messages):
        if m.get("role") != "user":
            continue
        content = m.get("content")
        if isinstance(content, list):
            content = " ".join(p.get("text", "") for p in content if isinstance(p, dict))
        content = content or ""
        if any(key in content for key in ("walk", "tools first", "second")):
            last_user, text = i, content
    step = sum(1 for m in messages[last_user + 1:]
               if m.get("role") == "assistant" and m.get("tool_calls"))
    steps = WALK if "walk" in text else TOOLS if "tools first" in text else [("Done.", [])]
    return steps[min(step, len(steps) - 1)]


def pieces(text):
    words = text.split(" ")
    third = max(1, len(words) // 3)
    return [" ".join(words[:third]) + " ", " ".join(words[third:2 * third]) + " ",
            " ".join(words[2 * third:])]


class Handler(BaseHTTPRequestHandler):
    def log_message(self, *args):
        pass

    def do_POST(self):
        length = int(self.headers.get("Content-Length") or 0)
        request = json.loads(self.rfile.read(length) or b"{}")
        prose, calls = scene(request)
        model = request.get("model") or "stub"
        finish = "tool_calls" if calls else "stop"
        if request.get("stream"):
            self.send_response(200)
            self.send_header("Content-Type", "text/event-stream")
            self.end_headers()

            def chunk(delta, finish_reason=None):
                data = {"id": "stub", "object": "chat.completion.chunk", "created": 0, "model": model,
                        "choices": [{"index": 0, "delta": delta, "finish_reason": finish_reason}]}
                self.wfile.write(b"data: " + json.dumps(data).encode() + b"\n\n")
                self.wfile.flush()

            if prose:
                for piece in pieces(prose):
                    time.sleep(0.3)
                    chunk({"role": "assistant", "content": piece})
            if calls:
                time.sleep(0.3)
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
