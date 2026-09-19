#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Loopback-only OpenAI-compatible endpoint for the thinking-fold cap shots (issue K48R).

    python3 stub-provider.py 8824

The T8CN stub (docs/qa_evidence/2026-09-19-thinking-fold/stub-provider.py) with the two
shapes this card needs — a block far taller than the cap, and a block that is one very
long line, which is the shape that used to escape a cap counted in source lines.

What the first user message says decides the turn:

  "long"   -> 240 markdown lines of reasoning over ~20 s, then a short answer
  "wall"   -> one 5,000-character paragraph, no newline in it at all, then the answer
  "quick"  -> three short lines (for the "open in pane" scene)
  anything else -> "Done." with no reasoning at all

The reasoning goes out as `reasoning` deltas (which the backend's _reasoning_text reads,
like `reasoning_content`), the answer as ordinary `content` deltas.
"""
import json
import sys
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

WORDS = ("and then it considered the ponies again, weighing feed against distance, "
         "because the ford is only safe before noon ")


def long_reasoning():
    yield "## Weighing the whole route\n\n"
    for n in range(240):
        yield f"{n + 1}. step {n + 1}: check the **gauge**, the feed and the `stable` module\n"


def wall_reasoning():
    paragraph = ""
    while len(paragraph) < 5000:
        paragraph += WORDS
    return [paragraph[:5000] + "\n"]


def chunks(parts, delay, key):
    for part in parts:
        time.sleep(delay)
        yield {key: part}


def turns(text):
    """Yield delta dicts for the whole turn."""
    if "long" in text:
        yield from chunks(long_reasoning(), 0.08, "reasoning")
        yield {"content": "Done. The route holds: ford before noon."}
        return
    if "wall" in text:
        # One line, in twenty pieces, so the fold is re-rendered while it grows.
        paragraph = wall_reasoning()[0]
        pieces = [paragraph[at:at + 250] for at in range(0, len(paragraph), 250)]
        yield from chunks(pieces, 0.35, "reasoning")
        yield {"content": "Done. One paragraph, and the fold stayed six rows."}
        return
    if "quick" in text:
        yield from chunks(["Short check. ", "The ponies are fine. ", "Nothing else to do.\n"],
                          0.4, "reasoning")
        yield {"content": "Done."}
        return
    yield {"content": "Done."}


def first_user(request):
    for m in request.get("messages") or []:
        if m.get("role") == "user":
            content = m.get("content")
            if isinstance(content, list):
                content = " ".join(p.get("text", "") for p in content if isinstance(p, dict))
            return content or ""
    return ""


class Handler(BaseHTTPRequestHandler):
    def log_message(self, *args):
        pass

    def do_POST(self):
        length = int(self.headers.get("Content-Length") or 0)
        request = json.loads(self.rfile.read(length) or b"{}")
        parts = turns(first_user(request))   # lazy: the sleeps pace the stream, not the setup
        model = request.get("model") or "relay-qa-stub"
        if request.get("stream"):
            self.send_response(200)
            self.send_header("Content-Type", "text/event-stream")
            self.end_headers()
            for part in parts:
                data = {"id": "stub", "object": "chat.completion.chunk", "created": 0,
                        "model": model,
                        "choices": [{"index": 0, "delta": part, "finish_reason": None}]}
                self.wfile.write(b"data: " + json.dumps(data).encode() + b"\n\n")
            done = {"id": "stub", "object": "chat.completion.chunk", "created": 0, "model": model,
                    "choices": [{"index": 0, "delta": {}, "finish_reason": "stop"}]}
            self.wfile.write(b"data: " + json.dumps(done).encode() + b"\n\n")
            self.wfile.write(b"data: [DONE]\n\n")
            return
        parts = list(parts)
        message = {"role": "assistant",
                   "reasoning_content": "".join(p.get("reasoning", "") for p in parts),
                   "content": "".join(p.get("content", "") for p in parts)}
        payload = {"id": "stub", "object": "chat.completion", "created": 0, "model": model,
                   "choices": [{"index": 0, "message": message, "finish_reason": "stop"}],
                   "usage": {"prompt_tokens": 1, "completion_tokens": 1, "total_tokens": 2}}
        raw = json.dumps(payload).encode()
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(raw)))
        self.end_headers()
        self.wfile.write(raw)


if __name__ == "__main__":
    ThreadingHTTPServer(("127.0.0.1", int(sys.argv[1])), Handler).serve_forever()
