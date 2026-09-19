#!/usr/bin/env bash
# SPDX-License-Identifier: AGPL-3.0-or-later
# Evidence driver, offline (fake-provider.py as a local model, isolated jail via launch.sh).
#
#   case "copy":     while the reasoning streams, drag-select part of it, wait while more arrives,
#                    then Ctrl+C in the prompt box. The clipboard must hold exactly the highlighted
#                    text; the second turn queues a WITHDRAWME prompt, makes it a steer (Enter on the
#                    empty prompt box), clicks the × on its "next tool call" row, and the requests log
#                    must never carry WITHDRAWME to the model.
#   case "select":   Copy on select on: a drag in the bubble alone fills the clipboard, no Ctrl+C.
#
# Coordinates are for the 1400x900 Xvfb screen and a fresh single-pane window.
# Usage: drive.sh <copy|select> [repo root] [build dir]
set -euo pipefail
CASE=${1:?copy or select}
ROOT=${2:-$(cd "$(dirname "$0")/../../.." && pwd)}
OUT=$(cd "$(dirname "$0")" && pwd)
JAIL=$(mktemp -d)
export DISPLAY=:${RELAY_QA_DISPLAY:-93}
[ "$CASE" = select ] && export COPY_ON_SELECT=1
bash "$OUT/launch.sh" "$JAIL" "$ROOT" "${3:-$ROOT/build}" >"$JAIL/launch.log" 2>&1 &
LAUNCH=$!
cleanup() { pkill -TERM -P "$LAUNCH" 2>/dev/null || true; kill "$LAUNCH" 2>/dev/null || true; sleep 2; rm -rf "$JAIL"; }
trap cleanup EXIT
sleep 12

shot() { import -window root "$OUT/implementer-$CASE-$1.png"; }
clip() { xclip -o -selection clipboard 2>/dev/null || true; }

printf 'EMPTY' | xclip -selection clipboard
xdotool mousemove 600 742 click 1
xdotool type --delay 20 "please plan this out"; xdotool key ctrl+Return
sleep 6
# Drag across the start of the bubble's text while it is still streaming.
xdotool mousemove 36 669 mousedown 1; sleep 0.3; xdotool mousemove 300 669; sleep 0.3
xdotool mousemove 520 669; sleep 0.3; xdotool mouseup 1
sleep 2.5
shot 01-selection-held-while-reasoning-streams
if [ "$CASE" = select ]; then
  printf '%s\n' "$(clip)" >"$OUT/$CASE-clipboard.txt"
  echo "clipboard after the drag alone: [$(clip)]"
  exit 0
fi
echo "clipboard before Ctrl+C: [$(clip)]"
xdotool key ctrl+c; sleep 0.5
shot 02-ctrl-c-in-the-prompt-box-copied-it
printf '%s\n' "$(clip)" >"$OUT/$CASE-clipboard.txt"
echo "clipboard after Ctrl+C: [$(clip)]"

sleep 55   # the first turn's 20 s of reasoning and 30 s tool call
xdotool type --delay 20 "second turn please"; xdotool key ctrl+Return
sleep 4
xdotool type --delay 15 "WITHDRAWME please also check the readme file too"; xdotool key Return
sleep 0.8; xdotool key Return   # Enter on the empty prompt box: at the next tool call
sleep 1.2
shot 03-steer-row-with-its-x
xdotool mousemove 1223 683 click 1
sleep 0.6
shot 04-withdrawn
sleep 55
shot 05-turn-finished-without-it
cp "$JAIL/requests.jsonl" "$OUT/$CASE-requests.jsonl"
if grep -q WITHDRAWME "$OUT/$CASE-requests.jsonl"; then echo "FAIL: the withdrawn steer reached the model"; exit 1; fi
echo "PASS: $(wc -l <"$OUT/$CASE-requests.jsonl") requests, none carried WITHDRAWME"
