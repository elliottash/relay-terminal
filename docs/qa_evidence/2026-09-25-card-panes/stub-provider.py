#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""An OpenAI-compatible endpoint for the #Y2BA card-pane run: two card panes plan at once.

Copied from docs/qa_evidence/2026-09-20-action-rows-left with two changes: a **threading**
server, because the point of the run is two card turns in flight together and a single-threaded
one would answer the second only after the first; and a scene for the fixture cards, whose text
says "slowly", so each plan turn streams for about ten seconds and both busy strips can be
photographed running.

A scene is picked by a keyword in a user message, never by the last message, because Relay's
worker appends a user-role message of its own at the end of a turn.

    python3 stub-provider.py <port>
"""
import json
import sys
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

PORT = int(sys.argv[1]) if len(sys.argv) > 1 else 8841

SCENES = {
    "card pane fixture": (
        "A plan for this card, in three steps:\n\n1. Read the files it names.\n"
        "2. Make the one change it asks for.\n3. Prove it with the test it lists."),
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


# The word that makes a turn take its time. The busy strip is only on screen while a turn runs,
# and it is now what carries the agent's name, the turn clock and the survey word (#PBX1,
# 2026-09-20) — so a run that has to photograph it needs a turn that lasts longer than the round
# trip to a stub on localhost, which is about a frame.
SLOW = "slowly"


def slow(request):
    for message in request.get("messages") or []:
        if message.get("role") != "user":
            continue
        content = message.get("content")
        if isinstance(content, list):
            content = " ".join(p.get("text", "") for p in content if isinstance(p, dict))
        if SLOW in (content or "").lower():
            return True
    return False


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

        pause = 1.6 if slow(request) else 0.0
        chunk({"role": "assistant", "content": ""})
        for part in pieces(prose):
            if pause:
                time.sleep(pause)
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
    ThreadingHTTPServer(("127.0.0.1", PORT), Handler).serve_forever()
