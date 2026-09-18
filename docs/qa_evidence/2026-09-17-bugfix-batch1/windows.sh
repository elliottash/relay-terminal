#!/usr/bin/env bash
# #RDQ7 — how many top-level windows does a start open?
#
#   docs/qa_evidence/2026-09-17-bugfix-batch1/windows.sh [clean|saved] [build-dir]
#   RELAY_QA_DISPLAY=:91 docs/qa_evidence/2026-09-17-bugfix-batch1/windows.sh clean
#
# `clean` starts with an empty profile; `saved` first opens a second window (Ctrl+N), lets the
# debounced save write the layout, then starts again so the restore path runs. Both print every
# mapped top-level window with its geometry, and shoot each window by id (capturing the root
# window comes out black now that Relay draws its own frame).
set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"
out=$PWD
root=$(cd ../../.. && pwd)
mode=${1:-clean}
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
# Another agent may already own that display number; a leaked run would type into their session.
if ! DISPLAY=$display xdotool getdisplaygeometry >/dev/null 2>&1 || ! kill -0 "$xvfb_pid" 2>/dev/null; then
    echo "display $display is not ours (set RELAY_QA_DISPLAY to a free one)"; exit 1
fi
export DISPLAY=$display

k() { xdotool key --delay 40 "$@"; }

# Every mapped top-level window of the running Relay, by id, with its geometry.
report() {   # report <label>
    echo "=== $1 ==="
    local n=0
    for w in $(xdotool search --pid "$relay_pid" 2>/dev/null); do
        # Only windows that are actually mapped (viewable) count as windows the user sees.
        xwininfo -id "$w" | grep -q 'IsViewable' || continue
        n=$((n + 1))
        echo "  win=$w name='$(xdotool getwindowname "$w" 2>/dev/null)' $(xdotool getwindowgeometry --shell "$w" 2>/dev/null | tr '\n' ' ')"
        import -window "$w" "$out/$mode-$1-win$n.png" 2>/dev/null
    done
    echo "  mapped top-level windows: $n"
}

start() {   # start <extra args...>
    "$build/relay" "$@" >>"$out/$mode-relay-stderr.log" 2>&1 &
    relay_pid=$!
    sleep 8
}

: >"$out/$mode-relay-stderr.log"

if [[ $mode == clean ]]; then
    start --workspace "$work"
    report 01-clean-start
    kill "$relay_pid"; wait "$relay_pid" 2>/dev/null
else
    # First run: open a second window so the saved layout has two. The save is debounced by a
    # second, so waiting is enough — no quit is needed to get the file written.
    start --workspace "$work"
    report 01-first-run
    k ctrl+n; sleep 8
    report 02-two-windows
    kill "$relay_pid" 2>/dev/null; wait "$relay_pid" 2>/dev/null
    sleep 1
    echo "--- saved layout ---"
    python3 - "$XDG_DATA_HOME/relay/state/windows.json" <<'PY'
import json, sys
try:
    state = json.load(open(sys.argv[1]))
except OSError as error:
    print("  no layout file:", error); raise SystemExit
print("  version", state.get("version"), "windows", len(state.get("windows", [])))
for window in state.get("windows", []):
    print("   ", window.get("geometry"), window.get("screen"), "tabs", len(window.get("tabs", [])))
PY
    # Second run: no --workspace, so the restore path runs.
    start
    report 03-after-restore
    kill "$relay_pid" 2>/dev/null; wait "$relay_pid" 2>/dev/null
fi
echo "screenshots in $out"
