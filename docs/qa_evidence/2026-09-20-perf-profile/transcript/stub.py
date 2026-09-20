#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Loopback OpenAI-compatible stub that streams controllable shapes (#PF4K, transcript area).

    python3 stub.py <port> [bigfile]

The scene is picked by a keyword in the *last user message that carries one* (the worker appends
user-role messages of its own at the end of a turn, which must not pick the scene).  Within a
scene the step is the number of assistant messages with tool_calls since that user message.

Keywords (all lower case, matched as substrings):

  zprose<chars>x<deltachars>r<rate>   prose of <chars> characters in <deltachars>-char content
                                      deltas at <rate> deltas/second, then stop
  zmd<blocks>                         a reply of <blocks> fenced code blocks + markdown tables
  zthink<chars>r<rate>                <chars> of reasoning_content at <rate>/s, then a short reply
  ztools<n>x<batch>                   <n> run_command calls of `cat <bigfile>` in batches of
                                      <batch>, then a one-line reply
  zturn                               "ok." -- a cheap turn for building a long conversation

Everything is generated, nothing is read from the network, and no key is used.
"""
import json
import re
import sys
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

BIGFILE = sys.argv[2] if len(sys.argv) > 2 else "/etc/hostname"

WORDS = ("relay terminal pane transcript fold anchor scrollback delta stream markdown render "
         "worker protocol session agent reply thinking tool call output widget layout paint "
         "buffer cursor column row grid wrap escape sequence hyperlink prose block").split()


def prose(n):
    out = []
    size = 0
    i = 0
    while size < n:
        w = WORDS[i % len(WORDS)]
        i += 1
        out.append(w)
        size += len(w) + 1
        if i % 17 == 0:
            out.append("\n\n")
            size += 2
    return " ".join(out)[:n]


def md(blocks):
    parts = []
    for b in range(blocks):
        parts.append(f"## Section {b + 1}\n\n" + prose(300) + "\n\n")
        parts.append("```python\n" + "".join(
            f"def step_{b}_{i}(x):\n    return x * {i} + len('{WORDS[i % len(WORDS)]}')\n"
            for i in range(10)) + "```\n\n")
        parts.append("| name | kind | cost | note |\n| --- | --- | --- | --- |\n" + "".join(
            f"| item_{b}_{r} | `{WORDS[r % len(WORDS)]}` | {r * 7} | a short remark |\n"
            for r in range(12)) + "\n")
    return "".join(parts)


def call(index, name, args):
    return {"id": f"call_{index}", "type": "function",
            "function": {"name": name, "arguments": json.dumps(args)}}


def pick(messages):
    """(keyword text, step) -- step = assistant messages with tool_calls since that user message."""
    last, text = -1, ""
    for i, m in enumerate(messages):
        if m.get("role") != "user":
            continue
        content = m.get("content")
        if isinstance(content, list):
            content = " ".join(p.get("text", "") for p in content if isinstance(p, dict))
        content = (content or "").lower()
        if "zprose" in content or "zmd" in content or "zthink" in content \
                or "ztools" in content or "zturn" in content:
            last, text = i, content
    step = sum(1 for m in messages[last + 1:]
               if m.get("role") == "assistant" and m.get("tool_calls"))
    return text, step


class Handler(BaseHTTPRequestHandler):
    def log_message(self, *args):
        pass

    def _sse_open(self):
        self.send_response(200)
        self.send_header("Content-Type", "text/event-stream")
        self.end_headers()

    def done(self):
        self.wfile.write(b"data: [DONE]\n\n")
        self.wfile.flush()

    def _chunk(self, model, delta, finish=None):
        data = {"id": "stub", "object": "chat.completion.chunk", "created": 0, "model": model,
                "choices": [{"index": 0, "delta": delta, "finish_reason": finish}]}
        self.wfile.write(b"data: " + json.dumps(data).encode() + b"\n\n")
        self.wfile.flush()

    def do_POST(self):
        length = int(self.headers.get("Content-Length") or 0)
        request = json.loads(self.rfile.read(length) or b"{}")
        model = request.get("model") or "stub"
        text, step = pick(request.get("messages") or [])
        if not request.get("stream"):
            raw = json.dumps({"id": "stub", "object": "chat.completion", "created": 0, "model": model,
                              "choices": [{"index": 0, "message": {"role": "assistant", "content": "ok."},
                                           "finish_reason": "stop"}],
                              "usage": {"prompt_tokens": 1, "completion_tokens": 1, "total_tokens": 2}}).encode()
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(raw)))
            self.end_headers()
            self.wfile.write(raw)
            return
        self._sse_open()
        try:
            self.play(model, text, step)
        except (BrokenPipeError, ConnectionResetError):
            return

    # ---- scenes ---------------------------------------------------------------------------
    def stream_text(self, model, body, size, rate, key="content"):
        """`body` in `size`-character deltas, paced at `rate` deltas a second."""
        gap = 1.0 / rate if rate > 0 else 0.0
        start = time.perf_counter()
        n = 0
        for i in range(0, len(body), size):
            piece = body[i:i + size]
            if key == "content":
                self._chunk(model, {"role": "assistant", "content": piece})
            else:
                self._chunk(model, {"role": "assistant", "reasoning_content": piece})
            n += 1
            if gap:
                due = start + n * gap
                now = time.perf_counter()
                if due > now:
                    time.sleep(due - now)

    def play(self, model, text, step):
        m = re.search(r"zprose(\d+)x(\d+)r(\d+)", text)
        if m:
            chars, size, rate = int(m.group(1)), int(m.group(2)), int(m.group(3))
            self.stream_text(model, prose(chars), size, rate)
            self._chunk(model, {}, "stop")
            self.done()
            return
        m = re.search(r"zmd(\d+)", text)
        if m:
            self.stream_text(model, md(int(m.group(1))), 20, 300)
            self._chunk(model, {}, "stop")
            self.done()
            return
        m = re.search(r"zthink(\d+)r(\d+)", text)
        if m:
            chars, rate = int(m.group(1)), int(m.group(2))
            self.stream_text(model, prose(chars), 20, rate, key="reasoning")
            self.stream_text(model, prose(2000), 20, 200)
            self._chunk(model, {}, "stop")
            self.done()
            return
        m = re.search(r"ztools(\d+)x(\d+)", text)
        if m:
            total, batch = int(m.group(1)), int(m.group(2))
            done = step * batch
            if done < total:
                take = min(batch, total - done)
                calls = [call(done + i + 1, "run_command", {"command": f"cat {BIGFILE}"})
                         for i in range(take)]
                self.stream_text(model, f"Batch {step + 1}: reading the file {take} times.\n", 20, 200)
                self._chunk(model, {"role": "assistant", "content": None,
                                    "tool_calls": [dict(c, index=i) for i, c in enumerate(calls)]})
                self._chunk(model, {}, "tool_calls")
            else:
                self.stream_text(model, "All batches done.\n", 20, 200)
                self._chunk(model, {}, "stop")
            self.done()
            return
        self.stream_text(model, "ok.", 3, 0)
        self._chunk(model, {}, "stop")
        self.done()

    def do_GET(self):
        raw = json.dumps({"data": [{"id": "stub"}]}).encode()
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(raw)))
        self.end_headers()
        self.wfile.write(raw)


if __name__ == "__main__":
    ThreadingHTTPServer(("127.0.0.1", int(sys.argv[1])), Handler).serve_forever()
