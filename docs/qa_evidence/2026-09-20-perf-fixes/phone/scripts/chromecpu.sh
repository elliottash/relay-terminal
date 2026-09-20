#!/usr/bin/env bash
# Chrome's CPU on the phone over a window (#PF4K phone area).
#
#   ./chromecpu.sh <label> <seconds>
#
# Sums utime+stime (jiffies, 100 Hz) over every com.android.chrome process before and after, so
# the number is CPU time attributable to the browser and not a sampled percentage. Also prints the
# `dumpsys batterystats --charged com.android.chrome` block and `dumpsys cpuinfo` rows at the end,
# which is what the brief asks for as the battery proxy.
set -uo pipefail
label=$1; secs=$2
A="adb -s ${SERIAL:-192.168.1.184:46849}"
out=${OUT:-/tmp/claude-1000/pf4k/phone/out}

snap() {
    $A shell 'for p in $(pgrep -f com.android.chrome); do cat /proc/$p/stat 2>/dev/null; done' 2>/dev/null |
        awk '{u+=$14; s+=$15} END {print u+0, s+0}'
}
read -r u0 s0 <<<"$(snap)"
t0=$(date +%s%3N)
sleep "$secs"
read -r u1 s1 <<<"$(snap)"
t1=$(date +%s%3N)
wall=$(( t1 - t0 ))
{
  echo "== $label  window ${wall} ms"
  echo "chrome cpu jiffies: utime $(( u1 - u0 ))  stime $(( s1 - s0 ))  total $(( u1 - u0 + s1 - s0 )) (100 Hz)"
  awk -v j="$(( u1 - u0 + s1 - s0 ))" -v w="$wall" 'BEGIN {printf "chrome cpu: %.2f s over %.1f s wall = %.1f%% of one core\n", j/100, w/1000, 100*(j/100)/(w/1000)}'
  echo "-- dumpsys cpuinfo (chrome rows)"
  $A shell dumpsys cpuinfo 2>/dev/null | grep -i -E "chrome|Load:" | head -8
  echo "-- batterystats --charged com.android.chrome"
  $A shell dumpsys batterystats --charged com.android.chrome 2>/dev/null |
      grep -E "Wake lock|Total running|Uid|Proc |Foreground|cpu=|wakeups|Wi-Fi" | head -20
} | tee "$out/$label.battery.txt"
