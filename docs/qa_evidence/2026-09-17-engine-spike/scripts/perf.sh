#!/bin/bash
# usage: perf.sh <name> <launch-cmd...>   (launch cmd gets the inner script path appended)
source /tmp/claude-1000/sp/env.sh
NAME=$1; shift
F=/tmp/claude-1000/sp/big200.txt
OUT=/tmp/claude-1000/sp/perf-$NAME.txt
rm -f $OUT
INNER=/tmp/claude-1000/sp/inner-$NAME.sh
cat > $INNER <<EOF
sleep 3
s=\$(date +%s.%N)
cat $F
e=\$(date +%s.%N)
echo "\$s \$e" > $OUT
sleep 600
EOF
"$@" bash $INNER >/dev/null 2>&1 &
LPID=$!
sleep 2
# find the terminal process (the launcher itself)
TPID=$LPID
tick() { awk '{print $14+$15}' /proc/$TPID/stat; }
c0=$(tick)
while [ ! -s $OUT ]; do sleep 0.2; done
# let the terminal finish rendering the backlog
sleep 1
c1=$(tick)
rss=$(awk '/VmHWM/{print $2}' /proc/$TPID/status)
read s e < $OUT
HZ=$(getconf CLK_TCK)
echo "$NAME wall_s=$(echo "$e - $s" | bc) terminal_cpu_s=$(echo "scale=2; ($c1 - $c0)/$HZ" | bc) peak_rss_kb=$rss"
import -window root $EV/13-perf-$NAME.png
kill $LPID; sleep 0.5; kill -9 $LPID 2>/dev/null
