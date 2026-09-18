#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""A loopback-only OpenAI-compatible endpoint that behaves like a model using run_in_terminal.

A prompt "run: <command>" or "fill: <command>" (prefix "slow" to answer after 5 s) is answered with a run_in_terminal tool call in
that mode; the tool result is answered with one line; a "Terminal hand-over result" follow-up is
answered with the exit status the stub read out of it. Every request is logged (tool names
offered, the last message) so the run can be checked afterwards.

    python3 stub-provider.py 8741 /path/to/requests.log
"""
import json
import re
import sys
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

LOG = open(sys.argv[2], "a", buffering=1) if len(sys.argv) > 2 else sys.stderr
END = "[End of Relay context]"


def text_of(message) -> str:
    content = message.get("content") or ""
    if isinstance(content, list):
        content = " ".join(p.get("text", "") for p in content if isinstance(p, dict))
    return content


def body(request: dict) -> dict:
    messages = request.get("messages") or []
    offered = [t["function"]["name"] for t in request.get("tools") or []]
    last = messages[-1] if messages else {}
    LOG.write(json.dumps({"offered": "run_in_terminal" in offered, "role": last.get("role"),
                          "last": text_of(last)[-1500:]}) + "\n")
    if last.get("role") == "tool":
        result = json.loads(text_of(last) or "{}") if text_of(last).startswith("{") else {}
        if result.get("ok"):
            return {"role": "assistant", "content": f"Handed over ({result.get('action')}). Watch your terminal."}
        return {"role": "assistant", "content": f"Could not hand it over: {result.get('refused')}."}
    prompt = text_of(last)
    if END in prompt:
        prompt = prompt.split(END, 1)[1].lstrip("\n")
    if prompt.startswith("Terminal hand-over result"):
        status = re.search(r"Exit status: (-?\d+)", prompt)
        seen = "saw its output" if "got:" in prompt else "saw no output"
        return {"role": "assistant", "content": f"FOLLOW-UP: exit status {status.group(1) if status else '?'}, {seen}."}
    if prompt.startswith("slow"):   # gives the person at the keyboard time to start a draft
        prompt = prompt[4:]
        time.sleep(5)
    match = re.match(r"(run|fill): (.+)", prompt, re.S)
    if not match or "run_in_terminal" not in offered:
        return {"role": "assistant", "content": "run_in_terminal offered: %s" % ("run_in_terminal" in offered)}
    args = {"command": match.group(2).strip(), "mode": "run" if match.group(1) == "run" else "prefill",
            "intent": "QA hand-over"}
    return {"role": "assistant", "content": None,
            "tool_calls": [{"id": "call_1", "type": "function",
                            "function": {"name": "run_in_terminal", "arguments": json.dumps(args)}}]}


class Handler(BaseHTTPRequestHandler):
    def do_POST(self):
        length = int(self.headers.get("Content-Length") or 0)
        request = json.loads(self.rfile.read(length) or b"{}")
        message = body(request)
        finish = "tool_calls" if message.get("tool_calls") else "stop"
        model = request.get("model") or "relay-qa-stub"
        if request.get("stream"):
            self.send_response(200)
            self.send_header("Content-Type", "text/event-stream")
            self.end_headers()
            delta = dict(message)
            if delta.get("tool_calls"):
                delta["tool_calls"] = [dict(call, index=i) for i, call in enumerate(delta["tool_calls"])]
            for choice in ({"index": 0, "delta": delta, "finish_reason": None},
                           {"index": 0, "delta": {}, "finish_reason": finish}):
                chunk = {"id": "stub", "object": "chat.completion.chunk", "created": 0, "model": model,
                         "choices": [choice]}
                self.wfile.write(b"data: " + json.dumps(chunk).encode() + b"\n\n")
            self.wfile.write(b"data: [DONE]\n\n")
            return
        raw = json.dumps({"id": "stub", "object": "chat.completion", "created": 0, "model": model,
                          "choices": [{"index": 0, "message": message, "finish_reason": finish}],
                          "usage": {"prompt_tokens": 1, "completion_tokens": 1, "total_tokens": 2}}).encode()
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(raw)))
        self.end_headers()
        self.wfile.write(raw)

    def log_message(self, *args):
        pass


if __name__ == "__main__":
    ThreadingHTTPServer(("127.0.0.1", int(sys.argv[1]) if len(sys.argv) > 1 else 8741), Handler).serve_forever()
