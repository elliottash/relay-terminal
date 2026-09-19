#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""A scripted OpenAI-compatible server for the card 3ES1 gap runs: no key, no credits.

Relay sees it as local model endpoints (local-models.json), so the pane can switch between a
large-window model and small-window ones exactly as it would between real ones. Replies are chosen
from the latest user prompt:

  "essay"  -> a long answer (about 7,500 tokens), to fill the conversation
  "sleep"  -> a run_command tool call `sleep 20; echo alpha`, then a one-line answer
  "bulky"  -> the same tool call, with a 60,000-character text beside it (one long current turn)
  no tools -> a short summary (compaction, titles and other side calls)

Every request is logged as one JSON line: the model it was sent to, whether it had tools, its
message count and its size in characters. Usage: fake-provider.py <port> <log file>
"""
import json
import sys
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

PORT, LOG = int(sys.argv[1]), sys.argv[2]
ESSAY = ("Relay keeps the whole conversation when the model changes. " * 520)[:30000]


def last_prompt(messages):
    for message in reversed(messages):
        if message.get("role") == "user" and isinstance(message.get("content"), str):
            return message["content"]
    return ""


def reply(body):
    messages, model = body.get("messages", []), body.get("model", "?")
    if not body.get("tools"):
        return {"role": "assistant", "content": f"SUMMARY by {model}: the user asked for two essays "
                                                "and then for a command to be run."}
    last = messages[-1] if messages else {}
    if last.get("role") == "tool":
        return {"role": "assistant", "content": f"Done on {model}: the command printed alpha."}
    prompt = last_prompt(messages)
    if "essay" in prompt:
        return {"role": "assistant", "content": f"Essay from {model}. " + ESSAY}
    if "sleep" in prompt or "bulky" in prompt:
        call = {"id": f"call_{len(messages)}", "type": "function",
                "function": {"name": "run_command", "arguments": json.dumps({"command": "sleep 20; echo alpha"})}}
        text = ("Notes before running it. " + ESSAY * 2) if "bulky" in prompt else ""
        return {"role": "assistant", "content": text, "tool_calls": [call]}
    return {"role": "assistant", "content": f"Answer from {model}."}


class Handler(BaseHTTPRequestHandler):
    def _send(self, obj):
        data = json.dumps(obj).encode()
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def do_GET(self):
        self._send({"object": "list", "data": [{"id": m, "object": "model"} for m in ("big", "small", "micro")]})

    def do_POST(self):
        body = json.loads(self.rfile.read(int(self.headers.get("Content-Length", 0))) or b"{}")
        size = len(json.dumps(body.get("messages", [])))
        with open(LOG, "a", encoding="utf-8") as log:
            log.write(json.dumps({"model": body.get("model"), "tools": bool(body.get("tools")),
                                  "messages": len(body.get("messages", [])), "chars": size}) + "\n")
        message = reply(body)
        # What a real server counts: the messages and the tool definitions, about 4 characters a token.
        prompt_tokens = (size + len(json.dumps(body.get("tools") or []))) // 4
        completion_tokens = len(json.dumps(message)) // 4
        self._send({"id": "fake", "object": "chat.completion", "model": body.get("model"),
                    "choices": [{"index": 0, "message": message,
                                 "finish_reason": "tool_calls" if message.get("tool_calls") else "stop"}],
                    "usage": {"prompt_tokens": prompt_tokens, "completion_tokens": completion_tokens,
                              "total_tokens": prompt_tokens + completion_tokens}})

    def log_message(self, *args):
        pass


ThreadingHTTPServer(("127.0.0.1", PORT), Handler).serve_forever()
