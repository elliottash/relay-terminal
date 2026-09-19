#!/usr/bin/env bash
# Live check of the "move left/right, then ↓ docks it beneath" chord (#Q7Y9) under Xvfb + xdotool,
# with the prompt box holding the keyboard (the normal state). Covers:
#   - Ctrl+Alt+Left then Ctrl+Alt+Down docks the focused pane beneath the pane on its left,
#   - Ctrl+Alt+Right then Ctrl+Alt+Down beneath the pane on its right,
#   - the two-second toast that teaches the chord while its window is open,
#   - the window expiring: Ctrl+Alt+Down is a plain Move-down again,
#   - keys typed while the window is open still reaching the shell (and closing the window),
#   - dragging a pane onto another's bottom edge, which docks it beneath and hints the chord.
#
# The focused pane reports its own geometry with `stty size` (rows, columns) into a file in its
# cwd before and after each chord: docking beneath halves the rows and (about) doubles the
# columns, which is the textual proof of the splitter's orientation; the screenshots show which
# pane ended up on top.
#
#   docs/qa_evidence/2026-09-19-pane-beneath-chord/drive.sh [build-dir]
#   RELAY_QA_DISPLAY=:94 docs/qa_evidence/2026-09-19-pane-beneath-chord/drive.sh
#
# Writes implementer-*.png, implementer-*.txt and implementer-relay-stderr.log next to this
# script. Needs Xvfb, xdotool and ImageMagick `import`. An isolated XDG_CONFIG_HOME keeps the
# run out of the real profile, so no provider keys exist in it.
set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"
out=$PWD
root=$(cd ../../.. && pwd)
build=${1:-$root/build}
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

shot() { import -window root "$out/implementer-$1.png"; }
t() { xdotool type --delay 12 "$1"; }
k() { xdotool key --delay 40 "$@"; }
size() { t "stty size > '$work/$1'"; k Return; sleep 1; }   # rows columns, from the focused pane
grip() {   # grip <pane-right-edge-x>: hover the pane first, then the chrome's ⠿ handle
    xdotool mousemove $(( $1 - 300 )) 400; sleep 0.6
    xdotool mousemove $(( $1 - 178 )) 55; sleep 0.8
}
twopanes() {   # LEFTPANE on the left, RIGHTPANE on the right, RIGHTPANE focused
    k ctrl+e; sleep 7
    t 'clear; echo RIGHTPANE'; k Return; sleep 2
    k alt+Left; sleep 1.5
    t 'clear; echo LEFTPANE'; k Return; sleep 2
    k alt+Right; sleep 1.5
}

"$build/relay" --workspace "$work" >"$out/implementer-relay-stderr.log" 2>&1 &
relay_pid=$!
sleep 7
win=$(xdotool search --pid "$relay_pid" --name Relay | tail -1)
[[ -z $win ]] && { echo "no Relay window"; exit 1; }
xdotool windowmove "$win" 0 0 windowsize "$win" $width $height
xdotool windowfocus "$win"
xdotool mousemove 700 450
sleep 2
shot 01-one-pane

# ----- the chord from the left: RIGHTPANE docks beneath LEFTPANE ---------------------------------
twopanes
size chord-left-before
shot 02-two-panes-right-focused
k ctrl+alt+Left; sleep 0.3
shot 03-toast-teaches-the-chord          # the two-second toast, while the window is open
k ctrl+alt+Down; sleep 2
size chord-left-after
shot 04-rightpane-docked-beneath-leftpane

# ----- the chord from the right: LEFTPANE docks beneath RIGHTPANE --------------------------------
k ctrl+w; sleep 2                         # back to one pane
twopanes
k alt+Left; sleep 1.5
size chord-right-before
k ctrl+alt+Right; sleep 0.2
k ctrl+alt+Down; sleep 2
size chord-right-after
shot 05-leftpane-docked-beneath-rightpane

# ----- two seconds later it is a plain Move-down again --------------------------------------------
k ctrl+w; sleep 2
twopanes
k ctrl+alt+Left; sleep 3                  # let the chord's window expire
k ctrl+alt+Down; sleep 1                  # a plain Move-down: no pane below, so the notice says so
shot 06-expired-window-plain-move-down
sleep 1.5

# ----- keys typed while the window is open still reach the shell ----------------------------------
k ctrl+alt+Right; sleep 1.5               # back to [LEFTPANE | RIGHTPANE]
k ctrl+alt+Left; sleep 0.2                # the chord's window opens again
t 'echo TYPEDWHILEOPEN'; k Return; sleep 1.5
shot 07-typed-keys-still-reach-the-shell
k ctrl+alt+Down; sleep 2                  # the typed keys closed the window: plain move, no dock
shot 08-after-typing-plain-move-down

# ----- dragging onto a bottom edge docks beneath and hints the chord ------------------------------
k ctrl+w; sleep 2
twopanes
grip $(( width - 6 ))
xdotool mousedown 1; sleep 0.5
for y in 200 450 700; do xdotool mousemove 350 $y; sleep 0.3; done
xdotool mousemove 350 780; sleep 1        # the left pane's bottom edge
shot 09-drag-shows-the-drop-zone
xdotool mouseup 1; sleep 1.5
shot 10-dropped-beneath-plus-chord-hint

for f in chord-left-before chord-left-after chord-right-before chord-right-after; do
    cp "$work/$f" "$out/implementer-$f.txt" 2>/dev/null || echo "$f: missing"
done
echo "geometry reports (rows columns):"
cat "$out"/implementer-chord-*.txt
echo "screenshots in $out"
