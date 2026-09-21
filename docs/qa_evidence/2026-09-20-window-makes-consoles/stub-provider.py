#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Loopback-only OpenAI-compatible endpoint for the #AGNT step 5 live run.

    python3 stub-provider.py 8871

Nothing here calls a provider: the profile points a local model endpoint at this process, so every
agent in the run — the terminal pane's, and the consoles the window makes for Options and Sessions
— answers out of the scenes below.  A scene is picked by a **keyword in a user message** rather
than by the last one, because the worker appends a user-role message of its own at the end of a
turn (the completion check) and that would take the scene away from the prompt that was typed.

The scenes, and what each is evidence of:

  "which panes are open"  app_panes, then the answer quotes the list it got back
                          -> `list_panes` reads `RelayWindow::allPanes()`, which is the same walk
                             `syncTabShares` publishes to the phone with.  An embedded console
                             must not be in it (card #AGNT step 5 item 2).
  "where is copy on"      prose with an `option:` link
                          -> a console's answer is an ordinary pane transcript, so the link is a
                             link (step 8) and the context resolves it in place.
  "say hello"             plain prose, short
                          -> a second console of the same tab, on the same worker and the same
                             conversation.

Anything else answers one line, so a stray turn cannot hang the run.
"""
import json
import sys
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

ROW = "option:terminal/copy_on_select"
OPTION_LINK = f"option:terminal/{ROW}"
PANES_KEY = "which panes are open"


def call(index, name, args):
    return {"id": f"call_{index}", "type": "function",
            "function": {"name": name, "arguments": json.dumps(args)}}


SCENES = {
    PANES_KEY: [
        ("", [call(1, "app_panes", {})]),
        ("", []),          # filled from the tool result, below
    ],
    "where is copy on": [
        (f"It is in Terminal: [Copy on select]({OPTION_LINK}). Clicking that opens the row.", []),
    ],
    "say hello": [
        ("Hello from the tab's one agent.", []),
    ],
}


def _text(message):
    content = message.get("content")
    if isinstance(content, list):
        content = " ".join(p.get("text", "") for p in content if isinstance(p, dict))
    return content or ""


def _pane_answer(messages):
    """Quote back exactly what the GUI answered `app_panes` with.

    The point of the scene is that the *window's* list reaches the transcript unedited, so the
    screenshot can be read for what is in it and — more to the point — what is not.
    """
    for message in reversed(messages):
        if message.get("role") != "tool":
            continue
        try:
            result = json.loads(_text(message))
        except Exception:
            continue
        panes = result.get("panes")
        if not isinstance(panes, list):
            continue
        titles = [str(p.get("title") or p.get("id")) for p in panes if isinstance(p, dict)]
        return ("PANELIST count=%d titles=%s" % (len(titles), " | ".join(titles) or "(none)"))
    return "PANELIST count=? titles=(no result)"


def scene(request):
    messages = request.get("messages") or []
    picked, at = None, -1
    for i, message in enumerate(messages):
        if message.get("role") != "user":
            continue
        text = _text(message).lower()
        for key in SCENES:
            if key in text:
                picked, at = key, i
    if picked is None:
        return "Nothing to do here.", []
    step = sum(1 for m in messages[at + 1:] if m.get("role") == "assistant" and m.get("tool_calls"))
    steps = SCENES[picked]
    prose, calls = steps[min(step, len(steps) - 1)]
    if picked == PANES_KEY and step >= 1:
        prose = _pane_answer(messages)
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
                self.wfile.write(b"data: " + json.dumps(data).encode() + b"\n\n")
                self.wfile.flush()

            if prose:
                for piece in pieces(prose):
                    time.sleep(0.2)
                    chunk({"role": "assistant", "content": piece})
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
