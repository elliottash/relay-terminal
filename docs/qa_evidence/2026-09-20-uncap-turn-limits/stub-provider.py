#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Loopback-only OpenAI-compatible endpoint for the uncapped-turn evidence (#2CZP).

    python3 stub-provider.py 8821

The scene is keyed on the last user message that names one *and is not Relay's own*. Two traps,
both of which bit this harness before it worked:

* it cannot be the plain last user message — the worker appends user-role messages of its own
  during a turn (the loop nudges, the completion check), which would change the scene half way;
* and Relay's own messages have to be excluded by more than their position, because the cadence
  recitation quotes the original prompt back verbatim. Keying on the last message that merely
  *contains* the scene word therefore matched the recitation, reset the step count to zero and
  made the sweep run for ever. Relay's injected notes open with "[Relay note:" / "[Relay
  reminder:" / "[Relay completion check", while a real prompt is either plain or opens with
  "[Relay context:" — the block Relay prepends to the user's own words. That is the test. It must
  also be the *last* match rather than the first: the GUI run asks both scenes in one
  conversation, and the earlier prompt is still in the history.

  "loop please"   every step asks for the same failing read of the same missing file. Nothing
                  varies, so loopdetect's `error` pattern fires at three calls in a row: nudge,
                  nudge, then the turn is stopped with limit.which == "loop".
  "sweep please"  a different file each step for 30 steps, then prose. A batch operation across
                  files is *not* a loop (the args differ every time), and 30 steps is past the
                  RECITE_STEPS mark, so exactly one cadence recitation should appear.
  else            "Done."
"""
import json
import sys
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

SWEEP_STEPS = 30


def call(index, name, args):
    return {"id": f"call_{index}", "type": "function",
            "function": {"name": name, "arguments": json.dumps(args)}}


def scene(request):
    messages = request.get("messages") or []
    asked, asked_at = "", -1
    for i, m in enumerate(messages):
        if m.get("role") != "user":
            continue
        content = m.get("content")
        if isinstance(content, list):
            content = " ".join(p.get("text", "") for p in content if isinstance(p, dict))
        content = content or ""
        head = content.lstrip()
        injected = head.startswith("[Relay") and not head.startswith("[Relay context:")
        if ("loop please" in content or "sweep please" in content) and not injected:
            asked, asked_at = content, i
    step = sum(1 for m in messages[asked_at + 1:] if m.get("role") == "assistant" and m.get("tool_calls"))
    if "loop please" in asked:
        # Identical arguments every time. The result is identical too (the file does not exist).
        return "", [call(step, "read_file", {"path": "nowhere/missing.txt"})]
    if "sweep please" in asked:
        if step < SWEEP_STEPS:
            return "", [call(step, "read_file", {"path": f"f{step}.txt"})]
        return f"Read all {SWEEP_STEPS} files. Nothing in them had changed.", []
    return "Done.", []


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
                chunk({"role": "assistant", "content": prose})
            if calls:
                time.sleep(0.05)
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
