#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Loopback-only OpenAI-compatible endpoint for the #7JD1 drive, with a request log.

The drives beside this one pick a scene by a keyword and stream it; this one does the same and
**writes every request's user messages to a jsonl** as it arrives. That log is the evidence the
screenshots cannot give: whether the prompt that was queued behind a stopped turn reached the
model, and *when* — before the Enter that resumed the queue, or after it.

    python3 stub-provider.py <port> <log path>

Two scenes, both picked by the first keyword in any user message (never the last message: the
worker appends a user-role completion check of its own at the end of a turn, which would take the
scene away from the prompt that was typed — the QA-stub gotcha the earlier drives recorded):

  "count slowly"   twenty words, one every 1.5 s  -> a turn long enough to queue behind and to Esc
  anything else    one short line                 -> a card's Discuss, a completion check, a seed

Each line of the log is `{at, helper, tail}`: when the request arrived, whether it is one of the
pane's helper turns (the title, the recap — they are handed the conversation so far, so every
prompt in it would otherwise look as if it had arrived again), and the last 400 characters of the
last user message, which is where the words that were typed are.

Nothing here reaches a provider, and it binds to 127.0.0.1 only.
"""
import json
import sys
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

PORT = int(sys.argv[1])
LOG = sys.argv[2] if len(sys.argv) > 2 else "/dev/null"
SLOW_WORDS = ("one two three four five six seven eight nine ten eleven twelve thirteen fourteen "
              "fifteen sixteen seventeen eighteen nineteen twenty").split(" ")


def _text(message):
    content = message.get("content")
    if isinstance(content, list):
        return " ".join(part.get("text", "") for part in content if isinstance(part, dict))
    return content if isinstance(content, str) else ""


class Handler(BaseHTTPRequestHandler):
    def log_message(self, *args):
        pass

    def do_POST(self):
        length = int(self.headers.get("Content-Length") or 0)
        request = json.loads(self.rfile.read(length) or b"{}")
        messages = request.get("messages") or []
        said = [_text(m) for m in messages if m.get("role") == "user"]
        prompt = said[-1] if said else ""
        # Relay prefixes a turn's prompt with its context block, so the words that were **typed**
        # are at the *end* of the last user message, not at its start: the log keeps the tail.
        # A helper turn — the pane's title and its recap — is handed the conversation so far and
        # would otherwise count as a second arrival of every prompt in it, so it is marked and the
        # drive counts only the turns that are not helpers.
        helper = prompt.lstrip().startswith(("Session so far", "Conversation:"))
        with open(LOG, "a", encoding="utf-8") as log:
            log.write(json.dumps({"at": round(time.time(), 3), "helper": helper,
                                  "tail": prompt[-400:]}) + "\n")
        slow = not helper and "count slowly" in prompt.lower()
        model = request.get("model") or "stub"

        if request.get("stream"):
            self.send_response(200)
            self.send_header("Content-Type", "text/event-stream")
            self.end_headers()

            def chunk(delta, finish_reason=None):
                data = {"id": "stub", "object": "chat.completion.chunk", "created": 0,
                        "model": model,
                        "choices": [{"index": 0, "delta": delta, "finish_reason": finish_reason}]}
                try:
                    self.wfile.write(b"data: " + json.dumps(data).encode() + b"\n\n")
                    self.wfile.flush()
                except BrokenPipeError:
                    raise SystemExit(0)

            for word in (SLOW_WORDS if slow else ["all right."]):
                time.sleep(1.5 if slow else 0.2)
                chunk({"role": "assistant", "content": word + " "})
            chunk({}, "stop")
            self.wfile.write(b"data: [DONE]\n\n")
            return

        payload = {"id": "stub", "object": "chat.completion", "created": 0, "model": model,
                   "choices": [{"index": 0, "message": {"role": "assistant", "content": "all right."},
                                "finish_reason": "stop"}],
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


ThreadingHTTPServer(("127.0.0.1", PORT), Handler).serve_forever()
