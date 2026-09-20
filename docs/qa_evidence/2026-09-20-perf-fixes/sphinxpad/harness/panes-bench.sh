#!/usr/bin/env bash
# #PF4K sphinxpad re-measure (#MDSG): what a keystroke in the sessions search costs, and what
# opening a big file in the file pane costs, before vs after, Qt5 and Qt6.
# Both sides run the SAME bench code and the SAME input file; nothing here reads the owner's store.
set -u
export DISPLAY=${RDISP:-:271}
H=/tmp/rx/pbench; rm -rf $H; mkdir -p $H/{cfg,data,state,cache,tmp}
export XDG_CONFIG_HOME=$H/cfg XDG_DATA_HOME=$H/data XDG_STATE_HOME=$H/state \
       XDG_CACHE_HOME=$H/cache TMPDIR=$H/tmp RELAY_KEYRING=off
BIG=$HOME/relay-perf/src2/src/Pane.h
echo "bench file: $BIG ($(wc -l < $BIG) lines, $(stat -c%s $BIG) bytes)  load=$(cut -d' ' -f1 /proc/loadavg)"
for plat in xcb offscreen; do
  for t in pb pa; do
    for q in 5 6; do
      d=/tmp/rx/build-$t-qt$q
      for rep in 1 2; do
        echo "--- $plat $t qt$q rep$rep"
        QT_QPA_PLATFORM=$plat RELAY_PERF_BENCH=1 timeout 300 \
          $d/relay-conversations-tests searchKeystrokeCost 2>&1 | grep -viE "^\*|^Totals|QSKIP|^PASS|^Config" | head -20
        QT_QPA_PLATFORM=$plat RELAY_PERF_BENCH=1 RELAY_PERF_FILE=$BIG timeout 300 \
          $d/relay-filepanes-tests openCost 2>&1 | grep -viE "^\*|^Totals|QSKIP|^PASS|^Config" | head -30
      done
    done
  done
done
echo PANES_BENCH_DONE
