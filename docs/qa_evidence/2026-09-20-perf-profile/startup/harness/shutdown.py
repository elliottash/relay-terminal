#!/usr/bin/env python3
"""SIGTERM-to-exit with a real multi-pane layout, repeated. Prints panes actually built and ms."""
import os, sys, time, glob, statistics
BASE = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, BASE)
import harness, idle
DISP = os.environ.get("DISPLAY", ":231")
panes, tabs, reps = int(sys.argv[1]), int(sys.argv[2]), int(sys.argv[3])
tag = sys.argv[4]
rows = []
for i in range(reps):
    prof = os.path.join(BASE, f"sd-{tag}")
    r = harness.run(f"{tag}#{i}", prof, ["--fresh"], DISP)
    time.sleep(6)
    idle.build_layout(r["pid"], panes, tabs)
    time.sleep(8)
    n = max(idle.panecount(r["pid"]), len(glob.glob(prof + "/tmp/relay-*")))
    t = time.monotonic()
    harness.PROCS[r["pid"]].terminate()
    harness.PROCS[r["pid"]].wait(180)
    rows.append((n, round((time.monotonic() - t) * 1000, 1)))
    time.sleep(1)
print(tag, "panes/term_ms:", rows,
      "median_term_ms:", round(statistics.median([x[1] for x in rows]), 1), flush=True)
