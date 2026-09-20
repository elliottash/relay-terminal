"""#3H5T: one streamed reply on the owner's Pixel 8, measured in its own Chrome.

    python3 phone.py <url> <label>

Opens a tab of its own over CDP (adb forward tcp:9222), drives tests/screen_harness.html through
200 frames of output, and reports the two costs the card names: how many `history_get` the client
sent, and what `fit` cost in the V8 sampling profile. Closes the tab it opened; touches nothing
else on the phone.
"""
import asyncio
import json
import subprocess
import sys
import time
import urllib.request

sys.path.insert(0, "/home/elliott/repos/relay-terminal")
from remote import ws  # noqa: E402

CDP = "http://127.0.0.1:9222"
SERIAL = "192.168.1.184:46849"


def chrome_jiffies():
    """utime+stime summed over every com.android.chrome process, as chromecpu.sh does it."""
    out = subprocess.run(
        ["adb", "-s", SERIAL, "shell",
         "for p in $(pgrep -f com.android.chrome); do cat /proc/$p/stat 2>/dev/null; done"],
        capture_output=True, text=True, timeout=60).stdout
    total = 0
    for line in out.splitlines():
        parts = line.split()
        if len(parts) > 15:
            total += int(parts[13]) + int(parts[14])
    return total


class Session:
    def __init__(self, socket):
        self.socket = socket
        self.seq = 0
        self.replies = {}
        self.pump = None

    async def start(self):
        self.pump = asyncio.create_task(self._read())

    async def _read(self):
        try:
            while True:
                raw = await self.socket.recv()
                message = json.loads(raw if isinstance(raw, str) else raw.decode())
                if "id" in message:
                    future = self.replies.pop(message["id"], None)
                    if future and not future.done():
                        future.set_result(message)
        except Exception:
            pass

    async def call(self, method, timeout=120, **params):
        self.seq += 1
        future = asyncio.get_running_loop().create_future()
        self.replies[self.seq] = future
        await self.socket.send(json.dumps({"id": self.seq, "method": method, "params": params}))
        message = await asyncio.wait_for(future, timeout)
        if "error" in message:
            raise RuntimeError(f"{method}: {message['error']}")
        return message.get("result", {})

    async def evaluate(self, expression, timeout=120):
        result = await self.call("Runtime.evaluate", timeout=timeout, expression=expression,
                                 returnByValue=True, awaitPromise=True)
        if result.get("exceptionDetails"):
            raise RuntimeError(json.dumps(result["exceptionDetails"])[:400])
        return result.get("result", {}).get("value")


def get(path):
    with urllib.request.urlopen(CDP + path, timeout=10) as reply:
        return json.load(reply)


def self_time(profile):
    """Self time per function, milliseconds, from a V8 sampling profile."""
    by_id = {node["id"]: node for node in profile["nodes"]}
    hits = {node["id"]: node.get("hitCount", 0) for node in profile["nodes"]}
    span = (profile["endTime"] - profile["startTime"]) / 1000.0        # ms
    samples = sum(hits.values()) or 1
    per = span / samples
    out = {}
    for node_id, count in hits.items():
        frame = by_id[node_id]["callFrame"]
        name = frame.get("functionName") or "(anonymous)"
        where = frame.get("url", "").rsplit("/", 1)[-1]
        key = f"{name} {where}:{frame.get('lineNumber', -1) + 1}"
        out[key] = out.get(key, 0) + count * per
    return span, sorted(out.items(), key=lambda item: -item[1])


async def main():
    url, label = sys.argv[1], sys.argv[2]
    frames = int(sys.argv[3]) if len(sys.argv) > 3 else 200
    every = int(sys.argv[4]) if len(sys.argv) > 4 else 10
    browser = await ws.connect(get("/json/version")["webSocketDebuggerUrl"])
    top = Session(browser)
    await top.start()
    target = (await top.call("Target.createTarget", url=url))["targetId"]
    try:
        for _ in range(60):
            await asyncio.sleep(0.5)
            page = [t for t in get("/json/list") if t.get("id") == target]
            if page and page[0].get("webSocketDebuggerUrl"):
                break
        socket = await ws.connect(page[0]["webSocketDebuggerUrl"])
        tab = Session(socket)
        await tab.start()
        await tab.call("Runtime.enable")
        await tab.call("Profiler.enable")
        await tab.call("Profiler.setSamplingInterval", interval=200)
        for _ in range(60):
            if await tab.evaluate("document.body && document.body.dataset.harnessReady === '1'"):
                break
            await asyncio.sleep(0.5)
        await tab.evaluate("screenHarness.open({})")
        for _ in range(40):
            if await tab.evaluate("screenHarness.report().held >= 80"):
                break
            await asyncio.sleep(0.5)
        await tab.evaluate("screenHarness.zero()")
        cpu0, wall0 = chrome_jiffies(), time.monotonic()
        await tab.call("Profiler.start")
        await tab.evaluate(f"screenHarness.stream({frames}, {every})", timeout=300)
        profile = (await tab.call("Profiler.stop"))["profile"]
        cpu = (chrome_jiffies() - cpu0) / 100.0
        wall = time.monotonic() - wall0
        report = await tab.evaluate("screenHarness.report()")
        span, top_frames = self_time(profile)
        busy = sum(ms for name, ms in top_frames if not name.startswith("(idle)")
                   and not name.startswith("(program)"))
        fit = sum(ms for name, ms in top_frames if name.startswith("fit "))
        print(json.dumps({
            "label": label, "url": url, "window_ms": round(span, 1),
            "frames": frames, "every_ms": every,
            "history_get": report["asks"],
            "refused_at_120_per_60s": max(0, report["asks"] - 120),
            "getComputedStyle(root)": report["style"],
            "clientWidth(root)": report["clientWidth"],
            "non_idle_js_ms": round(busy, 1),
            "fit_ms": round(fit, 1),
            "fit_share_of_non_idle_js": f"{(100 * fit / busy) if busy else 0:.1f}%",
            "chrome_cpu_s": round(cpu, 2), "wall_s": round(wall, 1),
            "chrome_cpu_share_of_one_core": f"{100 * cpu / wall:.1f}%",
            "top_js": [[name, round(ms, 1)] for name, ms in top_frames[:8]],
        }, indent=2))
        await socket.close()
    finally:
        await top.call("Target.closeTarget", targetId=target)
        await browser.close()


asyncio.run(main())
