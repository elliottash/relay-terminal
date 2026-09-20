#!/usr/bin/env bash
# #PPR4: scenario (d) — 200 run_command calls, 2 000 lines each — alternating before/after,
# three rounds, so the load other sessions put on this box averages out of the comparison.
set -uo pipefail
here=/tmp/claude-1000/pf4k/fix/toolout
before=/tmp/claude-1000/pf4k/build/relay
after=$here/after-build/relay
for round in 1 2 3; do
  for side in b a; do
    bin=$before; off=0; [[ $side = a ]] && { bin=$after; off=1; }
    OUT=$here/ab PORT=$((8970 + round * 2 + off)) RELAY_BIN=$bin \
      timeout 300 "$here/h/drive.sh" "d$side$round" 45 'please run ztools200x10 now' >/dev/null 2>&1
    echo "$side$round $(python3 "$here/h/an.py" "$here/ab/d$side$round.samples.tsv" 0 10 | tail -1)"
  done
done
