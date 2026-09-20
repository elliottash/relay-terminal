#!/usr/bin/env python3
"""GUI-thread CPU per turn, in groups of 50 turns (#PPR4).

    turncost.py <label> <out dir>

Reads <dir>/turns.marks (one "# prompt at <ns>" per turn, CLOCK_REALTIME ns) and
<dir>/turns.samples.tsv (cumulative jiffies, sampled every 200 ms from the window's own t0).
The sampler's t0 is the first sample; marks are absolute, so the two are aligned on the first
mark, which is also within a sample of the first turn.
"""
import sys

label, d = sys.argv[1], sys.argv[2]
marks = [int(l.split()[-1]) for l in open(d + "/turns.marks") if "prompt at" in l]
rows = []
with open(d + "/turns.samples.tsv") as f:
    f.readline()
    for line in f:
        p = line.split()
        if len(p) >= 10:
            rows.append((int(p[0]), int(p[1]) + int(p[2])))   # ms, gui jiffies
# Align: sample ms 0 is the sampler's start, which is before the first mark.
# Use the first mark as the origin and express marks in sampler-ms.
t0 = marks[0]
mark_ms = [(m - t0) / 1e6 for m in marks]
# find the sampler ms of the first mark: the sampler starts a moment earlier; assume the
# first mark lands at the sampler's own first sample plus the gap measured from the log.
# We only need differences, so shift marks so the first is at the first sample's ms.
shift = rows[0][0]
mark_ms = [m + shift for m in mark_ms]

def cpu_between(a, b):
    ja = jb = None
    for ms, j in rows:
        if ms <= a:
            ja = j
        if ms <= b:
            jb = j
    if ja is None or jb is None:
        return None
    return (jb - ja) / 100.0

print(f"# {label}: {len(marks)} turns, GUI-thread CPU per turn in groups of 50")
print("turns      gui_cpu_s  ms_per_turn")
for i in range(0, len(marks) - 49, 50):
    a, b = mark_ms[i], mark_ms[i + 49]
    c = cpu_between(a, b)
    if c is None:
        continue
    print(f"{i+1:4d}-{i+50:<4d}   {c:8.2f}   {c*1000/49:8.1f}")
