#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Evidence driver for the one queue list (#one-queue-list card), offline: fake-provider.py as a
# local model (90 s of reasoning, then a 20 s tool call), isolated jail via launch.sh.
#
# One running turn, every queue key on it, then the request log decides:
#   STEERONE    a steer, ↑ selects it (top row), Shift+Delete withdraws it     -> never reaches the model
#   STEERTWO    a steer, Enter on it takes it back into the prompt box, edited -> reaches the model only
#               as a later turn's prompt, "... EDITED", never as a steer
#   QUEUEDTHREE Ctrl+↑ on the head makes it a steer, Ctrl+↓ sends it back      -> runs as the next turn
#   QUEUEDFOUR  edited in place (" NOW"), Ctrl+↑ twice: to the head, then a steer -> delivered at the tool call
#   QUEUEDFIVE  Shift+Delete                                                   -> never reaches the model
#   STEERSIX    a steer, its × clicked                                         -> never reaches the model
#   QUEUEDEIGHT dragged above the steers                                       -> a steer, delivered at the tool call
#
# Coordinates are for the 1400x900 Xvfb screen and a fresh single-pane window.
# Usage: drive.sh [repo root] [build dir]
set -euo pipefail
ROOT=${1:-$(cd "$(dirname "$0")/../../.." && pwd)}
OUT=$(cd "$(dirname "$0")" && pwd)
JAIL=$(mktemp -d)
export DISPLAY=:${RELAY_QA_DISPLAY:-96}
export FAKE_REASON_SECONDS=90 FAKE_TOOL_SLEEP=20
bash "$OUT/launch.sh" "$JAIL" "$ROOT" "${2:-$ROOT/build}" >"$JAIL/launch.log" 2>&1 &
LAUNCH=$!
cleanup() { pkill -TERM -P "$LAUNCH" 2>/dev/null || true; kill "$LAUNCH" 2>/dev/null || true; sleep 2; rm -rf "$JAIL"; }
trap cleanup EXIT
sleep 12

shot() { import -window root "$OUT/implementer-$1.png"; }
say() { xdotool type --delay 8 "$1"; }

xdotool mousemove 600 742 click 1
say "please plan this out"; xdotool key ctrl+Return
sleep 3
for t in "please STEERONE also check the readme file" "please STEERTWO also look at the tests"; do
  say "$t"; xdotool key Return; sleep 0.8; xdotool key Return; sleep 1.2   # Enter on the empty box: a steer
done
for t in "please QUEUEDTHREE summarize the changes" "please QUEUEDFOUR list the open issues" "please QUEUEDFIVE remove me later"; do
  say "$t"; xdotool key Return; sleep 1
done
sleep 0.5; shot 01-steers-are-rows-at-the-top
xdotool key Up; sleep 0.6; shot 02-up-selects-the-top-steer
xdotool key shift+Delete; sleep 0.05; shot 03a-shift-delete-withdrawing
sleep 1.2; shot 03b-withdrawn-selection-on-the-next-steer
xdotool key Return; sleep 1; shot 04-enter-took-the-steer-back-into-the-prompt-box
xdotool key End; say " EDITED"; xdotool key Return; sleep 1; shot 05-edited-and-queued-as-a-prompt
xdotool key Up; sleep 0.4; xdotool key ctrl+Up; sleep 0.8; shot 06-ctrl-up-on-the-head-made-it-a-steer
xdotool key ctrl+Down; sleep 1.2; shot 07-ctrl-down-sent-it-back-to-the-queue
xdotool key Up; sleep 0.3; xdotool key Down; xdotool key Down; sleep 0.3; xdotool key shift+Delete; sleep 0.5
shot 08-shift-delete-removed-a-queued-row
xdotool key Escape; sleep 0.3
xdotool key Up; xdotool key Down; sleep 0.3; xdotool key End; say " NOW"; xdotool key Return; sleep 0.8
shot 09-queued-row-edited-in-place
xdotool key Up; xdotool key Down; sleep 0.3; xdotool key ctrl+Up; sleep 0.4; xdotool key ctrl+Up; sleep 0.8
shot 10-ctrl-up-twice-to-the-head-then-a-steer
xdotool key Escape; sleep 0.3
say "please STEERSIX this one goes by mouse"; xdotool key Return; sleep 0.8; xdotool key Return; sleep 1.5
shot 11-a-second-steer
sleep 20   # hints keep a 20 s gap between them, and ↑ just showed the queue-editing one
# Rows (bottom of the list is fixed above the prompt box, 28 px each): FOUR steer, SIX steer,
# THREE, TWO EDITED. The × of the SIX row:
xdotool mousemove 1224 624 click 1; sleep 0.8
shot 12-x-on-a-steer-and-its-hint
sleep 20
say "please QUEUEDEIGHT dragged to the top"; xdotool key Return; sleep 1.5
shot 13-before-the-drag
# Rows now: FOUR steer, THREE, TWO EDITED, EIGHT. Drag EIGHT (bottom row) above the steer row.
xdotool mousemove 300 680 mousedown 1; sleep 0.3
for y in 670 650 630 610 595 588 585; do xdotool mousemove 300 $y; sleep 0.1; done
sleep 0.3; xdotool mouseup 1; sleep 1
shot 14-dragged-above-the-steers-it-became-one
sleep 100   # the tool call at 90 s, its 20 s, then the steers are delivered
shot 15-steers-delivered-at-the-tool-call
sleep 150   # the rest of the turn, then QUEUEDTHREE runs as the next turn
shot 16-ctrl-down-steer-ran-as-the-next-turn
sleep 130
shot 17-the-edited-steer-ran-as-a-prompt
cp "$JAIL/requests.jsonl" "$OUT/requests.jsonl"
python3 - "$OUT/requests.jsonl" <<'PY'
import json, re, sys
rows = [json.loads(l) for l in open(sys.argv[1])]
fail = []
seen = [r["user_text"] for r in rows if r["tools"]]
def turns_with(word): return [i for i, t in enumerate(seen) if word in t]
for word in ("STEERONE", "STEERSIX", "QUEUEDFIVE"):
    if turns_with(word): fail.append(f"{word} reached the model")
if any("STEERTWO" in t and "STEERTWO also look at the tests EDITED" not in t for t in seen):
    fail.append("STEERTWO reached the model unedited")
if not turns_with("QUEUEDFOUR list the open issues NOW"): fail.append("QUEUEDFOUR (edited, steered) never delivered")
if not turns_with("QUEUEDEIGHT"): fail.append("QUEUEDEIGHT (dragged to a steer) never delivered")
first_three = turns_with("QUEUEDTHREE")
if not first_three: fail.append("QUEUEDTHREE never ran")
for i, t in enumerate(seen):
    print(i, [m.strip()[:60] for m in re.findall(r"(?:STEER|QUEUED)\w+[^|\n]*", t)])
print("FAIL: " + "; ".join(fail) if fail else f"PASS: {len(rows)} requests")
sys.exit(1 if fail else 0)
PY
