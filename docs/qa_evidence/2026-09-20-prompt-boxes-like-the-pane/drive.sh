#!/usr/bin/env bash
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# #PBX1 — every agent prompt box looks and behaves like the main pane's (owner, 2026-09-20: "i
# dont like the helper agent prompt UI … there is the useless help sentence, and then a bunch of
# wasted space, and then the tiny text box. and the buttons dont look as good", "[remove] the send
# button on all, make it like the pane agent", and of the card's Plan/Execute: "put buttons like
# that in a row above the chat box").
#
#   docs/qa_evidence/2026-09-20-prompt-boxes-like-the-pane/drive.sh [relay-binary] [out-dir]
#
# Drives a real Relay under Xvfb and photographs every prompt box in the app next to the one they
# were all made to match:
#
#   01  the terminal pane's prompt box and the Switchboard agent's, side by side, both idle
#   02  the same pair with a short conversation in the panel
#   03  the card page: Plan / Execute above the frame, the model box alone on the strip
#   04  the Options helper, expanded and idle
#   05  the Sessions helper, expanded
#   06  a ~350 px pane
#
# Isolated HOME / XDG_CONFIG_HOME / XDG_DATA_HOME / XDG_RUNTIME_DIR / TMPDIR under a short path
# (the 108-byte socket limit), RELAY_KEYRING=off so a run never touches the owner's real identity
# key, and **no provider account**: the profile points a local endpoint at stub-provider.py, so
# every agent in the run is that script. Needs Xvfb, xdotool, ImageMagick, tesseract.
#
# Each check writes one PASS/FAIL line to notes.txt naming the screenshot it was read from.
set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"
out=${2:-$PWD}
root=$(cd ../../.. && pwd)
# The shots here were taken with the binary land.py built from the exact tree it committed
# (/tmp/claude-1000/land/<me>/verify/build/relay): this checkout's build/relay carries other
# sessions' in-flight edits, so it is not evidence of what landed.
relay=${1:-$root/build/relay}
width=1500 height=940
port=${RELAY_QA_PORT:-8841}
narrow=${NARROW:-730}          # window width at which each of the two panes is ~355 px
mkdir -p "$out"
[[ -x $relay ]] || { echo "no relay binary at $relay"; exit 1; }

display=
for n in $(seq 150 199); do [[ -e /tmp/.X11-unix/X$n ]] || { display=:$n; break; }; done
[[ -z $display ]] && { echo "no free display"; exit 1; }

sandbox=$(mktemp -d /tmp/rl-pbx1.XXXX)
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

# A two-card board, so the list page is a list and the card page has a card.
card=$(PYTHONPATH="$root/backend" python3 - "$work" <<'FIX'
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
one = B.new_card("work", "Alpha plain card", "inbox", created="2026-09-18", rank="a",
                 request="a plain card, so the card page has something to draw")
B.write_new_card(board, one, "features")
two = B.new_card("work", "Second fixture card", "inbox", created="2026-09-19", rank="b",
                 request="a second card so the list is a list")
B.write_new_card(board, two, "features")
print(one.id.upper())
FIX
)
[[ -n $card ]] || { echo "fixture failed"; exit 1; }
echo "fixture card: #$card"

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
# There is no window manager under Xvfb, so the input focus follows the pointer: a screenshot that
# parks the pointer off the window (to keep a hover highlight out of the picture) also takes the
# keyboard away from it. So the pointer goes back where it was and the focus is set explicitly.
shot() {
    local X=0 Y=0
    eval "$(xdotool getmouselocation --shell 2>/dev/null)"
    xdotool mousemove $((width + 20)) $((height + 20)); sleep 0.6
    import -window "$win" "$out/$1.png"
    xdotool mousemove "$X" "$Y"
    xdotool windowfocus "$win" 2>/dev/null
    sleep 0.3
}
text() { tesseract "$out/$1.png" - --psm 6 2>/dev/null; }
words() {   # "word left top width height", read at 2x so the small type is legible
    convert "$out/$1.png" -scale 200% png:- 2>/dev/null \
        | tesseract - stdout --psm 11 tsv 2>/dev/null \
        | awk 'NF>=12 && $12 != "" {print $12, int($7/2), int($8/2), int($9/2), int($10/2)}'
}
word_xy() { words "$1" | awk -v want="$2" 'tolower($1) ~ tolower(want) {x=$2+$4/2; y=$3+$5/2} END {if (x) print x, y}'; }
click_at() { [[ -z ${1:-} || -z ${2:-} ]] && return 1; xdotool mousemove "$1" "$2" click 1; sleep 1.5; }
has()   { if text "$2" | grep -qi -- "$3"; then ok "$1 (\"$3\" in $2.png)"; else bad "$1: no \"$3\" in $2.png"; fi; }
# Word-level, read at 2x: the whole-page pass misses small chrome type, and "Send" has to be told
# from the placeholder's "Enter sends" — which is exactly the word that would be a false pass.
hasword()  { if [[ -n $(word_xy "$2" "$3") ]]; then ok "$1 (\"$3\" read in $2.png)"; else bad "$1: no \"$3\" in $2.png"; fi; }
hasntword(){ if [[ -n $(word_xy "$2" "$3") ]]; then bad "$1: \"$3\" is in $2.png and it should not be"
             else ok "$1 (no \"$3\" in $2.png)"; fi; }
