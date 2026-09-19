#!/usr/bin/env bash
# Live check of card #JXWT under Xvfb + xdotool: after `pane.splitRight` (Ctrl+E), the placement
# arrow must also work with Ctrl still held — Ctrl+Down, and Ctrl+Shift+Down (the Ctrl+Shift+E
# twin leaves both held) — while a chord the keymap binds keeps doing what it always does.
#
#   docs/qa_evidence/2026-09-19-ctrl-held-placement/drive.sh [build-dir]
#   RELAY_QA_DISPLAY=:94 docs/qa_evidence/2026-09-19-ctrl-held-placement/drive.sh
#
# Phase A (default keymap): bare Down, Ctrl+Down, Ctrl+Shift+Down, Ctrl+Right ("stay where it
# is"), Alt+Down (pane.focusDown — dismissed, not placement), and Ctrl+Down after the window has
# expired (passed on). Phase B (pane.focusDown bound to Ctrl+Shift+Down, the chord the konsole
# preset collides with): the bound chord is dismissed, plain Ctrl+Down still places.
#
# The assertion channel is the pane's own shell: after each placement the focused pane (the new
# one — placement keeps its focus) reports $COLUMNS and $LINES into a file in its working
# directory. Side by side the new pane is narrow; placed below it is wide but half as tall; a
# split that never happened reads full width AND full height, so a dead split cannot pass for a
# placement. Screenshots (implementer-*.png) are the eyeball evidence beside results.txt.
#
# Needs Xvfb, xdotool and ImageMagick. Isolated XDG dirs keep the run out of the real profile,
# so no provider keys exist in it.
set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"
out=$PWD
build=${1:-$(cd ../../.. && pwd)/build}
display=${RELAY_QA_DISPLAY:-:94}
width=1400 height=900
results="$out/results.txt"
: >"$results"

shot() { import -window root "$out/implementer-$1.png"; }
t() { xdotool type --delay 12 "$1"; }
k() { xdotool key --delay 40 "$@"; }

# The focused pane reports its grid into cols-<name>.txt; the layout follows from the numbers.
orientation() { # orientation <cols> <lines>
    if (( $1 < 120 )); then echo vertical
    elif (( $2 < 30 )); then echo horizontal
    else echo one; fi
}

# check <expected> <name>: screenshot, ask the focused pane for its grid, record PASS/FAIL.
check() {
    shot "$2"
    rm -f "$work/cols-$2.txt"
    t 'echo $COLUMNS $LINES > cols-'"$2"'.txt'; k Return; sleep 2
    local got=unreadable
    [[ -s $work/cols-$2.txt ]] && got=$(orientation $(cat "$work/cols-$2.txt"))
    if [[ $got == "$1" ]]; then echo "PASS  $2: $got ($(cat "$work/cols-$2.txt" 2>/dev/null), expected $1)" >>"$results"
    else echo "FAIL  $2: $got ($(cat "$work/cols-$2.txt" 2>/dev/null), expected $1)" >>"$results"; fi
    echo "$2 $got"
}

# Ctrl+E, then the given key inside the two-second placement window, then assert the layout.
split_then() { # split_then <key> <expected> <name>
    k ctrl+e; sleep 0.8
    k "$1"; sleep 2
    check "$2" "$3"
    k ctrl+w; sleep 2.5
}

phase_a() {
    echo "== phase A: default keymap =="
    t 'echo TOP'; k Return; sleep 1.5
    shot 00-one-pane
    # The regression check: a bare arrow must still place.
    split_then Down horizontal 01-bare-down-places-below
    # The fix: Ctrl is still held from Ctrl+E.
    split_then ctrl+Down horizontal 02-ctrl-down-places-below
    # The Ctrl+Shift+E twin leaves Shift down too.
    split_then ctrl+shift+Down horizontal 03-ctrl-shift-down-places-below
    # Right means "yes, where it is"; Ctrl+Right is that too.
    split_then ctrl+Right vertical 04-ctrl-right-keeps-it-right
    # Alt+Down is pane.focusDown: a bound shortcut is dismissed, not swallowed.
    split_then alt+Down vertical 05-alt-down-is-dismissed
    # After the window has expired, Ctrl+Down is nobody's chord: it is passed on untouched.
    k ctrl+e; sleep 0.8
    sleep 2.5   # let the placement window expire without pressing anything
    k ctrl+Down; sleep 2
    check vertical 06-ctrl-down-after-the-window-is-passed-on
    k ctrl+w; sleep 2.5
}

phase_b() {
    echo "== phase B: pane.focusDown bound to Ctrl+Shift+Down (the konsole preset's chord) =="
    t 'echo TOP'; k Return; sleep 1.5
    # The bound chord keeps its own meaning: dismissed, not placement.
    split_then ctrl+shift+Down vertical 07-bound-chord-is-dismissed
    # Plain Ctrl+Down is still free, so it still places.
    split_then ctrl+Down horizontal 08-ctrl-down-places-below
}

relay_up() { # relay_up <keybindings-json or ""> <log-name>
    export XDG_CONFIG_HOME=$(mktemp -d) XDG_DATA_HOME=$(mktemp -d) XDG_CACHE_HOME=$(mktemp -d)
    mkdir -p "$XDG_CONFIG_HOME/RelayTerminal/relay"
    printf '[instructions]\nonboarded=true\n[terminal]\nshell_integration=true\n' \
        >"$XDG_CONFIG_HOME/RelayTerminal/relay.conf"
    [[ -n $1 ]] && printf '%s\n' "$1" >"$XDG_CONFIG_HOME/RelayTerminal/relay/keybindings.json"
    work=$(mktemp -d)
    "$build/relay" --workspace "$work" --fresh >"$out/$2.log" 2>&1 &
    relay_pid=$!
    sleep 7
    win=$(xdotool search --pid "$relay_pid" --name Relay | tail -1)
    [[ -z $win ]] && { echo "no Relay window"; exit 1; }
    xdotool windowmove "$win" 0 0 windowsize "$win" $width $height
    xdotool windowfocus "$win"
    xdotool mousemove 700 450
    sleep 2
}

relay_down() {
    kill "$relay_pid" 2>/dev/null
    rm -rf "$XDG_CONFIG_HOME" "$XDG_DATA_HOME" "$XDG_CACHE_HOME" "$work"
    relay_pid=""
}

Xvfb "$display" -screen 0 ${width}x${height}x24 >/dev/null 2>&1 &
xvfb_pid=$!
sleep 1
# Another agent may already own that display number; a leaked run would type into their session.
if ! DISPLAY=$display xdotool getdisplaygeometry >/dev/null 2>&1 || ! kill -0 "$xvfb_pid" 2>/dev/null; then
    echo "display $display is not ours (set RELAY_QA_DISPLAY to a free one)"; exit 1
fi
export DISPLAY=$display
trap '[[ -n ${relay_pid:-} ]] && kill "$relay_pid" 2>/dev/null; kill "$xvfb_pid" 2>/dev/null' EXIT

relay_up "" phase-a-relay
phase_a
relay_down
relay_up '{"version": 1, "preset": "relay", "bindings": {"pane.focusDown": ["Ctrl+Shift+Down"]}, "program_keys": "shift-only"}' phase-b-relay
phase_b
relay_down

cat "$results"
grep -q '^FAIL' "$results" && exit 1
echo "all placements OK"
