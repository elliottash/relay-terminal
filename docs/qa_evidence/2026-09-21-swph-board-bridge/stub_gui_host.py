# SPDX-License-Identifier: AGPL-3.0-or-later
"""Stub sidecar for the #SWPH desktop-bridge live check.

It stands where `remote/gui_host.py` stands (RELAY_REMOTE_DIR), logs every line the GUI writes, says
remote control is on, and — once the GUI has published its first pane, so the window is up — plays a
paired phone: one `board_request` line at a time, each sent when the previous one has been answered
(or after a few seconds). The hub half of the card is a different task; this proves what the
desktop does with the hub's lines and what it writes back.
"""
import json, os, sys, threading, time

LOG = os.environ.get("SWPH_LOG", "/tmp/swph/sidecar.jsonl")
CARD = os.environ.get("SWPH_CARD", "K7Q2")
BASE = "https://join.relay-terminal.ai/d/stub"

REQUESTS = [
    {"type": "board_open"},
    {"type": "board_card_get", "id": CARD},
    {"type": "board_comment", "id": CARD, "text": "Yes, go ahead. Said from the phone.", "kind": "decision"},
    {"type": "board_move", "id": CARD, "status": "ready", "reason": "answered"},
    {"type": "board_create", "tab": "features", "title": "Dictated on the phone",
     "request": "A card filed from the phone by voice.", "labels": ["remote"]},
    {"type": "board_search", "query": "dictated"},
    {"type": "board_delete", "id": CARD},
    {"type": "board_card_get", "id": CARD, "path": "/etc/passwd"},
    {"type": "board_action", "id": CARD, "action": "execute"},
]

lock = threading.Lock()
answered = threading.Event()
started = threading.Event()
waiting_rid = [None]


def emit(obj):
    with lock:
        sys.stdout.write(json.dumps(obj) + "\n")
        sys.stdout.flush()


def phone(log):
    started.wait(60)
    time.sleep(4)
    for rid, request in enumerate(REQUESTS, 1):
        answered.clear()
        waiting_rid[0] = rid
        line = {"t": "board_request", "rid": rid, "device": "dev-stub-1", "name": "Elliott's iPhone",
                "request": request}
        log.write(json.dumps({"hub_to_gui": line}) + "\n")
        emit(line)
        answered.wait(25)
        time.sleep(1.5)       # the broadcasts that follow an answer
    log.write(json.dumps({"stub": "done"}) + "\n")


def main():
    log = open(LOG, "a", buffering=1)
    threading.Thread(target=phone, args=(log,), daemon=True).start()
    for line in sys.stdin:
        line = line.strip()
        if not line:
            continue
        try:
            message = json.loads(line)
        except Exception:
            continue
        kind = message.get("t")
        if kind == "frame":
            continue                      # the terminal's pixels are not what this run is about
        log.write(line + "\n")
        if kind == "start":
            emit({"t": "started", "base": BASE, "fingerprint": "ab:cd", "note": "stub", "addresses": []})
            emit({"t": "remote_state", "on": True, "address": message.get("address", ""), "base": BASE,
                  "online": True, "devices": 1, "reason": ""})
        elif kind == "pane":
            started.set()
        elif kind == "board_event" and message.get("rid") == waiting_rid[0]:
            answered.set()
        elif kind == "devices":
            emit({"t": "devices", "items": []})
        elif kind == "stop":
            emit({"t": "stopped"})


main()
