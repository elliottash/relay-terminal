#!/bin/bash
# The Switchboard scenarios, run on sphinxpad against one build.
#   sphinx-run.sh <build dir> <tag> <display>
# Prints one line per measurement; the parent tabulates them against spark's.
set -u
B=$1; TAG=$2; D=$3
cd ~/relay-perf/board || exit 1
export RELAY_BIN=$B/relay
for w in 340 3000; do
    rm -rf "ws$w"; mkdir -p "ws$w"; cp -r "issues$w" "ws$w/issues"
done

echo "=== $TAG  ($RELAY_BIN)"
"$RELAY_BIN" --version 2>/dev/null | head -1
ldd "$RELAY_BIN" | grep -oE 'libQt[56][A-Za-z]+\.so\.[0-9.]+' | sort -u | head -3

pkill -f "Xvfb $D" 2>/dev/null
Xvfb "$D" -screen 0 1600x1000x24 -nolisten tcp >/dev/null 2>&1 &
sleep 2
export DISPLAY=$D

read -r P W < <(./launch.sh "${D#:}" "$HOME/relay-perf/board/ws340" "${TAG}340")
[ -z "${P:-}" ] && { echo "launch failed"; exit 1; }
sleep 6
xdotool windowfocus --sync "$W"
python3 act.py --pid "$P" --win "$W" --region "650x810+660+40" --label "$TAG board.open 340 COLD" \
    --timeout 60 -- xdotool key ctrl+shift+s
sleep 2
python3 act.py --pid "$P" --win "$W" --region "650x810+660+40" --label "$TAG expand NEEDS QA" \
    --timeout 40 -- xdotool mousemove 720 578 click 1
sleep 1
xdotool mousemove 960 102 click 1; sleep 1
for c in s w i t c h; do
    python3 measure.py --pid "$P" --settle 0.25 --label "$TAG filter key '$c'" -- xdotool key "$c"
done
import -silent -window "$W" "out/$TAG-board.png" 2>/dev/null
kill -TERM "$P" 2>/dev/null; sleep 2

# the 3,000-card board: does the pane ever populate?
read -r P W < <(./launch.sh "${D#:}" "$HOME/relay-perf/board/ws3000" "${TAG}3000")
sleep 6; xdotool windowfocus --sync "$W"
xdotool key ctrl+shift+s; sleep 25
import -silent -window "$W" "out/$TAG-3000.png" 2>/dev/null
if pgrep -P "$P" -f worker.py >/dev/null; then echo "$TAG 3000: worker alive"; else echo "$TAG 3000: WORKER GONE (payload over the 8 MB cap)"; fi
kill -TERM "$P" 2>/dev/null; sleep 2
pkill -f "Xvfb $D" 2>/dev/null
