#!/usr/bin/env bash
# #4PW5 — the same "type into the pane you just made" check, from every way a pane gets made and
# from the states the source pane can be in when it happens.
#
#   docs/qa_evidence/2026-09-17-bugfix-batch1/newpane-variants.sh [konsole|relay] [build-dir]
#
# Each case types a marker without touching the mouse and then presses the close key. The tab
# title carries the pane count ("name · N"), and the saved layout is the exact counter, so a case
# that fails shows up as a marker that never appears plus a pane that does not close.
set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"
out=$PWD
root=$(cd ../../.. && pwd)
engine=${1:-relay}
build=${2:-$root/build}
display=${RELAY_QA_DISPLAY:-:91}
width=1400 height=900

export XDG_CONFIG_HOME=$(mktemp -d) XDG_DATA_HOME=$(mktemp -d) XDG_CACHE_HOME=$(mktemp -d)
export XDG_RUNTIME_DIR=$(mktemp -d)
work=$(mktemp -d)
mkdir -p "$XDG_CONFIG_HOME/RelayTerminal"
cat >"$XDG_CONFIG_HOME/RelayTerminal/relay.conf" <<'CONF'
[instructions]
onboarded=true
[terminal]
shell_integration=true
CONF
trap 'kill "${relay_pid:-0}" "${xvfb_pid:-0}" 2>/dev/null; rm -rf "$XDG_CONFIG_HOME" "$XDG_DATA_HOME" "$XDG_CACHE_HOME" "$XDG_RUNTIME_DIR" "$work"' EXIT

Xvfb "$display" -screen 0 ${width}x${height}x24 >/dev/null 2>&1 &
xvfb_pid=$!
sleep 1
if ! DISPLAY=$display xdotool getdisplaygeometry >/dev/null 2>&1 || ! kill -0 "$xvfb_pid" 2>/dev/null; then
    echo "display $display is not ours (set RELAY_QA_DISPLAY to a free one)"; exit 1
fi
export DISPLAY=$display

k() { xdotool key --delay 40 "$@"; }
t() { xdotool type --delay 15 "$1"; }

count() {
    python3 - "$XDG_DATA_HOME/relay/state/windows.json" <<'PY'
import json, sys
def leaves(node):
    return sum(leaves(c) for c in node["children"]) if "split" in node else 1
try:
    state = json.load(open(sys.argv[1]))
except OSError:
    print("?"); raise SystemExit
print(sum(leaves(tab) for w in state.get("windows", []) for tab in w.get("tabs", [])))
PY
}

# case <label> <wait-after-split>: type a marker into whatever has the keyboard, shoot, close.
check() {   # check <label> <before-count>
    sleep 2
    t 'echo MARKER'
    sleep 1.5
    import -window "$win" "$out/variant-$engine-$1.png" 2>/dev/null
    k ctrl+w
    sleep 4
    local after; after=$(count)
    if [[ $after == "$2" ]]; then
        printf '  %-34s typed + closed: PASS (panes %s -> %s)\n' "$1" "$(( $2 + 1 ))" "$after"
    else
        printf '  %-34s FAIL: panes still %s (expected %s)\n' "$1" "$after" "$2"
    fi
}

"$build/relay" --engine="$engine" --workspace "$work" >"$out/variant-$engine-relay-stderr.log" 2>&1 &
relay_pid=$!

# ----- 1. split before the first pane has finished starting -------------------------------------
sleep 3
win=$(xdotool search --pid "$relay_pid" --name Relay | tail -1)
[[ -z $win ]] && { echo "no Relay window"; exit 1; }
xdotool windowmove "$win" 0 0 windowsize "$win" $width $height
xdotool mousemove 700 450   # no window manager: X focus is PointerRoot
echo "=== $engine ==="
k ctrl+p; sleep 6
check 01-split-during-startup 1

# ----- 2. split down ----------------------------------------------------------------------------
sleep 4
k ctrl+shift+p; sleep 7
check 02-split-down 1

# ----- 3. split twice in a row, before the new pane has started ---------------------------------
k ctrl+p; sleep 1; k ctrl+p; sleep 8
sleep 2; t 'echo MARKER'; sleep 1.5
import -window "$win" "$out/variant-$engine-03-two-splits-in-a-row.png" 2>/dev/null
k ctrl+w; sleep 3; k ctrl+w; sleep 4
printf '  %-34s back to %s pane(s)\n' 03-two-splits-in-a-row "$(count)"

# ----- 4. split from a pane that has the terminal (native mode, Ctrl+H) -------------------------
k ctrl+h; sleep 3
k ctrl+p; sleep 7
check 04-split-while-native 1

# ----- 5. split while a program owns the terminal -----------------------------------------------
k ctrl+shift+h; sleep 2   # back to the prompt box
t 'sleep 60'; k Return; sleep 3
k ctrl+p; sleep 7
check 05-split-while-a-program-runs 1
k Escape; sleep 2

# ----- 6. split from the actions palette ---------------------------------------------------------
k ctrl+shift+a; sleep 2
t 'Split right'; sleep 1.5
k Return; sleep 8
check 06-split-from-the-palette 1
echo "screenshots in $out"
