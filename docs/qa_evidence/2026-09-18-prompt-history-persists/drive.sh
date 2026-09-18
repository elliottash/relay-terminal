#!/usr/bin/env bash
# Prompt history across a restart, live: type in one Relay, quit it, start another and press Up.
#
#   docs/qa_evidence/2026-09-18-prompt-history-persists/drive.sh [build-dir]
#
# Writes implementer-NN-*.png next to this script, plus relay-run{1,2,3}-stderr.log and
# history-file.txt (the store itself). Isolated XDG_CONFIG_HOME/XDG_DATA_HOME/XDG_CACHE_HOME *and*
# XDG_RUNTIME_DIR and TMPDIR: a live Relay on this machine shares the runtime dir, and its sockets
# and temp files would otherwise be picked up by this run. Needs Xvfb, xdotool and ImageMagick.
set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"
out=$PWD
root=$(cd ../../.. && pwd)
build=${1:-$root/build}

display=
for n in $(seq 140 180); do [[ -e /tmp/.X11-unix/X$n ]] || { display=:$n; break; }; done
[[ -z $display ]] && { echo "no free X display"; exit 1; }
echo "display $display"

export XDG_CONFIG_HOME=$(mktemp -d) XDG_DATA_HOME=$(mktemp -d) XDG_CACHE_HOME=$(mktemp -d)
export XDG_RUNTIME_DIR=$(mktemp -d) TMPDIR=$(mktemp -d)
chmod 700 "$XDG_RUNTIME_DIR"
work=$(mktemp -d)
mkdir -p "$XDG_CONFIG_HOME/RelayTerminal"
cat >"$XDG_CONFIG_HOME/RelayTerminal/relay.conf" <<'CONF'
[instructions]
onboarded=true
[terminal]
shell_integration=true
[windows]
restore=false
CONF
trap 'kill "${relay_pid:-0}" "${xvfb_pid:-0}" 2>/dev/null; rm -rf "$XDG_CONFIG_HOME" "$XDG_DATA_HOME" "$XDG_CACHE_HOME" "$XDG_RUNTIME_DIR" "$TMPDIR" "$work"' EXIT

Xvfb "$display" -screen 0 1400x900x24 >/dev/null 2>&1 &
xvfb_pid=$!
sleep 1
export DISPLAY=$display

history_file=$XDG_DATA_HOME/relay/state/prompt-history.txt
t() { xdotool type --delay 10 "$1"; }
k() { xdotool key --delay 40 "$@"; }
shot() { import -window "$win" "$out/implementer-$1.png"; }
rootshot() { import -window root "$out/implementer-$1.png"; }   # a modal dialog is its own window
box() { k ctrl+a; k Delete; t "$1"; }                           # type over whatever a browse recalled

start() {   # start(run-number); sets $relay_pid and $win
    "$build/relay" --workspace "$work" >"$out/relay-run$1-stderr.log" 2>&1 &
    relay_pid=$!
    sleep 7
    win=$(for w in $(xdotool search --onlyvisible --pid "$relay_pid" --name "^Relay"); do
            g=$(xdotool getwindowgeometry --shell "$w" 2>/dev/null)
            wdt=$(echo "$g" | sed -n 's/^WIDTH=//p'); hgt=$(echo "$g" | sed -n 's/^HEIGHT=//p')
            echo "$(( ${wdt:-0} * ${hgt:-0} )) $w"
          done | sort -rn | head -1 | cut -d' ' -f2)
    [[ -z $win ]] && { echo "no Relay window on run $1"; exit 1; }
    xdotool windowmove "$win" 0 0 windowsize "$win" 1400 900 windowfocus "$win"
    sleep 1
}

# ---------------------------------------------------------------- run 1: type three things
start 1
echo "== run 1: nothing remembered yet"
ls -l "$history_file" 2>&1 | sed 's/^/   /'
shot 01-run1-empty-box

t 'echo hello from the first run'
k Return
sleep 3
t 'printf "second\n"'
k Return
sleep 3
# A multi-line entry: Shift+Enter makes the newline, so the store has to survive one.
t 'echo one'
k shift+Return
t 'echo two'
k Return
sleep 3
shot 02-run1-three-commands-run

echo "== run 1: the history file"
cat "$history_file" | sed 's/^/   /'

# SIGTERM is an ordinary quit (src/main.cpp, installQuitSignals).
kill "$relay_pid"; wait "$relay_pid" 2>/dev/null; relay_pid=
sleep 2

# ---------------------------------------------------------------- run 2: Up walks back
start 2
shot 03-run2-fresh-window
k Up
sleep 1
shot 04-run2-up-recalls-the-multiline-entry
k Up
sleep 1
shot 05-run2-up-again
k Up
sleep 1
shot 06-run2-up-to-the-oldest
k Down; k Down; k Down
sleep 1
shot 07-run2-down-returns-to-the-empty-draft

# A second pane in the same window starts with the same history, and sees a line typed in the
# first one on its next Up: one history per person, not one per pane.
t 'echo typed in run two'
k Return
sleep 3
k ctrl+e         # a new pane to the right
sleep 3
shot 08-run2-second-pane
k Up
sleep 1
shot 09-run2-second-pane-sees-the-line-from-the-first

# Killed, not asked to quit: the line is already in the file.
box 'echo written before the kill'
k Return
sleep 3
kill -9 "$relay_pid"; wait "$relay_pid" 2>/dev/null; relay_pid=
sleep 2
echo "== after SIGKILL: the history file"
cat "$history_file" | sed 's/^/   /'
cp "$history_file" "$out/history-file.txt"
echo "== permissions"
stat -c '%a %n' "$history_file" "$(dirname "$history_file")" | sed 's/^/   /'

# ---------------------------------------------------------------- run 3: clear it
start 3
k Up
sleep 1
shot 10-run3-up-after-the-kill
k Escape
k ctrl+shift+a  # the actions palette
sleep 1
t 'clear prompt history'
sleep 1
shot 11-run3-palette-clear-prompt-history
k Return
sleep 2
rootshot 12-run3-confirm
k Left          # No is the default button; Yes is the one to the left of it
k Return
sleep 2
shot 13-run3-cleared
echo "== after clearing: the history file"
ls -l "$history_file" 2>&1 | sed 's/^/   /'
k Up            # the box that was open when it was cleared has nothing left to recall
sleep 1
shot 14-run3-up-after-clearing
box 'echo after the clear'
k Return
sleep 3
echo "== a line typed after the clear starts the file again"
cat "$history_file" 2>&1 | sed 's/^/   /'
echo "== done"
