#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""An OpenAI-compatible endpoint that makes the helper look up a key and then move it (#GMCF).

Two things have to be seen live: the helper's request carries `set_keybinding` and an
`app_action_list` that answers with keys, and the key it writes reaches the running app.  So this
stub plays one scene in three steps — look the action up, rebind it, say what it did — and dumps
every request it was sent (the tool names, and the last tool result) into `<dump>/req-NN.json`,
which is what the checks are read from.

A scene is picked by a **keyword in a user message**, never by the last message: Relay's worker
appends a user-role message of its own at the end of a turn (the QA note in
docs/qa_evidence/2026-09-19-helpful-line-breaks), so keying on the last one takes the scene away
from the prompt that was typed.

    python3 stub-provider.py <port> <dump-dir>
"""
import json
import sys
from http.server import BaseHTTPRequestHandler, HTTPServer
from pathlib import Path

PORT = int(sys.argv[1]) if len(sys.argv) > 1 else 8861
DUMP = Path(sys.argv[2] if len(sys.argv) > 2 else ".")

#: The word the run types. Everything else gets a shrug, so a stray turn cannot rebind anything.
KEYWORD = "move the close pane shortcut"

#: What the action is moved to. Free in every preset table, so nothing it displaces matters.
NEW_KEYS = ["Ctrl+Alt+Shift+K"]


def user_text(messages):
    out = []
    for message in messages:
        if message.get("role") != "user":
            continue
        content = message.get("content")
        if isinstance(content, list):
            content = " ".join(p.get("text", "") for p in content if isinstance(p, dict))
        out.append((content or "").lower())
    return "\n".join(out)


def calls_so_far(messages):
    """Which of this scene's tools the helper has already run, in order."""
    names = []
    for message in messages:
        for call in message.get("tool_calls") or []:
            names.append(((call.get("function") or {}).get("name")) or "")
    return names


def tool_call(name, arguments):
    return {"role": "assistant", "content": "",
            "tool_calls": [{"id": f"call-{name}", "type": "function",
                            "function": {"name": name, "arguments": json.dumps(arguments)}}]}


class Handler(BaseHTTPRequestHandler):
    seen = 0

    def log_message(self, *args):
        pass

    def do_POST(self):
        body = self.rfile.read(int(self.headers.get("content-length", 0)) or 0)
        request = json.loads(body or b"{}")
        messages = request.get("messages") or []
        tools = [((t.get("function") or {}).get("name")) or "" for t in request.get("tools") or []]
        Handler.seen += 1
        results = [m for m in messages if m.get("role") == "tool"]
        DUMP.mkdir(parents=True, exist_ok=True)
        (DUMP / f"req-{Handler.seen:02d}.json").write_text(json.dumps({
            "tools": tools,
            "system_head": (messages[0].get("content") or "")[:4000] if messages else "",
            "calls": calls_so_far(messages),
            "last_tool_result": (results[-1].get("content") if results else None),
        }, indent=1), encoding="utf-8")

        done = calls_so_far(messages)
        if KEYWORD not in user_text(messages):
            answer = {"role": "assistant", "content": "Nothing to say here."}
        elif "app_action_list" not in done:
            answer = tool_call("app_action_list", {"search": "close pane"})
        elif "set_keybinding" not in done:
            answer = tool_call("set_keybinding", {"action": "pane.close", "keys": NEW_KEYS})
        else:
            answer = {"role": "assistant",
                      "content": f"Done: pane.close is on {NEW_KEYS[0]} now."}
        self.stream(answer)

    def stream(self, answer):
        self.send_response(200)
        self.send_header("content-type", "text/event-stream")
        self.end_headers()

        def chunk(delta, finish_reason=None):
            payload = {"id": "stub", "object": "chat.completion.chunk", "created": 0,
                       "model": "stub",
                       "choices": [{"index": 0, "delta": delta, "finish_reason": finish_reason}]}
            self.wfile.write(b"data: " + json.dumps(payload).encode() + b"\n\n")
            self.wfile.flush()

        if answer.get("tool_calls"):
            chunk({"role": "assistant", "content": "", "tool_calls": [
                {"index": 0, **call} for call in answer["tool_calls"]]})
            chunk({}, "tool_calls")
        else:
            chunk({"role": "assistant", "content": ""})
            chunk({"content": answer.get("content") or ""})
            chunk({}, "stop")
        self.wfile.write(b"data: [DONE]\n\n")
        self.wfile.flush()

    def do_GET(self):
        # `/v1/models`, which Relay asks for when it configures a local endpoint.
        self.send_response(200)
        self.send_header("content-type", "application/json")
        self.end_headers()
        self.wfile.write(json.dumps({"data": [{"id": "stub"}]}).encode())


if __name__ == "__main__":
    HTTPServer(("127.0.0.1", PORT), Handler).serve_forever()
