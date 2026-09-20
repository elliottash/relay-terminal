#!/usr/bin/env bash
# Reproduction for card #0TJ9: "when i accessed a convo in the session manager, i couldnt
# scroll back".
#
# Run 1 under Xvfb prints MARKER-* lines in a pane and quits through the window (Ctrl+W, the
# way 2026-09-18-scrollback-survives-restart/drive.sh quits), so the pane's terminal text is
# saved to the scrollback store keyed by the pane's token, and the layout pairs that token with
# the pane's session id. A real saved conversation is then seeded into the profile (the
# isolated profile has no provider keys, so no turn can be run) and the layout node is pointed
# at it, standing in for "the pane this conversation lived in".
#
# Run 2 starts with --workspace (the saved layout is NOT restored — the old pane is gone, as it
# is days later), opens the session manager (Ctrl+Shift+Y) and opens the session in a new pane
# (Shift+Enter). Expected bug: the new pane shows the conversation but no MARKER-* scrollback,
# while the text still sits on disk in state/scrollback/, keyed by a pane token nothing looks
# up.
#
#   docs/qa_evidence/2026-09-20-session-resume-scrollback/drive.sh [build-dir]
#   RELAY_QA_DISPLAY=:95 docs/qa_evidence/2026-09-20-session-resume-scrollback/drive.sh
#
# Needs Xvfb, xdotool and ImageMagick `import`. Isolated XDG dirs keep the run out of the real
# profile. The seeded session file never leaves the machine and is deleted with the temp dirs.
set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"
out=$PWD
root=$(cd ../../.. && pwd)
build=${1:-$root/build}
display=${RELAY_QA_DISPLAY:-:78}
width=1400 height=900

export XDG_CONFIG_HOME=$(mktemp -d) XDG_DATA_HOME=$(mktemp -d) XDG_CACHE_HOME=$(mktemp -d) XDG_STATE_HOME=$(mktemp -d) XDG_RUNTIME_DIR=$(mktemp -d)
# XDG_RUNTIME_DIR must be isolated too: the layout lock is $XDG_RUNTIME_DIR/relay/windows.lock,
# and the owner's running Relay holds it, so without this the run never owns its layout and
# saves nothing (WindowManager::writesState).
workspace=$(mktemp -d)
mkdir -p "$XDG_CONFIG_HOME/RelayTerminal"
cat >"$XDG_CONFIG_HOME/RelayTerminal/relay.conf" <<'CONF'
[instructions]
onboarded=true
[terminal]
shell_integration=true
CONF
trap 'kill "${relay_pid:-0}" 2>/dev/null; kill "${xvfb_pid:-0}" 2>/dev/null; rm -rf "$XDG_CONFIG_HOME" "$XDG_DATA_HOME" "$XDG_CACHE_HOME" "$XDG_STATE_HOME" "$XDG_RUNTIME_DIR" "$workspace"' EXIT

Xvfb "$display" -screen 0 ${width}x${height}x24 >/dev/null 2>&1 &
xvfb_pid=$!
sleep 1
if ! DISPLAY=$display xdotool getdisplaygeometry >/dev/null 2>&1 || ! kill -0 "$xvfb_pid" 2>/dev/null; then
    echo "display $display is not ours (set RELAY_QA_DISPLAY to a free one)"; exit 1
fi
export DISPLAY=$display

t() { xdotool type --delay 12 "$1"; }
k() { xdotool key --delay 40 "$@"; }
largest_window() {
    win= ; local best=0 w
    for w in $(xdotool search --pid "$relay_pid" 2>/dev/null); do
        eval "$(xdotool getwindowgeometry --shell "$w" 2>/dev/null)"
        (( WIDTH * HEIGHT > best )) && { best=$(( WIDTH * HEIGHT )); win=$w; }
    done
}
shot() {
    xdotool mousemove $((width - 30)) $((height - 30)) 2>/dev/null; sleep 0.4
    import -window "${win:-root}" "$out/$1.png"
}
start() {   # start [extra args...]
    "$build/relay" "$@" >>"$out/relay-stderr.log" 2>&1 &
    relay_pid=$!
    sleep 9
    largest_window
    [ -z "${win:-}" ] && { echo "no Relay window"; exit 1; }
    xdotool windowmove "$win" 0 0 windowsize "$win" $width $height
    xdotool windowfocus "$win"; sleep 2
}
quit() {    # Ctrl+W on the last pane asks before it closes the window; Left picks Close.
    k ctrl+w; sleep 1.5
    k Left; sleep 0.4
    k Return
    for _ in $(seq 1 24); do kill -0 "$relay_pid" 2>/dev/null || break; sleep 0.5; done
    wait "$relay_pid" 2>/dev/null; relay_pid=0
    sleep 1
}

