#!/usr/bin/env python3
"""Drive the real Switchboard worker: plan one card, then plan a second while it runs."""
import json, os, subprocess, sys, threading, time

REPO = "/home/elliott/repos/relay-terminal"
WS = sys.argv[1]
SECOND_AFTER = float(os.environ.get("SECOND_AFTER", "6"))
SECOND = os.environ.get("SECOND", "1") == "1"

proc = subprocess.Popen([sys.executable, "-S", "-u", f"{REPO}/backend/worker.py"],
                        stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                        stderr=subprocess.DEVNULL, text=True, bufsize=1,
                        env={**os.environ, "RELAY_PANE_ID": "switchboard-repro"})
start = time.time()
lock = threading.Lock()
state = {"done": 0, "second_sent": False}

def send(msg):
    with lock:
        proc.stdin.write(json.dumps(msg) + "\n"); proc.stdin.flush()
    print(f"[{time.time()-start:7.1f}] >>> {json.dumps(msg)[:200]}", flush=True)

def reader():
    for line in proc.stdout:
        try: ev = json.loads(line)
        except Exception: continue
        name = ev.get("event")
        t = time.time() - start
        if name in ("delta", "thinking"):
            continue
        brief = {k: v for k, v in ev.items() if k in
                 ("event", "id", "card_id", "mode", "code", "tool", "text", "turn_id", "outcome", "cards", "stop_reason")}
        if isinstance(brief.get("text"), str) and len(brief["text"]) > 220:
            brief["text"] = brief["text"][:220] + "…"
        print(f"[{t:7.1f}] <<< {json.dumps(brief)}", flush=True)
        if name in ("done", "error", "cancelled") and ev.get("turn_id"):
            state["done"] += 1

threading.Thread(target=reader, daemon=True).start()
time.sleep(1.0)
send({"type": "configure", "workspace": WS, "preset": "glm-coding", "use_stored_key": True,
      "board": {"dir": WS}})
send({"type": "board_open"})
time.sleep(2.0)
send({"type": "board_ask", "id": "planA", "card": "ZW95", "mode": "plan"})
if SECOND:
    time.sleep(SECOND_AFTER)
    send({"type": "board_ask", "id": "planB", "card": "SPBN", "mode": "plan"})
want = int(os.environ.get("WANT", "1"))
deadline = time.time() + float(os.environ.get("BUDGET", "420"))
while time.time() < deadline and state["done"] < want:
    time.sleep(0.5)
print(f"[{time.time()-start:7.1f}] --- card turns ended: {state['done']} of {want}", flush=True)
send({"type": "shutdown"})
time.sleep(1)
proc.terminate()
