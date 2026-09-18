#!/usr/bin/env bash
# #4PW5 — is a pane active the moment it is created?
#
#   docs/qa_evidence/2026-09-17-bugfix-batch1/newpane.sh [konsole|relay] [build-dir]
#
# Splits with the keyboard, types straight away without clicking anything, and then presses the
# close key. The saved window layout is the pane counter (it records the splitter tree), and the
# screenshot shows whether the typed line landed in the new pane's prompt box. Each step is also
# tried through the pane chrome's "◫+" button, because the owner splits both ways.
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

panes() {   # panes <label>
    sleep 3   # the layout save is debounced by a second
    printf '%-30s panes=' "$1"
    python3 - "$XDG_DATA_HOME/relay/state/windows.json" <<'PY'
import json, sys
def leaves(node):
    if "split" in node:
        return sum(leaves(child) for child in node["children"])
    return 1
try:
    state = json.load(open(sys.argv[1]))
except OSError:
    print("?"); raise SystemExit
print(sum(leaves(tab) for w in state.get("windows", []) for tab in w.get("tabs", [])))
PY
    import -window "$win" "$out/newpane-$engine-$1.png" 2>/dev/null
}

"$build/relay" --engine="$engine" --workspace "$work" >"$out/newpane-$engine-relay-stderr.log" 2>&1 &
relay_pid=$!
sleep 8
win=$(xdotool search --pid "$relay_pid" --name Relay | tail -1)
[[ -z $win ]] && { echo "no Relay window"; exit 1; }
xdotool windowmove "$win" 0 0 windowsize "$win" $width $height
xdotool mousemove 700 450   # no window manager: X focus is PointerRoot
sleep 2
panes 01-one-pane

echo "=== $engine: split with the key, then type without clicking ==="
k ctrl+p; sleep 7
panes 02-after-split
t 'echo FRESHPANE'; sleep 1
panes 03-typed-into-the-new-pane
k Return; sleep 3
panes 04-ran-in-the-new-pane
echo "--- close key on the new pane ---"
k ctrl+w; sleep 4
panes 05-after-close-key

echo "=== $engine: split with the pane chrome button ==="
# The chrome sits in the pane's top-right corner and is shown while the mouse is over the pane;
# "◫+" (split right) is the second button.
xdotool mousemove $(( width - 300 )) 400; sleep 0.8
xdotool mousemove $(( width - 145 )) 55; sleep 1
xdotool click 1; sleep 7
panes 06-after-button-split
t 'echo BUTTONPANE'; sleep 1
panes 07-typed-after-button-split
k Return; sleep 3
k ctrl+w; sleep 4
panes 08-after-close-key
echo "screenshots in $out"
