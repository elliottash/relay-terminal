#!/usr/bin/env bash
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# #PBX1 — the action row above a prompt box is left-aligned buttons and nothing else (owner,
# 2026-09-20: "the plan / execute buttons etc, would those work better at the left? (in the
# switchboard card agent)" — "yes, lets do both left-aligned, drop the label").
#
#   docs/qa_evidence/2026-09-20-action-rows-left/drive.sh [relay-binary] [out-dir]
#
# Drives a real Relay under Xvfb and photographs the two rows the decision is about:
#
#   01  the Switchboard beside a terminal pane, idle: Check and Clean up at the head row's left,
#       and no "Switchboard agent" label in front of them
#   02  the same panel with a short conversation, and the busy strip caught mid-turn — where the
#       name, the turn clock and the survey word live now
#   03  the card page: Plan / Execute above the frame, at its left
#   04  the card page in a ~350 px pane, where the row is narrowest
#
# Each shot is read twice: once for the words, once for their **x positions**, because
# "left-aligned" is a claim about geometry and a screenshot that merely contains the word "Plan"
# would pass either way. The row check is "nothing on this button's own line starts left of it".
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

sandbox=$(mktemp -d /tmp/rl-rows.XXXX)
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
# The left edge of a word, and of everything that shares its line. "Left-aligned buttons and
# nothing else" is exactly "no word on this row begins left of this button", which is what a label
# in front of the buttons — the one the owner asked to drop — would break.
word_left() { words "$1" | awk -v want="$2" 'tolower($1) ~ tolower(want) {print $2; exit}'; }
row_words() {   # shot word: every word sharing that word's line, left to right, as "x:word"
    local at; at=$(word_xy "$1" "$2"); [[ -z $at ]] && return 1
    words "$1" | awk -v y="${at#* }" '$3 + $5/2 > y - 9 && $3 + $5/2 < y + 9 {print $2 ":" $1}' | sort -n -t: -k1
}
leftmost() {   # name shot word: PASS when nothing on that word's row starts left of it
    local at; at=$(word_xy "$2" "$3")
    if [[ -z $at ]]; then bad "$1: no \"$3\" in $2.png"; return; fi
    local mine; mine=$(word_left "$2" "$3")
    local first; first=$(row_words "$2" "$3" | head -1)
    local x=${first%%:*}
    if (( x >= mine - 6 )); then ok "$1 (\"$3\" starts its row in $2.png at x=$mine; row: $(row_words "$2" "$3" | tr '\n' ' '))"
    else bad "$1: \"${first#*:}\" is at x=$x, left of \"$3\" at x=$mine in $2.png"; fi
}
# The card page's action row, read as a band. Neither whole-page pass reads "Execute (x)" — a
# violet outline in violet type on the page's ground — so the band is cropped, its saturation
# tripled and then flattened to grey, which is what makes those strokes black on white. The band
# is placed from the reply box's own placeholder, 68 px under the row, so it follows the layout
# rather than a hard-coded y.
buttonrow() {   # shot: "x:word" for every word on the action row, left to right
    local at y; at=$(word_xy "$1" "^reply$"); [[ -z $at ]] && return 1
    y=${at#* }; y=${y%.*}            # word_xy centres a word, so its y can be a half pixel
    convert "$out/$1.png" -crop "x46+0+$(( y - 68 ))" +repage -scale 400% \
            -modulate 100,300,100 -colorspace gray -auto-level png:- 2>/dev/null \
        | tesseract - stdout --psm 6 tsv 2>/dev/null \
        | awk 'NR > 1 && NF >= 12 && $12 != "" && $12 != "text" {print int($7/4) ":" $12}' | sort -n -t: -k1
}
rowx() { buttonrow "$1" | awk -F: -v want="$2" 'tolower($2) ~ tolower(want) {print $1; exit}'; }
inrow() {   # name shot word
    local x; x=$(rowx "$2" "$3")
    if [[ -n $x ]]; then ok "$1 (\"$3\" read at x=$x on the action row of $2.png)"
    else bad "$1: no \"$3\" on the action row of $2.png (row: $(buttonrow "$2" | tr '\n' ' '))"; fi
}
roworder() {   # name shot first second
    local a b; a=$(rowx "$2" "$3"); b=$(rowx "$2" "$4")
    if [[ -z $a || -z $b ]]; then bad "$1: \"$3\" or \"$4\" not on the action row of $2.png"; return; fi
    if (( a < b )); then ok "$1 (\"$3\" at x=$a is left of \"$4\" at x=$b in $2.png)"
    else bad "$1: \"$3\" at x=$a is not left of \"$4\" at x=$b in $2.png"; fi
}
rowfirst() {   # name shot word: PASS when nothing else on the row starts left of it
    local mine first; mine=$(rowx "$2" "$3"); first=$(buttonrow "$2" | head -1)
    if [[ -z $mine ]]; then bad "$1: no \"$3\" on the action row of $2.png"; return; fi
    if (( ${first%%:*} >= mine - 6 )); then
        ok "$1 (\"$3\" starts the action row of $2.png at x=$mine; row: $(buttonrow "$2" | tr '\n' ' '))"
    else bad "$1: \"${first#*:}\" at x=${first%%:*} is left of \"$3\" at x=$mine in $2.png"; fi
}
rowmargin() {   # name shot word margin-word: the row's first button against the page's left margin
    local a b; a=$(rowx "$2" "$3"); b=$(word_left "$2" "$4")
    if [[ -z $a || -z $b ]]; then bad "$1: \"$3\" or \"$4\" not read in $2.png"; return; fi
    local d=$(( a - b )); (( d < 0 )) && d=$(( -d ))
    if (( d <= 40 )); then ok "$1 (\"$3\" at x=$a, the card page's left margin at x=$b in $2.png)"
    else bad "$1: \"$3\" at x=$a is $d px from the margin at x=$b in $2.png"; fi
}
order() {   # name shot first second: PASS when the first word is left of the second
    local a b; a=$(word_left "$2" "$3"); b=$(word_left "$2" "$4")
    if [[ -z $a || -z $b ]]; then bad "$1: \"$3\" or \"$4\" not read in $2.png"; return; fi
    if (( a < b )); then ok "$1 (\"$3\" at x=$a is left of \"$4\" at x=$b in $2.png)"
    else bad "$1: \"$3\" at x=$a is not left of \"$4\" at x=$b in $2.png"; fi
}
near_left() {   # name shot word edge-word: PASS when the button starts within 80 px of the pane's
                # own left margin, read off another thing that sits on it
    local a b; a=$(word_left "$2" "$3"); b=$(word_left "$2" "$4")
    if [[ -z $a || -z $b ]]; then bad "$1: \"$3\" or \"$4\" not read in $2.png"; return; fi
    local d=$(( a - b )); (( d < 0 )) && d=$(( -d ))
    if (( d <= 80 )); then ok "$1 (\"$3\" at x=$a, the pane's left margin at x=$b in $2.png)"
    else bad "$1: \"$3\" at x=$a is $d px from the margin at x=$b in $2.png"; fi
}
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


# ============================ (a) the Switchboard's head row, beside a terminal pane
launch || exit 1
k ctrl+shift+s; sleep 9
shot 01-switchboard-idle
has "a1 the Switchboard is open beside the terminal pane" 01-switchboard-idle "Switchboard"
hasword "a2 Check is on the head row" 01-switchboard-idle "^check$"
hasword "a3 Clean up beside it" 01-switchboard-idle "^clean$"
# The rule itself. INBOX is the list's section header, which sits on the pane's own left margin,
# so it says where "the left" is in a pane that starts half way across the window.
near_left "a4 Check starts at the panel's left margin" 01-switchboard-idle "^check$" "^inbox$"
order    "a5 Check comes before Clean up" 01-switchboard-idle "^check$" "^clean$"
leftmost "a6 nothing sits left of Check on its row — the name label is gone" 01-switchboard-idle "^check$"
# Said the other way round, because this is the thing that was dropped: the words "Switchboard
# agent" are still in the box's placeholder, so the shot has them; what it must not have is them
# on the buttons' line, which `leftmost` above is the general form of.
if row_words 01-switchboard-idle "^check$" | grep -qi "agent"; then
    bad "a7 the \"Switchboard agent\" label is off the head row: \"agent\" is still on Check's line"
else
    ok "a7 the \"Switchboard agent\" label is off the head row (no \"agent\" on Check's line)"
fi
has "a8 the box's placeholder is what names the agent now" 01-switchboard-idle "Ask the Switchboard agent"

# A turn, photographed while it runs: the name, the clock and (on a survey turn) the survey word
# are on the busy strip inside the box now, not on the head row.
at=$(word_xy 01-switchboard-idle "^sends,$")
[[ -z $at ]] && at=$(word_xy 01-switchboard-idle "^agent$")
click_at ${at% *} ${at#* }
t "how many cards are on this board? answer slowly"; k Return
running=0
for n in $(seq 1 14); do
    shot 02-turn-running
    if text 02-turn-running | grep -qiE "switchboard agent.{0,4}[0-9]+:[0-9][0-9]"; then running=1; break; fi
    sleep 1
done
if (( running )); then
    ok "b1 the busy strip carries the agent's name and the turn clock (02-turn-running.png: $(text 02-turn-running | grep -iE 'switchboard agent.{0,4}[0-9]+:[0-9][0-9]' | head -1 | sed 's/^ *//'))"
else
    bad "b1 no \"Switchboard agent · m:ss\" strip was caught in 02-turn-running.png"
fi
awaited "b2 the Switchboard agent answered into the panel's log" 03-switchboard-conversation "Inbox" 180
sleep 4; shot 03-switchboard-conversation
leftmost "b3 and the head row is still buttons at the left after a turn" 03-switchboard-conversation "^check$"

# ========================================================================= (c) the card page
# The list's sections come up folded and the focus is not on the list: click the INBOX header to
# unfold it, then the row under it.
open_card() {
    local at; at=$(word_xy "$1" "^inbox$")
    [[ -z $at ]] && return 1
    click_at ${at% *} ${at#* }          # unfold INBOX, which also focuses the list
    sleep 2
    xdotool key --window "$win" Return; sleep 4
}
shot _board
open_card _board
shot 04-card-page
inrow    "c1 Plan is on the row above the box" 04-card-page "^plan$"
inrow    "c2 Execute is beside it" 04-card-page "^execute$"
roworder "c3 Plan comes before Execute" 04-card-page "^plan$" "^execute$"
rowfirst "c4 nothing sits left of Plan on its row" 04-card-page "^plan$"
# The card page's own left margin: the body's "Issue" heading is on it.
rowmargin "c5 Plan starts at the card page's left margin" 04-card-page "^plan$" "^issue$"
has "c6 the box under the row still says what Enter does" 04-card-page "Enter discusses"

# ============================================================== (d) a ~350 px card page
launch || exit 1
k ctrl+shift+s; sleep 9
shot _list
open_card _list
xdotool windowsize "$win" "$narrow" "$height"; sleep 3
xdotool windowsize "$win" "$narrow" "$height"; sleep 4
shot 05-narrow-card-page
inrow    "d1 at ~350 px Plan is still whole" 05-narrow-card-page "^plan$"
inrow    "d2 and Execute beside it" 05-narrow-card-page "^execute$"
roworder "d3 in that order" 05-narrow-card-page "^plan$" "^execute$"
rowfirst "d4 and still at the row's left" 05-narrow-card-page "^plan$"
rowmargin "d5 at the card page's left margin there too" 05-narrow-card-page "^plan$" "^issue$"

note ""
note "$pass passed, $fail failed"
echo "$pass passed, $fail failed — shots and notes in $out"
