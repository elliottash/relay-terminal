#!/usr/bin/env bash
# #PPR4: alternating before/after rounds, so both sides see the same load on a shared box.
set -uo pipefail
here=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
B=/tmp/claude-1000/pf4k/fix/growth/base-src/build/relay
A=/tmp/claude-1000/pf4k/fix/growth/after-src/build/relay
for r in 1 2; do
  PERF_SECS= PERF2_SECS= "$here/growth.sh" "$B" "before-r$r" 250 >/dev/null 2>&1
  PERF_SECS= PERF2_SECS= "$here/growth.sh" "$A" "after-r$r"  250 >/dev/null 2>&1
done
SECS=65 "$here/think.sh" "$A" think-after 200000 200 >/dev/null 2>&1
echo "=== turns, ms per turn (250 back-to-back turns, two alternating rounds)"
for t in before-r1 after-r1 before-r2 after-r2; do
  echo "--- $t"; cat /tmp/claude-1000/pf4k/fix/growth/out/$t/turncost.txt
done
echo "=== one 200 000-character reasoning block, Activity pane open"
echo "--- before"; cat /tmp/claude-1000/pf4k/fix/growth/out/think-before/cpu.txt
echo "--- after";  cat /tmp/claude-1000/pf4k/fix/growth/out/think-after/cpu.txt
