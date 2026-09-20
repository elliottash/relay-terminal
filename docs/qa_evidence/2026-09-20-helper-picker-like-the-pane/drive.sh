#!/usr/bin/env bash
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# #PK5Q — the helper agent's model box is the terminal pane's model box (owner, 2026-09-20: "can
# you have the picker be the same as in the main terminal").
#
#   docs/qa_evidence/2026-09-20-helper-picker-like-the-pane/drive.sh [relay-binary] [out-dir]
#
# Drives a real Relay under Xvfb and photographs the popup of every model box in the app, so the
# lists can be read against each other:
#
#   01  the terminal pane's box, dropped open with Alt+M, beside the Switchboard
#   02  the Switchboard agent's box, dropped open with Alt+M from its composer
#   03  the two popups side by side, which is the picture the card is about
#   04  the card page's box, open over an open card
#   05  the Options helper's box, open from its panel's composer
#   …-list.png  each popup photographed as the X window it is, which is what the rows are read from
#   06  the model picker dialog, opened from a *helper's* "more models…" row
#
# Isolated HOME / XDG_CONFIG_HOME / XDG_DATA_HOME / XDG_RUNTIME_DIR / TMPDIR under a short path
# (the 108-byte socket limit) and RELAY_KEYRING=off, so a run never touches the owner's real
# identity key or his real provider keys. The catalog in the shots is made of **stub** keys:
# RELAY_<PRESET>_API_KEY is set to a word, which is all `has_stored_key` reads, and every agent in
# the run actually answers from stub-provider.py over a local endpoint. Nothing is ever sent to a
# provider. Needs Xvfb, xdotool, ImageMagick, tesseract.
#
# Each check writes one PASS/FAIL line to notes.txt naming the screenshot it was read from.
set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"
out=${2:-$PWD}
root=$(cd ../../.. && pwd)
# The shots here were taken with the binary land.py built from the exact tree it committed
# (/tmp/claude-1000/land/<me>/verify/build/relay): this checkout's build/relay carries other
# sessions' in-flight edits, so it is not evidence of what landed.
relay=${1:-/tmp/claude-1000/land/helperpicker/verify/build/relay}
width=1500 height=940
port=${RELAY_QA_PORT:-8853}
mkdir -p "$out"
[[ -x $relay ]] || { echo "no relay binary at $relay"; exit 1; }

display=
for n in $(seq 150 199); do [[ -e /tmp/.X11-unix/X$n ]] || { display=:$n; break; }; done
[[ -z $display ]] && { echo "no free display"; exit 1; }

