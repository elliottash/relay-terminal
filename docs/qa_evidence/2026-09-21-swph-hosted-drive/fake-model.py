#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""The #PH0N drive's scripted model, with the three scenes a card turn needs (#SWPH).

    fake-model.py <port> <request log> <path to #PH0N's fake-model.py>

#PH0N's file is loaded by path and only its `scene()` is replaced, so the server, the streaming
and the request log have one home. The scenes added here are picked, like its own, by a keyword in
the words the owner typed — which for a card turn arrive inside the card's seed block:

  DISCUSSIT   five seconds of nothing (so the phone's busy mark can be seen), then one sentence:
              the card turn's answer, which the worker appends to the card's thread
  SLOWCARD    #PH0N's two-minute answer — only Stop ends it
  PLANIT <ID> step 0: `board_read {id}`; step 1: `board_update_card` with the hash that came back
              and `replace_section {heading: "Plan"}` — the one write a Plan turn is allowed;
              step 2: one sentence

and two that are picked by what the *desktop* wrote, not the owner: a pane that Execute or Verify
opened is handed a task that starts "Execute #ID:" or "Verify #ID:" (src/BoardModel.cpp), and the
answer names the card back — which is how the run knows the card reached that pane's agent.
"""
import importlib.util
import json
import re
import sys
import time

PORT, LOG, PH0N = int(sys.argv[1]), sys.argv[2], sys.argv[3]
sys.argv = [sys.argv[0], str(PORT), LOG]
spec = importlib.util.spec_from_file_location("ph0n_fake_model", PH0N)
base = importlib.util.module_from_spec(spec)
spec.loader.exec_module(base)
ph0n_scene = base.scene

NEW_PLAN = ("**Goal.** One feed call an hour per port, and a stale answer rather than none.\n\n"
            "1. Wrap `next_high_tide` in a cache keyed by port.\n"
            "2. Expire entries after sixty minutes, on a clock the test can set.\n"
            "3. Serve the stale entry when the feed is down, and say so on the page.\n\n"
            "Rewritten by the fake planner (PLANNED-BY-FAKE).")


def call(name: str, arguments: dict, n: int) -> dict:
    return {"id": f"call_{name}_{n}", "type": "function",
            "function": {"name": name, "arguments": json.dumps(arguments)}}


def scene(request):
    messages = request.get("messages") or []
    if request.get("tools"):
        last, word, said = -1, "", ""
        for i, m in enumerate(messages):
            if m.get("role") != "user":
                continue
            text = base.text_of(m)
            # A hand-off wins over a keyword: the card comes attached with its thread, and the
            # thread of a card that was planned a minute ago has the owner's "PLANIT" in it.
            handed = re.search(r"^(Execute|Verify) #([0-9A-Z]{4}):", text, re.M)
            hit = handed.group(1).upper() if handed else next(
                (s for s in ("DISCUSSIT", "SLOWCARD", "PLANIT") if s in text), "")
            if hit:
                last, word, said = i, hit, text
        # The newest keyword wins, whichever file knows it.
        theirs = max((i for i, m in enumerate(messages) if m.get("role") == "user"
                      and any(s in base.text_of(m) for s in base.SCENES)), default=-1)
        if word and last > theirs:
            step = sum(1 for m in messages[last + 1:] if m.get("role") == "assistant" and m.get("tool_calls"))
            if word == "DISCUSSIT":
                time.sleep(5)
                return word, "Only our own three ports for now: the coast is a different feed. (DISCUSSED-BY-FAKE)", []
            if word == "SLOWCARD":
                return word, "SLOW", []
            if word in ("EXECUTE", "VERIFY"):
                card = re.search(r"^(?:Execute|Verify) #([0-9A-Z]{4}):", said, re.M).group(1)
                return f"{word}:{card}", (f"I have card #{card} and what came with it. "
                                          f"({'EXECUTING' if word == 'EXECUTE' else 'VERIFYING'}-BY-FAKE {card})"), []
            card = (re.search(r"PLANIT\s+([0-9A-Z]{4})", said) or [None, ""])[1]
            if step == 0:
                return word, "", [call("board_read", {"id": card}, 0)]
            if step == 1:
                raw = next((base.text_of(m) for m in reversed(messages) if m.get("role") == "tool"), "")
                found = re.search(r'"hash":\s*"([0-9a-f]{64})"', raw)
                return word, "", [call("board_update_card", {
                    "id": card, "base_hash": found.group(1) if found else "0" * 64,
                    "replace_section": {"heading": "Plan", "text": NEW_PLAN}}, 1)]
            return word, f"The plan on #{card} now has three steps; the third is what happens when the feed is down.", []
    return ph0n_scene(request)


base.scene = scene

if __name__ == "__main__":
    base.ThreadingHTTPServer(("127.0.0.1", PORT), base.Handler).serve_forever()
