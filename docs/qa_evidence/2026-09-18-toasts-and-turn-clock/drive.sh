#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Evidence driver for toasts-and-turn-clock, offline: the streaming fake local model and the
# isolated jail of ../2026-09-18-thinking-copy-and-steer-withdraw (launch.sh, fake-provider.py).
#
# During one agent turn: the clock ticks in the prompt-box strip, never as a toast; Ctrl+C on a
# selection in the thinking bubble raises "N characters copied"; a prompt sent at once behind it
# raises "Queued · …". Both toasts must appear, one after the other, each staying up while the
# clock keeps counting. Shots: implementer-NN-*.png (full screen) and implementer-NN-*-strip.png
# (the bottom of the pane: the toast corner and the strip under the prompt box).
# Coordinates are for the 1400x900 Xvfb screen and a fresh single-pane window.
# Usage: RELAY_QA_DISPLAY=95 RELAY_QA_PORT=18445 drive.sh [repo root] [build dir]
set -euo pipefail
ROOT=${1:-$(cd "$(dirname "$0")/../../.." && pwd)}
OUT=$(cd "$(dirname "$0")" && pwd)
HARNESS="$OUT/../2026-09-18-thinking-copy-and-steer-withdraw"
JAIL=$(mktemp -d)
export DISPLAY=:${RELAY_QA_DISPLAY:-95}
export RELAY_QA_PORT=${RELAY_QA_PORT:-18445}
bash "$HARNESS/launch.sh" "$JAIL" "$ROOT" "${2:-$ROOT/build}" >"$JAIL/launch.log" 2>&1 &
LAUNCH=$!
cleanup() { pkill -TERM -P "$LAUNCH" 2>/dev/null || true; kill "$LAUNCH" 2>/dev/null || true; sleep 2; rm -rf "$JAIL"; }
trap cleanup EXIT
sleep 12

shot() {
  import -window root "$OUT/implementer-$1.png"
  convert "$OUT/implementer-$1.png" -crop 1260x370+0+440 +repage "$OUT/implementer-$1-strip.png"
}

xdotool mousemove 600 742 click 1
xdotool type --delay 20 "please plan this out"; xdotool key ctrl+Return
sleep 6
shot 01-clock-in-the-strip-no-toast
# Select the start of the streaming reasoning, copy it, and at once send a second prompt.
xdotool mousemove 36 669 mousedown 1; sleep 0.3; xdotool mousemove 300 669; sleep 0.3
xdotool mousemove 520 669; sleep 0.3; xdotool mouseup 1
sleep 0.5
xdotool key ctrl+c
xdotool type --delay 5 "and then the readme"; xdotool key Return
sleep 0.4
shot 02-copied-toast-up-queued-waits
sleep 1.8
shot 03-queued-toast-follows
sleep 2.2
shot 04-queued-toast-still-up-clock-moved
sleep 3
shot 05-toasts-gone-clock-still-counting
echo "done: $OUT/implementer-0*.png"
