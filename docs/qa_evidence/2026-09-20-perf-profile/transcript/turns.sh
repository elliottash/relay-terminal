#!/usr/bin/env bash
# #PF4K transcript: build a long conversation one turn at a time, quit cleanly, then reopen the
# same profile in a fresh process and measure what the resume costs.
#
#   ./turns.sh [n]        (default 300)
set -uo pipefail
here=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd); cd "$here"
n=${1:-300}
args=(); for i in $(seq 1 "$n"); do args+=("zturn $i"); done

echo "=== turns n=$n $(date +%T) load=$(cut -d' ' -f1-3 /proc/loadavg)"
KEEP=1 TYPE_DELAY=4 TYPE_PAUSE=0.10 GAP=0.05 ./drive.sh turns "${SETTLE_AFTER:-180}" "${args[@]}" | tee out/turns.head
sb=$(awk '/^KEEP:/{print $3}' out/turns.head)
relay=$(awk -F'relay=' '/^KEEP:/{split($2,a," ");print a[1]}' out/turns.head)
stub=$(awk -F'stub=' '/^KEEP:/{split($2,a," ");print a[1]}' out/turns.head)
xvfb=$(awk -F'xvfb=' '/^KEEP:/{split($2,a," ");print a[1]}' out/turns.head)
echo "sb=$sb relay=$relay stub=$stub xvfb=$xvfb"
[[ -z $sb ]] && { echo "no sandbox"; exit 1; }

# Memory and the conversation's size while it is still up.
grep -E '^(VmRSS|VmHWM)' "/proc/$relay/status" > out/turns.mem.txt 2>/dev/null
awk '{print "smaps", $0}' "/proc/$relay/smaps_rollup" >> out/turns.mem.txt 2>/dev/null
du -sb "$sb/h/.local/share/relay" >> out/turns.mem.txt 2>/dev/null

echo "--- quitting (SIGTERM), timing the clean exit"
q0=$(date +%s%N)
kill -TERM "$relay" 2>/dev/null
for i in $(seq 1 200); do kill -0 "$relay" 2>/dev/null || break; sleep 0.1; done
echo "quit_ms=$(( ($(date +%s%N) - q0) / 1000000 ))" | tee -a out/turns.mem.txt
kill "$stub" "$xvfb" 2>/dev/null; sleep 1
ls -la "$sb/h/.local/share/relay/state/" >> out/turns.mem.txt 2>/dev/null
wc -l "$sb/h/.local/share/relay/state/scrollback/"*.txt >> out/turns.mem.txt 2>/dev/null

echo "=== resume $(date +%T)"
REUSE_SB=$sb ./drive.sh resume 40
echo "--- done $(date +%T)"
