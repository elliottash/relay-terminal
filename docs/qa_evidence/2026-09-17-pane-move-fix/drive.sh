#!/usr/bin/env bash
# Live check of pane movement under Xvfb + xdotool, with the prompt box holding the keyboard
# (the new normal since "the prompt box is the only input").
#
#   docs/qa_evidence/2026-09-17-pane-move-fix/drive.sh [konsole|relay] [build-dir]
#   RELAY_QA_DISPLAY=:93 docs/qa_evidence/2026-09-17-pane-move-fix/drive.sh relay
#
# Covers the two reported failures and the neighbouring keys and buttons:
#   Ctrl+Alt+Left / Ctrl+Alt+Right move the focused pane (the regression: Right did nothing),
#   Ctrl+Alt+Right while a program runs, Alt+arrow pane focus, dragging the ⠿ grip onto another
#   pane's edge, dropping the grip on the tab bar, and the ⇱ "Move to new tab" button.
#
# Writes <engine>-NN-*.png next to this script plus <engine>-relay-stderr.log. Needs Xvfb,
# xdotool and ImageMagick `import`. An isolated XDG_CONFIG_HOME/XDG_DATA_HOME keeps the run out
# of the real profile, so no provider keys exist in it.
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
trap 'kill "${relay_pid:-0}" "${xvfb_pid:-0}" 2>/dev/null; rm -rf "$XDG_CONFIG_HOME" "$XDG_DATA_HOME" "$XDG_CACHE_HOME" "$work"' EXIT

Xvfb "$display" -screen 0 ${width}x${height}x24 >/dev/null 2>&1 &
xvfb_pid=$!
sleep 1
# Another agent may already own that display number; a leaked run would type into their session.
if ! DISPLAY=$display xdotool getdisplaygeometry >/dev/null 2>&1 || ! kill -0 "$xvfb_pid" 2>/dev/null; then
    echo "display $display is not ours (set RELAY_QA_DISPLAY to a free one)"; exit 1
fi
export DISPLAY=$display

shot() { import -window root "$out/$engine-$1.png"; }
t() { xdotool type --delay 12 "$1"; }
k() { xdotool key --delay 40 "$@"; }
# The pane chrome sits in the pane's top-right corner and the ⠿ grip is its leftmost item, so
# the grip is a fixed offset in from the pane's right edge. Hover the pane first: the chrome is
# only shown while the mouse is over it.
grip() {   # grip <pane-right-edge-x>
    xdotool mousemove $(( $1 - 300 )) 400; sleep 0.6
    xdotool mousemove $(( $1 - 178 )) 55; sleep 0.8
}

"$build/relay" --engine="$engine" --workspace "$work" >"$out/$engine-relay-stderr.log" 2>&1 &
relay_pid=$!
sleep 7
win=$(xdotool search --pid "$relay_pid" --name Relay | tail -1)
[[ -z $win ]] && { echo "no Relay window"; exit 1; }
xdotool windowmove "$win" 0 0 windowsize "$win" $width $height
xdotool windowfocus "$win"
xdotool mousemove 700 450
sleep 2
shot 01-one-pane

# ----- two panes, each with a line of its own -------------------------------------------------
# Ctrl+P splits right, so both panes share one horizontal splitter.
k ctrl+p; sleep 7
t 'echo RIGHTPANE'; k Return; sleep 2.5
k alt+Left; sleep 1.5
t 'echo LEFTPANE'; k Return; sleep 2.5
k alt+Right; sleep 1.5
shot 02-two-panes-right-focused

# ----- Ctrl+Alt+arrow with the prompt box focused ----------------------------------------------
k ctrl+alt+Left; sleep 2.5
shot 03-ctrl-alt-left-moved-right-pane-left
# The regression: this used to do nothing, so a pane could never come back.
k ctrl+alt+Right; sleep 2.5
shot 04-ctrl-alt-right-moved-it-back

# ----- and while a program owns the terminal ---------------------------------------------------
t 'sleep 60'; k Return; sleep 2.5
k ctrl+alt+Left; sleep 2.5
shot 05-ctrl-alt-left-while-sleep-runs
k ctrl+alt+Right; sleep 2.5
shot 06-ctrl-alt-right-while-sleep-runs
k Escape; sleep 1.5   # Esc interrupts the program

# ----- Alt+arrow still only moves the focus ----------------------------------------------------
k alt+Left; sleep 1.5
shot 07-alt-left-focuses-the-left-pane
k alt+Right; sleep 1.5

# ----- dragging the ⠿ grip ---------------------------------------------------------------------
# Drag the right pane's grip onto the left pane's left half.
grip $(( width - 6 ))
xdotool mousedown 1; sleep 0.5
for x in $(( width - 300 )) $(( width / 2 )) 400 150; do xdotool mousemove $x 450; sleep 0.35; done
sleep 1
shot 08-drag-shows-the-drop-zone
xdotool mouseup 1; sleep 3
shot 09-dropped-on-the-left-edge

# ----- dropping the grip on the tab bar makes the pane a tab -----------------------------------
grip $(( width - 6 ))
xdotool mousedown 1; sleep 0.5
for x in $(( width - 300 )) 700 400; do xdotool mousemove $x 300; sleep 0.3; done
xdotool mousemove 400 17; sleep 1
shot 10-drag-over-the-tab-bar
xdotool mouseup 1; sleep 3
shot 11-dropped-as-a-second-tab

# ----- the ⇱ "Move to new tab" button ----------------------------------------------------------
# Back to the tab that still has two panes, and move one of them out with the button.
xdotool mousemove 60 17 click 1; sleep 2
xdotool key --delay 40 ctrl+p; sleep 7
xdotool mousemove $(( width - 300 )) 400; sleep 0.6
xdotool mousemove $(( width - 178 )) 55; sleep 0.8
shot 12-pane-chrome-on-hover
xdotool mousemove $(( width - 57 )) 55; sleep 0.6
xdotool click 1; sleep 3
shot 13-move-to-new-tab-button
echo "screenshots in $out"
