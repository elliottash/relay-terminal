#!/usr/bin/env bash
# #PF4K sphinxpad re-measure, Switchboard worker side (#7M6E): board_open / refresh / search.
# Backend only (no Qt): each side runs its OWN backend tree against a private copy of the SAME
# board files, so neither warms the other's page cache and the input is identical.
set -u
S=$HOME/relay-perf/src; S2=$HOME/relay-perf/src2
O=/tmp/rx/out; mkdir -p $O
M=/tmp/rx/bmeasure.py
for size in now 3000; do
  for side in b a; do
    root=$S; [ $side = a ] && root=$S2
    ws=/tmp/rx/bw-$size-$side; rm -rf $ws; mkdir -p $ws
    cp -r "/tmp/rx/issues-$size" "$ws/issues"
    echo "== $size $side  ($root)  load=$(cut -d' ' -f1 /proc/loadavg)"
    timeout 600 python3 "$M" "$root" "$ws" 2>&1 || echo "  FAILED"
    rm -rf $ws
  done
done 2>&1 | tee $O/board-backend.txt
echo BOARD_BE_DONE
