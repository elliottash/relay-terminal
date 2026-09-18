#!/usr/bin/env bash
# #G152 — do pane sizes survive taking control (Ctrl+H) and giving it back (Ctrl+Shift+H)?
#
#   docs/qa_evidence/2026-09-17-bugfix-batch1/panes.sh [konsole|relay] [h|v] [panes] [build-dir]
#   RELAY_QA_DISPLAY=:91 docs/qa_evidence/2026-09-17-bugfix-batch1/panes.sh relay v 3
#
# The saved window layout records each splitter's sizes, so it is the exact measurement: the run
# splits the window, then prints the sizes after every step. Sizes must not change when the
# composer is hidden and shown again, and the saved layout must never hold a collapsed size.
set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"
out=$PWD
root=$(cd ../../.. && pwd)
engine=${1:-konsole}
axis=${2:-h}
panes=${3:-3}
build=${4:-$root/build}
display=${RELAY_QA_DISPLAY:-:91}
width=1400 height=900
tag=$engine-$axis$panes

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

sizes() {   # sizes <label>
    sleep 3   # the layout save is debounced by a second
    printf '%-28s ' "$1"
    python3 - "$XDG_DATA_HOME/relay/state/windows.json" <<'PY'
import json, sys
def walk(node, depth=0):
    if "split" in node:
        yield (node["split"], node.get("sizes", []))
        for child in node["children"]:
            yield from walk(child, depth + 1)
try:
    state = json.load(open(sys.argv[1]))
except OSError:
    print("(no layout file yet)"); raise SystemExit
out = []
for window in state.get("windows", []):
    for tab in window.get("tabs", []):
        for axis, sizes in walk(tab):
            out.append(f"{axis}{sizes}")
print(" ".join(out) if out else "(one pane)")
PY
    import -window "$win" "$out/$tag-$1.png" 2>/dev/null
}

"$build/relay" --engine="$engine" --workspace "$work" >"$out/$tag-relay-stderr.log" 2>&1 &
relay_pid=$!
sleep 8
win=$(xdotool search --pid "$relay_pid" --name Relay | tail -1)
[[ -z $win ]] && { echo "no Relay window"; exit 1; }
xdotool windowmove "$win" 0 0 windowsize "$win" $width $height
xdotool mousemove 700 450   # no window manager: X focus is PointerRoot
sleep 2

echo "=== $engine, $panes panes, ${axis}-split ==="
split_key=ctrl+p
[[ $axis == v ]] && split_key=ctrl+shift+p
for ((i = 1; i < panes; ++i)); do k "$split_key"; sleep 7; done
sizes 01-after-splits
k ctrl+h;       sizes 02-took-control
k ctrl+shift+h; sizes 03-gave-it-back
k ctrl+h;       sizes 04-took-control-again
k ctrl+shift+h; sizes 05-gave-it-back-again
echo "screenshots in $out"
