#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""A loopback-only OpenAI-compatible endpoint for the pane-title QA run (issue JRWQ).

The run needs real agent turns and real side calls (real worker, real autosave, real cadence)
but must not reach the network or use anybody's API key. This answers /v1/chat/completions from
a canned table:

* a request that carries `tools` is an agent turn;
* a request whose system prompt names a coding session is the title side call (protocol 18.1);
* a request whose system prompt labels a tab is the tab_label call (protocol 18.3).

Every call is appended to calls.log next to this script, so the run can be checked for how many
title calls actually went out (the cadence: one after the first turn, none for the next four).

    python3 stub-provider.py 8747
"""
import json
import sys
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

LOG = Path(__file__).with_name("calls.log")

# Pane titles, by a word in the conversation. The panes in the run are deliberately on unrelated
# work, so the tab label has to join them with a semicolon.
TITLES = [
    ("drag", "Fixing pane drag"),
    ("release notes", "Release notes for 0.1"),
    ("keyring", "Importing keys from the keyring"),
]
REPLIES = [
    ("drag", "I looked at the pane drag code: dropTarget() picks the nearest edge."),
    ("release notes", "Here is a draft of the release notes for the 0.1 preview."),
    ("keyring", "Keys come from the desktop keyring through relay_core.keystore."),
]


def pick(table, text, default):
    for needle, value in table:
        if needle in text.lower():
            return value
    return default


class Handler(BaseHTTPRequestHandler):
    def do_POST(self):
        length = int(self.headers.get("Content-Length") or 0)
        request = json.loads(self.rfile.read(length) or b"{}")
        messages = request.get("messages") or []
        system = str((messages[0] or {}).get("content") or "") if messages else ""
        # Only what Relay sent as input: the system prompts name example titles of their own.
        body = "\n".join(str(m.get("content") or "") for m in messages if m.get("role") != "system")
        if request.get("tools"):
            kind = "turn"
            last = next((str(m.get("content") or "") for m in reversed(messages) if m.get("role") == "user"), "")
            text = pick(REPLIES, last, "I read your message and kept it in this conversation.")
        elif system.startswith("You name a coding session"):
            kind = "title"
            text = json.dumps({"title": pick(TITLES, body, "Looking around the repository")})
        elif system.startswith("You label a terminal tab"):
            kind = "tab_label"
            # These panes are on different work: Relay joins their titles itself.
            text = json.dumps({"related": False, "label": ""})
        else:
            kind = "other"
            text = "ok"
        with LOG.open("a", encoding="utf-8") as out:
            out.write(f"{time.strftime('%H:%M:%S')} {kind}\n")
        payload = json.dumps({"id": "qa", "object": "chat.completion", "model": request.get("model", "stub"),
                              "choices": [{"index": 0, "finish_reason": "stop",
                                           "message": {"role": "assistant", "content": text}}],
                              "usage": {"prompt_tokens": 20, "completion_tokens": 8, "total_tokens": 28}}).encode()
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(payload)))
        self.end_headers()
        self.wfile.write(payload)

    def log_message(self, *args):
        pass


if __name__ == "__main__":
    LOG.write_text("")
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 8747
    ThreadingHTTPServer(("127.0.0.1", port), Handler).serve_forever()
