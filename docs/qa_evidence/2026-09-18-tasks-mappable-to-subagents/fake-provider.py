#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""A scripted OpenAI-compatible server for card #QHR1 (tasks mappable to subagents): no key, no credits.

Relay sees it as a local model endpoint (local-models.json). The main agent, on a prompt that says
"three tasks", walks this script one model call at a time:

  1. update_todos: T1 changelog, T2 git status, T3 line counts, all pending
  2. agent(background, todo_id=T1): a subagent whose run sleeps 25 s
  3. update_todos: T2 in_progress (T1 is delegated, so it does not count against the one-in_progress rule)
  4. update_todos: T2 completed, T3 deferred ("left for later")
  5. a one-line answer

A subagent sleeps in run_command (T1's for 25 s, one the user hands over for 12 s), then reports.
Wake-up turns and anything else get a one-line answer. Each request's last message goes to the log.

Usage: fake-provider.py <port> <log.jsonl>
"""
import json
import sys
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

PORT, LOG = int(sys.argv[1]), sys.argv[2]
TODOS = [("Write the changelog entry", 25), ("Check the git status", 0), ("Count lines per file", 12)]


def text_of(message):
    content = message.get("content")
    return content if isinstance(content, str) else json.dumps(content)


def call(name, args, cid):
    return {"id": cid, "type": "function", "function": {"name": name, "arguments": json.dumps(args)}}


def reply(body):
    messages = body.get("messages", [])
    tools = {t.get("function", {}).get("name") for t in body.get("tools") or []}
    if not tools:
        return {"role": "assistant", "content": "Tasks and subagents"}
    last = messages[-1] if messages else {}
    if "agent" not in tools:   # a subagent
        task = next((text_of(m) for m in messages if m.get("role") == "user"), "")
        seconds = 25 if "CHANGELOG-TASK" in task else 12
        if last.get("role") == "tool":
            return {"role": "assistant", "content": "Report: done. Three files in the folder; nothing else to note."}
        return {"role": "assistant", "content": "Working on it.",
                "tool_calls": [call("run_command", {"command": f"sleep {seconds}; ls", "timeout_seconds": 120},
                                    f"call_sub_{len(messages)}")]}
    # The main agent: find where the script is from the last tool call id.
    step = 0
    for m in messages:
        for c in m.get("tool_calls") or []:
            if c["id"].startswith("call_main_"):
                step = int(c["id"].rsplit("_", 1)[1])
    user_turn = last.get("role") == "user" and "three tasks" in text_of(last)
    if user_turn:
        step = 0
    elif last.get("role") != "tool":
        return {"role": "assistant", "content": "Noted: the subagent's task is marked in the list."}
    nxt = step + 1
    ids = ["T1", "T2", "T3"]
    if nxt == 1:
        items = [{"text": t, "status": "pending"} for t, _ in TODOS]
        return {"role": "assistant", "content": "", "tool_calls": [call("update_todos", {"items": items}, "call_main_1")]}
    if nxt == 2:
        args = {"description": "Write the changelog", "subagent_type": "general", "background": True, "todo_id": "T1",
                "prompt": "CHANGELOG-TASK: write a one-line changelog entry for the last commit and report it."}
        return {"role": "assistant", "content": "T1 goes to a background subagent.",
                "tool_calls": [call("agent", args, "call_main_2")]}
    if nxt == 3:
        items = [{"id": i, "text": t, "status": s} for i, (t, _), s in zip(ids, TODOS, ["pending", "in_progress", "pending"])]
        return {"role": "assistant", "content": "", "tool_calls": [call("update_todos", {"items": items}, "call_main_3")]}
    if nxt == 4:
        items = [{"id": "T1", "text": TODOS[0][0], "status": "pending"},
                 {"id": "T2", "text": TODOS[1][0], "status": "completed"},
                 {"id": "T3", "text": TODOS[2][0], "status": "deferred", "note": "left for later"}]
        return {"role": "assistant", "content": "", "tool_calls": [call("update_todos", {"items": items}, "call_main_4")]}
    return {"role": "assistant", "content": "T2 is done, T1 is with subagent a1, and T3 is left for later."}


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
        with open(LOG, "a") as log:
            last = (body.get("messages") or [{}])[-1]
            log.write(json.dumps({"tools": "agent" in {t.get("function", {}).get("name") for t in body.get("tools") or []},
                                  "last": text_of(last)[:600], "reply": message}) + "\n")
        self._send({"id": "fake", "object": "chat.completion", "model": body.get("model"),
                    "choices": [{"index": 0, "message": message,
                                 "finish_reason": "tool_calls" if message.get("tool_calls") else "stop"}],
                    "usage": {"prompt_tokens": 900, "completion_tokens": 60, "total_tokens": 960}})

    def log_message(self, *args):
        pass


ThreadingHTTPServer(("127.0.0.1", PORT), Handler).serve_forever()
