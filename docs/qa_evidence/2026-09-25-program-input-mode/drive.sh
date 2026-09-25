#!/usr/bin/env bash
# Live pass for #S976 (program input mode). Relay on an isolated profile under an Xvfb, one
# terminal pane, driven with xdotool and scripts/relay-drive; screenshots land beside this file.
#
#   docs/qa_evidence/2026-09-25-program-input-mode/drive.sh [relay binary]
#
#   01  python3 at its >>> prompt, the prompt box in Auto: the busy row offers "Type into it
#       from here" and the hint says the program is waiting.
#   02  the same after input.modeProgram: chip "PROGRAM · python3", `print(1)` typed in the box.
#   03  after Enter: the REPL printed 1; no Take control, no queued line.
#   04  `exit()` in PROGRAM mode: python3 exits and the chip drops back to Auto by itself.
#   05  sqlite3 :memory: in PROGRAM mode, `.ta` + Tab completed to `.tables` from sqlite3's table.
#   06  after Enter: sqlite3 lists the table created a line earlier.
#   07  Terminal mode, `gti status` + Enter: a ✗ line with "did you mean git?", the text kept in
#       the box, and no agent turn (the pane has no provider signed in).
#   08  node at its prompt in PROGRAM mode: `1+1` printed 2 (node waits in epoll, not read()).
#   09  cat (not a REPL) in PROGRAM mode, a two-line draft + Enter: "cat takes one line at a
#       time", the draft stays in the box and nothing reaches cat.
#
# RELAY_DATA_DIR is honoured, so a binary built elsewhere (a land.py verify slot) can be driven
# against an export of the tree it was built from.
set -uo pipefail
here=$(cd "$(dirname "$0")" && pwd)
repo=$(cd "$here/../../.." && pwd)
bin=${1:-$repo/build/relay}
work=/tmp/claude-1000/tryit/s976
rm -rf "$work"; mkdir -p "$work/project"
sandbox=$work/sandbox
mkdir -p "$sandbox/home/.config/RelayTerminal" "$sandbox/run" "$sandbox/tmp"; chmod 700 "$sandbox/run"
# persistLocal=false: this sandbox has no systemd user bus, so memory isolation is unavailable and
# Relay would fall back to the tmux holder (#87HB), inside which a pane sees only the tmux client
# as its foreground process. The owner's desktop (isolation on, the default) runs no holder.
printf '[instructions]\nonboarded=true\n[security]\napprovals_chosen=true\n[terminal]\npersistLocal=false\n' > "$sandbox/home/.config/RelayTerminal/relay.conf"
export RELAY_KEYRING=off
unset RELAY_OPEN_SOCKET
export HOME=$sandbox/home XDG_RUNTIME_DIR=$sandbox/run TMPDIR=$sandbox/tmp \
       XDG_CONFIG_HOME=$sandbox/home/.config XDG_DATA_HOME=$sandbox/home/.local/share \
       XDG_CACHE_HOME=$sandbox/home/.cache
display=; for n in $(seq 200 259); do [[ -e /tmp/.X11-unix/X$n ]] || { display=:$n; break; }; done
Xvfb "$display" -screen 0 1400x900x24 >/dev/null 2>&1 & xvfb=$!
trap 'kill "${relay:-0}" "$xvfb" 2>/dev/null' EXIT
sleep 2; export DISPLAY=$display
(cd "$work/project" && exec "$bin" --workspace "$work/project" --fresh) > "$work/relay.log" 2>&1 & relay=$!
sleep 10

drive() { python3 "$repo/scripts/relay-drive" "$@"; }
shot() { import -window root "$here/$1.png"; }
t() { xdotool type --delay 40 "$1"; }
k() { xdotool key --delay 60 "$@"; }
focus_box() { xdotool mousemove 300 735 click 1; sleep 0.3; }

focus_box
t 'python3 -q'; k Return; sleep 3
shot 01-python-waiting-auto
drive action input.modeProgram >/dev/null; sleep 1
focus_box
t 'print(1)'; sleep 0.5
shot 02-python-program-chip
k Return; sleep 1.5
shot 03-python-printed
t 'exit()'; k Return; sleep 2.5
shot 04-python-exited-back-to-auto

t 'sqlite3 :memory:'; k Return; sleep 2.5
drive action input.modeProgram >/dev/null; sleep 1
focus_box
t 'create table evidence(x);'; k Return; sleep 1
t '.ta'; k Tab; sleep 0.8
shot 05-sqlite-tab-completed
k Return; sleep 1.5
shot 06-sqlite-tables
t '.quit'; k Return; sleep 2.5

drive action input.modeTerminal >/dev/null; sleep 1
focus_box
t 'gti status'; k Return; sleep 3
shot 07-terminal-typo-did-you-mean
k ctrl+a BackSpace; sleep 0.3

drive action input.modeAuto >/dev/null; sleep 0.5
t 'node'; k Return; sleep 3
drive action input.modeProgram >/dev/null; sleep 1
focus_box
t '1+1'; k Return; sleep 1.5
shot 08-node-program-printed
t '.exit'; k Return; sleep 2.5

t 'cat'; k Return; sleep 2
drive action input.modeProgram >/dev/null; sleep 1
focus_box
t 'first'; k shift+Return; t 'second'; k Return; sleep 1
shot 09-cat-multiline-refused
drive panes > "$here/panes-final.json" 2>&1 || true
cp "$work/relay.log" "$here/relay.log" 2>/dev/null || true
