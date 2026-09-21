#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Loopback-only OpenAI-compatible endpoint for the console write/Undo drive (card #AGNT).

    python3 stub-provider.py 8893

Nothing here calls a provider: the profile points a local model endpoint at this process, so the
Options helper's console answers out of the four scenes below. It is the sibling drive's stub
(`../2026-09-21-agents-are-consoles/stub-provider.py`) cut down to the app-command scenes and
given the two it did not have — an action, and the agent taking its own change back.

A scene is picked by a **keyword in a user message**, never by the last message: the worker
appends a user-role message of its own at the end of a turn, and that would take the scene away
from the prompt that was typed (the QA-stub gotcha). Within a scene the step is how many
assistant messages with tool calls have been sent since the message that picked it.

  "turn on copy on"      app_option_set   -> a write from a console: the row's marker, and the
                                            notice with its Undo (#FEJQ decision 6, §30.6)
  "put copy on select"   app_undo         -> the *agent* taking its own change back, which is
                                            itself a change and is announced like one
  "reload the themes"    app_action_run   -> "Agent ran Reload themes"
  "say hello"            prose            -> a plain turn

The change_id `app_undo` needs is read out of the earlier tool result, the way a model would
read it out of its own transcript.
"""
import json
import sys
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

# The row the option scenes name. A plain switch, so `app_option_set` can flip it and the
# notification's Undo can put it back. `RelayWindow::toggleRow` builds `row.id = "option:" + key`.
ROW_ID = "option:terminal/copy_on_select"


def call(index, name, args):
    return {"id": f"call_{index}", "type": "function",
            "function": {"name": name, "arguments": json.dumps(args)}}


def _text(message):
    content = message.get("content")
    if isinstance(content, list):
        return " ".join(part.get("text", "") for part in content if isinstance(part, dict))
    return content or ""


def _change_id(messages):
    """The change_id of the last option write, off the tool results already in the transcript."""
    found = ""
    for message in messages:
        if message.get("role") != "tool":
            continue
        try:
            result = json.loads(_text(message))
        except Exception:
            continue
        if isinstance(result, dict) and result.get("change_id"):
            found = str(result["change_id"])
    return found


def scenes(messages):
    return {
        "turn on copy on": [
            ("", [call(1, "app_option_set", {"id": ROW_ID, "value": True})], ""),
            ("I turned Copy on select on for you. Undo is on the notice if you would rather not.",
             [], ""),
        ],
        "put copy on select": [
            ("", [call(1, "app_undo", {"change_id": _change_id(messages) or "c1"})], ""),
            ("I put Copy on select back the way it was.", [], ""),
        ],
        "reload the themes": [
            ("", [call(1, "app_action_run", {"key": "theme.reload"})], ""),
            ("I reloaded the themes.", [], ""),
        ],
        "say hello": [("Hello from the Options helper.", [], "")],
    }


def scene(request):
    messages = request.get("messages") or []
    table = scenes(messages)
    picked, at = None, -1
    for i, message in enumerate(messages):
        if message.get("role") != "user":
            continue
        text = _text(message).lower()
        for key in table:
            if key in text:
                picked, at = key, i
    if picked is None:
        return "Nothing to do here.", []
    step = sum(1 for m in messages[at + 1:]
               if m.get("role") == "assistant" and m.get("tool_calls"))
    steps = table[picked]
    prose, calls, _ = steps[min(step, len(steps) - 1)]
    return prose, calls


def pieces(text):
    words = text.split(" ")
    third = max(1, len(words) // 3)
    return [" ".join(words[:third]) + " ", " ".join(words[third:2 * third]) + " ",
            " ".join(words[2 * third:])]


class Handler(BaseHTTPRequestHandler):
    def log_message(self, *args):
        pass

    def do_POST(self):
        length = int(self.headers.get("Content-Length") or 0)
        request = json.loads(self.rfile.read(length) or b"{}")
        prose, calls = scene(request)
        model = request.get("model") or "stub"
        finish = "tool_calls" if calls else "stop"
        if request.get("stream"):
            self.send_response(200)
            self.send_header("Content-Type", "text/event-stream")
            self.end_headers()

            def chunk(delta, finish_reason=None):
                data = {"id": "stub", "object": "chat.completion.chunk", "created": 0,
                        "model": model,
                        "choices": [{"index": 0, "delta": delta, "finish_reason": finish_reason}]}
                try:
                    self.wfile.write(b"data: " + json.dumps(data).encode() + b"\n\n")
                    self.wfile.flush()
                except BrokenPipeError:
                    raise SystemExit(0)

            if prose:
                for piece in pieces(prose):
                    time.sleep(0.2)
                    chunk({"role": "assistant", "content": piece + " "})
            if calls:
                time.sleep(0.2)
                chunk({"role": "assistant", "content": None,
                       "tool_calls": [dict(c, index=i) for i, c in enumerate(calls)]})
            chunk({}, finish)
            self.wfile.write(b"data: [DONE]\n\n")
            return
        message = {"role": "assistant", "content": prose or None}
        if calls:
            message["tool_calls"] = calls
        payload = {"id": "stub", "object": "chat.completion", "created": 0, "model": model,
                   "choices": [{"index": 0, "message": message, "finish_reason": finish}],
                   "usage": {"prompt_tokens": 1, "completion_tokens": 1, "total_tokens": 2}}
        raw = json.dumps(payload).encode()
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(raw)))
        self.end_headers()
        self.wfile.write(raw)

    def do_GET(self):
        raw = json.dumps({"data": [{"id": "stub"}]}).encode()
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(raw)))
        self.end_headers()
        self.wfile.write(raw)


if __name__ == "__main__":
    ThreadingHTTPServer(("127.0.0.1", int(sys.argv[1])), Handler).serve_forever()
