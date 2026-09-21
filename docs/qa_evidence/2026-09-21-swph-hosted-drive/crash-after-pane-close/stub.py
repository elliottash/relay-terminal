# SPDX-License-Identifier: AGPL-3.0-or-later
"""The stub sidecar of crash-after-pane-close/repro.sh: says remote control is on, and sends the
GUI one `board_request` line for every JSON line appended to $SWPH_CMDS."""
import json, os, sys, threading, time
LOG = os.environ["SWPH_LOG"]; CMDS = os.environ["SWPH_CMDS"]
BASE = "https://join.relay-terminal.ai/d/stub"
lock = threading.Lock()
def emit(obj):
    with lock:
        sys.stdout.write(json.dumps(obj) + "\n"); sys.stdout.flush()
def phone(log):
    sent = 0
    while True:
        time.sleep(0.3)
        try: lines = open(CMDS).read().splitlines()
        except OSError: continue
        for raw in lines[sent:]:
            sent += 1
            line = {"t": "board_request", "rid": sent, "device": "dev-stub-1", "name": "Elliott's iPhone", "request": json.loads(raw)}
            log.write(json.dumps({"hub_to_gui": line}) + "\n"); emit(line)
def main():
    log = open(LOG, "a", buffering=1)
    threading.Thread(target=phone, args=(log,), daemon=True).start()
    for line in sys.stdin:
        line = line.strip()
        if not line: continue
        try: message = json.loads(line)
        except Exception: continue
        kind = message.get("t")
        if kind == "frame": continue
        log.write(line[:600] + "\n")
        if kind == "start":
            emit({"t": "started", "base": BASE, "fingerprint": "ab:cd", "note": "stub", "addresses": []})
            emit({"t": "remote_state", "on": True, "address": message.get("address", ""), "base": BASE, "online": True, "devices": 1, "reason": ""})
        elif kind == "devices": emit({"t": "devices", "items": []})
        elif kind == "stop": emit({"t": "stopped"})
main()
