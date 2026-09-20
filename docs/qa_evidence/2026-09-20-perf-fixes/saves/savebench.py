#!/usr/bin/env python3
"""Per-save cost of SessionStore.save on a read-only copy of a real session (#GMCF).

  savebench.py <backend-root> <copy-dir>

Sizes and timings only; the copy is never read for its content and the owner's own store is
never opened. RELAY_INDEX=off keeps the shared conversation index out of the measurement.
"""
import json, os, shutil, statistics, sys, tempfile, time
sys.path.insert(0, os.path.join(sys.argv[1], "backend"))
os.environ["RELAY_INDEX"] = "off"
from relay_core.sessions import SessionStore

src = sys.argv[2]
name = [f for f in os.listdir(src) if f.endswith(".json") and not f.endswith(".meta.json")][0]
work = tempfile.mkdtemp(prefix="savebench-")
for f in os.listdir(src):
    shutil.copy(os.path.join(src, f), work)
data = json.load(open(os.path.join(work, name)))
store = SessionStore(work, index=False)
size = os.path.getsize(os.path.join(work, name))
print(f"{size/1024:.0f} KiB  {len(data.get('messages') or [])} msgs")


def timed(label, make, rounds=40):
    store.save(make(0))                      # warm the store's own cache
    best = []
    for i in range(rounds):
        payload = make(i + 1)
        t = time.perf_counter()
        store.save(payload)
        best.append((time.perf_counter() - t) * 1000)
    print(f"  {label:34s} median {statistics.median(best):6.2f} ms   min {min(best):6.2f} ms")


# A turn's save: one more message each time, as a running conversation does.
def moving(i):
    out = dict(data)
    out["messages"] = (data.get("messages") or []) + [{"role": "user", "content": f"turn {i}"}]
    out["turns"] = (data.get("turns") or 0) + i
    out["updated"] = time.time()
    return out


# A save with nothing behind it: the same conversation, a newer clock.
def idle(_i):
    return {**data, "updated": time.time()}


timed("a save that carries a new message", moving)
timed("a save with nothing new in it", idle)
