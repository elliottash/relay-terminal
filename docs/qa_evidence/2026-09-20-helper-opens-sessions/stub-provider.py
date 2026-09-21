#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Loopback-only OpenAI-compatible endpoint for the #H6VQ live run.

    python3 stub-provider.py 8841 <id1> <id2> <id3>

Nothing here calls a provider: the profile points a local model endpoint at this process, so
every agent in the run — the terminal pane's and the tab's helper — answers out of the scenes
below.  A scene is picked by a **keyword in a user message**, not by the last message, because the
worker appends a user-role message of its own at the end of a turn (the completion check) and that
would take the scene away from the prompt that was typed.  Within a scene the step is how many
assistant messages with tool calls have been sent since the message that picked it.

The scenes, and what each is evidence of:

  "in new panes"    app_sessions_search, then app_open {conversation, ids, new_pane: true},
                    and **no text at all** afterwards    -> the new target, and the worker's
                                                            "never answer with nothing" floor
  "one of them here" app_open {conversation, id, new_pane: false}   -> the other placement
  "no such session"  app_open {conversation, ids: [unknown]}        -> the per-id refusal
  "take your time"   a turn that never finishes until the stub is asked again -> the busy strip,
                                                                                the queue, Stop

Anything else answers one line, so a stray turn cannot hang the run.
"""
import json
import sys
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

IDS = [a for a in sys.argv[2:]] or ["a", "b", "c"]
# The "take your time" scene blocks here until something releases it; the drive script releases it
# by asking for /release on the same port.
HOLD = threading.Event()
#: The prose of a turn that goes on answering until it is stopped. It is streamed a word at a
#: time, because a cancel is noticed *between* chunks: a stub that simply blocked would hold the
#: socket open and the ✕ Stop under test would look as though it had done nothing.
HOLD_SENTINEL = "__hold__"
HOLD_WORDS = ["Looking", "through", "everything", "you", "have", "ever", "asked", "me", "about",
              "panes", "and", "sessions", "and", "the", "rest", "of", "it"] * 8


def call(index, name, args):
    return {"id": f"call_{index}", "type": "function",
            "function": {"name": name, "arguments": json.dumps(args)}}


def scenes():
    return {
        "in new panes": [
            # No prose at all, on purpose: this is the turn the owner met — three panes opened and
            # a panel that said nothing. The worker appends the tool's own sentence.
            ("", [call(1, "app_sessions_search", {"query": "pane", "limit": 5})]),
            ("", [call(2, "app_open", {"target": "conversation", "ids": IDS, "new_pane": True})]),
            ("", []),
        ],
        "one of them here": [
            ("", [call(3, "app_open", {"target": "conversation", "id": IDS[0],
                                       "new_pane": False})]),
            ("", []),
        ],
        "no such session": [
            ("", [call(4, "app_open", {"target": "conversation",
                                       "ids": ["nothing-answers-to-this", IDS[1]]})]),
            ("", []),
        ],
        "take your time": [
            ("Working on it.", [call(5, "app_sessions_search", {"query": "pane"})]),
            (HOLD_SENTINEL, []),
        ],
    }


def scene(request):
    messages = request.get("messages") or []
    table = scenes()
    picked, at = None, -1
    for i, message in enumerate(messages):
        if message.get("role") != "user":
            continue
        content = message.get("content")
        if isinstance(content, list):
            content = " ".join(p.get("text", "") for p in content if isinstance(p, dict))
        text = (content or "").lower()
        for key in table:
            if key in text:
                picked, at = key, i
    if picked is None:
        return "Nothing to do here.", []
    step = sum(1 for m in messages[at + 1:] if m.get("role") == "assistant" and m.get("tool_calls"))
    steps = table[picked]
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
                data = {"id": "stub", "object": "chat.completion.chunk", "created": 0,
                        "model": model,
                        "choices": [{"index": 0, "delta": delta, "finish_reason": finish_reason}]}
                self.wfile.write(b"data: " + json.dumps(data).encode() + b"\n\n")
                self.wfile.flush()

            if prose == HOLD_SENTINEL:
                # A turn that answers for a minute, or until the person presses Stop: the
                # connection drops when the worker cancels, which is the BrokenPipeError here.
                try:
                    for word in HOLD_WORDS:
                        time.sleep(1)
                        chunk({"role": "assistant", "content": word + " "})
                        if HOLD.is_set():
                            break
                except (BrokenPipeError, ConnectionResetError):
                    return
                prose = ""
            if prose:
                for piece in pieces(prose):
                    time.sleep(0.2)
                    chunk({"role": "assistant", "content": piece})
            if calls:
                time.sleep(0.2)
                # Indexed: the worker refuses an unindexed streamed tool call.
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
        if self.path.startswith("/release"):
            HOLD.set()
            self.send_response(200)
            self.send_header("Content-Length", "2")
            self.end_headers()
            self.wfile.write(b"ok")
            return
        raw = json.dumps({"data": [{"id": "stub"}]}).encode()
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(raw)))
        self.end_headers()
        self.wfile.write(raw)


if __name__ == "__main__":
    ThreadingHTTPServer(("127.0.0.1", int(sys.argv[1])), Handler).serve_forever()
