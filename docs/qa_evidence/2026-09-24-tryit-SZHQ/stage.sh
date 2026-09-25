#!/usr/bin/env bash
# Try-it staging for #SZHQ. Rerunnable, offline, no model. Wipes and reseeds its fixture.
set -euo pipefail

REPO="$(cd "$(dirname "$0")/../../.." && pwd)"
FIX=/tmp/claude-1000/tryit/szhq

# kill the stand-in live processes from a previous run, if any
for pidfile in "$FIX"/*.pid; do
  [[ -f "$pidfile" ]] && kill "$(cat "$pidfile")" 2>/dev/null || true
done
rm -rf "$FIX"

# The real layout: <scratch root>/claude-1000/<container "-home-...">/<one dir per session>
S="$FIX/scratch/claude-1000/-home-user-demo"
mkdir -p "$S/task-huge/build" "$S/session-live" "$S/session-gone/verify"

# 1) a big tree nobody has touched for 30 hours — gc should remove it
dd if=/dev/zero of="$S/task-huge/build/blob.bin" bs=1M count=200 status=none
# 2) a session that died yesterday, small verify build — gc should remove it
echo '{}' > "$S/session-gone/verify/manifest.json"
find "$S/task-huge" "$S/session-gone" -depth -exec touch -d '30 hours ago' {} +
# 3) a live session's tree, touched now, with a real process using it — gc must keep it
echo "the fix" > "$S/session-live/pane.h"
(cd "$S/session-live" && exec sleep 600) & echo $! > "$FIX/in-use.pid"
sleep 600 & echo $! > "$FIX/live.pid"

export RELAY_SCRATCH_ROOTS="$FIX/scratch/claude-1000"
export RELAY_SCRATCH_BUDGET_GB=0.1

echo
echo "Staged: three agent session trees under $S"
echo "  task-huge      200 MB, idle 30 h   -> should be reclaimed"
echo "  session-gone     4 KB, idle 30 h   -> should be reclaimed"
echo "  session-live     8 KB, in use now  -> must survive (a live process sits in it)"
echo "Budget forced to 0.1 GB so the situation is already over it."
echo
echo "Run, in order (each takes the two exported variables):"
echo "  $REPO/scripts/relay-scratch check      ; echo \"exit \$?\""
echo "  $REPO/scripts/relay-scratch gc                     # dry run"
echo "  $REPO/scripts/relay-scratch gc --apply
  ls $S"
