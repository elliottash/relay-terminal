#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Loopback-only OpenAI-compatible endpoint for card #CTRN's backend run.

    python3 stub-provider.py 8851

Nothing here calls a provider. The drive configures the worker straight at this port over plain
HTTP on 127.0.0.1, which is a local model server as far as `session_protocol.provider_config` is
concerned: no key is looked up and none exists (`RELAY_KEYRING=off` as well).

The scene is picked by a keyword in a **user** message rather than by the last message: the
worker appends a user-role message of its own at the end of a turn (the completion check), and
keying on the last one would take the scene away from the prompt that was typed. That is the
lesson `2026-09-20-helper-on-guest-main/stub-provider.py` records, and it applies unchanged.

The scenes, one per thing the run has to show:

* `#CRD1` — a Discuss that takes `HOLD` seconds, so a second prompt on the same card has
  something to queue behind.
* `#CRD2` — a Plan that calls `write_file` once. The worker refuses it (`CARD_BLOCKED`,
  `CardScope.refusal`, which names Execute) and hands the refusal back as the tool result; the
  scene then says what it was told, which is what lands on the card's thread.
* `#CRD3` / `#CRD4` — two Plans that take `HOLD` seconds each, so one can be cancelled while
  the other goes on running.
"""
import json
import sys
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

HOLD = 3.0


def user_text(messages) -> str:
    return " ".join(str(m.get("content") or "") for m in messages if m.get("role") == "user")


def tool_results(messages) -> list:
    return [m for m in messages if m.get("role") == "tool"]


def scene(messages):
    """(delay, message) for this request."""
    text = user_text(messages)
    if "#CRD2" in text:
        refused = tool_results(messages)
        if not refused:
            return 0.0, {"role": "assistant", "content": "Writing the plan to a file.",
                         "tool_calls": [{"id": "call-1", "type": "function",
                                         "function": {"name": "write_file",
                                                      "arguments": json.dumps(
                                                          {"path": "PLAN.md",
                                                           "content": "1. do it\n"})}}]}
        said = str(refused[-1].get("content") or "")[:400]
        return 0.0, {"role": "assistant",
                     "content": "I tried to write the plan to a file and was told no:\n\n"
                                f"> {' '.join(said.split())}\n\n"
                                "So the plan stays on the card."}
    if "#CRD1" in text:
        return HOLD, {"role": "assistant", "content": "This card is about card turns being "
                                                      "ordinary console turns."}
    if "#CRD3" in text or "#CRD4" in text:
        return HOLD, {"role": "assistant", "content": "Planned."}
    return 0.0, {"role": "assistant", "content": "One line, so a stray turn cannot hang the run."}


class Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def log_message(self, *args):
        sys.stderr.write("%s %s\n" % (time.strftime("%H:%M:%S"), args[0] % args[1:]))

    def do_POST(self):
        body = json.loads(self.rfile.read(int(self.headers.get("Content-Length") or 0)) or b"{}")
        delay, message = scene(body.get("messages") or [])
        if body.get("stream"):
            # A held turn has to be **streamed**, not slept on and answered in one piece: Stop
            # reaches the provider between chunks, so a stub that blocks for three seconds and
            # then answers is a turn no cancel can catch — which is what the first run of this
            # drive showed (`cancel surface=card:CRD3 -> #CRD3 done`).
            self.stream(message, delay)
            return
        reply = {"id": "stub", "object": "chat.completion", "created": int(time.time()),
                 "model": body.get("model", "stub"),
                 "choices": [{"index": 0,
                              "finish_reason": "tool_calls" if message.get("tool_calls") else "stop",
                              "message": message}],
                 "usage": {"prompt_tokens": 10, "completion_tokens": 10, "total_tokens": 20}}
        raw = json.dumps(reply).encode()
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(raw)))
        self.end_headers()
        self.wfile.write(raw)

    # ---- server-sent events ---------------------------------------------------
    def chunk(self, text: str) -> None:
        raw = text.encode()
        self.wfile.write(b"%x\r\n%s\r\n" % (len(raw), raw))
        self.wfile.flush()

    def event(self, delta: dict, finish=None) -> None:
        self.chunk("data: " + json.dumps(
            {"id": "stub", "object": "chat.completion.chunk", "model": "stub",
             "choices": [{"index": 0, "delta": delta, "finish_reason": finish}]}) + "\n\n")

    def stream(self, message: dict, delay: float) -> None:
        self.send_response(200)
        self.send_header("Content-Type", "text/event-stream")
        self.send_header("Transfer-Encoding", "chunked")
        self.end_headers()
        try:
            self.event({"role": "assistant"})
            waited = 0.0
            while waited < delay:                      # heartbeats, so a Stop lands mid-turn
                time.sleep(0.2)
                waited += 0.2
                self.chunk(": ping\n\n")
            if message.get("tool_calls"):
                call = message["tool_calls"][0]
                self.event({"content": message.get("content") or "",
                            "tool_calls": [{"index": 0, "id": call["id"], "type": "function",
                                            "function": call["function"]}]})
                self.event({}, finish="tool_calls")
            else:
                for word in (message.get("content") or "").split(" "):
                    self.event({"content": word + " "})
                self.event({}, finish="stop")
            self.chunk("data: [DONE]\n\n")
            self.wfile.write(b"0\r\n\r\n")
            self.wfile.flush()
        except (BrokenPipeError, ConnectionResetError):
            pass                                        # the turn was stopped: that is the point

    def do_GET(self):
        raw = json.dumps({"data": [{"id": "stub-model"}]}).encode()
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(raw)))
        self.end_headers()
        self.wfile.write(raw)


if __name__ == "__main__":
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 8851
    ThreadingHTTPServer(("127.0.0.1", port), Handler).serve_forever()
