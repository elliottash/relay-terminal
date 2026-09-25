#!/usr/bin/env bash
# Stage #0FBB's Try it: Relay on a throwaway project whose board holds two cards claimed by the
# one terminal pane, so the header reads "#K7Q2 (2)" before the title and a click on it lists
# both. Everything lives under /tmp/claude-1000/tryit/0fbb and nothing touches your real HOME.
#
# Run with a DISPLAY already set to open Relay on your screen; without one it starts an Xvfb
# (the machine pass) and leaves the screenshots beside this script:
#
#   docs/qa_evidence/2026-09-25-0FBB/stage.sh            stage, claim, screenshot, leave running
#   docs/qa_evidence/2026-09-25-0FBB/stage.sh --stop     stop what a previous run left behind
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
repo=$(cd "$here/../../.." && pwd)
bin=${RELAY_BIN:-$repo/build/relay}
work=/tmp/claude-1000/tryit/0fbb

if [[ ${1:-} == --stop ]]; then
    for f in relay.pid xvfb.pid; do
        [[ -f $work/$f ]] && kill "$(cat "$work/$f")" 2>/dev/null || true
    done
    exit 0
fi
[[ -x $bin ]] || { echo "no relay binary at $bin (build it or set RELAY_BIN)" >&2; exit 1; }

rm -rf "$work"; mkdir -p "$work/project" "$work/shots"
sandbox=$work/sandbox
mkdir -p "$sandbox/home/.config/RelayTerminal" "$sandbox/run" "$sandbox/tmp"; chmod 700 "$sandbox/run"
ln -sfn "/run/user/$(id -u)/bus" "$sandbox/run/bus"
printf '[instructions]\nonboarded=true\n[security]\napprovals_chosen=true\n' > "$sandbox/home/.config/RelayTerminal/relay.conf"

# A board with two open cards. Their `session` is written once the pane's token is known.
board=$work/project/.board
(cd "$work/project" && git init -q && git -c user.email=qa@relay -c user.name=qa commit -q --allow-empty -m init)
PYTHONPATH=$repo/backend python3 - "$board" <<'PY'
import sys
from relay_core.board import Board, scaffold
scaffold(Board(sys.argv[1]))
PY
mkdir -p "$board/features"
card() {   # id, slug, title, status
    cat > "$board/features/2026-09-25-$2.md" <<CARD
---
id: $1
type: work
status: $4
labels: [feature]
assignee: agent
rank: zzzzzzzzzzzzzzzzzzzr
created: '2026-09-25'
links: {plans: [], commits: [], evidence: [], related: [], github: null}
---
# $3

## Issue
$3
CARD
}
card K7Q2 voice-transcription "Voice transcription in the composer" executing
card M3XJ pane-title-floor "A pane title never widens its pane" needs-verification
card Q5QJ nobody-s-card "A card nobody has claimed" inbox

export RELAY_KEYRING=off
unset RELAY_OPEN_SOCKET   # discover only this disposable instance, never the caller's app
export HOME=$sandbox/home XDG_RUNTIME_DIR=$sandbox/run TMPDIR=$sandbox/tmp \
       XDG_CONFIG_HOME=$sandbox/home/.config XDG_DATA_HOME=$sandbox/home/.local/share \
       XDG_CACHE_HOME=$sandbox/home/.cache

if [[ -z ${DISPLAY:-} ]]; then
    display=; for n in $(seq 200 259); do [[ -e /tmp/.X11-unix/X$n ]] || { display=:$n; break; }; done
    [[ -n $display ]] || { echo "no free Xvfb display" >&2; exit 1; }
    Xvfb "$display" -screen 0 1400x900x24 >/dev/null 2>&1 & echo $! > "$work/xvfb.pid"
    sleep 2; export DISPLAY=$display
fi

(cd "$work/project" && exec "$bin" --workspace "$work/project" --fresh) > "$work/relay.log" 2>&1 &
echo $! > "$work/relay.pid"
sleep 10

# The pane's token, from the drive socket; then the two cards are this pane's claims.
token=$(python3 "$repo/scripts/relay-drive" panes | python3 -c 'import json,sys; print(json.load(sys.stdin)["panes"][0]["id"])')
echo "pane token $token"
# K7Q2 last, a second later: the claims come newest-updated first, so it is the one the chip names.
for f in pane-title-floor voice-transcription; do
    sed -i "s/^assignee: agent$/assignee: agent\nsession: $token/" "$board/features/2026-09-25-$f.md"
    sleep 1.2
done
echo "Relay pid $(cat "$work/relay.pid") on DISPLAY=$DISPLAY, HOME=$sandbox/home, project $work/project"
echo "Type ' #' in the pane's prompt box to load the card index (Esc closes the picker):"
echo "the header then reads '#K7Q2 (2)' before the title; click it for the list."
