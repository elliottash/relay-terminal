#!/usr/bin/env python3
"""Scripted OpenAI-compatible model for the #S5SH ssh runs. Logs each request's context note."""
import json, sys
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
PORT, LOG = int(sys.argv[1]), sys.argv[2]

def last_prompt(messages):
    for m in reversed(messages):
        if m.get("role") == "user" and isinstance(m.get("content"), str):
            return m["content"]
    return ""

def reply(body):
    messages = body.get("messages", [])
    if not body.get("tools"):
        return {"role": "assistant", "content": "Title"}
    last = messages[-1] if messages else {}
    if last.get("role") == "tool":
        return {"role": "assistant", "content": "The host answered: " + str(last.get("content"))[:300]}
    prompt = last_prompt(messages)
    if "on the host" in prompt:
        call = {"id": f"call_{len(messages)}", "type": "function",
                "function": {"name": "run_command", "arguments": json.dumps({"command": "hostname; pwd; echo REMOTE-OK", "host": "localhost"})}}
        return {"role": "assistant", "content": "", "tool_calls": [call]}
    return {"role": "assistant", "content": "Hello from the agent.\nThis reply should sit **in the terminal**, under the remote prompt.\nThird line."}

class H(BaseHTTPRequestHandler):
    def _send(self, obj):
        d = json.dumps(obj).encode()
        self.send_response(200); self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(d))); self.end_headers(); self.wfile.write(d)
    def do_GET(self):
        self._send({"object": "list", "data": [{"id": "big", "object": "model"}]})
    def do_POST(self):
        body = json.loads(self.rfile.read(int(self.headers.get("Content-Length", 0))) or b"{}")
        with open(LOG, "a") as f:
            f.write(json.dumps({"tools": bool(body.get("tools")), "messages": body.get("messages", [])[-3:]}) + "\n")
        m = reply(body)
        self._send({"id": "f", "object": "chat.completion", "model": "big",
                    "choices": [{"index": 0, "message": m, "finish_reason": "tool_calls" if m.get("tool_calls") else "stop"}],
                    "usage": {"prompt_tokens": 10, "completion_tokens": 10, "total_tokens": 20}})
    def log_message(self, *a): pass
ThreadingHTTPServer(("127.0.0.1", PORT), H).serve_forever()
