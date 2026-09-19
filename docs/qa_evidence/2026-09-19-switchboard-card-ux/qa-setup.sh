#!/usr/bin/env bash
# #VZ69 implementer evidence: fixture, private Xvfb, and Relay under it.
# The provider key is read from the environment (RELAY_GLM_CODING_API_KEY), never written here.
set -uo pipefail
root=/home/elliott/repos/relay-terminal
# The binary under test: an export of main plus this work only (see NOTES.md).
build=${RELAY_QA_BUILD:-$root/build}
qa=/tmp/vz69qa
display=${RELAY_QA_DISPLAY:-:87}
width=1500; height=950

rm -rf "$qa"; mkdir -p "$qa"
export XDG_CONFIG_HOME="$qa/config" XDG_DATA_HOME="$qa/data" XDG_CACHE_HOME="$qa/cache"
export XDG_RUNTIME_DIR="$qa/run" TMPDIR="$qa/tmp"
export RELAY_KEYRING=off          # the owner's remote identity key is never touched
mkdir -p "$XDG_CONFIG_HOME/RelayTerminal" "$XDG_DATA_HOME" "$XDG_CACHE_HOME" "$XDG_RUNTIME_DIR" "$TMPDIR"
chmod 700 "$XDG_RUNTIME_DIR"
printf '[instructions]\nonboarded=true\n[terminal]\nshell_integration=true\n[provider]\npreset=glm-coding\n' \
    >"$XDG_CONFIG_HOME/RelayTerminal/relay.conf"

proj="$qa/proj"
mkdir -p "$proj/issues/features" "$proj/src"
printf '%s\n' 'version: 1' 'tabs: [{id: features, folder: features}]' \
    'columns: [inbox, discussing, ready, in-progress, waiting, needs-qa, done]' \
    'agent: {autonomy: auto, max_creates_per_turn: 5}' >"$proj/issues/board.yaml"
card() {
    printf '%s\n' '---' "id: $1" 'type: work' "status: $3" 'labels: []' "created: '2026-09-19'" \
        '---' "# $2" '' '## Issue' "$4" >"$proj/issues/features/$1.md"
}
card AAAA 'Tab completion adds a stray dash after a folder' inbox \
    'Completing a directory gives "src/-" instead of "src/". It should end in a slash.'
card BBBB 'The queue badge keeps a stale count' ready \
    'After the last queued message is sent the badge still says 1 until the pane is redrawn.'
git -C "$proj" init -q
git -C "$proj" config user.name 'VZ69 QA'; git -C "$proj" config user.email 'qa@example.invalid'
git -C "$proj" add .; git -C "$proj" commit -qm fixture

Xvfb "$display" -screen 0 "${width}x${height}x24" >"$qa/xvfb.log" 2>&1 &
echo "$!" >"$qa/xvfb.pid"
sleep 1
export DISPLAY=$display
"$build/relay" --workspace "$proj" --fresh >"$qa/relay-stderr.log" 2>&1 &
echo "$!" >"$qa/relay.pid"
sleep 9
win=$(xdotool search --pid "$(cat "$qa/relay.pid")" --name Relay | tail -1)
test -n "$win" || { echo "no window"; exit 1; }
xdotool windowmove "$win" 0 0 windowsize "$win" "$width" "$height" windowfocus "$win"
sleep 2
echo "ready: display=$display win=$win proj=$proj"
