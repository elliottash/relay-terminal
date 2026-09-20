#!/usr/bin/env python3
"""#PF4K transcript: turn a samples.tsv into GUI-thread CPU per second and per delta.

    ./an.py out/<label>.samples.tsv <deltas_per_second> [bucket_seconds]

Columns are cumulative jiffies (CLK_TCK=100), so each row is differenced against the one before.
Only buckets where the GUI thread did real work are printed, plus the idle floor before/after.
"""
import sys

TCK = 100.0

path = sys.argv[1]
rate = float(sys.argv[2]) if len(sys.argv) > 2 else 0.0
bucket = float(sys.argv[3]) if len(sys.argv) > 3 else 2.0

rows = []
with open(path) as f:
    head = f.readline().split()
    for line in f:
        p = line.split()
        if len(p) < 10:
            continue
        rows.append([int(x) for x in p])

# ms gui_u gui_s proc_u proc_s threads rss worker vcs ncs
print(f"# {path}  rate={rate}/s  bucket={bucket}s  samples={len(rows)}")
print("t_s\tgui_cpu%\tproc_cpu%\tworker%\trss_MB\tcs/s\tms_per_delta")
t0 = rows[0][0]
i = 0
while i < len(rows) - 1:
    j = i
    while j < len(rows) - 1 and (rows[j][0] - rows[i][0]) < bucket * 1000:
        j += 1
    dt = (rows[j][0] - rows[i][0]) / 1000.0
    if dt <= 0:
        i = j
        continue
    gui = (rows[j][1] + rows[j][2] - rows[i][1] - rows[i][2]) / TCK
    proc = (rows[j][3] + rows[j][4] - rows[i][3] - rows[i][4]) / TCK
    work = (rows[j][7] - rows[i][7]) / TCK
    cs = (rows[j][8] + rows[j][9] - rows[i][8] - rows[i][9]) / dt
    per = (gui * 1000.0 / (rate * dt)) if rate else 0.0
    print(f"{(rows[i][0]-t0)/1000.0:6.1f}\t{100*gui/dt:7.1f}\t{100*proc/dt:8.1f}\t{100*work/dt:6.1f}"
          f"\t{rows[j][6]/1024.0:7.1f}\t{cs:6.0f}\t{per:8.3f}")
    i = j

# totals
tot_gui = (rows[-1][1]+rows[-1][2]-rows[0][1]-rows[0][2])/TCK
tot_proc = (rows[-1][3]+rows[-1][4]-rows[0][3]-rows[0][4])/TCK
print(f"# TOTAL gui_cpu_s={tot_gui:.2f} proc_cpu_s={tot_proc:.2f} rss_start_MB={rows[0][6]/1024:.1f} rss_end_MB={rows[-1][6]/1024:.1f} span_s={(rows[-1][0]-rows[0][0])/1000:.1f}")
