#!/usr/bin/env python3
"""A scripted OpenAI-compatible server: one run_command (echo), then a reply shaped like the one the
owner's screenshot showed, in two variants (a fenced code block, then plain lines)."""
import json
import sys
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

PORT = int(sys.argv[1])
LISTING = ("aes-paper-1          gamesfx              relit                   spriteforge\n"
           "ds4                  modalities           relit-wt-aspect-labels  sweet-street\n"
           "platform-lm          puzzlebench          relit-wt-family-cites   symbolic-music-generation")
REPLY_FENCED = "Fresh listing:\n\n**~/repos/** (22 projects):\n\n```\n" + LISTING + "\n```\n\nThat is all of them."
REPLY_PLAIN = "Fresh listing:\n\n**~/repos/** (22 projects):\n\n" + LISTING + "\n\nThat is all of them."


def reply(body):
    messages = body.get("messages", [])
    tools = {t.get("function", {}).get("name") for t in body.get("tools") or []}
    if not tools:
        return {"role": "assistant", "content": "Dark text repro"}
    last = messages[-1] if messages else {}
    users = [m for m in messages if m.get("role") == "user"]
    n = len(users)
    if last.get("role") == "tool":
        return {"role": "assistant", "content": REPLY_FENCED if n % 2 == 1 else REPLY_PLAIN}
    call = {"id": f"call_{len(messages)}", "type": "function",
            "function": {"name": "run_command",
                         "arguments": json.dumps({"command": "echo 'Fresh listing:'\necho\necho '~/repos/ (22 projects):'\necho\n" + "\n".join("echo '%s'" % l for l in LISTING.split("\n")) + "\necho\necho done", "timeout_seconds": 30})}}
    return {"role": "assistant", "content": "", "tool_calls": [call]}


class Handler(BaseHTTPRequestHandler):
    def log_message(self, *a):
        pass

    def do_POST(self):
        n = int(self.headers.get("Content-Length", 0))
        body = json.loads(self.rfile.read(n) or b"{}")
        msg = reply(body)
        stream = body.get("stream")
        if stream:
            self.send_response(200)
            self.send_header("Content-Type", "text/event-stream")
            self.end_headers()
            delta = {"role": "assistant", "content": msg.get("content", "")}
            if msg.get("tool_calls"):
                delta["tool_calls"] = [dict(c, index=i) for i, c in enumerate(msg["tool_calls"])]
            chunk = {"id": "x", "object": "chat.completion.chunk", "model": "fake",
                     "choices": [{"index": 0, "delta": delta, "finish_reason": None}]}
            self.wfile.write(b"data: " + json.dumps(chunk).encode() + b"\n\n")
            done = {"id": "x", "object": "chat.completion.chunk", "model": "fake",
                    "choices": [{"index": 0, "delta": {}, "finish_reason": "tool_calls" if msg.get("tool_calls") else "stop"}],
                    "usage": {"prompt_tokens": 10, "completion_tokens": 10}}
            self.wfile.write(b"data: " + json.dumps(done).encode() + b"\n\ndata: [DONE]\n\n")
        else:
            out = json.dumps({"id": "x", "object": "chat.completion", "model": "fake",
                              "choices": [{"index": 0, "message": msg,
                                           "finish_reason": "tool_calls" if msg.get("tool_calls") else "stop"}],
                              "usage": {"prompt_tokens": 10, "completion_tokens": 10}}).encode()
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(out)))
            self.end_headers()
            self.wfile.write(out)

    def do_GET(self):
        out = json.dumps({"object": "list", "data": [{"id": "fake", "object": "model"}]}).encode()
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(out)))
        self.end_headers()
        self.wfile.write(out)


ThreadingHTTPServer(("127.0.0.1", PORT), Handler).serve_forever()
