#!/usr/bin/env bash
# #PF4K sphinxpad re-measure, worker area (#TZWF): 300 stub turns, ask->first byte, Pss, spawn.
# Python only — the Qt build does not enter into it. Stub provider on loopback, no keys.
set -u
S=$HOME/relay-perf/src; S2=$HOME/relay-perf/src2
O=/tmp/rx/out; mkdir -p $O
cd /tmp/rx
for round in 1 2; do
  for side in b a; do
    root=$S; [ $side = a ] && root=$S2
    d=/tmp/rx/wb/$side$round; rm -rf $d; mkdir -p $d; cp /tmp/rx/turnbench.py $d/
    echo "== turnbench $side round $round  root=$root  load=$(cut -d' ' -f1 /proc/loadavg)"
    ( cd $d && timeout 900 python3 turnbench.py "$root" 300 )
  done
done 2>&1 | tee $O/worker-turns.txt

# spawn -> ready, on a read-only tree, with and without bytecode (the packaging fix)
cp $HOME/relay-perf/worker/startup.py /tmp/rx/startup.py
for side in b a; do
  root=$S; [ $side = a ] && root=$S2
  t=/tmp/rx/ro-$side; rm -rf $t; mkdir -p $t
  cp -r "$root/backend" $t/
  find $t -name __pycache__ -type d -prune -exec rm -rf {} + 2>/dev/null
  echo "== $side: read-only tree, NO bytecode"
  chmod -R a-w $t; XDG_CACHE_HOME=/tmp/rx/wx timeout 300 python3 /tmp/rx/startup.py "$t" 7
  chmod -R u+w $t
  python3 -m compileall -q --invalidation-mode unchecked-hash "$t/backend" >/dev/null 2>&1
  echo "== $side: read-only tree, bytecode installed (what the packaging fix does)"
  chmod -R a-w $t; XDG_CACHE_HOME=/tmp/rx/wx timeout 300 python3 /tmp/rx/startup.py "$t" 7
  chmod -R u+w $t; rm -rf $t
done 2>&1 | tee $O/worker-spawn.txt
echo WORKER_DONE
