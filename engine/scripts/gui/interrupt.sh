#!/usr/bin/env bash
# GUI Ctrl+C latency harness: start `cat FILE` (a flood), press Ctrl+C with
# xdotool 1.5 s later, report the time until the cat process is gone.
#
#   DISPLAY=:78 engine/scripts/gui/interrupt.sh NAME FILE WORKDIR launcher args...
set -euo pipefail
name=$1 file=$2 work=$3
shift 3
mkdir -p "$work"
inner="$work/inner-intr-$name.sh"
cat >"$inner" <<EOF
cd '$work'
exec bash --norc -i
EOF
"$@" bash "$inner" >"$work/intr-$name.log" 2>&1 &
lpid=$!
sleep 2
win=$(xdotool search --pid "$lpid" | tail -1)
xdotool windowfocus "$win"
xdotool mousemove 300 200
# Several copies so even a fast terminal is still flooding 1.5 s later.
xdotool type "cat $file $file $file $file $file $file $file $file"
xdotool key Return
sleep 1.5
catpid=$(pgrep -n -f "^cat $file $file")
if [ -z "$catpid" ]; then
    echo "$name: flood already finished before Ctrl+C (use a bigger FILE)"
    kill "$lpid"
    exit 1
fi
t0=$(date +%s.%N)
xdotool key ctrl+c
for _ in $(seq 3000); do [ -d "/proc/$catpid" ] || break; sleep 0.005; done
t1=$(date +%s.%N)
sleep 1
xdotool type 'echo typed-after-interrupt'
xdotool key Return
sleep 0.5
command -v import >/dev/null && import -window root "$work/interrupt-$name.png" || true
echo "$name ctrl+c->cat exit: $(echo "$t1 - $t0" | bc) s"
kill "$lpid" 2>/dev/null || true
sleep 0.3
kill -9 "$lpid" 2>/dev/null || true
