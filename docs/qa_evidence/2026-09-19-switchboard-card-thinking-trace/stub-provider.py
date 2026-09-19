#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Loopback-only OpenAI-compatible endpoint for the card thinking-trace screenshots (#9K5H).

    python3 stub-provider.py 8841

Same shape as the thinking-fold stub (2026-09-19-thinking-fold): reasoning goes out as
`reasoning_content` deltas, the answer as `content` deltas, and the planner-asks stub
(2026-09-19-the-planner-asks-questions) supplies the tool-call rounds. What the first user
message says decides the turn:

  "Discuss** turn"  round 1: ~5 s of reasoning, then a `board_comment` call that posts the
                    mid-turn question to the card's thread; round 2 (after the tool result):
                    a second reasoning block, then the final answer.
  "Plan** turn"     round 1: `board_read` the card; round 2: ~5 s of reasoning, then a
                    `board_update_card` call that writes the `## Plan` section with the hash
                    round 1 was told; round 3: a short answer.

Anything else answers "Done." with no reasoning — the terminal panes' asks land there.
"""
import json
import re
import sys
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

DISCUSS_FIRST = [
    "The card asks where the thinking trace belongs.\n\n",
    "In the terminals the reasoning streams under a fold, so the owner can watch a plan being ",
    "made. A card discussion has no fold: the thread is the one surface both sides read.\n\n",
    "So the trace should run *in the thread* — above the answer it precedes, sealed where an ",
    "entry lands under it. Then a question I ask reads after the thinking it came from.\n\n",
    "Before answering I should ask the one thing that decides the shape: whether the trace is ",
    "also written to the card file, or stays a live view of the turn.\n",
]

DISCUSS_SECOND = [
    "Live view it is — the file is the record of what was said, not of what was thought.\n\n",
    "The answer follows: the trace lives in the thread, sealed above each entry, and never ",
    "reaches the card file.\n",
]

PLAN_FIRST = [
    "Planning the card: the trace must be shown in the Switchboard the same way the terminals ",
    "show it.\n\n",
    "The pieces: the worker already tags thinking_delta with the card; the card detail renders ",
    "the thread; the trace needs a block of its own between the entries.\n\n",
    "The plan writes itself: render the tail, seal on append, settle the header when the block ",
    "ends. Nothing is written to the card file by it.\n",
]

PLAN_SECOND = ["The plan section is written. A sentence to close the turn."]

ANSWER_DISCUSS = ("The trace belongs in the thread: it streams above the answer, seals where an "
                  "entry lands under it, and never reaches the card file.")
ANSWER_PLAN = "The plan is on the card. Execute hands it to a terminal pane as it is."


def chunks(parts, delay=0.55, key="reasoning_content"):
    for part in parts:
        time.sleep(delay)
        yield {key: part}


def call(name, arguments):
    return {"role": "assistant", "content": None,
            "tool_calls": [{"id": "call_" + name, "type": "function",
                            "function": {"name": name, "arguments": json.dumps(arguments)}}]}


def first_user(request):
    for m in request.get("messages") or []:
        if m.get("role") == "user":
            content = m.get("content")
            if isinstance(content, list):
                content = " ".join(p.get("text", "") for p in content if isinstance(p, dict))
            return content or ""
    return ""


def turn_view(request):
    """The last user message and the tool results that followed it: one card's conversation is
    reused across turns (protocol 19.4), so the whole history cannot be counted per turn."""
    messages = request.get("messages") or []
    last = -1
    for i, m in enumerate(messages):
        if m.get("role") == "user":
            last = i
    tail = messages[last + 1:] if last >= 0 else []
    text = ""
    if last >= 0:
        content = messages[last].get("content")
        if isinstance(content, list):
            content = " ".join(p.get("text", "") for p in content if isinstance(p, dict))
        text = content or ""
    return text, [m for m in tail if m.get("role") == "tool"]


def hash_of(request):
    for m in reversed(request.get("messages") or []):
        if m.get("role") == "tool":
            found = re.search(r'"hash"\s*:\s*"([0-9a-f]+)"', str(m.get("content") or ""))
            if found:
                return found.group(1)
    return "deadbeef"


def body(request):
    text, tools = turn_view(request)
    if "Discuss** turn" in text:
        if not tools:
            return {"reasoning": DISCUSS_FIRST,
                    "call": call("board_comment", {
                        "id": "TRC1", "kind": "question",
                        "text": "1. Should the thinking trace also be written to the card file, "
                                "or stay a live view of the running turn? I recommend the live "
                                "view: the file is the record of what was said, not what was "
                                "thought."})}
        return {"reasoning": DISCUSS_SECOND, "content": ANSWER_DISCUSS}
    if "Plan** turn" in text:
        if not tools:
            return {"call": call("board_read", {"id": "TRC1"})}
        if len(tools) == 1:
            return {"reasoning": PLAN_FIRST,
                    "call": call("board_update_card", {
                        "id": "TRC1", "base_hash": hash_of(request),
                        "replace_section": {
                            "heading": "Plan",
                            "text": "1. Render the reasoning tail in the card's thread, under a "
                                    "`thinking…` header that settles to `thought for N s`.\n"
                                    "2. Seal the block above any thread entry that lands under "
                                    "it, so a question reads after the thinking it came from.\n"
                                    "3. Never write the trace to the card file.\n"}})}
        return {"reasoning": PLAN_SECOND, "content": ANSWER_PLAN}
    return {"content": "Done."}


class Handler(BaseHTTPRequestHandler):
    def log_message(self, *args):
        pass

    def do_POST(self):
        length = int(self.headers.get("Content-Length") or 0)
        request = json.loads(self.rfile.read(length) or b"{}")
        turn = body(request)
        model = request.get("model") or "relay-qa-stub"
        deltas = list(chunks(turn.get("reasoning") or []))
        if request.get("stream"):
            self.send_response(200)
            self.send_header("Content-Type", "text/event-stream")
            self.end_headers()

            def emit(delta):
                data = {"id": "stub", "object": "chat.completion.chunk", "created": 0,
                        "model": model, "choices": [{"index": 0, "delta": delta,
                                                     "finish_reason": None}]}
                self.wfile.write(b"data: " + json.dumps(data).encode() + b"\n\n")

            for delta in deltas:
                emit(delta)
            finish = "stop"
            if "call" in turn:
                head = dict(turn["call"])
                head["tool_calls"] = [dict(c, index=i) for i, c in enumerate(head["tool_calls"])]
                emit(head)
                finish = "tool_calls"
            elif "content" in turn:
                emit({"content": turn["content"]})
            data = {"id": "stub", "object": "chat.completion.chunk", "created": 0, "model": model,
                    "choices": [{"index": 0, "delta": {}, "finish_reason": finish}]}
            self.wfile.write(b"data: " + json.dumps(data).encode() + b"\n\n")
            self.wfile.write(b"data: [DONE]\n\n")
            return
        message = {"role": "assistant",
                   "reasoning_content": "".join(d.get("reasoning_content", "") for d in deltas),
                   "content": ""}
        finish = "stop"
        if "call" in turn:
            message.update(turn["call"])
            finish = "tool_calls"
        else:
            message["content"] = turn.get("content", "")
        payload = {"id": "stub", "object": "chat.completion", "created": 0, "model": model,
                   "choices": [{"index": 0, "message": message, "finish_reason": finish}],
                   "usage": {"prompt_tokens": 1, "completion_tokens": 1, "total_tokens": 2}}
        raw = json.dumps(payload).encode()
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(raw)))
        self.end_headers()
        self.wfile.write(raw)


if __name__ == "__main__":
    ThreadingHTTPServer(("127.0.0.1", int(sys.argv[1])), Handler).serve_forever()
