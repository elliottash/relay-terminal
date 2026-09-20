#!/usr/bin/env bash
set -uo pipefail
here=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd); cd "$here"
echo "=== strace2 (covers the turns) $(date +%T)"
SETTLE=30 PRE_SLEEP=3 GAP=6 STRACE_OUT=$here/out/strace2.txt RELAY_BIN=$here/straced-relay \
    ./drive.sh strace2 60 'please stream zprose20000x20r200 now' 'please run ztools20x5 now'
echo "--- strace2 done $(date +%T) lines=$(wc -l < out/strace2.txt 2>/dev/null)"
