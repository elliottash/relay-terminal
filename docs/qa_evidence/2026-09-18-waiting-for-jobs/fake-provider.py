#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""A scripted OpenAI-compatible server for card #KP4M ("waiting for N jobs . . ."): no key, no credits.

Relay sees it as a local model endpoint (local-models.json). Three prompts walk three scripts, one
per way the session ends up waiting on background work.

  "*hold for the job"   — blocked on command_output:
      1. run_command(background) "sleep 25" → handed back as a job at once
      2. command_output(job_id, wait_seconds=120) → the main turn blocks here
      3. a one-line answer once the job is back

  "*leave it running"   — the turn ends and a job is left running:
      1. run_command(background) "sleep 35"
      2. a one-line answer, so the turn ends while the job still runs

  "*both at once"       — a subagent and a job, the mixed line:
      1. agent(background) "the helper" → a subagent that sleeps 35 s
      2. run_command(background) "sleep 35"
      3. a one-line answer, so the turn ends with both still running

A subagent sleeps in run_command (the number is in its prompt), then reports. Wake-up turns and
anything else get a one-line answer. Each request's last message goes to the log.

Usage: fake-provider.py <port> <log.jsonl>
"""
import json
import re
import sys
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

PORT, LOG = int(sys.argv[1]), sys.argv[2]


def text_of(message):
    content = message.get("content")
    return content if isinstance(content, str) else json.dumps(content)


def call(name, args, cid):
    return {"id": cid, "type": "function", "function": {"name": name, "arguments": json.dumps(args)}}


def job(seconds, cid):
    return {"role": "assistant", "content": "Starting it in the background.",
            "tool_calls": [call("run_command", {"command": f"sleep {seconds}; echo ready", "background": True},
                                cid)]}


def last_job_id(messages):
    """The job id the worker handed back, from the newest tool result that carries one."""
    for message in reversed(messages):
        if message.get("role") != "tool":
            continue
        found = re.search(r'"job_id"\s*:\s*"([^"]+)"', text_of(message))
        if found:
            return found.group(1)
    return None


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
        return {"role": "assistant", "content": "Waiting for jobs"}
    last = messages[-1] if messages else {}
    if "agent" not in tools:
        return subagent_reply(messages, last)
    # The main agent: the script it is on, and how far along, come from the last tool call id.
    script, step = "", 0
    for message in messages:
        for made in message.get("tool_calls") or []:
            if made["id"].startswith("call_main_"):
                _, _, script, step = made["id"].split("_")
                step = int(step)
    text = text_of(last) if last.get("role") == "user" else ""
    for name, phrase in (("hold", "hold for the job"), ("left", "leave it running"), ("both", "both at once")):
        if phrase in text:
            script, step = name, 0
            break
    else:
        if last.get("role") != "tool":
            return {"role": "assistant", "content": "Noted."}
    nxt = step + 1
    if script == "hold":
        if nxt == 1:
            return job(25, "call_main_hold_1")
        if nxt == 2:
            return {"role": "assistant", "content": "It is running; I will wait for it.",
                    "tool_calls": [call("command_output", {"job_id": last_job_id(messages), "wait_seconds": 120},
                                        "call_main_hold_2")]}
        return {"role": "assistant", "content": "The job is finished; nothing out of the ordinary."}
    if script == "left":
        if nxt == 1:
            return job(35, "call_main_left_1")
        return {"role": "assistant", "content": "It is running in the background; I am done for now."}
    if script == "both":
        if nxt == 1:
            args = {"description": "the helper", "subagent_type": "general", "background": True,
                    "prompt": "SLEEP 35 seconds, then report what is in the folder."}
            return {"role": "assistant", "content": "Starting the helper.",
                    "tool_calls": [call("agent", args, "call_main_both_1")]}
        if nxt == 2:
            return job(35, "call_main_both_2")
        return {"role": "assistant", "content": "Both are running in the background; I am done for now."}
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
