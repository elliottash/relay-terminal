#!/usr/bin/env bash
# Live check of "a move key pressed at the page's edge carries the pane past it, into a column
# or row of its own" under Xvfb + xdotool, with the prompt box holding the keyboard. Covers:
#   - Ctrl+Alt+Right on the bottom pane of a stack in the rightmost column (the ask itself):
#     the pane becomes the whole of a new rightmost column, the stack keeps the rest;
#   - Ctrl+Alt+Right again on that lone rightmost pane: the notice, not a no-op re-layout;
#   - Ctrl+Alt+Left on the bottom pane of a stack in the leftmost column: new leftmost column;
#   - Ctrl+Alt+Up from the left pane of a two-pane top row: new full-width top row;
#   - Ctrl+Alt+Down from the right pane of a two-pane bottom row: new full-width bottom row;
#   - Ctrl+Alt+Right on a page that is one stack (rows): the root is wrapped, the pane becomes
#     a full-height right column — the two-pane page where takeLeaf would unwrap the root;
#   - one pane left: the only-pane notice;
#   - quit and relaunch: the moved layout is the layout that comes back.
#
# The focused pane reports its own geometry with `stty size` (rows, columns) before and after
# each move: a pane given its own column goes to the page's full height (rows about double),
# one given its own row to the page's full width (columns about double). Screenshots record the
# layouts and the two notices.
#
#   docs/qa_evidence/2026-09-19-pane-move-past-page-edge/drive.sh [build-dir]
#   RELAY_QA_DISPLAY=:94 docs/qa_evidence/2026-09-19-pane-move-past-page-edge/drive.sh
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
if ! DISPLAY=$display xdotool getdisplaygeometry >/dev/null 2>&1 || ! kill -0 "$xvfb_pid" 2>/dev/null; then
    echo "display $display is not ours (set RELAY_QA_DISPLAY to a free one)"; exit 1
fi
export DISPLAY=$display

shot() { import -window root "$out/implementer-$1.png"; }
t() { xdotool type --delay 12 "$1"; }
k() { xdotool key --delay 40 "$@"; }
size() { t "stty size > '$work/$1'"; k Return; sleep 1; }   # rows columns, from the focused pane
label() { t "clear; echo $1"; k Return; sleep 1; }

"$build/relay" --workspace "$work" >"$out/implementer-relay-stderr.log" 2>&1 &
relay_pid=$!
sleep 7
win=$(xdotool search --pid "$relay_pid" --name Relay | tail -1)
[[ -z $win ]] && { echo "no Relay window"; exit 1; }
xdotool windowmove "$win" 0 0 windowsize "$win" $width $height
xdotool windowfocus "$win"
xdotool mousemove 700 450
sleep 2

# ----- Ctrl+Alt+Right out of a stack in the rightmost column (the ask) ---------------------------
label ONE
k ctrl+e; sleep 0.4; k Right; sleep 6          # [ONE | TWO], TWO focused (Right keeps it there)
label TWO
k ctrl+e; sleep 0.4; k Down; sleep 6           # THREE docked BELOW TWO: [ONE | TWO over THREE]
label THREE
size right-before
shot 02-stack-in-rightmost-column
k ctrl+alt+Right; sleep 1.5                    # THREE past the edge: [ONE | TWO | THREE]
size right-after
shot 03-three-in-own-column

# ----- the same key on the pane that now has the edge to itself -----------------------------------
k ctrl+alt+Right; sleep 0.6
shot 04-edge-to-itself-notice
sleep 2

