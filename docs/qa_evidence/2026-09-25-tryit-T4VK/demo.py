#!/usr/bin/env python3
"""The #T4VK situation as a printed timeline: an agent parked in agent_wait gets a steered
message without its turn being interrupted.

Reuses the HandoffTests harness (scripted providers, gated subagent) so no model and no
network are involved. What you see is the worker's event flow with timestamps.
"""
import sys
import threading
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[3]))          # repo root
sys.path.insert(1, str(Path(__file__).resolve().parents[3] / "backend"))

import tests.test_subagents as ts                                      # noqa: E402

t0 = time.monotonic()


def at(label):
    print(f"[t+{time.monotonic() - t0:5.2f}s] {label}", flush=True)


case = ts.HandoffTests("test_steer_ends_agent_wait_and_lands_next_step")
case.setUp()
try:
    steer_text = "actually, summarise only the python files"

    def steer_later():
        time.sleep(0.5)
        at(f">>> you type \"{steer_text}\" and press Enter  (the pane steers it into the turn)")
        case.turns.submit(steer_text, when="steer")

    agent, provider = case.with_turns([
        ts.calls(ts.call("agent", {"description": "bg", "prompt": "X gate:g1",
                                   "subagent_type": "general", "background": True}, "c1")),
        lambda messages, cancel: (
            threading.Thread(target=steer_later, daemon=True).start(),
            ts.calls(ts.call("agent_wait", {"id": "a1", "timeout_seconds": 600}, "c2")))[1],
        ts.final("acknowledged the message and went on"),
    ])
    at("turn starts: \"go\" — the agent spawns a background subagent (a1), then calls agent_wait")
    case.turns.submit("go")
    case.rec.wait(lambda e: e["event"] == "agent_finished")
    waited = case.tool_results(provider, 2)["c2"]
    at(f"agent_wait returned: stopped_for_user_message={waited.get('stopped_for_user_message')} "
       f"timed_out={waited['timed_out']} a1.status={waited['agents'][0]['status']}")
    messages = provider.seen[2][0]
    at("the model's next request ends with the user message:")
    print("          " + messages[-1]["content"].strip().replace("\n", "\n          "), flush=True)
    time.sleep(0.3)
    at(f"turn finished. turns started: {len(case.rec.of('agent_started'))}; "
       f"a1 still running in the background, untouched")
    print(flush=True)
    print("Before the fix, this same scene sits parked for the whole 10-minute wait: the", flush=True)
    print("steered message has no step boundary to land at, and the only way in is Enter,", flush=True)
    print("Enter — interrupting the turn. Everything above should read as one Enter.", flush=True)
finally:
    case.tearDown()
