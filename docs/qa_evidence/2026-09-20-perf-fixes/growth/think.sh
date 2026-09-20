#!/usr/bin/env bash
# #PPR4 item B: the Activity pane open (Alt+Shift+R) while a long reasoning block streams.
#   ./think.sh <relay binary> <tag> [chars] [rate]
set -uo pipefail
here=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
bin=$1; tag=$2; chars=${3:-200000}; rate=${4:-4000}
out=/tmp/claude-1000/pf4k/fix/growth/out/$tag
rm -rf "$out"; mkdir -p "$out"
echo "=== $tag zthink${chars}r${rate} $(date +%T) load=$(cut -d' ' -f1-3 /proc/loadavg)"
RELAY_BIN=$bin OUT=$out POST_KEYS="alt+shift+r" POST_AFTER=2 \
  "$here/drive.sh" think "${SECS:-75}" "please stream zthink${chars}r${rate} now" | tee "$out/head.txt"
python3 "$here/an.py" "$out/think.samples.tsv" 0 5 | tee "$out/cpu.txt"
