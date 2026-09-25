#!/usr/bin/env bash
# Stage #K4SQ: a disposable project whose board has three cards (one body references #AA02 with a
# clickable link), Relay open on it at 2200x1200 — wide enough that the Switchboard pane is above
# kCardSplitWidth (900), so an ordinary open would split list + card. The link click itself is made
# through the app's named `open` seam (scripts/relay-drive open AA01), which is the same
# openBoardCard() a `#ID` clicked in chat or a notification calls.
#
# Takes no arguments, needs no model and no network, and can be run again: a previous instance on
# this display is stopped first.
set -euo pipefail
root=$(cd "$(dirname "$0")/../../.." && pwd)       # the relay-terminal checkout
bin=${RELAY_BIN:-$root/build/relay}
display=187
proj=/tmp/claude-1000/tryit/k4sq
sandbox=/tmp/claude-1000/tryit/k4sq-sandbox
pidfile=$sandbox/relay.pid

# Stop a previous run, if any.
if [ -f "$pidfile" ] && kill -0 "$(cat "$pidfile")" 2>/dev/null; then
    kill "$(cat "$pidfile")" 2>/dev/null || true
    sleep 1
fi
rm -rf "$sandbox"
mkdir -p "$sandbox" "$proj"
export XDG_RUNTIME_DIR=$sandbox/xdg
mkdir -p "$XDG_RUNTIME_DIR" "$sandbox/home"
chmod 700 "$XDG_RUNTIME_DIR"   # Qt refuses a runtime directory that is group/world accessible

# The disposable project: a git repo with a small board.
cd "$proj"
if [ ! -d .git ]; then git init -q .; fi
mkdir -p .board/features
cat > .board/board.yaml <<'YAML'
version: 1
tabs: [{id: features, folder: features}]
columns: [inbox, discussing, planning, planned, executing, needs-verification, done]
YAML
cat > .board/features/aa01-pack-the-bags.md <<'MD'
---
id: AA01
type: work
status: inbox
labels: [errand]
rank: aaaa000000000000000001
created: '2026-09-24'
---
# Pack the bags

## Issue
Two suitcases and a duffel.

## Notes
The checklist lives on #AA02 — open it from here when you start.
MD
cat > .board/features/aa02-checklist.md <<'MD'
---
id: AA02
type: work
status: planned
labels: [errand]
rank: aaaa000000000000000002
created: '2026-09-24'
---
# Write the checklist

## Issue
Tickets, chargers, the blue folder.

## Notes
Done means the list is checked off against #AA01.
MD
cat > .board/features/aa03-water-the-plants.md <<'MD'
---
id: AA03
type: work
status: done
labels: [errand]
rank: aaaa000000000000000003
created: '2026-09-24'
---
# Water the plants

## Issue
Balcony and kitchen.

## Execution Summary
Watered on the 24th; nothing left.
MD
git add -A 2>/dev/null && git -c user.email=stage@example.com -c user.name=stage commit -qm "stage" 2>/dev/null || true

# The app, isolated: own display, own home, no keyring, no model calls.
Xvfb ":$display" -screen 0 2200x1200x24 >"$sandbox/xvfb.log" 2>&1 &
echo $! > "$sandbox/xvfb.pid"
sleep 1
cd "$root"
DISPLAY=":$display" HOME=$sandbox/home RELAY_KEYRING=off RELAY_SESSION=stage-k4sq \
    "$bin" --workspace "$proj" --clean-shell --fresh >"$sandbox/relay.log" 2>&1 &
echo $! > "$pidfile"
sleep 12

# The link click — the same openBoardCard() a `#ID` in chat calls — landing on the card alone.
# XDG_RUNTIME_DIR points relay-drive at THIS app's open-socket, not the user session's (#K4SQ).
XDG_RUNTIME_DIR=$sandbox/xdg "$root/scripts/relay-drive" open AA01
sleep 2
echo "Relay is running on display :$display, showing card AA01 as a card link opened it."
