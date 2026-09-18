#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""A scripted OpenAI-compatible server for card #WD83 (subagents in one tabbed pane): no key, no credits.

Relay sees it as a local model endpoint (local-models.json). Replies:

  main agent, a prompt that says "subagents"  -> three background `agent` calls in one response
  main agent, after tool results or a wake-up -> a one-line answer
  subagent (its prompt carries SUB-A/B/C)     -> one run_command that sleeps (A 45 s, B 60 s, C 8 s),
                                                 then a short report
  no tools (titles, summaries)                -> a short text

Usage: fake-provider.py <port>
"""
import json
import re
import sys
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

PORT = int(sys.argv[1])
SLEEP = {"A": 45, "B": 60, "C": 8}
AGENTS = [("A", "explore", "List the fixture files"), ("B", "general", "Count lines per file"),
          ("C", "general", "Check the git status")]


def text_of(message):
    content = message.get("content")
    return content if isinstance(content, str) else json.dumps(content)


def reply(body):
    messages = body.get("messages", [])
    tools = {t.get("function", {}).get("name") for t in body.get("tools") or []}
    if not tools:
        return {"role": "assistant", "content": "Subagents demo"}
    everything = " ".join(text_of(m) for m in messages if m.get("role") == "user")
    marker = re.search(r"SUB-([ABC])", everything)
    last = messages[-1] if messages else {}
    if "agent" not in tools and marker:   # a subagent
        which = marker.group(1)
        if last.get("role") == "tool":
            return {"role": "assistant", "content": f"Report {which}: the fixture has three files; nothing else to note."}
        call = {"id": f"call_{which}_{len(messages)}", "type": "function",
                "function": {"name": "run_command",
                             "arguments": json.dumps({"command": f"sleep {SLEEP[which]}; ls -la", "timeout_seconds": 120})}}
        return {"role": "assistant", "content": f"Looking at it ({which}).", "tool_calls": [call]}
    if last.get("role") == "user" and "subagents" in text_of(last):
        calls = [{"id": f"call_spawn_{w}", "type": "function",
                  "function": {"name": "agent", "arguments": json.dumps(
                      {"description": d, "prompt": f"SUB-{w}: {d} in the current folder and report.",
                       "subagent_type": t, "background": True})}} for w, t, d in AGENTS]
        return {"role": "assistant", "content": "Starting three background agents.", "tool_calls": calls}
    return {"role": "assistant", "content": "Noted; the agents report when they finish."}


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
        self._send({"id": "fake", "object": "chat.completion", "model": body.get("model"),
                    "choices": [{"index": 0, "message": message,
                                 "finish_reason": "tool_calls" if message.get("tool_calls") else "stop"}],
                    "usage": {"prompt_tokens": 900, "completion_tokens": 60, "total_tokens": 960}})

    def log_message(self, *args):
        pass


ThreadingHTTPServer(("127.0.0.1", PORT), Handler).serve_forever()
