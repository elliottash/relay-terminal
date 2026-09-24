#!/usr/bin/env python3
"""The scripted model behind the M5FZ Try it: a loopback-only OpenAI-compatible server that
needs no key and no network. One conversation shape is recognised:

- the main agent's first reply spawns one background subagent (tool `agent`);
- the subagent's first reply calls read_file + run_command on the seeded repo;
- the subagent's second reply is its final report; the main agent's second reply closes.

Everything else (a key test, a models probe) gets a plain text completion. Each scripted reply is
logged to the log file so stage.sh can wait for the sequence without touching the app.
"""
import base64
import hashlib
import json
import sys
import threading
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

PORT = int(sys.argv[1]) if len(sys.argv) > 1 else 8731
LOG = sys.argv[2] if len(sys.argv) > 2 else "/tmp/mock.log"

SUBAGENT_MARKER = "one task inside a Linux terminal"   # only the subagent system prompt says this
# Any valid X25519 public key: the worker derives its shared secret against it, and this gateway
# never checks the proof, so the private half is discarded.
EPHEMERAL = "ceTplJBfJwbxAItDkt6ivfqG6+0U+7+0BmhBI6stEQo="

def tool_call(call_id, name, arguments):
    return {"id": call_id, "type": "function",
            "function": {"name": name, "arguments": json.dumps(arguments)}}

def spawn_call():
    return tool_call("mock-agent-1", "agent", {
        "description": "notes QA sweep",
        "prompt": "Read notes.md, then count its lines with `grep -c . notes.md`, then report.",
        "subagent_type": "general",
        "background": True})

def subagent_tools():
    return [tool_call("mock-read-1", "read_file", {"path": "notes.md"}),
            tool_call("mock-run-1", "run_command", {"command": "grep -c . notes.md"})]

class Handler(BaseHTTPRequestHandler):
    def log_message(self, *args):
        pass

    def _log(self, tag):
        with open(LOG, "a", encoding="utf-8") as handle:
            handle.write(tag + "\n")

    def do_GET(self):
        path = self.path.split("?")[0].replace("/v1", "", 1) if self.path.split("?")[0].startswith("/v1") else self.path.split("?")[0]
        if path.rstrip("/") == "/models":
            body = json.dumps({"object": "list", "data": [{"id": "mock-1"}]}).encode()
            self._send_json(body)
            self._log("models")
            return
        if path.rstrip("/") == "/quota":
            self._send_json(json.dumps({"percent_left": 100, "resets_at": None}).encode())
            return
        self.send_error(404)

    def _send_json(self, body):
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_POST(self):
        length = int(self.headers.get("Content-Length") or 0)
        payload = json.loads(self.rfile.read(length) or b"{}")
        raw_path = self.path.split("?")[0]
        path = raw_path[3:] if raw_path.startswith("/v1") else raw_path

        # --- the hosted session's challenge / proof / register exchange (hosted.Session._register):
        # this gateway grants a token to whoever proves nothing at all, and echoes back the
        # installation id the client derives from its own key so the identity check passes.
        if path.rstrip("/") == "/challenge":
            self._send_json(json.dumps({"ephemeral_public": EPHEMERAL, "challenge": "m5fz"}).encode())
            return
        if path.rstrip("/") == "/register":
            static = base64.b64decode(payload.get("static_pubkey") or "")
            installation = hashlib.sha256(static).hexdigest()[:32] if static else ""
            self._send_json(json.dumps({"token": "mock-session", "expires_in": 3600,
                                        "installation_id": installation, "plan": "free"}).encode())
            self._log("register")
            return

        messages = payload.get("messages") or []
        system = ""
        for message in messages:
            if message.get("role") == "system":
                system = str(message.get("content") or "")
                break
        subagent = SUBAGENT_MARKER in system
        has_tool_results = any(m.get("role") == "tool" for m in messages)
        has_agent_call = any(m.get("role") == "assistant" and m.get("tool_calls") for m in messages)

        if subagent and not has_tool_results:
            message, finish, tag = {"role": "assistant", "content": None, "tool_calls": subagent_tools()}, "tool_calls", "sub_tools"
        elif subagent:
            message, finish, tag = {"role": "assistant", "content": "REPORT[S] Read `notes.md` (3 lines) and counted them with `grep -c . notes.md` (output: 3). Nothing else was needed."}, "stop", "sub_final"
        elif not has_agent_call and messages:
            message, finish, tag = {"role": "assistant", "content": None, "tool_calls": [spawn_call()]}, "tool_calls", "main_spawn"
        else:
            message, finish, tag = {"role": "assistant", "content": "The notes QA sweep finished. Open it from the subagents strip to read its transcript."}, "stop", "main_final"
        self._log(tag)

        if payload.get("stream"):
            self.send_response(200)
            self.send_header("Content-Type", "text/event-stream")
            self.end_headers()
            streamed = dict(message)
            if streamed.get("tool_calls"):
                streamed["tool_calls"] = [dict(call, index=i) for i, call in enumerate(streamed["tool_calls"])]
            chunk = {"id": "chatcmpl-mock", "object": "chat.completion.chunk", "created": 1, "model": "mock-1",
                     "choices": [{"index": 0, "delta": {"role": "assistant", **streamed}, "finish_reason": None}]}
            self.wfile.write(b"data: " + json.dumps(chunk).encode() + b"\n\n")
            done = {"id": "chatcmpl-mock", "object": "chat.completion.chunk", "created": 1, "model": "mock-1",
                    "choices": [{"index": 0, "delta": {}, "finish_reason": finish}]}
            self.wfile.write(b"data: " + json.dumps(done).encode() + b"\n\n")
            self.wfile.write(b"data: [DONE]\n\n")
            return
        body = json.dumps({"id": "chatcmpl-mock", "object": "chat.completion", "created": 1, "model": "mock-1",
                           "choices": [{"index": 0, "message": message, "finish_reason": finish}],
                           "usage": {"prompt_tokens": 1, "completion_tokens": 1, "total_tokens": 2}}).encode()
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

if __name__ == "__main__":
    threading.current_thread().name = "mock-model"
    # Plain HTTP to a plain port is only allowed for a loopback/local model server, so the mock
    # listens on 127.0.0.1 itself (the hosted-gateway override RELAY_HOSTED_URL points here).
    server = ThreadingHTTPServer(("127.0.0.1", PORT), Handler)
    print(f"mock model on 127.0.0.1:{PORT}, log {LOG}", flush=True)
    server.serve_forever()