# One full-width band of a shot, enlarged: the action row's outlined labels ("Execute (x)" in the
# agent's violet on the page's ground) are what neither whole-page pass reads reliably, and the
# row's height on the screen is the one thing that is the same in every shot here.
band() {   # name top [height]
    convert "$out/$1.png" -crop "x${3:-40}+0+$2" +repage -scale 300% png:- 2>/dev/null \
        | tesseract - stdout --psm 6 2>/dev/null
}
hasband() { if band "$2" "$4" "${5:-40}" | grep -qi -- "$3"; then ok "$1 (\"$3\" read in the band at y=$4 of $2.png)"
            else bad "$1: no \"$3\" in the band at y=$4 of $2.png"; fi; }
hasnt() { if text "$2" | grep -qi -- "$3"; then bad "$1: \"$3\" is in $2.png and it should not be"
          else ok "$1 (no \"$3\" in $2.png)"; fi; }
await() {   # shot pattern [seconds]
    local waited=0 limit=${3:-45}
    while :; do
        shot "$1"
        text "$1" | grep -qi -- "$2" && return 0
        (( waited >= limit )) && return 1
        sleep 3; waited=$((waited + 3))
    done
}
awaited() { if await "$2" "$3" "${4:-45}"; then ok "$1 (\"$3\" in $2.png)"; else bad "$1: no \"$3\" in $2.png after ${4:-45}s"; fi; }

