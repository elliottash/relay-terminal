#!/usr/bin/env bash
# #PF4K sphinxpad re-measure: idle CPU / wakeups / memory, before vs after, Qt5 and Qt6.
# Three runs of every (binary, panes) cell, binaries interleaved inside a run so that any
# drift in the laptop's state is spread across all four columns rather than one.
set -u
cd /tmp/rx/h
export DISPLAY=:271 SKIP_FIRSTRUN=1
OUT=${OUT:-/tmp/rx/out}
mkdir -p "$OUT"
SECS=${SECS:-45}

declare -A BIN=(
  [b5]=$HOME/relay-perf/build/relay
  [a5]=$HOME/relay-perf/build2-qt5/relay
  [b6]=$HOME/relay-perf/build-qt6/relay
  [a6]=$HOME/relay-perf/build2-qt6/relay
)
declare -A ROOT=(
  [b5]=$HOME/relay-perf/src [b6]=$HOME/relay-perf/src
  [a5]=$HOME/relay-perf/src2 [a6]=$HOME/relay-perf/src2
)

for run in 1 2 3; do
  for panes in 1 4; do
    for t in b5 a5 b6 a6; do
      tag="${t}-${panes}-${run}"
      echo "### $tag $(date +%H:%M:%S) load=$(cut -d' ' -f1 /proc/loadavg)" >> "$OUT/idle.log"
      RELAY_BIN="${BIN[$t]}" RELAY_DATA_DIR="${ROOT[$t]}" \
        timeout 300 python3 idle.py "$tag" "$panes" 1 "$SECS" > "$OUT/idle-$tag.json" 2>> "$OUT/idle.log"
      python3 - "$OUT/idle-$tag.json" "$tag" >> "$OUT/idle.tsv" <<'PY'
import json, sys
try:
    d = json.load(open(sys.argv[1]))
except Exception as e:
    print(sys.argv[2], "FAILED", e); raise SystemExit
gui = d["rows"][0]
print("\t".join(str(x) for x in (
    sys.argv[2], d["panes"], d["panes_built"], d["load"], d["total_cpu_pct"], d["total_wakeups_s"],
    gui["cpu_pct"], gui["wakeups_s"], gui["pss_kb"], d["mem_at_start"].get("Rss"),
    sum(r["pss_kb"] or 0 for r in d["rows"]), d["nproc"])))
PY
      sleep 3
    done
  done
done
echo DONE >> "$OUT/idle.log"
