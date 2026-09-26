# The evidence model for card #83YV's GUI drive: an OpenAI-compatible endpoint that answers
# one scripted turn. The agent loop, the worker, the shared kernel and the pty are the real
# ones; only the model half is this script, in the pattern of
# docs/qa_evidence/2026-09-20-agent-console-extraction/stub-provider.py.
#
# Turn: "add one to x and print it" -> tool call py_run_cell("print(x + 1)") -> tool call
# py_variables() -> final prose. Every other request gets a plain answer.

import json
from http.server import BaseHTTPRequestHandler, HTTPServer

MODEL = "stub-1"
TURN_ID = {"py": "call-py", "vars": "call-vars"}


def make_tool_call(call_id, name, arguments):
    return {"index": 0, "id": call_id, "type": "function",
            "function": {"name": name, "arguments": json.dumps(arguments)}}


def body_text(payload):
    return "\n".join(str(m.get("content") or "") for m in payload.get("messages", []))


def wants_prompt(payload):
    return "add one to x" in body_text(payload)


def tool_result_seen(payload, call_id):
    return any(m.get("role") == "tool" and m.get("tool_call_id") == call_id
               for m in payload.get("messages", []))


def offered(payload, tool):
    return any(t.get("function", {}).get("name") == tool for t in payload.get("tools", []))


def completion(model, message, finish="stop"):
    return {"id": "chatcmpl-stub", "object": "chat.completion", "created": 1, "model": model,
            "choices": [{"index": 0, "finish_reason": finish, "message": message}],
            "usage": {"prompt_tokens": 1, "completion_tokens": 1, "total_tokens": 2}}


class Handler(BaseHTTPRequestHandler):
    def log_message(self, *args):
        pass

    def _send(self, code, obj):
        raw = json.dumps(obj).encode()
        self.send_response(code)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(raw)))
        self.end_headers()
        self.wfile.write(raw)

    def do_GET(self):
        if self.path.rstrip("/").endswith("/models"):
            self._send(200, {"object": "list", "data": [{"id": MODEL, "object": "model"}]})
        else:
            self._send(404, {"error": "not found"})

    def do_POST(self):
        if not self.path.rstrip("/").endswith("/chat/completions"):
            self._send(404, {"error": "not found"})
            return
        length = int(self.headers.get("Content-Length") or 0)
        payload = json.loads(self.rfile.read(length) or b"{}")
        model = payload.get("model") or MODEL
        if wants_prompt(payload) and offered(payload, "py_run_cell"):
            if not tool_result_seen(payload, TURN_ID["py"]):
                call = make_tool_call(TURN_ID["py"], "py_run_cell",
                                      {"code": "print(x + 1)", "intent": "add one to the user's x"})
                self._send(200, completion(model, {"role": "assistant", "content": None,
                                                   "tool_calls": [call]}, "tool_calls"))
                return
            if offered(payload, "py_variables") and not tool_result_seen(payload, TURN_ID["vars"]):
                call = make_tool_call(TURN_ID["vars"], "py_variables", {})
                self._send(200, completion(model, {"role": "assistant", "content": None,
                                                   "tool_calls": [call]}, "tool_calls"))
                return
            self._send(200, completion(model, {
                "role": "assistant",
                "content": "x + 1 printed 42 in the console, and py_variables lists x = 41."}))
            return
        self._send(200, completion(model, {"role": "assistant",
                                           "content": "The stub model answers this card's turn only."}))


if __name__ == "__main__":
    HTTPServer(("127.0.0.1", int(__import__("sys").argv[1])), Handler).serve_forever()
