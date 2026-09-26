#!/usr/bin/env bash
# The agent's pass over the staged #E34S scenario: the checks a machine can make before a person
# judges the helper's answer. Opens the Board (the Switchboard helper) and a card conversation
# under the staged profile, one screenshot each. Every drive call aims at the staged window's
# own socket (XDG_RUNTIME_DIR from run.sh) and is bounded by `timeout`.
#
#   RELAY_BIN=<relay> ./me-pass.sh <stage dir> <out dir>
set -uo pipefail
bin=${RELAY_BIN:?set RELAY_BIN to a relay binary with #E34S}
stage=${1:?stage dir from stage.sh}; out=${2:?out dir for captures}
repo=$(cd "$(dirname "$0")/../../../.." && pwd)
mkdir -p "$out"; rm -f "$out"/*.png "$out"/run.txt

info=$(RELAY_BIN="$bin" "$stage/home/run.sh") || exit 1
echo "$info" > "$out/run.txt"; eval "$info"
export DISPLAY
d() { timeout 20 env XDG_RUNTIME_DIR="$XDG_RUNTIME_DIR" DISPLAY="$DISPLAY" \
          "$repo/scripts/relay-drive" "$@"; }
shot() { timeout 20 import -window root "$out/$1.png" 2>&1; }

sleep 3; shot 00-main-window
d action board.open; sleep 8; shot 10-board-helper
d open W1A2 || d open 2026-09-25-water-the-office-plants; sleep 5
shot 20-card-conversation
d panes > "$out/panes.json" 2>&1 || true

kill -TERM "$RELAY_PID" "$XVFB_PID" 2>/dev/null; sleep 2
pkill -f "rl-e34s-home" 2>/dev/null
ls -la "$out"
