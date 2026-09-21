#!/usr/bin/env bash
# A pane with no shell, asked something, live (card #AGNT).
#
#   drive-console.sh <relay-console-harness> <out-dir>
#
# The decision this proves: a pane *is* the agent console, so a helper surface is the same `Pane`
# with a context whose spec says `shell: false`. `tests/console_harness.cpp` builds one in a bare
# `QMainWindow` — no `RelayWindow`, no tabs, no window manager — which is also what shows that a
# console can be embedded by a host that knows nothing about panes (steps 5-7).
#
# What the shots hold, in order:
#
#   01 the console at rest: the action row above the box with its letters, the context's
#      placeholder in the box, and no mode chip — there is nothing for a line to go to but the agent
#   02 a turn, with the answer streaming into the vterm and a ✦ thinking fold under its anchor
#   03 the same fold unfolded with Alt+R
#   04 a second prompt queued behind a running turn, with the queue strip
#   05 Esc during that turn
#   06 the console afterwards
#
# Same isolation as drive.sh: Xvfb, a private HOME / XDG_* / TMPDIR under a short path,
# RELAY_KEYRING=off and no provider account — the profile points a local endpoint at
# stub-provider.py, so the agent is that script.
set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"
harness=${1:?say which relay-console-harness binary}
out=${2:?say where the shots go}
root=$(cd ../../.. && pwd)
width=1400 height=900
port=${RELAY_QA_PORT:-8861}
mkdir -p "$out"
[[ -x $harness ]] || { echo "no harness binary at $harness"; exit 1; }

display=
for n in $(seq 170 199); do [[ -e /tmp/.X11-unix/X$n ]] || { display=:$n; break; }; done
[[ -z $display ]] && { echo "no free display"; exit 1; }

sandbox=$(mktemp -d /tmp/rl-cons.XXXX)
stub_pid= xvfb_pid= app_pid=
cleanup() {
    local pid
    for pid in $app_pid $stub_pid $xvfb_pid; do kill "$pid" 2>/dev/null; done
    rm -rf "$sandbox" 2>/dev/null
}
trap cleanup EXIT

Xvfb "$display" -screen 0 $((width + 40))x$((height + 40))x24 >/dev/null 2>&1 &
xvfb_pid=$!
sleep 2
kill -0 "$xvfb_pid" 2>/dev/null || { echo "Xvfb died on $display"; exit 1; }
export DISPLAY=$display RELAY_KEYRING=off

mkdir -p "$sandbox/home" "$sandbox/run" "$sandbox/tmp"; chmod 700 "$sandbox/run"
ln -sfn "/run/user/$(id -u)/bus" "$sandbox/run/bus"
export HOME=$sandbox/home XDG_RUNTIME_DIR=$sandbox/run TMPDIR=$sandbox/tmp
export XDG_CONFIG_HOME=$HOME/.config XDG_DATA_HOME=$HOME/.local/share XDG_CACHE_HOME=$HOME/.cache
work=$HOME/project
mkdir -p "$XDG_CONFIG_HOME/RelayTerminal" "$XDG_CONFIG_HOME/relay" "$XDG_DATA_HOME" "$work"
printf 'the fixture file the read_file call reads\n' >"$work/fixture.txt"

# A board, because the harness's context calls itself "switchboard" and a board console with no
# board is refused in a sentence -- which is one of the real constraints card #AGNT kept, and the
# first live run of this script walked straight into it: "This tab has no Switchboard, so there is
# nothing for the Switchboard pane to talk about." The harness sends no `board` block (it has no
# window to ask), so the worker walks up from the workspace and finds this.
PYTHONPATH="$root/backend" python3 - "$work" <<'FIX'
import sys
from pathlib import Path
from relay_core import board as B

work = Path(sys.argv[1])
root = work / "issues"
root.mkdir(parents=True, exist_ok=True)
(root / B.BOARD_CONFIG).write_text(
    "tabs: [{id: features, folder: features}]\n"
    "columns: [inbox, discussing, ready, in-progress, needs-qa, done]\n", encoding="utf-8")
board = B.Board(root, work)
card = B.new_card("work", "A fixture card for the console drive", "inbox", created="2026-09-20",
                  rank="a", request="so the board the console talks about is not empty")
B.write_new_card(board, card, "features")
FIX

python3 "$PWD/stub-provider.py" "$port" >/dev/null 2>&1 &
stub_pid=$!
sleep 1

cat >"$XDG_CONFIG_HOME/RelayTerminal/relay.conf" <<CONF
[instructions]
onboarded=true
[theme]
name=relay-dark
[provider]
preset=local:stub
[approvals]
mode=allow
CONF
printf '{"version": 1, "endpoints": [{"id": "local:stub", "label": "Stub", "base_url": "http://127.0.0.1:%s/v1", "model": "stub", "server": "openai-compatible", "context_window": 131072}]}\n' \
    "$port" >"$XDG_CONFIG_HOME/relay/local-models.json"

