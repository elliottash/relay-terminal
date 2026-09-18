#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""A scripted OpenAI-compatible server for card #V7QD ("waiting for N subagents . . ."): no key, no credits.

Relay sees it as a local model endpoint (local-models.json). Two prompts walk two scripts, one per
way the main agent ends up waiting on its subagents.

  "*hold for the helpers"  — blocked on agent_wait:
      1. agent(background) "scan one"     → a1, which sleeps 45 s
      2. agent(background) "scan two"     → a2, which sleeps 45 s
      3. agent_wait (no id: all of them)  → the main turn blocks here
      4. a one-line answer once they are both back

  "*hand it over"          — the turn ends and only a subagent is left:
      1. agent(background) "the long one" → a3, which sleeps 40 s
      2. a one-line answer, so the turn ends while a3 is still running

A subagent sleeps in run_command (the number is in its prompt), then reports. Wake-up turns and
anything else get a one-line answer. Each request's last message goes to the log.

Usage: fake-provider.py <port> <log.jsonl>
"""
import json
import sys
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

PORT, LOG = int(sys.argv[1]), sys.argv[2]


def text_of(message):
    content = message.get("content")
    return content if isinstance(content, str) else json.dumps(content)


def call(name, args, cid):
    return {"id": cid, "type": "function", "function": {"name": name, "arguments": json.dumps(args)}}


def spawn(description, prompt, cid):
    args = {"description": description, "subagent_type": "general", "background": True, "prompt": prompt}
    return {"role": "assistant", "content": f"Starting {description}.", "tool_calls": [call("agent", args, cid)]}


def subagent_reply(messages, last):
    """A subagent: sleep for the number in its prompt, then report."""
    task = next((text_of(m) for m in messages if m.get("role") == "user"), "")
    seconds = next((w for w in task.split() if w.isdigit()), "10")
    if last.get("role") == "tool":
        return {"role": "assistant", "content": "Report: done. Nothing out of the ordinary."}
    return {"role": "assistant", "content": "Working on it.",
            "tool_calls": [call("run_command", {"command": f"sleep {seconds}; ls", "timeout_seconds": 300},
                                f"call_sub_{len(messages)}")]}


def reply(body):
    messages = body.get("messages", [])
    tools = {t.get("function", {}).get("name") for t in body.get("tools") or []}
    if not tools:
        return {"role": "assistant", "content": "Waiting for subagents"}
    last = messages[-1] if messages else {}
    if "agent" not in tools:
        return subagent_reply(messages, last)
    # The main agent: the script it is on, and how far along, come from the last tool call id.
    script, step = "", 0
    for m in messages:
        for c in m.get("tool_calls") or []:
            if c["id"].startswith("call_main_"):
                _, _, script, step = c["id"].split("_")
                step = int(step)
    text = text_of(last) if last.get("role") == "user" else ""
    if "hold for the helpers" in text:
        script, step = "hold", 0
    elif "hand it over" in text:
        script, step = "hand", 0
    elif last.get("role") != "tool":
        return {"role": "assistant", "content": "Noted."}
    nxt = step + 1
    if script == "hold":
        if nxt == 1:
            return spawn("scan one", "SLEEP 45 seconds, then report what is in the folder.", "call_main_hold_1")
        if nxt == 2:
            return spawn("scan two", "SLEEP 45 seconds, then report what is in the folder.", "call_main_hold_2")
        if nxt == 3:
            return {"role": "assistant", "content": "Both are running; I will wait for them.",
                    "tool_calls": [call("agent_wait", {"timeout_seconds": 300}, "call_main_hold_3")]}
        return {"role": "assistant", "content": "Both helpers are back; nothing out of the ordinary."}
    if script == "hand":
        if nxt == 1:
            return spawn("the long one", "SLEEP 40 seconds, then report what is in the folder.", "call_main_hand_1")
        return {"role": "assistant", "content": "It is running in the background; I am done for now."}
    return {"role": "assistant", "content": "Nothing to do."}


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
