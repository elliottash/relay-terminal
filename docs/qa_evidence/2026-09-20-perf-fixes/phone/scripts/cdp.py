#!/usr/bin/env python3
"""Minimal Chrome DevTools Protocol driver for the phone's Chrome (#PF4K phone area).

    python3 cdp.py list
    python3 cdp.py profile <seconds> <out.cpuprofile> [url-substring]
    python3 cdp.py eval '<js>' [url-substring]
    python3 cdp.py console <seconds> [url-substring]

Talks to 127.0.0.1:9222, which `adb forward tcp:9222 localabstract:chrome_devtools_remote`
points at the phone's Chrome. Nothing here writes to the phone beyond attaching a debugger to
the tab we opened ourselves.
"""
import asyncio
import json
import sys
import urllib.request

import websockets


def targets():
    with urllib.request.urlopen("http://127.0.0.1:9222/json/list", timeout=5) as fh:
        return json.load(fh)


def pick(match):
    for target in targets():
        if target.get("type") != "page":
            continue
        if not match or match in target.get("url", ""):
            return target
    raise SystemExit(f"no page target matching {match!r}")


class Session:
    def __init__(self, ws):
        self.ws = ws
        self.seq = 0
        self.events = []

    async def call(self, method, **params):
        self.seq += 1
        await self.ws.send(json.dumps({"id": self.seq, "method": method, "params": params}))
        while True:
            message = json.loads(await self.ws.recv())
            if message.get("id") == self.seq:
                if "error" in message:
                    raise SystemExit(f"{method}: {message['error']}")
                return message.get("result", {})
            self.events.append(message)

    async def pump(self, seconds):
        end = asyncio.get_event_loop().time() + seconds
        while True:
            left = end - asyncio.get_event_loop().time()
            if left <= 0:
                return
            try:
                message = json.loads(await asyncio.wait_for(self.ws.recv(), left))
            except asyncio.TimeoutError:
                return
            self.events.append(message)


async def run():
    what = sys.argv[1]
    if what == "list":
        for target in targets():
            print(target.get("type"), target.get("url", "")[:120], target.get("id"))
        return
    if what == "profile":
        seconds, out = float(sys.argv[2]), sys.argv[3]
        match = sys.argv[4] if len(sys.argv) > 4 else ""
        target = pick(match)
        async with websockets.connect(target["webSocketDebuggerUrl"], max_size=None) as ws:
            session = Session(ws)
            await session.call("Profiler.enable")
            await session.call("Profiler.setSamplingInterval", interval=200)
            await session.call("Profiler.start")
            await session.pump(seconds)
            result = await session.call("Profiler.stop")
            open(out, "w").write(json.dumps(result["profile"]))
            print("wrote", out, "url", target["url"][:100])
        return
    if what == "eval":
        expression = sys.argv[2]
        if expression.startswith("@"):
            expression = open(expression[1:]).read()
        match = sys.argv[3] if len(sys.argv) > 3 else ""
        target = pick(match)
        async with websockets.connect(target["webSocketDebuggerUrl"], max_size=None) as ws:
            session = Session(ws)
            result = await session.call("Runtime.evaluate", expression=expression,
                                        returnByValue=True, awaitPromise=True)
            print(json.dumps(result.get("result", {}).get("value"), indent=1)[:20000])
        return
    if what == "console":
        seconds = float(sys.argv[2])
        match = sys.argv[3] if len(sys.argv) > 3 else ""
        target = pick(match)
        async with websockets.connect(target["webSocketDebuggerUrl"], max_size=None) as ws:
            session = Session(ws)
            await session.call("Runtime.enable")
            await session.call("Log.enable")
            await session.pump(seconds)
            for event in session.events:
                if event.get("method") in ("Runtime.consoleAPICalled", "Log.entryAdded",
                                           "Runtime.exceptionThrown"):
                    print(json.dumps(event)[:600])
        return
    raise SystemExit(__doc__)


asyncio.run(run())
