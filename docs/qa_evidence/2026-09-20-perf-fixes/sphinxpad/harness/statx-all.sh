#!/usr/bin/env bash
# #PF4K sphinxpad re-measure (#057J): filesystem syscalls per key press, and at idle.
# statx.sh types 30 characters into the prompt box and never presses Return; input mode is
# forced to shell in the profile it writes, so nothing typed can start an agent turn.
set -u
export DISPLAY=:271
O=/tmp/rx/out; mkdir -p $O
declare -A BIN=(
  [b5]=$HOME/relay-perf/build/relay      [a5]=$HOME/relay-perf/build2-qt5/relay
  [b6]=$HOME/relay-perf/build-qt6/relay  [a6]=$HOME/relay-perf/build2-qt6/relay )
declare -A ROOT=(
  [b5]=$HOME/relay-perf/src  [b6]=$HOME/relay-perf/src
  [a5]=$HOME/relay-perf/src2 [a6]=$HOME/relay-perf/src2 )

for run in 1 2 3; do
  for t in b5 a5 b6 a6; do
    RELAY_DATA_DIR="${ROOT[$t]}" timeout 300 /tmp/rx/h/statx.sh "$t-$run" "${BIN[$t]}" 2>&1 \
      | grep -Ev "^\{"
    sleep 2
  done
done 2>&1 | tee $O/per-key.txt

# syscalls a second at idle, 1 and 4 panes
for run in 1 2; do
  for t in b5 a5 b6 a6; do
    for panes in 1 4; do
      RELAY_DATA_DIR="${ROOT[$t]}" SKIP_FIRSTRUN=1 timeout 300 \
        python3 /tmp/rx/h/syscalls.py "$t-$panes-$run" "${BIN[$t]}" "$panes" 20 2>&1 | grep -Ev "^\{"
      sleep 2
    done
  done
done 2>&1 | tee $O/syscalls-idle.txt
echo STATX_DONE
