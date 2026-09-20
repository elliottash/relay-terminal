#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""An OpenAI-compatible endpoint that answers the prompts this run types, and nothing else.

The shots for #PBX1 are about the *shape* of a prompt box, so the only thing an agent has to do
here is put a few lines in a log: a panel with an empty conversation and a panel with a short one
look different, and both have to be photographed. No tools, no board writes — a scene is a
paragraph.

A scene is picked by a **keyword in a user message**, never by the last message, because Relay's
worker appends a user-role message of its own at the end of a turn (the QA note in
docs/qa_evidence/2026-09-19-helpful-line-breaks): keying on the last message would take the scene
away from the prompt that was typed.

    python3 stub-provider.py <port>
"""
import json
import sys
from http.server import BaseHTTPRequestHandler, HTTPServer

PORT = int(sys.argv[1]) if len(sys.argv) > 1 else 8841

SCENES = {
    # The Switchboard agent, asked something a board agent would be asked. Two short paragraphs:
    # enough for the log to have a shape, short enough that the box under it is still the subject
    # of the picture.
    "how many cards": (
        "Two cards, both in **Inbox**: `#ALPH` *Alpha plain card* and `#BETA` *Second fixture "
        "card*.\n\nNeither has a plan yet, so neither is ready to hand to a pane."),
    # The Options helper.
    "where is copy on select": (
        "It is in **Terminal › Copy on select**. Selecting text in the terminal then puts it on "
        "the clipboard without a Ctrl+C."),
    # The Sessions helper.
    "which sessions": (
        "Three of them touched it today. The most recent is the one this window is in."),
}


def scene(request):
    messages = request.get("messages") or []
    picked = None
    for message in messages:
        if message.get("role") != "user":
            continue
        content = message.get("content")
        if isinstance(content, list):
            content = " ".join(p.get("text", "") for p in content if isinstance(p, dict))
        text = (content or "").lower()
        for key in SCENES:
            if key in text:
                picked = key
    return SCENES[picked] if picked else "Nothing to say here."


def pieces(text):
    """The answer in a handful of deltas, so the panel really streams one."""
    step = max(1, len(text) // 6)
    return [text[i:i + step] for i in range(0, len(text), step)] or [""]


class Handler(BaseHTTPRequestHandler):
    def log_message(self, *args):
        pass

    def do_POST(self):
        body = self.rfile.read(int(self.headers.get("content-length", 0)) or 0)
        request = json.loads(body or b"{}")
        prose = scene(request)
        self.send_response(200)
        self.send_header("content-type", "text/event-stream")
        self.end_headers()

        def chunk(delta, finish_reason=None):
            payload = {"id": "stub", "object": "chat.completion.chunk", "created": 0,
                       "model": "stub",
                       "choices": [{"index": 0, "delta": delta,
                                    "finish_reason": finish_reason}]}
            self.wfile.write(b"data: " + json.dumps(payload).encode() + b"\n\n")
            self.wfile.flush()

        chunk({"role": "assistant", "content": ""})
        for part in pieces(prose):
            chunk({"content": part})
        chunk({}, "stop")
        self.wfile.write(b"data: [DONE]\n\n")
        self.wfile.flush()

    def do_GET(self):
        # `/v1/models`, which Relay asks for when it configures a local endpoint.
        self.send_response(200)
        self.send_header("content-type", "application/json")
        self.end_headers()
        self.wfile.write(json.dumps({"data": [{"id": "stub"}]}).encode())


if __name__ == "__main__":
    HTTPServer(("127.0.0.1", PORT), Handler).serve_forever()
