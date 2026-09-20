#!/usr/bin/env python3
"""Count WebSocket bytes and main-thread work on the phone's Relay tab (#PF4K phone area).

    python3 wsbytes.py <seconds> <out-prefix> [url-substring] [--profile]

Attaches to the phone's Chrome over `adb forward tcp:9222 …`, turns on Network (for the RRP
WebSocket, which is one Noise frame per message) and optionally the sampling profiler, and prints
frames in / out and their payload bytes. Writes `<prefix>.frames.tsv` and, with --profile,
`<prefix>.cpuprofile` plus a self-time table.
"""
import asyncio
import base64
import collections
import json
import sys
import urllib.request

import websockets

seconds = float(sys.argv[1])
prefix = sys.argv[2]
match = sys.argv[3] if len(sys.argv) > 3 and not sys.argv[3].startswith("--") else ""
want_profile = "--profile" in sys.argv


def page(match):
    with urllib.request.urlopen("http://127.0.0.1:9222/json/list", timeout=5) as fh:
        for target in json.load(fh):
            if target.get("type") == "page" and (not match or match in target.get("url", "")):
                return target
    raise SystemExit("no matching page")


def selftime(profile):
    """Self time per function from a CDP sampling profile, in ms."""
    nodes = {node["id"]: node for node in profile["nodes"]}
    hits = collections.Counter()
    deltas = profile.get("timeDeltas") or []
    samples = profile.get("samples") or []
    for index, node_id in enumerate(samples):
        hits[node_id] += deltas[index] if index < len(deltas) else 0
    rows = []
    for node_id, micros in hits.items():
        frame = nodes[node_id]["callFrame"]
        name = frame.get("functionName") or "(anonymous)"
        url = (frame.get("url") or "").rsplit("/", 1)[-1]
        rows.append((micros / 1000.0, f"{name} @{url}:{frame.get('lineNumber', -1) + 1}"))
    rows.sort(reverse=True)
    return rows


async def main():
    target = page(match)
    async with websockets.connect(target["webSocketDebuggerUrl"], max_size=None) as ws:
        seq = 0

        async def call(method, **params):
            nonlocal seq
            seq += 1
            await ws.send(json.dumps({"id": seq, "method": method, "params": params}))

        await call("Network.enable", maxPostDataSize=0)
        if want_profile:
            await call("Profiler.enable")
            await call("Profiler.setSamplingInterval", interval=200)
            await call("Profiler.start")
        frames = []
        loop = asyncio.get_event_loop()
        end = loop.time() + seconds
        stop_id = None
        while True:
            left = end - loop.time()
            if left <= 0:
                break
            try:
                message = json.loads(await asyncio.wait_for(ws.recv(), left))
            except asyncio.TimeoutError:
                break
            method = message.get("method")
            if method in ("Network.webSocketFrameReceived", "Network.webSocketFrameSent"):
                params = message["params"]
                data = params["response"].get("payloadData") or ""
                opcode = params["response"].get("opcode")
                size = len(base64.b64decode(data)) if opcode == 2 else len(data)
                frames.append((params["timestamp"], "in" if "Received" in method else "out",
                               opcode, size))
        profile = None
        if want_profile:
            seq += 1
            stop_id = seq
            await ws.send(json.dumps({"id": stop_id, "method": "Profiler.stop"}))
            while True:
                message = json.loads(await asyncio.wait_for(ws.recv(), 60))
                if message.get("id") == stop_id:
                    profile = message["result"]["profile"]
                    break
    with open(prefix + ".frames.tsv", "w") as fh:
        fh.write("ts\tdir\topcode\tbytes\n")
        for row in frames:
            fh.write("\t".join(str(x) for x in row) + "\n")
    incoming = [f for f in frames if f[1] == "in"]
    outgoing = [f for f in frames if f[1] == "out"]
    print(f"frames in={len(incoming)} bytes_in={sum(f[3] for f in incoming)} "
          f"out={len(outgoing)} bytes_out={sum(f[3] for f in outgoing)}")
    if profile:
        open(prefix + ".cpuprofile", "w").write(json.dumps(profile))
        rows = selftime(profile)
        total = sum(ms for ms, _ in rows)
        print(f"--- JS self time, total {total:.0f} ms over {seconds:.0f} s wall")
        for ms, name in rows[:25]:
            if ms < 0.5:
                break
            print(f"{ms:9.1f} ms  {100 * ms / total:5.1f}%  {name}")


asyncio.run(main())
