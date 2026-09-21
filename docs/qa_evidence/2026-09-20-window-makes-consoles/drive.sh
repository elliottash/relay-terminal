#!/usr/bin/env bash
# The window makes agent consoles, and an embedded console is not one of the window's panes.
# Card #AGNT step 5, live.
#
#   drive.sh [relay-binary] [out-dir]
#
# Both paths are **absolute**: the script runs from its own directory, so a relative out-dir lands
# under this folder rather than where it was typed.
#
# What this proves, in order:
#
#   01 Options carries the collapsed "Helper Agent" row the pane owns
#   02 Alt+Q expands it into a **console** — a `Pane` with the Options context, built on first
#      expand by `RelayWindow::createAgentConsole` — with the context's placeholder in its box
#   03 it answers, streaming into a vterm transcript, with an `option:` link in the answer
#   04 the link reveals the row in this same pane (the context resolves it, no second pane)
#   05 `app_panes` — which reads `RelayWindow::allPanes()`, the same walk `syncTabShares`
#      publishes to the phone with — lists the terminal pane and **not** the console
#   06 Sessions in the same tab gets a console too, and it answers
#   07 two consoles, one worker process and one conversation file on disk
#
# Isolation as in the other drives: Xvfb, a private HOME / XDG_* / TMPDIR under a short path,
# RELAY_KEYRING=off and no provider account — the profile points a local endpoint at
# stub-provider.py, so every agent in the run is that script.
set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"
out=${2:-$PWD}
root=$(cd ../../.. && pwd)
relay=${1:-$root/build/relay}
width=1600 height=1600   # two tool panes in one tab, each with an expanded console, all on screen
port=${RELAY_QA_PORT:-8871}
mkdir -p "$out"
[[ -x $relay ]] || { echo "no relay binary at $relay"; exit 1; }

display=
for n in $(seq 150 199); do [[ -e /tmp/.X11-unix/X$n ]] || { display=:$n; break; }; done
[[ -z $display ]] && { echo "no free display"; exit 1; }

