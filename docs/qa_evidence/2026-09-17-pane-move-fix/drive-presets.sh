#!/usr/bin/env bash
# The neighbouring keys and buttons, checked alongside the pane-move fix:
#   - the Konsole preset's Ctrl+Shift+arrow pane focus (Alt+arrow is covered by drive.sh),
#   - the ⧉ "Move tab to new window" button on a hovered tab.
#
#   docs/qa_evidence/2026-09-17-pane-move-fix/drive-presets.sh [konsole|relay] [build-dir]
#
# Writes preset-<engine>-NN-*.png next to this script.
set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"
out=$PWD
root=$(cd ../../.. && pwd)
engine=${1:-konsole}
build=${2:-$root/build}
display=${RELAY_QA_DISPLAY:-:77}
width=1400 height=900

export XDG_CONFIG_HOME=$(mktemp -d) XDG_DATA_HOME=$(mktemp -d) XDG_CACHE_HOME=$(mktemp -d)
work=$(mktemp -d)
mkdir -p "$XDG_CONFIG_HOME/RelayTerminal"
cat >"$XDG_CONFIG_HOME/RelayTerminal/relay.conf" <<'CONF'
[instructions]
onboarded=true
[terminal]
shell_integration=true
CONF
# The Konsole preset: Ctrl+Shift+arrow focuses panes, Ctrl+Shift+T opens a tab. Keymap keeps
# keybindings.json in QStandardPaths::AppConfigLocation, which is <org>/<app>, not beside relay.conf.
mkdir -p "$XDG_CONFIG_HOME/RelayTerminal/relay"
cat >"$XDG_CONFIG_HOME/RelayTerminal/relay/keybindings.json" <<'KEYS'
{"version": 1, "preset": "konsole", "bindings": {}}
KEYS
trap 'kill "${relay_pid:-0}" "${xvfb_pid:-0}" 2>/dev/null; rm -rf "$XDG_CONFIG_HOME" "$XDG_DATA_HOME" "$XDG_CACHE_HOME" "$work"' EXIT

Xvfb "$display" -screen 0 ${width}x${height}x24 >/dev/null 2>&1 &
xvfb_pid=$!
sleep 1
# Another agent may already own that display number; a leaked run would type into their session.
if ! DISPLAY=$display xdotool getdisplaygeometry >/dev/null 2>&1 || ! kill -0 "$xvfb_pid" 2>/dev/null; then
    echo "display $display is not ours (set RELAY_QA_DISPLAY to a free one)"; exit 1
fi
export DISPLAY=$display
shot() { import -window root "$out/preset-$engine-$1.png"; }
t() { xdotool type --delay 12 "$1"; }
k() { xdotool key --delay 40 "$@"; }

"$build/relay" --engine="$engine" --workspace "$work" >"$out/preset-$engine-relay-stderr.log" 2>&1 &
relay_pid=$!
sleep 7
win=$(xdotool search --pid "$relay_pid" --name Relay | tail -1)
[[ -z $win ]] && { echo "no Relay window"; exit 1; }
xdotool windowmove "$win" 0 0 windowsize "$win" $width $height
xdotool windowfocus "$win"
xdotool mousemove 700 450
sleep 2

# Konsole preset: Ctrl+Shift+( splits right (Shift+9 on this layout).
k ctrl+shift+9; sleep 8
t 'echo RIGHTPANE'; k Return; sleep 2.5
shot 01-two-panes
# Ctrl+Shift+Left / Right move the focus between them.
k ctrl+shift+Left; sleep 1.5
t 'echo now in the LEFT pane'; sleep 1
shot 02-ctrl-shift-left-focused-the-left-pane
k Escape; sleep 0.5
k ctrl+shift+Right; sleep 1.5
t 'echo back in the RIGHT pane'; sleep 1
shot 03-ctrl-shift-right-focused-the-right-pane
k Escape; sleep 0.5

# A second tab, then the ⧉ button that moves a tab to its own window.
k ctrl+shift+t; sleep 7
shot 04-second-tab
xdotool mousemove 60 17; sleep 1.5
shot 05-tab-hover-shows-the-detach-button
# The ⧉ appears at the hovered tab's left edge; the space for it is always reserved.
xdotool mousemove 12 17; sleep 0.8
xdotool click 1; sleep 5
shot 06-tab-moved-to-a-new-window
echo "screenshots in $out"
