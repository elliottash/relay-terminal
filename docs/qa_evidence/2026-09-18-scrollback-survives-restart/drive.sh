#!/usr/bin/env bash
# "When Relay quits and restarts and reloads sessions, it should still have the scrollback"
# (owner, 2026-09-18): implementer evidence under Xvfb with an isolated profile. No provider
# account is needed — nothing here asks the agent anything; the pane's shell does the printing.
#
#   docs/qa_evidence/2026-09-18-scrollback-survives-restart/drive.sh [build-dir]
#
#   implementer-a-before-quit.png   run 1: 120 lines printed, most of them scrolled off
#   implementer-b-restored.png      run 2: the same pane, reopened, with that text back
#   implementer-c-scrolled-back.png run 2: PageUp to the top of the restored history and its rule
#   implementer-d-second-quit.png   run 3: new output after the restore is saved in its turn
#   implementer-store.txt           the state directory: layout node, file size, first/last lines
#
# Needs Xvfb, xdotool, ImageMagick.
set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"
out=$PWD
root=$(cd ../../.. && pwd)
build=${1:-$root/build}
width=1100 height=760

display=
for n in $(seq 160 199); do [[ -e /tmp/.X11-unix/X$n ]] || { display=:$n; break; }; done
[[ -z $display ]] && { echo "no free display"; exit 1; }

sandbox=${RELAY_SHOT_HOME:-/tmp/relay-scrollback-shots}
xvfb_pid= relay_pid=
cleanup() { kill "${relay_pid:-0}" "${xvfb_pid:-0}" 2>/dev/null; rm -rf "$sandbox"; }
trap cleanup EXIT

Xvfb "$display" -screen 0 $((width + 40))x$((height + 40))x24 >/dev/null 2>&1 &
xvfb_pid=$!
sleep 2
kill -0 "$xvfb_pid" 2>/dev/null || { echo "Xvfb died on $display"; exit 1; }
export DISPLAY=$display
export RELAY_KEYRING=off
export HOME=$sandbox
export XDG_CONFIG_HOME=$HOME/.config XDG_DATA_HOME=$HOME/.local/share XDG_CACHE_HOME=$HOME/.cache
# Its own runtime directory too: the layout file has one owner at a time, held as a lock under
# $XDG_RUNTIME_DIR, and a Relay running in the real session would make this one "the second Relay"
# and stop it saving anything.
export XDG_RUNTIME_DIR=$HOME/run
work=$HOME/project

t() { xdotool type --delay 14 "$1"; }
k() { xdotool key --delay 45 "$@"; }

prepare() {
    rm -rf "$sandbox"
    mkdir -p "$XDG_CONFIG_HOME/RelayTerminal" "$XDG_DATA_HOME" "$XDG_CACHE_HOME" "$XDG_RUNTIME_DIR" "$work"
    chmod 700 "$XDG_RUNTIME_DIR"
    printf "PS1='\\\\w \$ '\nunset PROMPT_COMMAND\n" >"$HOME/.bashrc"
    cat >"$XDG_CONFIG_HOME/RelayTerminal/relay.conf" <<CONF
[instructions]
onboarded=true
[theme]
name=relay-dark
CONF
}

largest_window() {
    win= ; local best=0 w
    for w in $(xdotool search --pid "$relay_pid" 2>/dev/null); do
        eval "$(xdotool getwindowgeometry --shell "$w" 2>/dev/null)"
        (( WIDTH * HEIGHT > best )) && { best=$(( WIDTH * HEIGHT )); win=$w; }
    done
}

shot() {
    xdotool mousemove $((width + 20)) $((height + 20)); sleep 0.6
    import -window "$win" "$out/implementer-$1.png"
}

start() {   # start [extra args...]: a run of Relay on the same profile
    "$build/relay" "$@" >>"$out/relay-stderr.log" 2>&1 &
    relay_pid=$!
    sleep 8
    largest_window
    [[ -z $win ]] && { echo "no Relay window"; exit 1; }
    xdotool windowmove "$win" 0 0 windowsize "$win" $width $height
    xdotool windowfocus "$win"; sleep 2
}

quit() {    # Ctrl+W on the last pane asks before it closes the window; Left picks Close.
    k ctrl+w; sleep 1.5
    k Left; sleep 0.4
    k Return
    for _ in $(seq 1 20); do kill -0 "$relay_pid" 2>/dev/null || break; sleep 0.5; done
    wait "$relay_pid" 2>/dev/null; relay_pid=
    sleep 1
}

prepare
: >"$out/relay-stderr.log"

# ----- run 1: print more than a screenful, then quit -----------------------------------------
start --workspace "$work"
t 'echo MARKER-BEFORE-QUIT; seq 1 120; echo TAIL-BEFORE-QUIT'; k Return; sleep 3
shot a-before-quit
quit

{
    echo "== state directory after the quit =="
    find "$XDG_DATA_HOME/relay/state" -type f -printf '%s\t%p\n' | sort
    echo
    echo "== the pane node in windows.json =="
    python3 - "$XDG_DATA_HOME/relay/state/windows.json" <<'PY'
import json, sys
doc = json.load(open(sys.argv[1]))
def panes(node):
    if "split" in node:
        for child in node["children"]:
            yield from panes(child)
    elif "pane" in node:
        yield node["pane"]
for window in doc["windows"]:
    for tab in window["tabs"]:
        for pane in panes(tab):
            print(json.dumps({k: pane[k] for k in ("cwd", "engine_core", "session_id", "scrollback") if k in pane}, indent=2))
PY
    echo
    echo "== saved scrollback: line count, then the first and last lines =="
    file=$(ls "$XDG_DATA_HOME"/relay/state/scrollback/*.txt | head -1)
    wc -l "$file"
    head -3 "$file"
    echo '...'
    tail -3 "$file"
} >"$out/implementer-store.txt" 2>&1

# ----- run 2: the same profile, no --workspace, so the saved layout is reopened ---------------
cd "$work"
start
shot b-restored
k Prior Prior Prior Prior; sleep 1.5   # PageUp to the top of the restored history
shot c-scrolled-back
k Next Next Next Next; sleep 1         # back to the bottom
t 'echo MARKER-AFTER-RESTART'; k Return; sleep 2
quit

# ----- run 3: the second quit saved the restored text and the new line together --------------
start
shot d-second-quit
{
    echo
    echo "== after the second restart: the store still holds one file per pane =="
    find "$XDG_DATA_HOME/relay/state" -type f -printf '%s\t%p\n' | sort
    echo
    echo "== does the saved text carry both runs? =="
    grep -c 'MARKER-BEFORE-QUIT' "$XDG_DATA_HOME"/relay/state/scrollback/*.txt
    grep -c 'MARKER-AFTER-RESTART' "$XDG_DATA_HOME"/relay/state/scrollback/*.txt
} >>"$out/implementer-store.txt" 2>&1
quit

printf 'done: %s\n' "$out"
