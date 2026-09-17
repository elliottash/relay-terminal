#!/usr/bin/env bash
# GUI throughput harness: time `cat FILE` inside a terminal on an X display.
#
#   DISPLAY=:78 engine/scripts/gui/perf-cat.sh NAME FILE WORKDIR launcher args...
#
# The launcher gets "bash INNER_SCRIPT" appended (e.g. `relay-vterm-spike --size 100x30 -e`,
# `konsole --nofork -e`, `xterm -geometry 100x30 -e`). Wall time is measured by the
# shell inside the terminal (so it includes back-pressure from a slow terminal);
# terminal CPU is user+sys of the launcher process over the same window, plus a
# one-second settle for the final repaint. Needs bc and ImageMagick `import`
# (optional screenshot into WORKDIR).
set -euo pipefail
name=$1 file=$2 work=$3
shift 3
mkdir -p "$work"
out="$work/perf-$name.txt"
inner="$work/inner-$name.sh"
rm -f "$out"
cat >"$inner" <<EOF
sleep 3
s=\$(date +%s.%N)
cat '$file'
e=\$(date +%s.%N)
echo "\$s \$e" > '$out'
sleep 600
EOF
"$@" bash "$inner" >/dev/null 2>&1 &
lpid=$!
sleep 2
tick() { awk '{print $14+$15}' "/proc/$lpid/stat"; }
c0=$(tick)
while [ ! -s "$out" ]; do sleep 0.2; done
sleep 1
c1=$(tick)
rss=$(awk '/VmHWM/{print $2}' "/proc/$lpid/status")
read -r s e <"$out"
hz=$(getconf CLK_TCK)
echo "$name wall_s=$(echo "$e - $s" | bc) terminal_cpu_s=$(echo "scale=2; ($c1 - $c0)/$hz" | bc) peak_rss_kb=$rss"
command -v import >/dev/null && import -window root "$work/perf-$name.png" || true
kill "$lpid" 2>/dev/null || true
sleep 0.5
kill -9 "$lpid" 2>/dev/null || true
