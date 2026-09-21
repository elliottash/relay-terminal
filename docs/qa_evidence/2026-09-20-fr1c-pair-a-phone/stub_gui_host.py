# SPDX-License-Identifier: AGPL-3.0-or-later
"""Stub sidecar for the #FR1C live check: logs every GUI line, answers `start`, `pair`,
`pair_code` and `devices`. The real sidecar half is task 2 of the same card; this one exists to
prove what the GUI sends and what it does with the answers."""
import json, os, sys

LOG = os.environ.get("FR1C_LOG", "/tmp/fr1c/sidecar.jsonl")
BASE = "https://join.relay-terminal.ai/d/stub"


def emit(obj):
    sys.stdout.write(json.dumps(obj) + "\n")
    sys.stdout.flush()


def qr(n=21):
    # Not a real QR: a checkerboard of the right shape, so the dialog has a pixmap to draw.
    return [[(row + column) % 2 for column in range(n)] for row in range(n)]


def main():
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
                       "reason": "", "current": True},
                      {"value": "192.168.1.9", "kind": "ip", "available": True,
                       "where": "this network", "label": "192.168.1.9 — this network",
                       "reason": "", "current": False}]})
            emit({"t": "remote_state", "on": bool(message.get("always")),
                  "address": message.get("address", ""), "base": BASE,
                  "online": True, "devices": 0, "reason": ""})
        elif kind == "pair":
            emit({"t": "pairing", "url": BASE + "/pair#s=stubsecret", "qr": qr(), "expires": 120})
        elif kind == "pair_code":
            emit({"t": "pair_code", "code": "ABCD", "pin": "4829", "expires": 600})
        elif kind == "devices":
            emit({"t": "devices", "items": []})
        elif kind == "stop":
            emit({"t": "stopped"})


main()
