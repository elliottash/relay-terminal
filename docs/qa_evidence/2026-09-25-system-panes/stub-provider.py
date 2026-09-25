#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""An OpenAI-compatible endpoint for the #3B1B live pass: agents docked on Tests and Sharing.

Adapted from docs/qa_evidence/2026-09-25-artifact-panes/stub-provider.py. The point of this
pass is that the answer CITES THE VISIBLE ROW, so the stub answers by echoing the
"On screen now:" line the context sent with the ask — the one line that names the pane's
screen — plus a sentence naming the row it selected. Two scenes, picked by a keyword in a
user message (never the last message: the worker appends one of its own):

  "why did the last run fail"  the Tests pane's question; answers citing its screen line.
  "what is being shared"       the Sharing pane's question; answers citing its screen line.

    python3 stub-provider.py <port> <log path>
"""
import json
import sys
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

PORT = int(sys.argv[1]) if len(sys.argv) > 1 else 8869
LOG = open(sys.argv[2], "a") if len(sys.argv) > 2 else None


def text_of(message):
    content = message.get("content")
    if isinstance(content, list):
        content = " ".join(p.get("text", "") for p in content if isinstance(p, dict))
    return content or ""


def scene(messages):
    picked = None
    for message in messages:
        if message.get("role") != "user":
            continue
        text = text_of(message).lower()
        if "why did the last run fail" in text:
            picked = "tests"
        elif "what is being shared" in text:
            picked = "sharing"
    return picked


def screen_line(messages):
    # The context's brief mentions the phrase, so the LIVE line is the LAST one, in the
    # last message that has it: the pane's screen is posted with the ask, after the brief.
    for message in reversed(messages):
        for line in reversed(text_of(message).splitlines()):
            if "On screen now:" in line:
                return line.strip()
    return ""


def last_user_tail(messages, size=300):
    for message in reversed(messages):
        if message.get("role") == "user":
            return text_of(message)[-size:]
    return ""


class Handler(BaseHTTPRequestHandler):
    def log_message(self, *args):
        pass

    def do_POST(self):
        body = self.rfile.read(int(self.headers.get("content-length", 0)) or 0)
        request = json.loads(body or b"{}")
        messages = request.get("messages") or []
        picked = scene(messages)
        screen = screen_line(messages)
        if LOG:
            LOG.write(json.dumps({"scene": picked, "screen": screen,
                                  "last_user_tail": last_user_tail(messages)})
                     + "\n")
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
        if screen:
            prose = (f"The pane tells me: {screen} — that is the row I would act on next.\n"
                     f"(stub answer for the {'Tests' if picked == 'tests' else 'Sharing'} pane)")
        else:
            prose = "The pane sent no screen line, so I cannot see a row to name. (stub)"
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
