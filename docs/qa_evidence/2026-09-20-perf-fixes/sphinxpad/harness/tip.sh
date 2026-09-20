#!/usr/bin/env bash
# #PF4K sphinxpad: the third column — main's tip (src3 / build3-qt5, Qt5 only: the tip does not
# compile on Qt6). Only the rows later commits touched.
#   tip.sh turns|think|ipc|prompt
set -u
O=/tmp/rx/out; mkdir -p $O
H=$HOME/relay-perf
declare -A BIN=( [b5]=$H/build/relay [a5]=$H/build2-qt5/relay [t5]=$H/build3-qt5/relay )
declare -A ROOT=( [b5]=$H/src [a5]=$H/src2 [t5]=$H/src3 )
mode=${1:-turns}

case $mode in
turns)
  for round in 1 2; do
    for t in b5 a5 t5; do
      echo "#### $t round $round  load=$(cut -d' ' -f1 /proc/loadavg)"
      RELAY_DATA_DIR="${ROOT[$t]}" PERF_SECS= PERF2_SECS= \
        timeout 900 /tmp/rx/gr/growth.sh "${BIN[$t]}" "$t-r$round" 250 2>&1 \
        | grep -vE "^WARNING|^input_mode" | tail -12
      sleep 3
    done
  done
  ;;
think)
  for t in b5 a5 t5; do
    echo "#### $t  load=$(cut -d' ' -f1 /proc/loadavg)"
    RELAY_DATA_DIR="${ROOT[$t]}" SECS=65 \
      timeout 900 /tmp/rx/gr/think.sh "${BIN[$t]}" "think-$t" 200000 200 2>&1 \
      | grep -vE "^WARNING|^input_mode" | tail -6
    sleep 3
  done
  ;;
ipc)
  # worker -> GUI bytes per turn: 60 cheap turns, whole channel captured
  args=(); for i in $(seq 1 60); do args+=("zturn $i"); done
  for t in b5 a5 t5; do
    echo "#### $t  load=$(cut -d' ' -f1 /proc/loadavg)"
    rm -rf /tmp/rx/gr/out/ipc-$t
    OUT=/tmp/rx/gr/out IPC=1 PORT=$((8850 + RANDOM % 40)) RELAY_BIN="${BIN[$t]}" \
      RELAY_DATA_DIR="${ROOT[$t]}" TYPE_DELAY=4 TYPE_PAUSE=0.10 GAP=0.05 \
      timeout 900 /tmp/rx/gr/drive.sh "ipc-$t" 10 "${args[@]}" 2>&1 | tail -2
    python3 /tmp/rx/tr/reduce.py "/tmp/rx/gr/out/ipc-$t.ipc/worker2gui.jsonl" | head -12
    echo "   bytes per turn: $(python3 -c "
import os,sys
n=os.path.getsize('/tmp/rx/gr/out/ipc-$t.ipc/worker2gui.jsonl')
print(f'{n} total, {n/60:.0f} B/turn')")"
    sleep 3
  done
  ;;
prompt)
  for t in b5 a5 t5; do
    p="${ROOT[$t]}/docs/qa_evidence/2026-09-20-perf-fixes/prompt/promptsize.py"
    echo "#### $t ($p)"
    [ -f "$p" ] && timeout 300 python3 "$p" 2>&1 | head -40 || echo "   not in this tree"
  done
  ;;
esac
echo "TIP_${mode}_DONE"