# ----- Ctrl+Alt+Left out of a stack in the leftmost column ----------------------------------------
k ctrl+w; sleep 1.5; k ctrl+w; sleep 1.5       # back to [ONE] (closes THREE, then TWO)
k ctrl+e; sleep 0.4; k Left; sleep 6           # TWO docked LEFT of ONE: [TWO | ONE], TWO focused
label TWO
k ctrl+e; sleep 0.4; k Down; sleep 6           # THREE docked BELOW TWO: [TWO over THREE | ONE]
label THREE
size left-before
shot 05-stack-in-leftmost-column
k ctrl+alt+Left; sleep 1.5                     # THREE past the edge: [THREE | TWO | ONE]
size left-after
shot 06-three-in-own-left-column
k ctrl+w; sleep 1.5; k ctrl+w; sleep 2         # close two of the three panes

# ----- Ctrl+Alt+Up from the left pane of a two-pane top row ---------------------------------------
k ctrl+e; sleep 0.4; k Down; sleep 6           # [ONE over TWO], TWO focused
label TWO
k alt+Up; sleep 1.5
label ONE
k ctrl+e; sleep 0.4; k Right; sleep 6          # THREE right of ONE: [ONE|THREE over TWO]
label THREE
k alt+Left; sleep 1.5                          # focus ONE, the top row's left pane
size up-before
shot 07-top-row-of-two
k ctrl+alt+Up; sleep 1.5                       # ONE past the edge: full-width top row
size up-after
shot 08-one-in-own-top-row
k ctrl+w; sleep 1.5; k ctrl+w; sleep 2         # close two of the three rows

# ----- Ctrl+Alt+Down from the right pane of a two-pane bottom row ---------------------------------
k ctrl+e; sleep 0.4; k Down; sleep 6           # [ONE over TWO], TWO focused
label TWO

# ----- Ctrl+Alt+Down from the right pane of a two-pane bottom row ---------------------------------
k ctrl+e; sleep 0.4; k Right; sleep 6          # THREE right of TWO: [ONE over TWO|THREE]
label THREE
size down-before
shot 09-bottom-row-of-two
k ctrl+alt+Down; sleep 1.5                     # THREE past the edge: full-width bottom row
size down-after
shot 10-three-in-own-bottom-row
k ctrl+w; sleep 1.5; k ctrl+w; sleep 2         # close two of the three rows

# ----- Ctrl+Alt+Right on a page that is one stack: the root is wrapped, two panes total ------------
k ctrl+e; sleep 0.4; k Down; sleep 6           # [ONE over TWO], TWO focused
label TWO

# ----- Ctrl+Alt+Right on a page that is one stack: the root is wrapped, two panes total ------------
size wrap-before
shot 11-one-stack-two-panes
k ctrl+alt+Right; sleep 1.5                    # TWO past the edge: [ONE | TWO], TWO full height
size wrap-after
shot 12-two-full-height-right-column

# ----- the only pane in its tab --------------------------------------------------------------------
k ctrl+w; sleep 2                              # closes TWO: [ONE] alone (ONE is a full column)
size only-before
k ctrl+alt+Right; sleep 0.6
shot 13-only-pane-notice

# ----- quit and relaunch: the layout that comes back is the layout that was left -------------------
kill "$relay_pid"; wait "$relay_pid" 2>/dev/null; sleep 1
"$build/relay" --workspace "$work" >>"$out/implementer-relay-stderr.log" 2>&1 &
relay_pid=$!
sleep 7
win=$(xdotool search --pid "$relay_pid" --name Relay | tail -1)
xdotool windowmove "$win" 0 0 windowsize "$win" $width $height
xdotool windowfocus "$win"; sleep 2
shot 14-restored-after-relaunch

for f in right-before right-after left-before left-after up-before up-after down-before down-after wrap-before wrap-after only-before; do
    cp "$work/$f" "$out/implementer-$f.txt" 2>/dev/null || echo "$f: missing"
done
echo "geometry reports (rows columns), before then after each move:"
for f in right left up down wrap; do
    printf '  %-5s %s -> %s\n' "$f" "$(cat "$out/implementer-$f-before.txt" 2>/dev/null)" "$(cat "$out/implementer-$f-after.txt" 2>/dev/null)"
done
echo "screenshots in $out"
