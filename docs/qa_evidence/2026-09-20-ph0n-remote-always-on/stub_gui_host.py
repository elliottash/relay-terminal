"""Stub sidecar for the #PH0N live check: logs every GUI line, answers `start`."""
import json, os, sys, threading, time

LOG = os.environ.get("PH0N_LOG", "/tmp/ph0n/sidecar.jsonl")

def emit(obj):
    sys.stdout.write(json.dumps(obj) + "\n")
    sys.stdout.flush()

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
        if message.get("t") == "start":
            emit({"t": "started", "base": "https://join.relay-terminal.ai/d/stub",
                  "fingerprint": "ab:cd", "note": "stub",
                  "addresses": [
                      {"value": "spark.tail0.ts.net", "kind": "tailscale", "available": True,
                       "where": "your tailnet", "label": "https://spark.tail0.ts.net — tailnet",
                       "reason": "", "current": False},
                      {"value": "relay-terminal.ai", "kind": "hosted", "available": True,
                       "where": "anywhere", "label": "relay-terminal.ai — works from anywhere",
                       "reason": "", "current": True},
                      {"value": "192.168.1.9", "kind": "ip", "available": True,
                       "where": "this network", "label": "192.168.1.9 — reachable from this network",
                       "reason": "", "current": False}]})
            emit({"t": "remote_state", "on": bool(message.get("always")),
                  "address": message.get("address", ""),
                  "base": "https://join.relay-terminal.ai/d/stub",
                  "online": True, "devices": 2, "reason": ""})
        elif message.get("t") == "stop":
            emit({"t": "stopped"})

main()
