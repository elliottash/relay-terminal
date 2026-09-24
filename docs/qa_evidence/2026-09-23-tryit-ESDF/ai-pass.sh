#!/usr/bin/env bash
# The AI's pass over the staged #ESDF situation: every mechanical step played in the real app,
# one screenshot per step, named controls only (scripts/relay-drive), no coordinates. What is
# left is the person's judgement, and that is all the card's Try it asks.
#
#   RELAY_BIN=/tmp/esdf-tryit/build/relay ./ai-pass.sh
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
driver=$(cd "$here/../../.." && pwd)/scripts/relay-drive
out="$here/captures"; mkdir -p "$out"
work=${RELAY_QA_DIR:-$HOME/relay-qa/esdf-notes}
"$here/stage.sh"
# stage.sh's exports live in its own process; the driver and screenshots need the same sandbox
# env here, so stage.sh records the display it chose and this reapplies the rest.
sandbox=/tmp/claude-1000/tryit/esdf
export HOME=$sandbox/home XDG_RUNTIME_DIR=$sandbox/run TMPDIR=$sandbox/tmp \
       XDG_CONFIG_HOME=$sandbox/home/.config XDG_DATA_HOME=$sandbox/home/.local/share \
       XDG_CACHE_HOME=$sandbox/home/.cache RELAY_KEYRING=off
unset RELAY_OPEN_SOCKET
display=":$(cat "$sandbox/run/display")"
export DISPLAY=$display
shot() { import -window root "$out/$1.png"; }
drive() { "$driver" "$@"; }
wait_read() {
    local i
    for i in $(seq 1 60); do drive read "$1" > "$out/read-$1.json" 2>&1 && return 0; sleep 1; done
    echo "named control did not become readable: $1" >&2; return 1
}

wait_read boardFilter
# 1. The default: one flat list, newest first, a stage on every row.
drive read boardFilter > "$out/01-flat.json"
shot 01-board-opens-flat-recent
# 2. The Stage cell: click to sections. The notice is the mode switch's own words.
drive press boardHeaderStage
sleep 1
drive read notice > "$out/02-sections-notice.json"
shot 02-stage-click-sections
# 3. And back: one list again.
drive press boardHeaderStage
sleep 1
drive read notice > "$out/03-flat-notice.json"
shot 03-stage-click-flat-again
# 4. The sort cells still work on the flat list: click Updated until it is oldest-first.
drive press boardHeaderUpdated
sleep 1
drive press boardHeaderUpdated
sleep 1
shot 04-flat-sorted-oldest-updated
# 5. The choice survives a restart of the app on the same sandbox.
kill -TERM "$(cat /tmp/claude-1000/tryit/esdf/run/esdf-relay.pid)"
sleep 3
(cd "$work" && exec "${RELAY_BIN:?}" --workspace "$work" --clean-shell) > /tmp/claude-1000/tryit/esdf/relay2.log 2>&1 &
echo $! > /tmp/claude-1000/tryit/esdf/run/esdf-relay.pid
sleep 12
drive action board.open
sleep 3
wait_read boardFilter
shot 05-after-restart
echo "ai pass done; captures in $out"
