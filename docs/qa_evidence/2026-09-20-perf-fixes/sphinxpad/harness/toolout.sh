#!/usr/bin/env bash
# #PF4K sphinxpad re-measure, tool output (#PPR4): worker->GUI bytes per tool call and the GUI
# CPU of the 200-tool-call scenario, before vs after, Qt5 and Qt6.
# Everything runs against the loopback stub provider drive.sh configures; no keys, no network.
set -u
T=/tmp/rx/tr
O=/tmp/rx/out; mkdir -p $O
declare -A BIN=(
  [b5]=$HOME/relay-perf/build/relay      [a5]=$HOME/relay-perf/build2-qt5/relay
  [b6]=$HOME/relay-perf/build-qt6/relay  [a6]=$HOME/relay-perf/build2-qt6/relay )
declare -A ROOT=(
  [b5]=$HOME/relay-perf/src  [b6]=$HOME/relay-perf/src
  [a5]=$HOME/relay-perf/src2 [a6]=$HOME/relay-perf/src2 )

mode=${1:-cpu}

if [ "$mode" = ipc ]; then
  for t in b5 a5; do
    echo "== ipc $t  load=$(cut -d' ' -f1 /proc/loadavg)"
    OUT=$T/out PORT=$((8990 + RANDOM % 50)) IPC=1 RELAY_BIN="${BIN[$t]}" RELAY_DATA_DIR="${ROOT[$t]}" \
      timeout 400 $T/drive.sh "ipc-$t" 40 'please stream zprose20000x20r200 now' 'please run ztools20x5 now' \
      2>&1 | tail -3
    python3 $T/reduce.py "$T/out/ipc-$t.ipc/worker2gui.jsonl" 2>&1 | head -18
  done
  exit 0
fi

for round in 1 2; do
  for t in b5 a5 b6 a6; do
    lab="d$t$round"
    echo "== $lab  load=$(cut -d' ' -f1 /proc/loadavg)"
    OUT=$T/out PORT=$((8900 + round * 10 + RANDOM % 8)) RELAY_BIN="${BIN[$t]}" RELAY_DATA_DIR="${ROOT[$t]}" \
      timeout 400 $T/drive.sh "$lab" 45 'please run ztools200x10 now' 2>&1 | tail -2
    python3 $T/an.py "$T/out/$lab.samples.tsv" 0 10 2>/dev/null | tail -1 | sed "s/^/$lab /"
  done
done
echo TOOLOUT_DONE
