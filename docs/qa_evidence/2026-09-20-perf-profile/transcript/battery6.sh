#!/usr/bin/env bash
set -uo pipefail
here=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd); cd "$here"
run() { echo "=== $1 $(date +%T) load=$(cut -d' ' -f1-3 /proc/loadavg)"; shift; "$@"; echo "--- done $(date +%T)"; }
sysprose() { SYSCALL_SECS=15 PERF_AFTER=5 ./drive.sh sysprose 30 'please stream zprose40000x20r200 now'; }
systools() { SYSCALL_SECS=15 PERF_AFTER=6 ./drive.sh systools 60 'please run ztools60x10 now'; }
nolinks2() { EXTRA_CONF=$'[terminal]\ncolour_links=false' ./drive.sh nolinks2 26 'please stream zprose40000x20r100 now'; }
nolinks3() { EXTRA_CONF=$'[terminal]\ncolour_links=false' ./drive.sh nolinks3 26 'please stream zprose40000x20r100 now'; }
links2()   { ./drive.sh links2 26 'please stream zprose40000x20r100 now'; }
links3()   { ./drive.sh links3 26 'please stream zprose40000x20r100 now'; }
for s in ${@:-sysprose systools nolinks2 links2 nolinks3 links3}; do run "$s" "$s"; done