sandbox=$(mktemp -d /tmp/rl-pk5q.XXXX)
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
# Stub keys, so the catalog in the popups is a real catalog with several providers in it. The word
# is not a key and is never spent: every turn in this run goes to the local endpoint below.
export RELAY_GLM_CODING_API_KEY=stub RELAY_KIMI_CODE_API_KEY=stub
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
# parks the pointer off the window also takes the keyboard away from it. A popup, though, closes
# the moment the pointer leaves it — so `shot` leaves the pointer alone and `shotaway` is the one
# that parks it, for the pictures with nothing open.
shot()  { import -window root "$out/$1.png"; sleep 0.3; }
shotaway() {
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
hasnt() { if text "$2" | grep -qi -- "$3"; then bad "$1: \"$3\" is in $2.png and it should not be"
          else ok "$1 (no \"$3\" in $2.png)"; fi; }
# Word-level, read at 2x: the whole-page pass loses the popup's last line and its brackets.
hasword() { if [[ -n $(word_xy "$2" "$3") ]]; then ok "$1 (\"$3\" read in $2.png)"; else bad "$1: no \"$3\" in $2.png"; fi; }

# The open popup, photographed as the window it is. A dropped-open combo box is its own X window,
# so the rows can be had without cutting anything out of the page behind them — which matters here
# because the two boxes sit over different backgrounds, and cropping by what OCR could find in the
# page was how the first runs both "differed" on a row both popups had and then "agreed" on two
# equally unreadable crops. `import -window <the popup>` is the whole list and nothing else.
popup_window() {
    local w best= area=0
    for w in $(xdotool search --pid "$relay_pid" 2>/dev/null); do
        [[ $w == "$win" ]] && continue
        unset WIDTH HEIGHT
        eval "$(xdotool getwindowgeometry --shell "$w" 2>/dev/null)"
        [[ -z ${WIDTH:-} || -z ${HEIGHT:-} ]] && continue
        # A popup is tall and narrow beside the window itself, and mapped: the widest such window
        # wins, so a tooltip or a one-row menu never stands in for the list.
        (( WIDTH >= 120 && WIDTH < 700 && HEIGHT >= 120 )) || continue
        xdotool search --onlyvisible --pid "$relay_pid" 2>/dev/null | grep -qx "$w" || continue
        (( WIDTH * HEIGHT > area )) && { area=$(( WIDTH * HEIGHT )); best=$w; }
    done
    echo "$best"
}
shotlist() {   # name — the open popup alone, for reading its rows
    local p; p=$(popup_window)
    [[ -z $p ]] && return 1
    import -window "$p" "$out/$1.png" 2>/dev/null
}
# Where the open popup is on the screen. The run has to know: the first version of this script
# photographed the *terminal pane's* popup twice and called the two lists identical, because Alt+M
# in the Switchboard's composer was falling through to the pane. A popup anchored under the box it
# was asked to open is the only proof that the right box opened.
popup_x() {
    local p; p=$(popup_window)
    [[ -z $p ]] && { echo -1; return; }
    unset X
    eval "$(xdotool getwindowgeometry --shell "$p" 2>/dev/null)"
    echo "${X:--1}"
}
leftpopup()  { local x; x=$(popup_x); if (( x >= 0 && x < 740 )); then ok "$1 (popup at x=$x, under the left pane's box)"
               else bad "$1: the open popup is at x=$x, not under the left pane's box"; fi; }
rightpopup() { local x; x=$(popup_x); if (( x >= 740 )); then ok "$1 (popup at x=$x, under the right pane's box)"
               else bad "$1: the open popup is at x=$x — that is the *terminal pane's* box, not the helper's"; fi; }
rows() {
    [[ -s "$out/$1.png" ]] || { echo "NO POPUP SHOT $1"; return; }
    convert "$out/$1.png" -scale 200% png:- 2>/dev/null \
        | tesseract - stdout --psm 6 2>/dev/null \
        | sed -e 's/[[:space:]]\+$//' -e 's/^[^A-Za-z0-9]*//' -e '/^$/d'
}

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
    shotaway _approvals
    local yes; yes=$(word_xy _approvals "recommend")
    [[ -n $yes ]] && click_at ${yes% *} ${yes#* }
    sleep 2
}
# Put the cursor in a helper's composer, by the one word only a prompt box's placeholder has:
# "sends," out of "Enter sends, a second prompt queues". Not "helper" — the Switchboard's own
# Alt+Q notice says "The helper agent is in Options, Actions, Sessions and the Switchboard", and
# `word_xy` answers with the *last* match, so clicking "helper" clicked that notice down in the
# terminal pane and left the cursor where it was. Alt+M then opened the terminal pane's box, and
# the run photographed one list twice and called the two identical.
#
# So focus is proved rather than assumed: a word is typed and read back out of the box before any
# key is pressed, and taken out again afterwards.
focus_helper() {
    k alt+q; sleep 2
    shotaway "$1"
    local at; at=$(word_xy "$1" "^sends,$")
    [[ -n $at ]] && click_at ${at% *} ${at#* }
    sleep 1
    t "zqxj"; sleep 1
    shotaway "$1-typed"
    if [[ -n $(word_xy "$1-typed" "zqxj") ]]; then
        ok "${2:-focus} (a word typed into the composer came back out of it, $1-typed.png)"
    else
        bad "${2:-focus}: what was typed did not land in the composer; see $1-typed.png"
    fi
    k ctrl+a; k BackSpace; sleep 1
}
closepopup() { k Escape; sleep 1; }

# ========================== (a) the terminal pane's model box, and the Switchboard agent's, at Alt+M
launch || exit 1
k ctrl+shift+s; sleep 9
shotaway _idle
has "a0 the Switchboard is open beside the terminal pane" _idle "Switchboard"

# The terminal pane's own box. Alt+M is the pane's key and always has been.
click_at 240 880                      # the pane's prompt box, bottom left
k alt+m; sleep 2
shot 01-pane-box
shotlist 01-pane-box-list || bad "a0b the pane's popup is not a window this run could find"
leftpopup "a0c the popup that opened is the terminal pane's own"
has "a1 the pane's popup names the Main role row" 01-pane-box "(main)"
has "a2 with the Flash role row under it" 01-pane-box "flash)"
has "a3 the per-model catalog is in the list" 01-pane-box "glm-5.3"
has "a4 'more models…' is the second-to-last row" 01-pane-box "more models"
has "a5 and the gear says customize" 01-pane-box "customize"
closepopup

# The Switchboard agent's box, from its own composer, with the same key (#PK5Q).
focus_helper _switchboard-composer "b0a the cursor is in the Switchboard agent's composer"
k alt+m; sleep 2
shot 02-switchboard-box
shotlist 02-switchboard-box-list || bad "b0b the helper's popup is not a window this run could find"
rightpopup "b0c Alt+M opened the *Switchboard agent's* box, not the terminal pane's"
has "b1 Alt+M drops the helper's box open too" 02-switchboard-box "(main)"
has "b2 the Flash role row is there" 02-switchboard-box "flash)"
has "b3 the per-model catalog is there — it never was before this card" 02-switchboard-box "glm-5.3"
has "b4 'more models…' opens the picker from a helper" 02-switchboard-box "more models"
has "b5 and the gear is the same gear" 02-switchboard-box "customize"
hasnt "b6 the old helper-only rows are gone: Follow Main" 02-switchboard-box "Follow Main"
hasnt "b7 and the Model roles gear row" 02-switchboard-box "Model roles"

# The picture the card is about: the two lists, read off the two shots.
rows 01-pane-box-list >"$out/_rows-pane.txt"
rows 02-switchboard-box-list >"$out/_rows-switchboard.txt"
# A list of four lines is a box that never filled; the comparison has to fail on that, not pass.
if (( $(wc -l <"$out/_rows-pane.txt") < 10 )); then
    bad "c1 the pane's popup read as only $(wc -l <"$out/_rows-pane.txt") rows; see _rows-pane.txt"
elif diff "$out/_rows-pane.txt" "$out/_rows-switchboard.txt" >"$out/_rows.diff" 2>&1; then
    # An empty file is the answer, but it does not read as one: say so in it.
    echo "# no difference: the two lists are the same rows, in the same order" >"$out/_rows.diff"
    ok "c1 the two popups are the same $(wc -l <"$out/_rows-pane.txt") rows (01-pane-box-list.png against 02-switchboard-box-list.png)"
else
    bad "c1 the two popups differ; see _rows.diff"
fi
# One picture of both, so a reader does not have to flip between two files.
convert "$out/01-pane-box-list.png" -bordercolor gray40 -border 2 "$sandbox/left.png" 2>/dev/null
convert "$out/02-switchboard-box-list.png" -bordercolor gray40 -border 2 "$sandbox/right.png" 2>/dev/null
convert "$sandbox/left.png" "$sandbox/right.png" -background black +append "$out/03-two-popups-side-by-side.png" 2>/dev/null
[[ -s "$out/03-two-popups-side-by-side.png" ]] && ok "c2 both popups in one picture (03-two-popups-side-by-side.png)"
closepopup

# ======================================================================== (d) the card page's box
open_card() {
    local at; at=$(word_xy "$1" "^inbox$")
    [[ -z $at ]] && return 1
    click_at ${at% *} ${at#* }          # unfold INBOX, which also focuses the list
    sleep 2
    xdotool key --window "$win" Return; sleep 4
}
shotaway _board
open_card _board
shotaway _card-page
at=$(word_xy _card-page "discusses")    # the reply box's placeholder
[[ -n $at ]] && click_at ${at% *} ${at#* }
k alt+m; sleep 2
shot 04-card-page-box
shotlist 04-card-page-box-list
rightpopup "d0 Alt+M in the card's reply box opened the card page's own box"
has "d1 the card page's box carries the same rows" 04-card-page-box "(main)"
has "d2 with the catalog" 04-card-page-box "glm-5.3"
hasword "d3 and 'more models…'" 04-card-page-box "^models"
closepopup

# ============================================== (e) an embedded helper panel's box (Options)
launch || exit 1
k ctrl+shift+o; sleep 5
focus_helper _options-composer "e0a the cursor is in the Options helper's composer"
k alt+m; sleep 2
shot 05-options-helper-box
shotlist 05-options-helper-box-list
rightpopup "e0 Alt+M in the Options helper's composer opened that panel's box"
has "e1 the Options helper's box is the same list" 05-options-helper-box "(main)"
hasword "e2 with the catalog" 05-options-helper-box "^glm"
hasword "e3 and 'more models…'" 05-options-helper-box "^models"
closepopup

# ================================= (f) the model picker dialog, opened from a helper's own row
# Ctrl+Alt+M from the helper's composer is the key the pane uses for the same dialog; the
# "more models…" row is the mouse path to it. Both end in relay::ModelPicker.
click_at $(word_xy _options-composer "^sends,$" 2>/dev/null || echo "400 880")
k ctrl+alt+m; sleep 3
shot 06-picker-from-helper
has "f1 the picker dialog opened over the helper" 06-picker-from-helper "Filter\|filter\|Sort\|sort"
hasword "f2 it is the catalog, with the models in it" 06-picker-from-helper "glm"
has "f3 and the reasoning level beside the model" 06-picker-from-helper "Reasoning\|reasoning\|effort"
k Escape; sleep 1

note ""
note "$pass passed, $fail failed"
echo "$pass passed, $fail failed — shots and notes in $out"
