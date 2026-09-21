#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""A scripted, streaming OpenAI-compatible model for the #PH0N hosted drive.

    fake-model.py <port> <request log>

Relay sees it as a local endpoint (`local:fake`), so the run costs nothing and needs no key.
The scene is picked by a keyword in the prompt the phone typed — the *last user message typed at
the prompt*, not the last user-role message, because the worker appends user-role messages of its
own at the end of a turn (memory note "QA stub provider gotchas"):

  QUICK      prose in four pieces over about six seconds, so the inbox chip has time to read
             "running · Ns" before it reads "finished"
  SLOWTURN   a sentence every half second for two minutes — long enough that only Stop ends it
  ASKME      step 0: one `ask_user` call with two options; step 1 (after the answer came back as
             the tool result): "You chose: <the answer>"
  OFFLINEQ   one short line — the request log is what proves it arrived exactly once
  GUESTQ     one short line
  anything else: "Done."

With no tools in the request it is a side call: a recap (the system prompt names one) is answered
with the JSON the recap expects, and anything else (a pane title, a suggestion) with two words.

Every request is logged as one JSON line: the tail of each user message, whether tools were
offered, and the scene picked — that log is read by run.py (an OFFLINEQ that arrived twice would
be two lines).
"""
import json
import sys
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

PORT, LOG = int(sys.argv[1]), sys.argv[2]
SCENES = ("QUICK", "SLOWTURN", "ASKME", "OFFLINEQ", "GUESTQ")


def text_of(message) -> str:
    content = message.get("content")
    if isinstance(content, list):
        content = " ".join(part.get("text", "") for part in content if isinstance(part, dict))
    return content or ""


def ask_call():
    return {"id": "call_ask_1", "type": "function", "function": {"name": "ask_user", "arguments": json.dumps({
        "questions": [{"header": "Which branch", "question": "Which branch should the fix land on?",
                       "options": [{"label": "main", "description": "The shared branch everyone commits to.",
                                    "recommended": True},
                                   {"label": "a side branch", "description": "Off to one side, merged later."}]}]})}}


def scene(request):
    messages = request.get("messages") or []
    if not request.get("tools"):
        system = " ".join(text_of(m) for m in messages if m.get("role") == "system")
        if "recap" in system.lower():
            return "recap", json.dumps({"summary": "RECAP: the phone sent prompts, stopped a turn and answered "
                                                   "the branch question; everything else stands as before.",
                                        "next_action": "carry on from the phone"}), []
        return "side", "Drive pane", []
    last, word = -1, ""
    for i, m in enumerate(messages):
        if m.get("role") != "user":
            continue
        text = text_of(m)
        hit = next((s for s in SCENES if s in text), "")
        if hit:
            last, word = i, hit
    step = sum(1 for m in messages[last + 1:] if m.get("role") == "assistant" and m.get("tool_calls"))
    if word == "QUICK":
        return word, "The pwd command prints the absolute path of the working directory, and this answer came from the fake model.", []
    if word == "SLOWTURN":
        return word, "SLOW", []
    if word == "ASKME":
        if step == 0:
            return word, "", [ask_call()]
        # The tool result is the worker's JSON: {"ok": true, "answers": [{"header", "question",
        # "answer"}], "summary": "…"}; the answer's text is what the phone tapped.
        raw = next((text_of(m) for m in reversed(messages) if m.get("role") == "tool"), "")
        try:
            answer = json.loads(raw)["answers"][0]["answer"]
        except (ValueError, KeyError, IndexError, TypeError):
            answer = raw.strip()[:80]
        return word, f"You chose: {answer}", []
    if word == "OFFLINEQ":
        return word, "The offline prompt arrived, once.", []
    if word == "GUESTQ":
        return word, "The guest's prompt ran on the desktop.", []
    return "default", "Done.", []


class Handler(BaseHTTPRequestHandler):
    def log_message(self, *args):
        pass

    def do_GET(self):
        raw = json.dumps({"object": "list", "data": [{"id": "fake", "object": "model"}]}).encode()
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(raw)))
        self.end_headers()
        self.wfile.write(raw)

    def chunk(self, delta, finish=None):
        data = {"id": "fake", "object": "chat.completion.chunk", "created": 0, "model": "fake",
                "choices": [{"index": 0, "delta": delta, "finish_reason": finish}]}
        self.wfile.write(b"data: " + json.dumps(data).encode() + b"\n\n")
        self.wfile.flush()

    def do_POST(self):
        request = json.loads(self.rfile.read(int(self.headers.get("Content-Length") or 0)) or b"{}")
        name, prose, calls = scene(request)
        messages = request.get("messages") or []
        with open(LOG, "a", encoding="utf-8") as log:
            log.write(json.dumps({"at": time.time(), "scene": name, "tools": bool(request.get("tools")),
                                  "stream": bool(request.get("stream")),
                                  "users": [text_of(m)[-70:] for m in messages if m.get("role") == "user"]}) + "\n")
        finish = "tool_calls" if calls else "stop"
        if not request.get("stream"):
            message = {"role": "assistant", "content": prose or None}
            if calls:
                message["tool_calls"] = calls
            raw = json.dumps({"id": "fake", "object": "chat.completion", "created": 0, "model": "fake",
                              "choices": [{"index": 0, "message": message, "finish_reason": finish}],
                              "usage": {"prompt_tokens": 1, "completion_tokens": 1, "total_tokens": 2}}).encode()
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(raw)))
            self.end_headers()
            self.wfile.write(raw)
            return
        self.send_response(200)
        self.send_header("Content-Type", "text/event-stream")
        self.end_headers()
        try:
            if prose == "SLOW":
                self.chunk({"role": "assistant", "content": "This turn is slow on purpose. "})
                for n in range(1, 241):
                    time.sleep(0.5)
                    self.chunk({"content": f"Sentence {n} of a long answer. "})
            elif prose:
                words = prose.split(" ")
                quarter = max(1, len(words) // 4)
                pieces = [" ".join(words[i:i + quarter]) + " " for i in range(0, len(words), quarter)]
                self.chunk({"role": "assistant", "content": pieces[0]})
                for piece in pieces[1:]:
                    time.sleep(1.5 if name == "QUICK" else 0.3)
                    self.chunk({"content": piece})
            if calls:
                time.sleep(0.3)
                self.chunk({"role": "assistant", "content": None,
                            "tool_calls": [dict(c, index=i) for i, c in enumerate(calls)]})
            self.chunk({}, finish)
            self.wfile.write(b"data: [DONE]\n\n")
        except (BrokenPipeError, ConnectionResetError):
            pass        # the turn was stopped: the worker hung up


if __name__ == "__main__":
    ThreadingHTTPServer(("127.0.0.1", PORT), Handler).serve_forever()
