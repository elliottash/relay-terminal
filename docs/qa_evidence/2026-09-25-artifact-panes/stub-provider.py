#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""An OpenAI-compatible endpoint for the #PBZ4 live pass: the agent docked under README.md.

Adapted from docs/qa_evidence/2026-09-25-card-panes/stub-provider.py. Two scenes, picked by a
keyword in a user message (never the last message: the worker appends one of its own):

  "add a sentence"  waits DELAY seconds — the window in which the driver types into the buffer —
                    then calls edit_file on README.md, inserting one sentence under "Run it.";
                    once the tool result is back it answers in a line.
  "outline"         the relay.markdown plugin's /outline prompt: answers with an outline.

    python3 stub-provider.py <port> <absolute path of README.md> [delay seconds]
"""
import json
import sys
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

PORT = int(sys.argv[1]) if len(sys.argv) > 1 else 8861
README = sys.argv[2] if len(sys.argv) > 2 else "README.md"
DELAY = float(sys.argv[3]) if len(sys.argv) > 3 else 10.0
LOG = open(sys.argv[4], "a") if len(sys.argv) > 4 else None


def text_of(message):
    content = message.get("content")
    if isinstance(content, list):
        content = " ".join(p.get("text", "") for p in content if isinstance(p, dict))
    return (content or "").lower()


def scene(messages):
    picked = None
    for message in messages:
        if message.get("role") != "user":
            continue
        text = text_of(message)
        for key in ("add a sentence", "outline"):
            if key in text:
                picked = key
    return picked


class Handler(BaseHTTPRequestHandler):
    def log_message(self, *args):
        pass

    def do_POST(self):
        body = self.rfile.read(int(self.headers.get("content-length", 0)) or 0)
        request = json.loads(body or b"{}")
        messages = request.get("messages") or []
        picked = scene(messages)
        answered = any(m.get("role") == "tool" for m in messages)
        if LOG:
            LOG.write(json.dumps({"scene": picked, "tool_results": [m.get("content") for m in messages
                                                                     if m.get("role") == "tool"]}) + "\n")
            LOG.flush()
        self.send_response(200)
        self.send_header("content-type", "text/event-stream")
        self.end_headers()

        def chunk(delta, finish_reason=None):
            payload = {"id": "stub", "object": "chat.completion.chunk", "created": 0, "model": "stub",
                       "choices": [{"index": 0, "delta": delta, "finish_reason": finish_reason}]}
            self.wfile.write(b"data: " + json.dumps(payload).encode() + b"\n\n")
            self.wfile.flush()

        chunk({"role": "assistant", "content": ""})
        if picked == "add a sentence" and not answered:
            chunk({"content": "Adding the sentence under Usage."})
            time.sleep(DELAY)
            arguments = json.dumps({"path": README, "old_string": "Run it.\n",
                                    "new_string": "Run it.\n\nThe docked agent added this sentence.\n"})
            chunk({"tool_calls": [{"index": 0, "id": "call_pbz4", "type": "function",
                                   "function": {"name": "edit_file", "arguments": arguments}}]})
            chunk({}, "tool_calls")
        else:
            prose = {"add a sentence": "Done: one sentence added under Usage, as one undo step.",
                     "outline": "Outline:\n\n- Scratch project: what this is\n- Usage: how to run it\n"
                                "- Notes: open items"}.get(picked, "Nothing to say here.")
            chunk({"content": prose})
            chunk({}, "stop")
        self.wfile.write(b"data: [DONE]\n\n")
        self.wfile.flush()

    def do_GET(self):
        self.send_response(200)
        self.send_header("content-type", "application/json")
        self.end_headers()
        self.wfile.write(json.dumps({"data": [{"id": "stub"}]}).encode())


if __name__ == "__main__":
    ThreadingHTTPServer(("127.0.0.1", PORT), Handler).serve_forever()