: >"$out/relay-stderr.log"

# ----- run 1: marked output, clean quit ------------------------------------------------------
start --workspace "$workspace"
k ctrl+i; sleep 1   # input mode: terminal, so the markers cannot be routed to the agent
t "seq -f 'MARKER-%g' 1 80"; k Return
sleep 3
shot 01-run1-markers
quit

state=$XDG_DATA_HOME/relay/state
{
    echo "== state directory after the quit =="
    find "$state" -type f -printf '%s\t%p\n' 2>&1 | sort
    echo
    echo "== pane nodes in windows.json (session_id paired with scrollback id) =="
    python3 - "$state/windows.json" <<'PY'
import json, sys
def walk(n):
    if isinstance(n, dict):
        keys = {k: n.get(k) for k in ('session_id', 'scrollback', 'directory') if k in n}
        if keys: print(keys)
        for v in n.values(): walk(v)
    elif isinstance(n, list):
        for v in n: walk(v)
walk(json.load(open(sys.argv[1])))
PY
    echo
    echo "== the saved text, head and tail =="
    for f in "$state/scrollback/"*.txt; do echo "-- $f"; head -3 "$f"; echo "..."; tail -3 "$f"; done
} >"$out/run1-state.txt" 2>&1
[ -f "$state/windows.json" ] || { echo "run 1 saved no layout; see run1-state.txt"; exit 1; }

# Seed a real saved conversation (no provider keys here, so no turn can be run to make one) and
# make run 1's scrollback pane the pane it lived in, as it would be had the conversation
# happened there: point its layout node at the seeded session id.
seed=$(find ~/.local/share/relay/sessions -name '*.json' ! -name '*.meta.json' -printf '%s %p\n' 2>/dev/null | sort -n | head -1 | cut -d' ' -f2)
wsdir=$(find "$XDG_DATA_HOME/relay/sessions" -mindepth 1 -maxdepth 1 -type d | head -1)
mkdir -p "$wsdir"
cp "$seed" "$wsdir/"; [ -f "${seed%.json}.meta.json" ] && cp "${seed%.json}.meta.json" "$wsdir/"
seed_id=$(basename "$seed" .json)
python3 - "$state/windows.json" "$seed_id" <<'PY'
import json, sys
path, sid = sys.argv[1], sys.argv[2]
d = json.load(open(path))
def walk(n):
    if isinstance(n, dict):
        if 'scrollback' in n: n['session_id'] = sid
        for v in n.values(): walk(v)
    elif isinstance(n, list):
        for v in n: walk(v)
walk(d)
json.dump(d, open(path, 'w'))
PY
{
    echo
    echo "== seeded session $seed_id (smallest session file from the real profile, deleted with the temp dirs) =="
    echo "== windows.json pane nodes after the seed =="
    python3 - "$state/windows.json" <<'PY'
import json, sys
def walk(n):
    if isinstance(n, dict):
        keys = {k: n.get(k) for k in ('session_id', 'scrollback') if k in n}
        if keys: print(keys)
        for v in n.values(): walk(v)
    elif isinstance(n, list):
        for v in n: walk(v)
walk(json.load(open(sys.argv[1])))
PY
} >>"$out/run1-state.txt" 2>&1

# ----- run 2: old pane gone (--workspace starts fresh), resume from the manager --------------
start --workspace "$workspace"
shot 02-run2-fresh
k ctrl+shift+y; sleep 5
shot 03-run2-manager
k Down; sleep 1           # select the session row (the only one)
k shift+Return; sleep 9   # "Open in new pane": loads the conversation, spawns a fresh pane
shot 04-run2-resumed
k Prior Prior Prior Prior Prior; sleep 1   # PageUp: anything above the conversation?
shot 05-run2-paged-up

{
    echo "== scrollback store after run 2's resume: the text is still on disk, keyed by run 1's pane token =="
    ls -la "$state/scrollback/" 2>&1
    echo
    echo "== nothing in run 2's fresh panes points at that id: the manager resume path"
    echo "   (Pane::openSavedSession -> RelayWindow::openFork -> createPane) builds a pane"
    echo "   whose scrollback id is its own new token, and no saved text is read. =="
} >"$out/run2-state.txt" 2>&1

quit
echo "done: screenshots and state in $out"
