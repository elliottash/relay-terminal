#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Loopback-only OpenAI-compatible endpoint for the #CTRN card-turn drive.

Copied from the #AGNT drive beside it (`docs/qa_evidence/2026-09-21-agents-are-consoles/`) and
given one scene of its own, `trace the card`: reasoning deltas, then a tool call, then prose. That
is the shape a card turn has to draw in the card's **console** since #CTRN — a thinking fold and a
tool row — while the thread view stays on the settled entries the worker writes.

    python3 stub-provider.py 8891 [session-id ...]

Nothing here calls a provider: the profile points a local model endpoint at this process, so every
agent in the run — the terminal pane's, and every console the window makes for the Switchboard, a
card, Options, Actions and Sessions — answers out of the scenes below.

A scene is picked by a **keyword in a user message**, never by the last one: the worker appends a
user-role message of its own at the end of a turn (the completion check) and that would take the
scene away from the prompt that was typed (the QA-stub gotcha the #FEJQ run recorded). Within a
scene the step is how many assistant messages with tool calls have been sent since the message
that picked it, so a multi-call scene walks forward once per round trip.

The scenes, and what each is evidence of:

  "explain the fold"  reasoning deltas then prose  -> a console's ✦ thinking bubble, folded and
                                                      unfolded with Alt+R, exactly the pane's
  "read the fixture"  a read_file call then prose  -> a ▸ tool-call row in a console
  "count slowly"      a long, slow prose stream    -> a turn to queue a second prompt behind, to
                                                      edit and remove in the §12 strip, and to Esc
  "where is copy on"  prose with an `option:` link -> the transcript's link, resolved in place by
                                                      the context (steps 6-8)
  "turn on copy on"   app_option_set               -> an agent write shown with Undo, and the row
                                                      marked "changed by the agent" (#FEJQ)
  "open the sessions" app_sessions_search+app_open -> the Sessions helper opening three panes and
                                                      saying so in text (#H6VQ)
  "which panes"       app_panes                    -> the window's own pane list, which is the walk
                                                      the phone is published from: a console must
                                                      not be in it (step 5 item 2)
  "open the card"     app_open {switchboard, card} -> a console working across contexts (the
                                                      owner's rule), from Options and from Sessions
  "read the board"    board_list                   -> a terminal pane's agent using board tools
  "say hello"         short prose                  -> a plain turn, for the card and the restart
  "how much do you.." the number of user messages  -> a restarted tab came back to the conversation
                                                      it had, rather than to a fresh one

Everything is deterministic on purpose: the same words, the same chunks, the same sleeps, so two
runs of the same drive differ only where Relay prints a clock. Anything else answers one line, so
a stray turn — a card's Discuss, a completion check — cannot hang the run.
"""
import json
import sys
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

# The row the option scenes name. It is a plain switch, so `app_option_set` can flip it and the
# notification's Undo can put it back.
#
# The **catalog id** and the **link target** are not the same string: `RelayWindow::toggleRow`
# builds `row.id = "option:" + key`, so `app_option_set` takes `option:terminal/copy_on_select`
# while an `option:` link in prose is written `option:<section>/<row>` and resolves by section.
# A real model reads the id off `app_option_list`; a stub has to spell it, and spelling it the
# other way is answered "Relay has no option row 'terminal/copy_on_select'".
ROW = "terminal/copy_on_select"
ROW_ID = "option:" + ROW
CARD = "AAAA"          # the fixture card the drive writes; `--card` overrides it

REASONING = ("Looking at what this console has to draw. The fold is an OSC 8 run over the rows "
             "the block printed, and the anchor above it is rewritten when the block settles. "
             "That is the pane's own machinery, which is the point of the card. ")

SESSIONS = []          # filled from argv: the three seeded conversation ids


def call(index, name, args):
    return {"id": f"call_{index}", "type": "function",
            "function": {"name": name, "arguments": json.dumps(args)}}


def scenes():
    return {
        "explain the fold": [
            ("A reasoning block folds under its anchor row, and the anchor says how long it took.",
             [], REASONING),
        ],
        "read the fixture": [
            ("", [call(1, "read_file", {"path": "fixture.txt"})], ""),
            ("The file says what the drive wrote into it.", [], ""),
        ],
        "count slowly": [
            ("one two three four five six seven eight nine ten eleven twelve thirteen fourteen "
             "fifteen sixteen seventeen eighteen nineteen twenty", [], ""),
        ],
        # The link's label is a word that appears **nowhere else** on screen, so a driver that has
        # to click it cannot land on the same words in the prose instead — which is how the
        # integration drive read "the row was revealed" while the Options pane had not moved.
        "where is copy on": [
            (f"It is in Terminal: [OPENROW](option:{ROW}). Clicking that opens the row.",
             [], ""),
        ],
        "turn on copy on": [
            ("", [call(1, "app_option_set", {"id": ROW_ID, "value": True})], ""),
            ("I turned Copy on select on for you. Undo is on the notice if you would rather not.",
             [], ""),
        ],
        "open the sessions": [
            ("", [call(1, "app_sessions_search", {"query": "pane"})], ""),
            ("", [call(2, "app_open", {"target": "conversation", "ids": SESSIONS[:3],
                                       "new_pane": True})], ""),
            ("", [], ""),   # the worker says what the tool did; nothing to add
        ],
        "which panes": [
            ("", [call(1, "app_panes", {})], ""),
            ("", [], ""),   # filled from the tool result, below
        ],
        "open the card": [
            ("", [call(1, "app_open", {"target": "switchboard", "card": CARD})], ""),
            (f"Opened card #{CARD} in the Switchboard.", [], ""),
        ],
        "read the board": [
            ("", [call(1, "board_list", {})], ""),
            ("", [], ""),   # filled from the tool result, below
        ],
        # How much of the conversation the worker actually handed the model. A restarted tab that
        # came back to the file it had asks this and gets a number greater than one; a tab that
        # started a new conversation gets one.
        "how much do you remember": [
            ("", [], ""),   # filled from the request, below
        ],
        # #CTRN: one card turn with everything in it — reasoning, a tool call, an answer. The
        # tool is `read_file`, which a Discuss turn has in either backend (CARD_READ_TOOLS today,
        # the console's list after step 3), so the drive does not depend on which one has landed.
        "trace the card": [
            ("", [call(1, "read_file", {"path": "fixture.txt"})], REASONING),
            ("TRACEDONE the fixture is what the drive wrote into it.", [], ""),
        ],
        # #CTRN decision 3: a Plan turn is *offered* the writers and refused when it calls one,
        # in a sentence that names Execute (`board_tools.CardScope.refusal`). The turn carries on
        # afterwards, which is the other half of the check.
        "plan the write": [
            ("", [call(1, "write_file", {"path": "plan.md", "content": "the plan"})], ""),
            ("PLANDONE the file stays unwritten; the plan is on the thread.", [], ""),
        ],
        # A turn long enough to still be running when the drive has opened another card, started
        # its turn, stopped that one and come back — which is three screenshots, a section to
        # unfold and two page loads away. 130 words at a word and a half a second is three
        # minutes, and the phase reads it twice before it ends.
        "hold the line": [
            (" ".join(["the card holds the line while the other one is stopped"] * 12), [], ""),
        ],
        "say hello": [
            ("Hello from the tab's one agent.", [], ""),
        ],
    }


def _text(message):
    content = message.get("content")
    if isinstance(content, list):
        content = " ".join(p.get("text", "") for p in content if isinstance(p, dict))
    return content or ""


def _last_tool_result(messages):
    for message in reversed(messages):
        if message.get("role") != "tool":
            continue
        try:
            return json.loads(_text(message))
        except Exception:
            return None
    return None


def _pane_answer(messages):
    """Quote back exactly what the GUI answered `app_panes` with.

    The point of the scene is that the *window's* list reaches the transcript unedited, so the
    screenshot can be read for what is in it and — more to the point — what is not.
    """
    result = _last_tool_result(messages) or {}
    panes = result.get("panes")
    if not isinstance(panes, list):
        return "PANELIST count=? titles=(no result)"
    titles = [str(p.get("title") or p.get("id")) for p in panes if isinstance(p, dict)]
    return "PANELIST count=%d titles=%s" % (len(titles), " | ".join(titles) or "(none)")


def _board_answer(messages):
    """Say how many cards the board tool came back with, and name the first."""
    result = _last_tool_result(messages) or {}
    cards = result.get("cards")
    if not isinstance(cards, list):
        cards = result.get("rows") if isinstance(result.get("rows"), list) else None
    if not isinstance(cards, list):
        return "BOARDLIST count=? (no result)"
    first = ""
    if cards and isinstance(cards[0], dict):
        first = str(cards[0].get("id") or cards[0].get("title") or "")
    return "BOARDLIST count=%d first=%s" % (len(cards), first or "(none)")


def scene(request):
    messages = request.get("messages") or []
    table = scenes()
    picked, at = None, -1
    for i, message in enumerate(messages):
        if message.get("role") != "user":
            continue
        text = _text(message).lower()
        # The **first** match in the message, not whichever key comes last in this dict. A card's
        # prompt is the words that were just typed followed by the card's own file and its
        # thread (`board_protocol` seeds it), so every keyword an earlier turn on this card used
        # is in this turn's prompt too: the #CTRN drive asked "hold the line on this card" and
        # was answered by the "trace the card" scene, whose words are an entry on the thread.
        best = min(((text.find(key), key) for key in table if key in text), default=None)
        if best is not None:
            picked, at = best[1], i
    if picked is None:
        return "Nothing to do here.", [], ""
    step = sum(1 for m in messages[at + 1:]
               if m.get("role") == "assistant" and m.get("tool_calls"))
    steps = table[picked]
    prose, calls, reasoning = steps[min(step, len(steps) - 1)]
    if picked == "which panes" and step >= 1:
        prose = _pane_answer(messages)
    if picked == "read the board" and step >= 1:
        prose = _board_answer(messages)
    if picked == "how much do you remember":
        prose = "HISTORY turns=%d" % sum(1 for m in messages if m.get("role") == "user")
    return prose, calls, reasoning


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
        prose, calls, reasoning = scene(request)
        model = request.get("model") or "stub"
        finish = "tool_calls" if calls else "stop"
        # One scene streams a word at a time for half a minute, so there is a turn to queue a
        # second prompt behind, edit it in the strip, remove it and press Esc.
        # The card scene streams slowly too (#CTRN): the busy strip, the thread view that must
        # stay empty and the console's own fold are all read *while the turn runs*, and a turn
        # that is over in four seconds cannot be photographed.
        body = json.dumps(request.get("messages") or [])
        slow = ("count slowly" in body or "trace the card" in body
                or "hold the line" in body)
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

            for piece in pieces(reasoning) if reasoning else []:
                time.sleep(0.3)
                chunk({"role": "assistant", "reasoning_content": piece})
            if prose:
                for piece in (prose.split(" ") if slow else pieces(prose)):
                    time.sleep(1.5 if slow else 0.2)
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
    port = int(sys.argv[1])
    for argument in sys.argv[2:]:
        if argument.startswith("--card="):
            CARD = argument.split("=", 1)[1]
        else:
            SESSIONS.append(argument)
    ThreadingHTTPServer(("127.0.0.1", port), Handler).serve_forever()
