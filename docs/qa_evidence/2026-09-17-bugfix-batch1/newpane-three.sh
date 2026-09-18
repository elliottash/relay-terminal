#!/usr/bin/env bash
# #4PW5 — the third pane, which is the shape the owner reported it in (they had three panes open
# when they hit the Ctrl+H sizing bug the same day).
#
#   docs/qa_evidence/2026-09-17-bugfix-batch1/newpane-three.sh [engine] [build-dir] [tag]
#
# Splits twice, types a marker into the third pane without clicking, then closes it with the close
# key. Screenshots are taken after a settle so the window's backing store is not captured mid-repaint.
set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"
out=$PWD
root=$(cd ../../.. && pwd)
engine=${1:-relay}
build=${2:-$root/build}
tag=${3:-$engine}
display=${RELAY_QA_DISPLAY:-:91}
width=1400 height=900

export XDG_CONFIG_HOME=$(mktemp -d) XDG_DATA_HOME=$(mktemp -d) XDG_CACHE_HOME=$(mktemp -d)
export XDG_RUNTIME_DIR=$(mktemp -d)
work=$(mktemp -d)
mkdir -p "$XDG_CONFIG_HOME/RelayTerminal"
printf '[instructions]\nonboarded=true\n[terminal]\nshell_integration=true\n' >"$XDG_CONFIG_HOME/RelayTerminal/relay.conf"
trap 'kill "${relay_pid:-0}" "${xvfb_pid:-0}" 2>/dev/null; rm -rf "$XDG_CONFIG_HOME" "$XDG_DATA_HOME" "$XDG_CACHE_HOME" "$XDG_RUNTIME_DIR" "$work"' EXIT

Xvfb "$display" -screen 0 ${width}x${height}x24 >/dev/null 2>&1 &
xvfb_pid=$!
sleep 1
if ! DISPLAY=$display xdotool getdisplaygeometry >/dev/null 2>&1 || ! kill -0 "$xvfb_pid" 2>/dev/null; then
    echo "display $display is not ours (set RELAY_QA_DISPLAY to a free one)"; exit 1
fi
export DISPLAY=$display
k() { xdotool key --delay 40 "$@"; }
count() {
    python3 - "$XDG_DATA_HOME/relay/state/windows.json" <<'PY'
import json, sys
def leaves(n): return sum(leaves(c) for c in n["children"]) if "split" in n else 1
try: state = json.load(open(sys.argv[1]))
except OSError: print("?"); raise SystemExit
print(sum(leaves(t) for w in state.get("windows", []) for t in w.get("tabs", [])))
PY
}

"$build/relay" --engine="$engine" --workspace "$work" >"$out/three-$tag-relay-stderr.log" 2>&1 &
relay_pid=$!
sleep 8
win=$(xdotool search --pid "$relay_pid" --name Relay | tail -1)
[[ -z $win ]] && { echo "no Relay window"; exit 1; }
xdotool windowmove "$win" 0 0 windowsize "$win" $width $height
xdotool mousemove 700 450
sleep 2
echo "=== $tag: three panes ==="
k ctrl+p; sleep 8
k ctrl+p; sleep 8
sleep 3
echo "  panes after two splits: $(count)"
xdotool type --delay 15 'echo THIRDPANE'
sleep 3
import -window "$win" "$out/three-$tag-01-typed-into-the-third-pane.png" 2>/dev/null
k Return; sleep 3
import -window "$win" "$out/three-$tag-02-ran-in-the-third-pane.png" 2>/dev/null
k ctrl+w; sleep 5
echo "  panes after the close key: $(count)"
import -window "$win" "$out/three-$tag-03-after-close.png" 2>/dev/null
echo "screenshots in $out"
