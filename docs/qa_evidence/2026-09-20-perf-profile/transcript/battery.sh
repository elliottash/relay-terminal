#!/usr/bin/env bash
# #PF4K transcript: the scenario battery.  ./battery.sh [scenario...]   (default: all)
set -uo pipefail
here=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
cd "$here"
run() { local label=$1; shift; echo "=== $label $(date +%T) load=$(cut -d' ' -f1-3 /proc/loadavg)"; "$@" ; echo "--- $label done $(date +%T)"; }

a100()  { PERF_AFTER=6 PERF_SECS=12 ./drive.sh a100  26 'please stream zprose40000x20r100 now'; }
a1000() { PERF_AFTER=4 PERF_SECS=8  ./drive.sh a1000 16 'please stream zprose40000x20r1000 now'; }
md()    { PERF_AFTER=5 PERF_SECS=12 ./drive.sh md    40 'please stream zmd40 now'; }
think() { PERF_AFTER=5 PERF_SECS=12 ./drive.sh think 40 'please stream zthink40000r200 now'; }
tools() { PERF_AFTER=8 PERF_SECS=25 ./drive.sh tools 150 'please run ztools200x10 now'; }
idle()  { ./drive.sh idle 25; }

for s in "${@:-a100 a1000 md think tools idle}"; do run "$s" "$s"; done
