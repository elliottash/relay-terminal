#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""A scripted OpenAI-compatible server for card 31BM's run: no key, no credits.

Same shape as the 3ES1 gap-run provider (local model endpoints), with one addition: the
compaction summary streams slowly, as SSE deltas, so the context chip's "compacting… N%"
is on screen long enough to capture. Other replies are instant.

  tools present            -> short answers / a `sleep 20; echo alpha` run_command call
  no tools + "Transcript"  -> the compaction summary: 60 SSE deltas of 100 chars, 0.4 s apart
  no tools, anything else  -> a one-line summary (titles, recaps)

Every request is logged as one JSON line. Usage: fake-provider.py <port> <log file>
"""
import json
import sys
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

PORT, LOG = int(sys.argv[1]), sys.argv[2]
ESSAY = ("Relay keeps the whole conversation when the model changes. " * 520)[:30000]
SUMMARY = ("The user asked for two long essays and then a short question; this summary streams "
           "slowly so the compacting chip can be photographed. " * 9)[:1200]


def last_prompt(messages):
    for message in reversed(messages):
        if message.get("role") == "user" and isinstance(message.get("content"), str):
            return message["content"]
    return ""


def reply(body):
    messages, model = body.get("messages", []), body.get("model", "?")
    if not body.get("tools"):
        return {"role": "assistant", "content": f"SUMMARY by {model}: the user asked for two essays."}
    last = messages[-1] if messages else {}
    if last.get("role") == "tool":
        return {"role": "assistant", "content": f"Done on {model}: the command printed alpha."}
    prompt = last_prompt(messages)
    if "essay" in prompt:
        return {"role": "assistant", "content": f"Essay from {model}. " + ESSAY}
    if "sleep" in prompt:
        call = {"id": f"call_{len(messages)}", "type": "function",
                "function": {"name": "run_command", "arguments": json.dumps({"command": "sleep 20; echo alpha"})}}
        return {"role": "assistant", "content": "", "tool_calls": [call]}
    return {"role": "assistant", "content": f"Answer from {model}."}


class Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def _send(self, obj):
        data = json.dumps(obj).encode()
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def _stream_summary(self, model):
        self.send_response(200)
        self.send_header("Content-Type", "text/event-stream")
        self.send_header("Cache-Control", "no-cache")
        self.end_headers()
        for i in range(0, len(SUMMARY), 20):
            chunk = {"id": "fake", "object": "chat.completion.chunk", "model": model,
                     "choices": [{"index": 0, "delta": {"content": SUMMARY[i:i + 20]}, "finish_reason": None}]}
            self.wfile.write(f"data: {json.dumps(chunk)}\n\n".encode())
            self.wfile.flush()
            time.sleep(0.4)
        final = {"id": "fake", "object": "chat.completion.chunk", "model": model,
                 "choices": [{"index": 0, "delta": {}, "finish_reason": "stop"}],
                 "usage": {"prompt_tokens": 9000, "completion_tokens": len(SUMMARY) // 4,
                           "total_tokens": 9000 + len(SUMMARY) // 4}}
        self.wfile.write(f"data: {json.dumps(final)}\n\ndata: [DONE]\n\n".encode())
        self.wfile.flush()

    def do_GET(self):
        self._send({"object": "list", "data": [{"id": m, "object": "model"} for m in ("big", "small")]})

    def do_POST(self):
        body = json.loads(self.rfile.read(int(self.headers.get("Content-Length", 0)) or b"{}"))
        size = len(json.dumps(body.get("messages", [])))
        with open(LOG, "a", encoding="utf-8") as log:
            log.write(json.dumps({"t": round(time.time(), 1), "model": body.get("model"), "tools": bool(body.get("tools")),
                                  "messages": len(body.get("messages", [])), "chars": size,
                                  "stream": bool(body.get("stream")),
                                  "summary": not body.get("tools")
                                  and any("Transcript of the earlier conversation" in str(m.get("content"))
                                          for m in body.get("messages", []))}) + "\n")
        if body.get("stream") and not body.get("tools") \
                and any("Transcript of the earlier conversation" in str(m.get("content"))
                        for m in body.get("messages", [])):
            self._stream_summary(body.get("model", "?"))
            return
        message = reply(body)
        prompt_tokens = (size + len(json.dumps(body.get("tools") or []))) // 4
        completion_tokens = len(json.dumps(message)) // 4
        self._send({"id": "fake", "object": "chat.completion", "model": body.get("model"),
                    "choices": [{"index": 0, "message": message,
                                 "finish_reason": "tool_calls" if message.get("tool_calls") else "stop"}],
                    "usage": {"prompt_tokens": prompt_tokens, "completion_tokens": completion_tokens,
                              "total_tokens": prompt_tokens + completion_tokens}})

    def log_message(self, *args):
        pass


if __name__ == "__main__":
    ThreadingHTTPServer(("127.0.0.1", PORT), Handler).serve_forever()