t() { xdotool type --delay 25 "$1"; }
k() { xdotool key --delay 60 "$@"; }
win=
# The pointer is parked *inside* the window while the shot is taken, not outside it. There is no
# window manager under Xvfb, so input focus follows the pointer: parking it off the window — which
# is what the terminal drive does, because that window is frameless and grabs focus back — left
# this one focused on the root, and three prompts in a row were typed into nothing.
shot() {
    local X=0 Y=0
    eval "$(xdotool getmouselocation --shell 2>/dev/null)"
    xdotool mousemove $((width - 60)) 8; sleep 0.8
    # There is no compositor under Xvfb and this is a plain QMainWindow, so a region the app has
    # damaged is sometimes left unpainted — a black block over the transcript. A one-pixel resize
    # and back forces a full expose, which is cheaper than reading a lie.
    xdotool windowsize "$win" $((width - 1)) "$height"; sleep 0.2
    xdotool windowsize "$win" "$width" "$height"; sleep 0.6
    import -window "$win" "$out/$1.png"
    xdotool mousemove "$X" "$Y"
    xdotool windowfocus "$win" 2>/dev/null
    sleep 0.3
}
text() { tesseract "$out/$1.png" - --psm 6 2>/dev/null; }
words() {
    convert "$out/$1.png" -scale 200% png:- 2>/dev/null \
        | tesseract - stdout --psm 11 tsv 2>/dev/null \
        | awk 'NF>=12 && $12 != "" {print $12, int($7/2), int($8/2), int($9/2), int($10/2)}'
}
word_xy() { words "$1" | awk -v want="$2" 'tolower($1) ~ tolower(want) {x=$2+$4/2; y=$3+$5/2} END {if (x) print x, y}'; }
await() {
    local waited=0 limit=${3:-60}
    while :; do
        shot "$1"
        text "$1" | grep -qi -- "$2" && return 0
        (( waited >= limit )) && return 1
        sleep 3; waited=$((waited + 3))
    done
}
: >"$out/notes.txt"
note() { echo "$*" >>"$out/notes.txt"; }
ok()  { note "PASS $*"; }
bad() { note "FAIL $*"; }
has()   { if text "$2" | grep -qi -- "$3"; then ok "$1 (\"$3\" in $2.png)"; else bad "$1: no \"$3\" in $2.png"; fi; }
# The same, read at 2x: a button's label is small type that the whole-page pass often misses.
hasword() { if [[ -n $(word_xy "$2" "$3") ]]; then ok "$1 (\"$3\" read in $2.png)"
            else bad "$1: no \"$3\" in $2.png"; fi; }
hasnt() { if text "$2" | grep -qi -- "$3"; then bad "$1: \"$3\" is in $2.png and it should not be"
          else ok "$1 (no \"$3\" in $2.png)"; fi; }

(cd "$work" && exec "$harness" "$work") >>"$sandbox/harness.log" 2>&1 &
app_pid=$!
sleep 10
best=0
for w in $(xdotool search --pid "$app_pid" 2>/dev/null); do
    eval "$(xdotool getwindowgeometry --shell "$w" 2>/dev/null)"
    (( WIDTH * HEIGHT > best )) && { best=$(( WIDTH * HEIGHT )); win=$w; }
done
[[ -z $win ]] && { echo "no harness window"; tail -40 "$sandbox/harness.log"; exit 1; }
xdotool windowmove "$win" 0 0 windowsize "$win" $width $height
xdotool windowfocus "$win"; sleep 4

# The composer is the bottom strip of a bare window here, so its row is a fixed offset from the
# foot rather than something to look for: the action row is above it and the chips below it.
focus_prompt() {
    xdotool windowfocus "$win" 2>/dev/null
    xdotool mousemove 200 $((height - 73)) click 1
    sleep 1
}
# The box is cleared first: a key the harness cannot answer — Alt+R, say, which is a window
# shortcut and there is no window here — falls through to the composer as a plain letter.
ask() { focus_prompt; k ctrl+a; k Delete; t "$1"; k Return; }

# ----- 01 the console at rest --------------------------------------------------------------------
shot 01-console
has "the context's placeholder is in the box" 01-console "Ask about this board"
hasword "the action row carries the context's first action" 01-console "Check"
hasword "…and its second" 01-console "Clean"
# A console has no shell, so no chip offers to send a line to one. Read from the chip row rather
# than the whole window: the words "terminal" and "auto" both appear in the pane's own prose.
# `consolemode`'s theRoutingIsLockedToTheAgent is the assertion; this is the picture of it.

# ----- 02/03 a turn, streaming, with its thinking fold ---------------------------------------------
ask "explain the fold please"
await 02-answer "thought for" 75 || bad "the answer never reached the transcript"
has "the answer streamed into the vterm with a settled reasoning anchor" 02-answer "thought for"
has "…and the answer's own words are on screen" 02-answer "folds under its anchor"
# Alt+R is not checked here and cannot be: `agent.thinkingPanel` is a *window* shortcut, and the
# harness has no window to hold the keymap — the key falls through to the composer as a letter.
# What the fold does is proved twice over already: docs/…/after/03-fold-open.png is the terminal
# pane unfolding the same block, and the anchor above says "thought for 1 s" here.
shot 03-after-answer

# ----- 04 a second prompt queued, with the strip ----------------------------------------------------
ask "count slowly for me"
sleep 3
focus_prompt; t "count slowly again"; k Return
sleep 2
shot 04-queued
has "the second prompt queued, with its strip" 04-queued "queue"

# ----- 05 Esc during a turn --------------------------------------------------------------------------
focus_prompt
k Escape; sleep 3
shot 05-esc

# ----- 06 afterwards ----------------------------------------------------------------------------------
sleep 6
shot 06-after

cp "$sandbox/harness.log" "$out/harness.log" 2>/dev/null
grep -c '^PASS' "$out/notes.txt" | sed 's/^/PASS lines: /'
grep -c '^FAIL' "$out/notes.txt" | sed 's/^/FAIL lines: /'
