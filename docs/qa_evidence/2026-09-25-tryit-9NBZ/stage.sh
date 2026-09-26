#!/usr/bin/env bash
# Try it staging for #9NBZ — find in cards. Seeds a disposable project with a small board
# (one card repeats "parallax" in three sections, so there is something to find and step
# through), then opens this checkout's Relay on it under an isolated profile. Safe to run
# twice: the fixture is rebuilt from scratch each time, and the owner's real profile, keyring
# and boards are never touched.
set -euo pipefail

repo=/home/elliott/repos/relay-terminal
bin=${RELAY_BIN:-$repo/build/relay}
root=/home/elliott/.cache/relay/scratch/tryit/9nbz
proj=$root/proj
home=$root/home

rm -rf "$proj" "$home"
mkdir -p "$proj/.board/changes" "$home/.config/RelayTerminal" "$root/run" "$root/tmp"
chmod 700 "$root/run"

cat > "$proj/.board/board.yaml" <<'EOF'
version: 1
tabs: [{id: features, folder: features}, {id: bugs, folder: changes}, {id: done, filter: 'status:done,dropped'}]
columns: [inbox, discussing, ready, in-progress, done]
agent: {autonomy: auto}
EOF

card() {  # id file status title body
    cat > "$proj/.board/changes/$2.md" <<EOF
---
id: $1
type: work
status: $3
labels: []
rank: aaa
created: '2026-09-25'
source: Try it staging, 2026-09-25
---

# $4

$5
EOF
}

card P4RA P4RA inbox 'Parallax drift on the horizon strip' \
'# Parallax drift on the horizon strip

## Issue
The horizon strip draws its background with a fixed parallax offset, and on a wide window
the parallax term goes negative and the strip drifts a full tile to the left.

## Done means
The parallax offset is clamped to the tile size, and a window dragged from 800 to 2560
pixels wide shows no drift in the strip at any point of the drag.

## Tests
- Grab the strip at three widths and compare: the parallax seam never crosses the left edge.'

card Q1CK Q1CK discussing 'Scrolling the long card list stutters' \
'# Scrolling the long card list stutters

## Issue
With 400 cards the list repaints every row on each wheel tick. The parallax strip above it
compounds the stall, since it repaints with the same pass.

## Done means
Wheel scrolling through 400 cards holds a steady frame budget.'

card M7CH M7CH inbox 'Toolbar wraps at 1080p' \
'# Toolbar wraps at 1080p

## Issue
The card toolbar adds a second row below 1200 pixels, pushing the document out of view.

## Done means
The toolbar keeps one row at 1080p; the overflow items fold into the kebab menu.'

card D0NE D0NE done 'Retire the old welcome card' \
'# Retire the old welcome card

## Resolution
Removed in 2.4; the onboarding tour covers everything it said.'

git -C "$proj" init -q
git -C "$proj" add -A
GIT_AUTHOR_NAME=try GIT_AUTHOR_EMAIL=try@it GIT_COMMITTER_NAME=try GIT_COMMITTER_EMAIL=try@it \
    git -C "$proj" commit -qm 'staged board for #9NBZ Try it'

printf '[instructions]\nonboarded=true\n[security]\napprovals_chosen=true\n' \
    > "$home/.config/RelayTerminal/relay.conf"

export HOME=$home XDG_RUNTIME_DIR=$root/run TMPDIR=$root/tmp
export XDG_CONFIG_HOME=$home/.config XDG_DATA_HOME=$home/.local/share XDG_CACHE_HOME=$home/.cache
export RELAY_KEYRING=off
unset RELAY_OPEN_SOCKET   # discover only this disposable instance, never the caller's app
exec "$bin" --workspace "$proj" --clean-shell --fresh