launch() {   # a fresh window on the same profile; the phases do not share a layout
    [[ -n $relay_pid ]] && { kill "$relay_pid" 2>/dev/null; wait "$relay_pid" 2>/dev/null; sleep 2; }
    (cd "$work" && exec "$relay" --workspace "$work" --fresh) >>"$sandbox/relay.log" 2>&1 &
    relay_pid=$!
    sleep 10
    win= ; local best=0 w
    for w in $(xdotool search --pid "$relay_pid" 2>/dev/null); do
        eval "$(xdotool getwindowgeometry --shell "$w" 2>/dev/null)"
        (( WIDTH * HEIGHT > best )) && { best=$(( WIDTH * HEIGHT )); win=$w; }
    done
    [[ -z $win ]] && { echo "no Relay window"; tail -40 "$sandbox/relay.log"; return 1; }
    xdotool windowmove "$win" 0 0 windowsize "$win" $width $height
    xdotool windowfocus "$win"; sleep 4
    # The first launch asks how tools are approved, in a pane of its own. Take the recommendation.
    shot _approvals
    local yes; yes=$(word_xy _approvals "recommend")
    [[ -n $yes ]] && click_at ${yes% *} ${yes#* }
    sleep 2
}
# A helper's composer, by its placeholder: "Ask the Options helper — Enter sends, …". Alt+Q
# expands the panel and puts the cursor in the box, but a screenshot in between hands the focus
# back to the window rather than to the widget, so the box is clicked as well. The Send button
# that this used to be found by is gone — which is what the run is here to show.
focus_helper() {
    k alt+q; sleep 2
    shot "$1"
    local at; at=$(word_xy "$1" "^helper$")
    [[ -z $at ]] && at=$(word_xy "$1" "^sends,$")
    [[ -n $at ]] && click_at ${at% *} ${at#* }
    sleep 1
}

# ============================== (a) the pane's prompt box and the Switchboard agent's, side by side
launch || exit 1
k ctrl+shift+s; sleep 9
shot 01-pane-and-switchboard-idle
has "a1 the Switchboard is open beside the terminal pane" 01-pane-and-switchboard-idle "Switchboard"
has "a2 the panel's box says whose box it is" 01-pane-and-switchboard-idle "Ask the Switchboard agent"
hasnt "a3 and the help sentence that stood over it is gone" 01-pane-and-switchboard-idle "Ask about the board itself"
hasntword "a4 there is no Send button on the strip" 01-pane-and-switchboard-idle "^send$"
hasword "a5 the actions that need no typing are on the head row" 01-pane-and-switchboard-idle "^clean$"
hasword "a5 with Check beside it" 01-pane-and-switchboard-idle "^check$"

# A short conversation, so the log has a shape and the box under it can be compared with a full
# panel rather than an empty one.
at=$(word_xy 01-pane-and-switchboard-idle "^sends,$")
[[ -z $at ]] && at=$(word_xy 01-pane-and-switchboard-idle "^agent$")
click_at ${at% *} ${at#* }
t "how many cards are on this board?"; k Return
awaited "a6 the Switchboard agent answered into the panel's log" 02-switchboard-conversation "Inbox" 180
sleep 4; shot 02-switchboard-conversation
hasntword "a7 and still no Send button while a turn has run" 02-switchboard-conversation "^send$"

# ========================================================================= (b) the card page
# The list's sections come up folded and the focus is not on the list: click the INBOX header to
# unfold it, then the row under it. Searching the whole page for the card's title would find it in
# the agent's answer in the panel's log instead, which is how the first run never opened a card.
open_card() {
    local at; at=$(word_xy "$1" "^inbox$")
    [[ -z $at ]] && return 1
    click_at ${at% *} ${at#* }          # unfold INBOX, which also focuses the list
    sleep 2
    xdotool key --window "$win" Return; sleep 4
}
shot _board
open_card _board
shot 03-card-page
hasword "b1 the card page is open" 03-card-page "^board$"
hasband "b2 Plan is on the row above the box" 03-card-page "plan" 758
hasband "b3 Execute is beside it" 03-card-page "execute" 758
has "b4 the box's placeholder is the only thing inside it that talks" 03-card-page "Enter discusses"
hasntword "b5 and the card page has no Send button either" 03-card-page "^send$"

# ======================================================================= (c) the Options helper
launch || exit 1
k ctrl+shift+o; sleep 5
focus_helper 04-options-helper
has "c1 expanded, the head says which helper it is" 04-options-helper "Options helper"
has "c2 and the box names it too" 04-options-helper "Ask the Options helper"
hasnt "c3 no help sentence over an empty conversation" 04-options-helper "Ask about this pane"
hasntword "c4 and no Send button" 04-options-helper "^send$"

# ====================================================================== (d) the Sessions helper
launch || exit 1
k ctrl+shift+y; sleep 5
focus_helper 05-sessions-helper
has "d1 expanded, the head says which helper it is" 05-sessions-helper "Sessions helper"
has "d2 and the box names it too" 05-sessions-helper "Ask the Sessions helper"
hasntword "d3 and no Send button" 05-sessions-helper "^send$"

# ============================================================== (e) a ~350 px pane on the card
launch || exit 1
k ctrl+shift+s; sleep 9
shot _list
open_card _list
xdotool windowsize "$win" "$narrow" "$height"; sleep 3
xdotool windowsize "$win" "$narrow" "$height"; sleep 4
shot 06-narrow-pane
hasband "e1 at ~350 px the card page still shows Plan whole" 06-narrow-pane "plan" 758
hasband "e2 and Execute whole beside it" 06-narrow-pane "execute" 758

note ""
note "$pass passed, $fail failed"
echo "$pass passed, $fail failed — shots and notes in $out"