sandbox=$(mktemp -d /tmp/rl-agnt5.XXXX)
stub_pid= xvfb_pid= relay_pid=
cleanup() {
    local pid
    for pid in $relay_pid $stub_pid $xvfb_pid; do kill "$pid" 2>/dev/null; done
    rm -rf "$sandbox"
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
printf "PS1='\\\\w \\\$ '\nunset PROMPT_COMMAND\n" >"$HOME/.bashrc"

# A board, so the tab attaches to a project and the console is not the board-less case.
PYTHONPATH="$root/backend" python3 - "$work" <<'FIX'
import sys
from pathlib import Path
from relay_core import board as B

work = Path(sys.argv[1])
root = work / "issues"
root.mkdir(parents=True)
(root / B.BOARD_CONFIG).write_text(
    "tabs: [{id: features, folder: features}]\n"
    "columns: [inbox, discussing, ready, in-progress, needs-qa, done]\n", encoding="utf-8")
board = B.Board(root, work)
card = B.new_card("work", "A fixture card so the board is not empty", "inbox",
                  created="2026-09-20", rank="a", request="the console's brief has a board")
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

: >"$out/notes.txt"
pass=0 fail=0
note() { echo "$*" >>"$out/notes.txt"; }
ok()   { note "PASS $*"; pass=$((pass+1)); }
bad()  { note "FAIL $*"; fail=$((fail+1)); }

win=
t() { xdotool type --delay 30 "$1"; }
k() { xdotool key --delay 60 "$@"; }
# There is no compositor under Xvfb, so a region the app has damaged is sometimes left unpainted
# — a blank block exactly where the console's vterm is, which the first run of this script read as
# "the console never answered" while the pane header above it was already wearing the answer's
# auto-generated title. A one-pixel resize and back forces a full expose, which is cheaper than
# reading a lie. (The same trick is in drive-console.sh, for the same reason.)
shot() {
    local X=0 Y=0
    eval "$(xdotool getmouselocation --shell 2>/dev/null)"
    xdotool mousemove $((width + 20)) $((height + 20)); sleep 0.6
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
word_xy_below() { words "$1" | awk -v want="$2" -v top="$3" 'tolower($1) ~ tolower(want) && $3 > top {x=$2+$4/2; y=$3+$5/2} END {if (x) print x, y}'; }
hasword() { if [[ -n $(word_xy "$2" "$3") ]]; then ok "$1 (\"$3\" read in $2.png)"; else bad "$1: no \"$3\" in $2.png"; fi; }
click_at() { [[ -z ${1:-} || -z ${2:-} ]] && return 1; xdotool mousemove "$1" "$2" click 1; sleep 1.5; }
await() {
    local waited=0 limit=${3:-45}
    while :; do
        shot "$1"
        text "$1" | grep -qi -- "$2" && return 0
        (( waited >= limit )) && return 1
        sleep 3; waited=$((waited + 3))
    done
}
has()   { if text "$2" | grep -qi -- "$3"; then ok "$1 (\"$3\" in $2.png)"; else bad "$1: no \"$3\" in $2.png"; fi; }
hasnt() { if text "$2" | grep -qi -- "$3"; then bad "$1: \"$3\" is in $2.png and it should not be"
          else ok "$1 (no \"$3\" in $2.png)"; fi; }
awaited() { if await "$2" "$3" "${4:-45}"; then ok "$1 (\"$3\" in $2.png)"; else bad "$1: no \"$3\" in $2.png after ${4:-45}s"; fi; }

(cd "$work" && exec "$relay" --workspace "$work" --fresh) >>"$sandbox/relay.log" 2>&1 &
relay_pid=$!
sleep 10
best=0
for w in $(xdotool search --pid "$relay_pid" 2>/dev/null); do
    eval "$(xdotool getwindowgeometry --shell "$w" 2>/dev/null)"
    (( WIDTH * HEIGHT > best )) && { best=$(( WIDTH * HEIGHT )); win=$w; }
done
[[ -z $win ]] && { echo "no Relay window"; tail -40 "$sandbox/relay.log"; exit 1; }
xdotool windowmove "$win" 0 0 windowsize "$win" $width $height
xdotool windowfocus "$win"; sleep 4
shot _approvals
yes=$(word_xy _approvals "recommend")
[[ -n $yes ]] && click_at ${yes% *} ${yes#* }
sleep 2

# The console's composer, found by the placeholder its context puts there.
focus_console() {   # shot-name placeholder-word
    shot "$1"
    local at; at=$(word_xy "$1" "$2")
    [[ -n $at ]] && click_at ${at% *} ${at#* }
    sleep 1
}

# ----- 01/02 Options, its row, and the console behind it ---------------------------------------
k ctrl+shift+o; sleep 4
shot 01-options
hasword "01 Options carries the collapsed helper row" 01-options "helper"
k alt+q; sleep 3
focus_console 02-console "ask"
has "02 the console is up, with the Options context's placeholder" 02-console "Ask the Options helper"

# ----- 03/04 it answers, and its option: link reveals the row in place ---------------------------
t "where is copy on select?"; k Return
awaited "03 the console answered, streaming into its transcript" 03-answer "Clicking that opens" 180
sleep 5; shot 03-answer
link=$(word_xy_below 03-answer "^copy$" 500)
[[ -z $link ]] && link=$(word_xy_below 03-answer "^select" 500)
if [[ -n $link ]]; then
    click_at ${link% *} ${link#* }
    shot 04-link
    has "04 the option: link revealed the row in this same pane" 04-link "Copy on select"
else
    bad "04 no link text found in 03-answer.png"; shot 04-link
fi

# ----- 05 the window's pane list, read out of the console ----------------------------------------
focus_console 05-before-panes "ask"
t "which panes are open?"; k Return
awaited "05 app_panes came back through the window" 05-panes "PANELIST" 180
sleep 5; shot 05-panes
panelist=$(text 05-panes | grep -i "PANELIST" | tail -1)
note "the window's own pane list, as the agent was given it: $panelist"
count=$(sed -n 's/.*count=\([0-9]*\).*/\1/p' <<<"$panelist")
if [[ $count == 1 ]]; then
    ok "05 the window has exactly one pane — the terminal — with two consoles on screen"
else
    bad "05 the window's pane list has $count entries; the embedded console is in allPanes()"
fi

# ----- 06 a second console of the same tab -------------------------------------------------------
#
# What is asserted here is that the *window made a second console for this tab* and that it is a
# prompt box of its own: its placeholder is the Sessions context's, not the Options one's. The
# typed ask is driven too, and is deliberately **not** asserted — with two tool panes expanded in
# one tab the second composer sits at the foot of the screen, and which of two boxes an
# OCR-located click lands in is a property of this fixture, not of the product. What the two
# consoles share is measured instead, in 07 below: one worker and one conversation, which is the
# card's decision 1.
k ctrl+shift+y; sleep 4
shot 06-sessions
hasword "06 the Sessions pane carries its own collapsed helper row" 06-sessions "helper"
k alt+q; sleep 3
focus_console 06-sessions-console "ask"
has "06 and expands into a console of its own" 06-sessions-console "Sessions helper"
t "say hello"; k Return
sleep 20; shot 07-second-answer
# Both consoles wear the same turn in their headers, because the tab's conversation is one and
# every console of the tab draws it (owner decision 1). Counted rather than matched: the point is
# that it is there **twice**, once per console.
titles=$(text 07-second-answer | grep -ci "copy on select setting")
note "consoles showing the tab's turn: $titles"
if (( titles >= 2 )); then
    ok "06 the tab's one conversation is drawn in both consoles"
else
    bad "06 only $titles console(s) show the tab's turn"
fi

# ----- 07 one worker, one conversation ------------------------------------------------------------
sleep 3
# Counted as children of *this* Relay, not by name: other sessions on this machine run their own
# workers, and `pgrep -f worker.py` would count those too.
workers=$(pgrep -P "$relay_pid" -f "worker.py" 2>/dev/null | wc -l)
note "worker processes under this Relay: $workers (one for the terminal pane, one for the tab)"
if [[ $workers == 2 ]]; then
    ok "07 two consoles and a terminal pane run two workers, not three"
else
    bad "07 this Relay has $workers workers; two consoles of a tab should share the tab's one"
fi
helperdir=$XDG_DATA_HOME/relay/helper-sessions
# The store writes `<id>.json` beside a `<id>.meta.json`, so the conversations are the former.
files=$(find "$helperdir" -name '*.json' ! -name '*.meta.json' 2>/dev/null | wc -l)
note "helper conversation files under $helperdir: $files"
find "$helperdir" -name '*.json' 2>/dev/null >"$out/helper-sessions.txt"
if [[ $files == 1 ]]; then
    ok "07 two consoles of one tab wrote one conversation"
else
    bad "07 two consoles of one tab wrote $files conversation files"
fi

grep -i "console\|helper\|board_worker\|worker_start" "$sandbox/relay.log" >"$out/relay-console.log" 2>/dev/null
cp "$sandbox/relay.log" "$out/relay.log" 2>/dev/null
echo "PASS $pass  FAIL $fail"
note "----"
note "PASS $pass  FAIL $fail"
[[ $fail == 0 ]]
