#!/usr/bin/env bash
# #PF4K transcript, second battery: tool-heavy turns, a long conversation, resume, the IPC channel.
set -uo pipefail
here=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd); cd "$here"
run() { echo "=== $1 $(date +%T) load=$(cut -d' ' -f1-3 /proc/loadavg)"; shift; "$@"; echo "--- done $(date +%T)"; }

tools() { PERF_AFTER=10 PERF_SECS=25 ./drive.sh tools 170 'please run ztools200x10 now'; }
ipc()   { IPC=1 ./drive.sh ipc 30 'please stream zprose20000x20r200 now' 'please run ztools20x5 now'; }
bpf()   { BPF_SECS=20 PERF_AFTER=5 ./drive.sh bpf 30 'please stream zprose40000x20r200 now'; }

turns() {
    local n=${TURNS:-300} args=()
    for i in $(seq 1 "$n"); do args+=("zturn $i"); done
    KEEP=1 TYPE_DELAY=4 TYPE_PAUSE=0.12 GAP=0.05 ./drive.sh turns 240 "${args[@]}"
}

for s in "${@:-tools ipc bpf}"; do run "$s" "$s"; done
