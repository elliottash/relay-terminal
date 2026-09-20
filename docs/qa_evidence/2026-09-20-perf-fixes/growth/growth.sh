#!/usr/bin/env bash
# #PPR4 item 2: N turns back to back, GUI-thread CPU per turn, plus an early and a late perf
# window from the same run.  ./growth.sh <relay binary> <tag> [turns]
set -uo pipefail
here=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
bin=$1; tag=$2; n=${3:-250}
out=/tmp/claude-1000/pf4k/fix/growth/out/$tag
rm -rf "$out"; mkdir -p "$out"
args=(); for i in $(seq 1 "$n"); do args+=("zturn $i"); done
echo "=== $tag n=$n $(date +%T) load=$(cut -d' ' -f1-3 /proc/loadavg)"
RELAY_BIN=$bin OUT=$out TYPE_DELAY=4 TYPE_PAUSE=0.10 GAP=0.05 \
  PERF_AFTER=${PERF_AFTER:-4} PERF_SECS=${PERF_SECS:-10} \
  PERF2_AFTER=${PERF2_AFTER:-24} PERF2_SECS=${PERF2_SECS:-10} \
  "$here/drive.sh" turns "${SETTLE_AFTER:-6}" "${args[@]}" | tee "$out/head.txt"
python3 "$here/turncost.py" "$tag" "$out" | tee "$out/turncost.txt"
