#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""A scripted OpenAI-compatible server for the ⓘ and session-manager runs (cards #Y63Z, #R6J0):
no key, no credits, loopback only. Relay sees it as a local model endpoint.

  main agent, prompt with "delegate" -> two `agent` calls in one response (two subagent threads),
                                        then, with their reports back, a short answer
  a subagent (its system prompt says "You are subagent") -> one list_directory call, then a report
  anything else with tools            -> a one-line answer
  no tools (titles, recaps)           -> a short title-like line

Usage reports carry a `cost`, the way OpenRouter reports one, so the Cost row has something to show.
Usage: fake-provider.py <port>
"""
import json
import sys
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

PORT = int(sys.argv[1])


def text_of(message):
    content = message.get("content")
    return content if isinstance(content, str) else ""


def reply(body):
    messages = body.get("messages", [])
    if not body.get("tools"):
        return {"role": "assistant", "content": "Survey of the session index"}
    system = text_of(messages[0]) if messages else ""
    last = messages[-1] if messages else {}
    if "You are subagent" in system:
        if last.get("role") == "tool":
            task = next((text_of(m) for m in messages if m.get("role") == "user"), "")
            return {"role": "assistant", "content": "Report: the workspace holds notes.md and src/. "
                                                     f"Task was: {task[:80]}"}
        return {"role": "assistant", "content": "", "tool_calls": [
            {"id": "sub_ls", "type": "function", "function": {"name": "list_directory", "arguments": json.dumps({"path": "."})}}]}
    if last.get("role") == "tool":
        return {"role": "assistant", "content": "Both subagents reported back: the workspace holds notes.md and src/."}
    prompt = next((text_of(m) for m in reversed(messages) if m.get("role") == "user"), "")
    if "delegate" in prompt:
        calls = []
        for n, (description, task) in enumerate((("Survey the workspace", "List the files in the workspace and say what they are."),
                                                 ("Check the notes", "Read notes.md and summarise it in one line."))):
            calls.append({"id": f"spawn_{n}", "type": "function", "function": {
                "name": "agent", "arguments": json.dumps({"description": description, "prompt": task,
                                                          "subagent_type": "general"})}})
        return {"role": "assistant", "content": "Starting two subagents.", "tool_calls": calls}
    return {"role": "assistant", "content": "Answer: the capybara is the largest rodent."}


class Handler(BaseHTTPRequestHandler):
    def _send(self, obj):
        data = json.dumps(obj).encode()
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def do_GET(self):
        self._send({"object": "list", "data": [{"id": "fake", "object": "model"}]})

    def do_POST(self):
        body = json.loads(self.rfile.read(int(self.headers.get("Content-Length", 0))) or b"{}")
        message = reply(body)
        prompt_tokens = len(json.dumps(body.get("messages", []))) // 4
        completion_tokens = max(1, len(json.dumps(message)) // 4)
        self._send({"id": "fake", "object": "chat.completion", "model": body.get("model"),
                    "choices": [{"index": 0, "message": message,
                                 "finish_reason": "tool_calls" if message.get("tool_calls") else "stop"}],
                    "usage": {"prompt_tokens": prompt_tokens, "completion_tokens": completion_tokens,
                              "total_tokens": prompt_tokens + completion_tokens,
                              "cost": round((prompt_tokens + 4 * completion_tokens) * 2e-7, 6)}})

    def log_message(self, *args):
        pass


ThreadingHTTPServer(("127.0.0.1", PORT), Handler).serve_forever()
