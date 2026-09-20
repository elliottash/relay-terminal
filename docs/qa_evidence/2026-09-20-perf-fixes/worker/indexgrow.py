#!/usr/bin/env python3
"""What one autosave of a growing conversation costs conv_index (#TZWF).

  indexgrow.py <src-root> <store-dir> [turns]

`indexbench.py` times update_session against a session that does not change, which after the
incremental fix is the cheapest case there is. This one does what the pane does: append a turn,
save, append a turn, save — the shape the 46 ms in finding 3 was measured on.

<store-dir> is a COPY of ~/.local/share/relay (sizes and timings only, no content is printed).
Nothing writes to the real store.
"""
import glob, json, os, shutil, statistics, sys, time

ROOT, STORE = sys.argv[1], sys.argv[2]
TURNS = int(sys.argv[3]) if len(sys.argv) > 3 else 12
sys.path.insert(0, os.path.join(ROOT, "backend"))
os.environ["XDG_DATA_HOME"] = os.path.join(STORE, "xdg")
os.makedirs(os.path.join(STORE, "xdg", "relay"), exist_ok=True)
from relay_core import conv_index  # noqa: E402

PROSE = "bench text " * 40


def append_turn(data: dict, n: int) -> None:
    """One more turn, written the way agent.py's autosave leaves it."""
    epoch = str(data.get("epoch", 0) or 0)
    items = data.setdefault("checkpoints", {}).setdefault("items", [])
    turn = max([item.get("turn") or 0 for item in items if isinstance(item, dict)] or [0]) + 1
    prompt = f"bench prompt {n} {PROSE}"
    data["messages"].append({"role": "user", "content": prompt})
    items.append({"turn": turn, "prompt": prompt, "prompt_preview": prompt[:40],
                  "time": time.time(), "locations": {epoch: len(data["messages"])}, "files": {}})
    data["messages"].append({"role": "assistant", "content": f"bench reply {n} {PROSE}"})
    data["turns"] = turn
    data["updated"] = time.time()


work = os.path.join(STORE, "grow.db")
for suffix in ("", "-wal", "-shm"):
    if os.path.exists(work + suffix):
        os.unlink(work + suffix)
shutil.copy(os.path.join(STORE, "index.db"), work)
os.environ["RELAY_INDEX_PATH"] = work
index = conv_index.ConversationIndex(work)

files = sorted((os.path.getsize(f), f) for f in glob.glob(os.path.join(STORE, "sessions", "*", "*.json"))
               if not f.endswith("meta.json"))
print(f"store: {len(files)} session files, {sum(s for s, _ in files)/1e6:.1f} MB; "
      f"{TURNS} appended turns per session\n")
print("one autosave of a conversation that just grew by a turn:")
picks = []
for target in (5e3, 5e4, 2e5, 5e5, 1.6e6):
    best = min(files, key=lambda sf: abs(sf[0] - target))
    if best not in picks:
        picks.append(best)
for size, path in picks:
    data = json.load(open(path))
    directory = os.path.dirname(path)
    messages = len(data.get("messages") or [])
    index.update_session(data, session_dir=directory)      # the state an open pane is already in
    times = []
    for n in range(TURNS):
        append_turn(data, n)
        start = time.perf_counter()
        index.update_session(data, session_dir=directory)
        times.append((time.perf_counter() - start) * 1000)
    print(f"  {size/1024:8.0f} KiB  {messages:5d} messages  ->  median {statistics.median(times):6.2f} ms"
          f"   (first {times[0]:6.2f}, last {times[-1]:6.2f})")
print("\nloadavg", os.getloadavg())
