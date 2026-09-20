#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Loopback-only OpenAI-compatible endpoint for the #FEJQ live run.

    python3 stub-provider.py 8831 <CARD-ID>

Nothing here calls a provider: the profile points a local model endpoint at this process, and
every agent in the run — the terminal pane's and the tab's helper — answers out of the scenes
below.  A scene is picked by a **keyword in a user message**, not by the last message, because the
worker appends a user-role message of its own at the end of a turn (the completion check) and that
would take the scene away from the prompt that was typed.  Within a scene the step is how many
assistant messages with tool calls have been sent since the message that picked it.

The scenes, and what each is evidence of:

  "open options at copy"   app_open {options, terminal, <the row>}                 -> §30.3 `open`
  "turn copy on select on" app_option_set {<the row>: true}                    -> §30.3 and §30.6
  "run the activity action" app_action_run {agent.internalsPane}                  -> agent_safe
  "search my sessions"     app_sessions_search {relay}                            -> worker-side
  "open the fixture card"  app_open {switchboard, <CARD-ID>}                       -> the board
  "where is copy on"       prose with an `option:` link                    -> a helper's answer
  "which conversation"     prose with an `option:` link, asked in Sessions -> the manager's link
  "switch copy on select"  app_option_set, asked *in the Options helper*   -> the helper's own
                                                                              app_command pipe
  "is this board mine"     plain prose                                     -> one tab's own log

Anything else answers one line, so a stray turn cannot hang the run.
"""
import json
import sys
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

CARD = (sys.argv[2] if len(sys.argv) > 2 else "AAAA").upper()
# A toggle row's catalog id is "option:" + its QSettings key (RelayWindow::toggleRow);
# the first live run learned that the hard way, with "Relay has no option row" in the
# transcript. The link scheme is `option:<section>/<row>`, split at the *first* slash,
# so a row id with a slash of its own arrives whole.
ROW = "option:terminal/copy_on_select"
OPTION_LINK = f"option:terminal/{ROW}"


def call(index, name, args):
    return {"id": f"call_{index}", "type": "function",
            "function": {"name": name, "arguments": json.dumps(args)}}


def scenes():
    return {
        "open options at copy": [
            ("I will open Options at that row.",
             [call(1, "app_open", {"target": "options", "section": "terminal", "row": ROW})]),
            (f"Options is open at **Copy on select** (`{ROW}`), in the Terminal section.", []),
        ],
        "turn copy on select on": [
            ("", [call(2, "app_option_set", {"id": ROW, "value": True})]),
            ("Done: **Copy on select** went from off to on. The row is marked in Options and the "
             "notification offers Undo.", []),
        ],
        "run the activity action": [
            ("", [call(3, "app_action_run", {"key": "agent.internalsPane"})]),
            ("Ran the `agent.internalsPane` action, which opens the Activity pane beside the "
             "terminal.", []),
        ],
        "search my sessions": [
            ("", [call(4, "app_sessions_search", {"query": "relay", "limit": 5})]),
            ("That search was answered inside Relay, out of the session index — no pane had to "
             "be open and nothing was sent anywhere.", []),
        ],
        "open the fixture card": [
            ("", [call(5, "app_open", {"target": "switchboard", "card": CARD})]),
            (f"The Switchboard is open on `#{CARD}`.", []),
        ],
        "switch copy on select": [
            ("", [call(6, "app_option_set", {"id": ROW, "value": True})]),
            ("I turned **Copy on select** on for you: off → on. Undo is on the notification, and "
             "the row says so until you touch it.", []),
        ],
        "where is copy on": [
            (f"It is in Terminal: [Copy on select]({OPTION_LINK}). Clicking that opens the row.", []),
        ],
        "which conversation": [
            (f"The setting those turns were about is [Copy on select]({OPTION_LINK}).", []),
        ],
        "is this board mine": [
            ("This answer belongs to the tab it was asked in, and to no other.", []),
        ],
    }


def scene(request):
    messages = request.get("messages") or []
    table = scenes()
    picked, at = None, -1
    for i, message in enumerate(messages):
        if message.get("role") != "user":
            continue
        content = message.get("content")
        if isinstance(content, list):
            content = " ".join(p.get("text", "") for p in content if isinstance(p, dict))
        text = (content or "").lower()
        for key in table:
            if key in text:
                picked, at = key, i
    if picked is None:
        return "Nothing to do here.", []
    step = sum(1 for m in messages[at + 1:] if m.get("role") == "assistant" and m.get("tool_calls"))
    steps = table[picked]
    return steps[min(step, len(steps) - 1)]


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
                # Indexed: the worker refuses an unindexed streamed tool call.
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
