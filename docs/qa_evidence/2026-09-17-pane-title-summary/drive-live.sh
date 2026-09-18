#!/usr/bin/env bash
# The same screens as drive.sh, but against a real provider, so the pane titles and the tab label
# in the screenshot are written by an actual model on the chores role (issue JRWQ, protocol 17).
#
#   docs/qa_evidence/2026-09-17-pane-title-summary/drive-live.sh [build-dir] [preset]
#
# The key is never handled here: `configurePreset` sends `use_stored_key`, and the worker reads it
# from the desktop keyring itself, so it never crosses the GUI, this script or any file it writes.
# Everything else is still isolated (XDG_CONFIG_HOME/XDG_DATA_HOME in temp directories, a throwaway
# workspace). Three short turns, which cost three agent calls plus two title calls and the tab
# judgement. Requires a stored key for <preset> (default glm-coding) and network access.
set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"
out=$PWD
root=$(cd ../../.. && pwd)
build=${1:-$root/build}
preset=${2:-glm-coding}
display=:93

export XDG_CONFIG_HOME=$(mktemp -d) XDG_DATA_HOME=$(mktemp -d) XDG_CACHE_HOME=$(mktemp -d)
work=$(mktemp -d)
mkdir -p "$XDG_CONFIG_HOME/RelayTerminal"
cat >"$XDG_CONFIG_HOME/RelayTerminal/relay.conf" <<CONF
[instructions]
onboarded=true
[terminal]
shell_integration=true
CONF
trap 'kill "${relay_pid:-0}" "${xvfb_pid:-0}" 2>/dev/null; rm -rf "$XDG_CONFIG_HOME" "$XDG_DATA_HOME" "$XDG_CACHE_HOME" "$work"' EXIT

Xvfb "$display" -screen 0 1500x950x24 >/dev/null 2>&1 &
xvfb_pid=$!
sleep 1
export DISPLAY=$display

shot() {
    import -window root "$out/implementer-live-$1.png"
    kill -0 "${relay_pid:-0}" 2>/dev/null || echo "relay is gone before $1"
}
t() { xdotool type --delay 14 "$1"; }
k() { xdotool key --delay 45 "$@"; }

"$build/relay" --workspace "$work" >"$out/relay-live-stderr.log" 2>&1 &
relay_pid=$!
sleep 6
win=$(xdotool search --name Relay | tail -1)
[[ -z $win ]] && { echo "no Relay window"; exit 1; }
xdotool windowmove "$win" 0 0 windowsize "$win" 1500 950
xdotool windowfocus "$win"
xdotool mousemove 700 700
sleep 2

# /model <preset> configures the pane from the keyring entry; the key stays in the worker.
t "/model $preset"; sleep 0.5
k Return; sleep 6
shot 01-real-model-configured

t 'in two sentences, explain how a terminal pane drag should choose which edge it drops on'; sleep 0.5
k ctrl+Return; sleep 60
shot 02-first-turn-real-model-title

# A second pane on unrelated work: the tab label has to join them.
k ctrl+e; sleep 4
t "/model $preset"; sleep 0.5
k Return; sleep 6
t 'in two sentences, what should the release notes of a 0.1 terminal preview cover?'; sleep 0.5
k ctrl+Return; sleep 60
shot 03-two-real-titles-and-tab-label

echo "screenshots in $out"
