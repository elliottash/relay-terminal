#!/usr/bin/env bash
# #PF4K transcript, third battery: event-loop stalls, the links-at-rest A/B, a 300-turn
# conversation and its resume in a fresh process.
set -uo pipefail
here=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd); cd "$here"
run() { echo "=== $1 $(date +%T) load=$(cut -d' ' -f1-3 /proc/loadavg)"; shift; "$@"; echo "--- done $(date +%T)"; }

bpf()      { BPF_SECS=18 PERF_AFTER=4 ./drive.sh bpf 30 'please stream zprose40000x20r200 now'; }
bpftools() { BPF_SECS=18 PERF_AFTER=6 ./drive.sh bpftools 60 'please run ztools60x10 now'; }
nolinks()  { EXTRA_CONF=$'[terminal]\ncolour_links=false' ./drive.sh nolinks 26 'please stream zprose40000x20r100 now'; }
links()    { ./drive.sh links 26 'please stream zprose40000x20r100 now'; }

for s in ${@:-bpf bpftools nolinks links}; do run "$s" "$s"; done
