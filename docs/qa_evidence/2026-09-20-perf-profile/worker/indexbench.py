#!/usr/bin/env python3
"""conv_index cost against a read-only copy of the owner's store.

  indexbench.py <src-root> <store-dir>

Never touches ~/.local/share/relay; <store-dir> is the copy.
"""
import glob, json, os, shutil, sqlite3, statistics, sys, time

ROOT, STORE = sys.argv[1], sys.argv[2]
sys.path.insert(0, os.path.join(ROOT, "backend"))
os.environ["XDG_DATA_HOME"] = os.path.join(STORE, "xdg")
os.makedirs(os.path.join(STORE, "xdg", "relay"), exist_ok=True)
from relay_core import conv_index  # noqa: E402


def med(f, n=5):
    xs = []
    for _ in range(n):
        a = time.perf_counter(); f(); xs.append(time.perf_counter() - a)
    return statistics.median(xs) * 1000


files = sorted((os.path.getsize(f), f) for f in glob.glob(os.path.join(STORE, "sessions", "*", "*.json"))
               if not f.endswith("meta.json"))
print(f"store: {len(files)} session files, {sum(s for s, _ in files)/1e6:.1f} MB")

# --- update_session: what one autosave costs, by conversation size -----------------
work = os.path.join(STORE, "bench.db")
for p in (work, work + "-wal", work + "-shm"):
    if os.path.exists(p):
        os.unlink(p)
shutil.copy(os.path.join(STORE, "index.db"), work)
os.environ["RELAY_INDEX_PATH"] = work
idx = conv_index.ConversationIndex(work)
print("\nupdate_session (one autosave) by session-file size:")
picks = []
for target in (5e3, 5e4, 2e5, 5e5, 1.6e6):
    best = min(files, key=lambda sf: abs(sf[0] - target))
    if best not in picks:
        picks.append(best)
for size, path in picks:
    data = json.load(open(path))
    n = len(data.get("messages") or [])
    d = os.path.dirname(path)
    ms = med(lambda: idx.update_session(data, session_dir=d))
    print(f"  {size/1024:8.0f} KiB  {n:5d} messages  turns={data.get('turns')}  ->  {ms:7.2f} ms")

# --- full rebuild --------------------------------------------------------------
fresh = os.path.join(STORE, "rebuild.db")
for p in (fresh, fresh + "-wal", fresh + "-shm"):
    if os.path.exists(p):
        os.unlink(p)
idx2 = conv_index.ConversationIndex(fresh)
t = time.perf_counter()
count = 0
import resource
r0 = resource.getrusage(resource.RUSAGE_SELF).ru_maxrss
for size, path in files:
    try:
        data = json.load(open(path))
    except Exception:
        continue
    if data.get("kind") != "relay_session":
        continue
    idx2.update_session(data, session_dir=os.path.dirname(path))
    count += 1
wall = time.perf_counter() - t
r1 = resource.getrusage(resource.RUSAGE_SELF).ru_maxrss
print(f"\nfull rebuild: {count} sessions in {wall:.1f} s  ({wall/max(count,1)*1000:.0f} ms each), "
      f"db {os.path.getsize(fresh)/1e6:.1f} MB, peak RSS {r1/1024:.0f} MiB (was {r0/1024:.0f})")

# --- query latency -------------------------------------------------------------
print("\nqueries against the copy of the live index:")
live = conv_index.ConversationIndex(os.path.join(STORE, "index.db"))
for q in ("", "relay", "board_tools", "profile performance"):
    ms = med(lambda: live.search(q, scope="all", limit=50) if q else live.search("", scope="all", limit=50), n=5)
    print(f"  {q!r:26s} {ms:7.2f} ms")
print("loadavg", os.getloadavg())
