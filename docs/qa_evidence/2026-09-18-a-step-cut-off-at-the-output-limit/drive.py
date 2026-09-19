#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Card #G5MK evidence: drive a real Agent and a real ChatProvider against a mock SSE endpoint
that reaches the output limit, and print what the pane would see.

Three runs, selected by argv[1]:
  retried   first call: reasoning only, finish_reason=length -> retried, second call answers
  twice     both calls truncate -> the turn fails, the request stays open
  partial   answer text then finish_reason=length -> no retry, the text is kept

Usage: PYTHONPATH=backend python3 drive.py retried
"""
import json
import sys
import tempfile
import threading
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

from relay_core.agent import Agent
from relay_core.provider import ProviderConfig

MODE = sys.argv[1] if len(sys.argv) > 1 else "retried"
STATE = {"calls": 0, "sent": []}


def sse(*chunks):
    out = b""
    for chunk in chunks:
        out += b"data: " + json.dumps(chunk).encode() + b"\n\n"
    return out + b"data: [DONE]\n\n"


def delta(d=None, finish=None):
    return {"choices": [{"delta": d or {}, "finish_reason": finish}]}


class Handler(BaseHTTPRequestHandler):
    def log_message(self, *args):
        pass

    def do_POST(self):
        body = json.loads(self.rfile.read(int(self.headers["Content-Length"])))
        STATE["calls"] += 1
        STATE["sent"].append(body)
        call = STATE["calls"]
        print(f"  -> request {call}: max_tokens={body['max_tokens']} messages={len(body['messages'])}")
        truncate = call == 1 or (MODE == "twice" and call == 2)
        if truncate:
            payload = [{"choices": [], "usage": {"prompt_tokens": 48000,
                                                 "completion_tokens": body["max_tokens"],
                                                 "total_tokens": 48000 + body["max_tokens"]}},
                       delta({"reasoning_content": "thinking that fills the whole budget "}),
                       delta({"reasoning_content": "and never reaches an answer"})]
            if MODE == "partial":
                payload.append(delta({"content": "The cyan reads dark because "}))
            payload.append(delta(finish="length"))
            data = sse(*payload)
        else:
            data = sse(delta({"content": "Done: shell #0a6b8a, agent #7c3aed."}),
                       delta(finish="stop"))
        self.send_response(200)
        self.send_header("Content-Type", "text/event-stream")
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)


server = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
threading.Thread(target=server.serve_forever, daemon=True).start()
port = server.server_address[1]

events = []


def emit(event):
    events.append(event)
    kind = event.get("event")
    if kind in ("provider_retry", "error", "done", "delta", "status", "usage"):
        detail = event.get("text") or json.dumps(event.get("usage") or "")
        if kind == "status" and not detail.startswith("Output limit"):
            return
        print(f"  {kind}: {detail}")


with tempfile.TemporaryDirectory() as workspace:
    config = ProviderConfig(f"http://127.0.0.1:{port}/v1", "mock-5.3", "", max_tokens=32768)
    agent = Agent(config, workspace, emit)
    print(f"== {MODE} ==")
    agent.ask("in the light theme, the terminal and agent colors are too dark")
    print(f"  provider calls: {STATE['calls']}")
    print(f"  usage counted: {json.dumps(agent.usage_totals)}")
    print(f"  requests still open: {agent.requests.open_count()}")
    for message in agent.messages[1:]:
        content = (message.get("content") or "")
        if not isinstance(content, str):
            content = "<parts>"
        print(f"  message {message['role']}"
              f"{'/' + message['relay_kind'] if message.get('relay_kind') else ''}: "
              f"{content[:150].replace(chr(10), ' ')}")
server.shutdown()
