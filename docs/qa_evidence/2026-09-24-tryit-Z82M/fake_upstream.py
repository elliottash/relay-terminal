#!/usr/bin/env python3
"""The fake OpenRouter for the #Z82M Try-it stage: a scripted chat that calls
media_generate once, and a picture endpoint that returns a real PNG.

Only this and the model's decision are faked: the gateway, the registration
proof, the desktop's media tool and the quota chip are the shipped code, run
against this local listener instead of openrouter.ai.
"""
import base64
import json
import sys
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

PNG = Path(sys.argv[2] if len(sys.argv) > 2 else "lighthouse.png").read_bytes()
PNG_B64 = base64.b64encode(PNG).decode()
PORT = int(sys.argv[1])


def sse(payload: dict) -> bytes:
    return b"data: " + json.dumps(payload).encode() + b"\n\n"


class Handler(BaseHTTPRequestHandler):
    def log_message(self, *args):
        pass

    def do_POST(self):
        body = json.loads(self.rfile.read(int(self.headers.get("Content-Length", 0))) or b"{}")
        if self.path.endswith("/images"):
            reply = json.dumps({"created": 1, "data": [{"b64_json": PNG_B64}],
                                "usage": {"cost": 0.003}}).encode()
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(reply)))
            self.end_headers()
            self.wfile.write(reply)
            return
        if not self.path.endswith("/chat/completions"):
            self.send_response(404)
            self.send_header("Content-Length", "0")
            self.end_headers()
            return
        tool_ran = any(m.get("role") == "tool" for m in body.get("messages", []))
        self.send_response(200)
        self.send_header("Content-Type", "text/event-stream")
        self.send_header("Connection", "close")
        self.end_headers()
        if not tool_ran:
            call = {"index": 0, "id": "call_tryit", "type": "function",
                    "function": {"name": "media_generate",
                                 "arguments": json.dumps({
                                     "kind": "image",
                                     "prompt": "a lighthouse on a rocky coast at dusk, "
                                               "a warm beam sweeping over dark water",
                                     "output_path": "lighthouse"})}}
            self.wfile.write(sse({"choices": [{"delta": {"role": "assistant",
                                                         "tool_calls": [call]}, "index": 0}]}))
            self.wfile.write(sse({"choices": [{"delta": {}, "finish_reason": "tool_calls",
                                               "index": 0}]}))
        else:
            self.wfile.write(sse({"choices": [{"delta": {"content": "Here is your "
                                                                        "lighthouse."},
                                                "index": 0}]}))
            self.wfile.write(sse({"choices": [{"delta": {}, "finish_reason": "stop",
                                               "index": 0}]}))
            self.wfile.write(sse({"choices": [], "usage": {"prompt_tokens": 60,
                                                           "completion_tokens": 50}}))
        self.wfile.write(b"data: [DONE]\n\n")
        self.wfile.flush()


if __name__ == "__main__":
    ThreadingHTTPServer(("127.0.0.1", PORT), Handler).serve_forever()
