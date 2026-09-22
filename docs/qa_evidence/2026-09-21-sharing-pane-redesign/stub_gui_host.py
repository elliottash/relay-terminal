# SPDX-License-Identifier: AGPL-3.0-or-later
"""Stub sidecar for the #SHRP screenshots: logs every GUI line and plays one of three scenes.

`SHRP_SCENE` picks it:
  quiet   three panes published, nobody visiting — remote control on, iPhone and iPad connected.
  guests  alice (editor) on the pane the Sharing pane is opened from, a live viewer link on
          the first pane published, and one quiet pane.
  knock   the quiet scene, plus alice knocking on the last pane published — which is what
          opens the Sharing pane, as the real sidecar's knock would.

It answers `start`, `devices`, `participants`, `pair`, `pair_code` and `stop`; a `pane` line is
remembered so the participants and the knock can name real pane ids.
"""
import json
import os
import sys
import time

LOG = os.environ.get("SHRP_LOG", "/tmp/shrp/sidecar.jsonl")
SCENE = os.environ.get("SHRP_SCENE", "quiet")
BASE = "https://join.relay-terminal.ai/d/stub"

panes = []          # ids in the order the GUI published them
knocked = False


def emit(obj):
    sys.stdout.write(json.dumps(obj) + "\n")
    sys.stdout.flush()


def qr(n=21):
    return [[(row + column) % 2 for column in range(n)] for row in range(n)]


def participants():
    if SCENE != "guests" or not panes:
        return {"t": "participants", "items": [], "invites": []}
    here, first = panes[-1], panes[0]
    return {"t": "participants",
            "items": [{"id": "a1", "name": "alice", "platform": "Chrome", "role": "editor",
                       "panes": [here], "invite": "i1", "fingerprint": "AB12 CD34 EF56",
                       "expires": int(time.time()) + 3600, "online": True, "driving": []}],
            "invites": [{"id": "i9f3c2a", "panes": [first], "role": "viewer", "uses": 1,
                         "expires": 86400}]}


def main():
    global knocked
    log = open(LOG, "a", buffering=1)
    for line in sys.stdin:
        line = line.strip()
        if not line:
            continue
        log.write(line + "\n")
        try:
            message = json.loads(line)
        except Exception:
            continue
        kind = message.get("t")
        if kind == "start":
            emit({"t": "started", "base": BASE, "fingerprint": "ab:cd", "note": "stub",
                  "addresses": [
                      {"value": "relay-terminal.ai", "kind": "hosted", "available": True,
                       "where": "anywhere", "label": "relay-terminal.ai — works from anywhere",
                       "reason": "", "current": True}]})
            emit({"t": "remote_state", "on": bool(message.get("always")),
                  "address": message.get("address", "relay-terminal.ai"), "base": BASE,
                  "online": True, "devices": 2, "reason": ""})
        elif kind == "devices":
            emit({"t": "devices", "items": [
                {"id": "d1", "name": "iPhone", "platform": "Safari", "capability": "full",
                 "fingerprint": "11:22", "password_entry": False, "online": True},
                {"id": "d2", "name": "iPad", "platform": "Safari", "capability": "full",
                 "fingerprint": "33:44", "password_entry": False, "online": True},
                {"id": "d3", "name": "old Pixel", "platform": "Chrome", "capability": "view",
                 "fingerprint": "55:66", "password_entry": False, "online": False}]})
        elif kind == "pane":
            if message.get("id") not in panes:
                panes.append(message.get("id"))
        elif kind == "unpane":
            if message.get("id") in panes:
                panes.remove(message.get("id"))
        elif kind == "participants":
            emit(participants())
            if SCENE == "knock" and len(panes) >= 3 and not knocked:
                knocked = True
                emit({"t": "knock", "participant": "a1", "name": "alice", "platform": "Chrome",
                      "fingerprint": "AB12 CD34 EF56", "peer": "192.0.2.7", "code": "48213",
                      "role": "editor", "pane": panes[-1], "panes": [panes[-1]], "invite": "i1"})
        elif kind == "pair":
            emit({"t": "pairing", "url": BASE + "/pair#s=stubsecret", "qr": qr(), "expires": 120})
        elif kind == "pair_code":
            emit({"t": "pair_code", "code": "ABCD", "pin": "4829", "expires": 600})
        elif kind == "stop":
            emit({"t": "stopped"})


main()
