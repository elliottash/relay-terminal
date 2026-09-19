#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Loopback-only OpenAI-compatible endpoint for the thinking-fold screenshots (issue T8CN).

    python3 stub-provider.py 8814

Copied from the subagent-badge run (#YMSR) with the thinking modes this issue needs.
What the first user message says decides the turn:

  "markdown"   -> ~6 s of markdown-rich reasoning in 10 chunks, then a short answer
  "quick"      -> ~1.5 s of reasoning, then "Done."          (for the mode/theme scenes)
  "twoblocks"  -> reasoning, a partial answer, more reasoning, the final answer:
                  two thinking blocks in one turn (the second anchor is thinking-2)
  anything else-> "Done." with no reasoning at all

The reasoning goes out as `reasoning_content` deltas (the Kimi/GLM spelling the
backend's _reasoning_text reads), the answer as ordinary `content` deltas.
"""
import json
import sys
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

REASONING_MARKDOWN = [
    "## Weighing the request\n\n",
    "The user asked about **pony logistics**. ",
    "What matters:\n\n",
    "- Herding cats is *different* from herding ponies\n",
    "- The `stable` module already handles feed\n",
    "- Distance: [the map](https://example.com/map) says 3 km\n\n",
    "### Steps\n\n1. Count the ponies\n2. Check the weather\n3. Set out early\n\n",
    "> An early start avoids the heat.\n\n",
    "A longer line to watch the fold wrap and the new five-pixel line spacing: ",
    "the quick brown fox jumps over the lazy dog while the terminal measures.\n",
]

REASONING_SHORT = ["Short check. ", "The ponies are fine. ", "Nothing else to do.\n"]

ANSWER_MARKDOWN = ("Done. The ponies are ready — early start, three kilometres, "
                   "light rain expected.")
ANSWER_SHORT = "Done."


def chunks(parts, delay, key, finish=None):
    for part in parts:
        time.sleep(delay)
        yield {key: part}
    if finish:
        time.sleep(delay)
        yield finish


def turns(text):
    """Yield (part, kind) pairs — kind 'reasoning' or 'content' — for the whole turn."""
    if "twoblocks" in text:
        yield from chunks(["First, reconsider the route. ", "The bridge is out. ",
                           "So: the ford.\n"], 0.4, "reasoning")
        yield {"content": "Let me verify the water level. "}
        yield from chunks(["The gauge reads low. ", "Safe before noon. ",
                           "After that it rises.\n"], 0.4, "reasoning")
        yield {"content": "Verified: ford before noon, and both thinking blocks are shown."}
        return
    if "markdown" in text:
        yield from chunks(REASONING_MARKDOWN, 0.6, "reasoning")
        yield {"content": ANSWER_MARKDOWN}
        return
    if "quick" in text:
        yield from chunks(REASONING_SHORT, 0.4, "reasoning")
        yield {"content": ANSWER_SHORT}
        return
    yield {"content": ANSWER_SHORT}


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
        message = {"role": "assistant", "content": "",
                   "reasoning_content": "".join(p.get("reasoning_content", "") for p in parts)}
        message["content"] = "".join(p.get("content", "") for p in parts)
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
