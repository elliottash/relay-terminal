#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Loopback-only OpenAI-compatible endpoint for the Switchboard page-agent QA run (#8YQ9).

    python3 stub-provider.py 8823

Shaped after docs/qa_evidence/2026-09-19-helpful-line-breaks/stub-provider.py.  Two things it
does differently, both learned from that run:

* **Scenes are keyed on a keyword anywhere in any user message**, never on "the last user
  message": the worker appends user-role messages of its own at the end of a turn (the
  completion check), and the page agent's *first* prompt is wrapped in a whole-board roster.
* **Every scene is a list of steps**, indexed by how many assistant messages already follow the
  message that picked it, so the completion check does not get the whole answer a second time.

Scenes, and what each is for in the evidence:

  "[Switchboard survey]"        the survey's opening turn on a fresh board (item 10)
  "duplicates of each other"    a long, slowly streamed answer: room to queue a second prompt
                                and to press Stop while it is still running (items 2, 3, 4)
  "sort the inbox by rank"      the queued prompts, so the queue visibly drains in order
  "label the two voice cards"
  anything else                 one line

The keys are whole phrases out of the typed prompt, not single words: the page agent's *first*
prompt carries the entire board roster (`board_chat.chat_prompt`) and its brief, so a one-word
key like "label" matches the roster's `· labels -` column and picks the wrong scene. That is
exactly what happened on the first take of this run.

`--slow N` scales the per-piece delay; the long scene streams for about 40 seconds by default so
there is time to drive the panel by hand.
"""
import json
import sys
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

SLOW = 1.0

LONG = (
    "Looking at the board as a whole, the two cards that overlap are **#A1B2 Voice input in the "
    "composer** and **#C3D4 Dictate a prompt with the mic**. They are the same feature written "
    "twice: one asks for a microphone in the composer, the other for dictation from the composer. "
    "Both sit in Inbox, both are unranked against each other, and neither carries a plan yet, so "
    "merging them loses nothing. I would keep #A1B2 as the surviving card, since its title names "
    "the surface rather than the gesture, and fold #C3D4's issue text in under a `Duplicate of` "
    "line. The rest of the board reads cleanly: #E5F6 is a layout question in Discussing, #G7H8 "
    "and #N2M3 are the two cards in Executing, and #J9K1 is waiting for verification. Say the "
    "word and I will merge the two voice cards; nothing is written until you do."
)

SCENES = {
    "[Switchboard survey]": [
        "This project was just given a Switchboard, so here is what it already tracks.\n\n"
        "**Found:** one tracker — `TODO.md`, with two items: *Write the README* and *Add a "
        "licence header to every source file*. An import would create two cards, one per item, "
        "each carrying the `source` key that stops it being imported twice.\n\n"
        "**Left alone:** Relay does not touch the project's own files; the cards live in the "
        "board folder beside them.\n\n"
        "This turn is read-only — every write tool refuses — so nothing has been created. Tick "
        "the items you want and press Import, or tell me which parts to convert.",
        "Nothing further.",
    ],
    "duplicates of each other": [LONG, "Nothing further."],
    "sort the inbox by rank": ["Sorted by rank in my head: #A1B2, #C3D4, then the card with no id — which the "
             "board's own Check will tell you about.", "Nothing further."],
    "label the two voice cards": ["The Inbox cards are all unlabelled; a `voice` label would cover the two of them.",
              "Nothing further."],
}


def scene(request):
    """The scene whose keyword appears latest in the message list, and its step."""
    messages = request.get("messages") or []
    picked, at = None, -1
    for index, message in enumerate(messages):
        if message.get("role") != "user":
            continue
        content = message.get("content")
        if isinstance(content, list):
            content = " ".join(p.get("text", "") for p in content if isinstance(p, dict))
        content = content or ""
        for key in SCENES:
            if key in content:
                picked, at = key, index
    if picked is None:
        return "Done."
    steps = SCENES[picked]
    step = sum(1 for m in messages[at + 1:] if m.get("role") == "assistant")
    return steps[min(step, len(steps) - 1)]


def pieces(text, count=24):
    words = text.split(" ")
    size = max(1, len(words) // count)
    out = []
    for start in range(0, len(words), size):
        out.append(" ".join(words[start:start + size]) + " ")
    return out


class Handler(BaseHTTPRequestHandler):
    def log_message(self, *args):
        pass

    def do_POST(self):
        length = int(self.headers.get("Content-Length") or 0)
        request = json.loads(self.rfile.read(length) or b"{}")
        prose = scene(request)
        model = request.get("model") or "stub"
        if request.get("stream"):
            self.send_response(200)
            self.send_header("Content-Type", "text/event-stream")
            self.end_headers()

            def chunk(delta, finish_reason=None):
                data = {"id": "stub", "object": "chat.completion.chunk", "created": 0,
                        "model": model,
                        "choices": [{"index": 0, "delta": delta,
                                     "finish_reason": finish_reason}]}
                self.wfile.write(b"data: " + json.dumps(data).encode() + b"\n\n")
                self.wfile.flush()

            try:
                for piece in pieces(prose):
                    time.sleep(0.3 * SLOW if len(prose) > 200 else 0.15)
                    chunk({"role": "assistant", "content": piece})
                chunk({}, "stop")
                # A usage row on the final chunk, so the worker can work out how much context is
                # left and emit the `context` event the page's chip follows (19.18).
                self.wfile.write(b"data: " + json.dumps(
                    {"id": "stub", "object": "chat.completion.chunk", "created": 0,
                     "model": model, "choices": [],
                     "usage": {"prompt_tokens": 9000, "completion_tokens": 300,
                               "total_tokens": 9300}}).encode() + b"\n\n")
                self.wfile.write(b"data: [DONE]\n\n")
                self.wfile.flush()
            except BrokenPipeError:      # the GUI pressed Stop: the worker closed the response
                pass
            return
        payload = {"id": "stub", "object": "chat.completion", "created": 0, "model": model,
                   "choices": [{"index": 0, "message": {"role": "assistant", "content": prose},
                                "finish_reason": "stop"}],
                   "usage": {"prompt_tokens": 9000, "completion_tokens": 300,
                             "total_tokens": 9300}}
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
    port = int(sys.argv[1])
    if len(sys.argv) > 3 and sys.argv[2] == "--slow":
        SLOW = float(sys.argv[3])
    ThreadingHTTPServer(("127.0.0.1", port), Handler).serve_forever()
