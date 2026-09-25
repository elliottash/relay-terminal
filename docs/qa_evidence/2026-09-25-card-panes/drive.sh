#!/usr/bin/env bash
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# #Y2BA — several cards open at once, each in a pane of its own (owner: "allow pressing new card
# multiple times, it splits the second 'new card' pane vertically").
#
#   docs/qa_evidence/2026-09-25-card-panes/drive.sh [relay-binary] [out-dir]
#
# Drives a real Relay under Xvfb:
#
#   00  the Board opened with Ctrl+Shift+A on a three-card board
#   00b a click on the first row: on a board this wide its page opens beside the rows
#   01  Shift+Enter on a row: that card in a pane of its own beside the list, which stays a list
#   02  a second card, opened on the list and sent to its own pane with the page's ⤴ button:
#       two card panes and the list, three panes in the tab
#   03  Plan pressed in both card panes: both busy strips running at once
#   04  both turns finished
#   05  Relay quit (SIGTERM, the ordinary quit) and started again on the saved layout: both card
#       panes are back on their cards; layout.json is the saved `{"card": …}` nodes
#
# Isolated HOME / XDG dirs / TMPDIR, RELAY_KEYRING=off, and no provider account: the profile
# points a local endpoint at stub-provider.py (threaded, so two turns run at once). Needs Xvfb,
# xdotool, ImageMagick, tesseract. Each check writes a PASS/FAIL line to notes.txt.
set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"
out=${2:-$PWD}
root=$(cd ../../.. && pwd)
# The shots were taken with a binary built from a clean `git archive` of the landed tree: this
# checkout's build/relay carries other sessions' in-flight edits, so it is not evidence.
relay=${1:-$root/build/relay}
width=1500 height=940
port=${RELAY_QA_PORT:-8853}
mkdir -p "$out"
[[ -x $relay ]] || { echo "no relay binary at $relay"; exit 1; }

display=${RELAY_QA_DISPLAY:-}
if [[ -z $display ]]; then
  for n in $(seq 150 199); do [[ -e /tmp/.X11-unix/X$n ]] || { display=:$n; break; }; done
fi
[[ -z $display ]] && { echo "no free display"; exit 1; }

sandbox=$(mktemp -d /tmp/rl-y2ba.XXXX)
stub_pid= xvfb_pid= relay_pid=
cleanup() {
    local pid
    for pid in $relay_pid $stub_pid $xvfb_pid; do kill "$pid" 2>/dev/null; done
    cp "$sandbox/relay.log" "$out/relay.log" 2>/dev/null
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

# Three cards. Their text says "card pane fixture" (the stub's scene) and "slowly" (its pace).
ids=$(PYTHONPATH="$root/backend" python3 - "$work" <<'FIX'
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
out = []
for rank, (title, day) in enumerate([("Alpha first card", "2026-09-18"),
                                     ("Bravo second card", "2026-09-19"),
                                     ("Charlie third card", "2026-09-20")]):
    card = B.new_card("work", title, "inbox", created=day, rank="abc"[rank],
                      request="card pane fixture: plan this slowly, one step at a time")
    B.write_new_card(board, card, "features")
    out.append(card.id.upper())
print(" ".join(out))
FIX
)
[[ -n $ids ]] || { echo "fixture failed"; exit 1; }
echo "fixture cards: $ids"

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
[models]
; Rank 1 of the Main list is what a new pane and the Board's helper start on (#MDL1). Without it a
; machine with Claude Code installed ranks that harness first, the helper runs on it, and a card
; console refuses to be a second agent on the helper's guest (#E34S): no Plan turn ever runs.
tier/main=local:stub|stub|
[roles]
; The card consoles are helpers (13.1): on the stub too.
switchboard/preset=local:stub
switchboard/model=stub
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
# Same, with every space dropped from both sides: the list row squeezes "Alpha first card" into
# "Alpha firstcard" at some widths, and both readings are the same truth.
hasflat(){ if text "$2" | tr -d '[:space:]' | grep -qiF "$(printf %s "$3" | tr -d '[:space:]')"; then ok "$1 (\"$3\" in $2.png, spaces as read)"; else bad "$1: no \"$3\" in $2.png"; fi; }
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

count() { words "$1" | awk -v want="$2" 'tolower($1) ~ tolower(want) {n++} END {print n+0}'; }
xs() { words "$1" | awk -v want="$2" 'tolower($1) ~ tolower(want) {print int($2+$4/2) " " int($3+$5/2)}'; }

launch() {   # $1: "fresh" for a new window, anything else reopens the saved layout
    [[ -n $relay_pid ]] && { kill "$relay_pid" 2>/dev/null; wait "$relay_pid" 2>/dev/null; sleep 2; }
    if [[ ${1:-} == fresh ]]; then
        (cd "$work" && exec "$relay" --workspace "$work" --fresh) >>"$sandbox/relay.log" 2>&1 &
    else
        (cd "$work" && exec "$relay") >>"$sandbox/relay.log" 2>&1 &
    fi
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
    shot _approvals
    local yes; yes=$(word_xy _approvals "recommend")
    [[ -n $yes ]] && click_at ${yes% *} ${yes#* }
    sleep 2
}

# ================================================================ (a) one card in its own pane
launch fresh || exit 1
k ctrl+shift+a; sleep 9
shot 00-board
has "a1 the Board is open" 00-board "Board"
# A click on Alpha's row: the list takes the keyboard, and on a board this wide the row's page
# opens beside the rows — so Shift+Enter below also proves that page leaves the list with the card.
at=$(word_xy 00-board "^alpha$")
click_at ${at% *} ${at#* }
sleep 2
shot 00b-alpha-selected
k shift+Return; sleep 6
shot 01-one-card-pane
hasword "a2 Shift+Enter put a card in a pane of its own (its header says Close pane)" 01-one-card-pane "^close$"
has "a3 that pane is on the card" 01-one-card-pane "Alpha first card"
hasword "a4 the list is still a list beside it (its rows)" 01-one-card-pane "^bravo$"
[[ ${STOP_AFTER:-} == a ]] && { note "$pass passed, $fail failed (stopped after a)"; exit 0; }

# ================================== (b) a second card, sent to its own pane with the ⤴ button
at=$(word_xy 01-one-card-pane "^bravo$")
click_at ${at% *} ${at#* }; sleep 1
xdotool key --window "$win" Return; sleep 5      # opens on the list Board's own page
shot 01b-second-card-on-the-list
# The chip is small: the words() read upscales the 2x shot again (4x) and psm 11 there drops the
# word entirely, while the shot at its own 2x reads "Own" and "pane" cleanly — so this one lookup
# falls back to a TSV pass at the shot's native scale (coordinates stay 1:1 with the window).
word_xy_native() { tesseract "$out/$1.png" - --psm 11 tsv 2>/dev/null | awk -F'\t' -v want="$2" 'tolower($12) ~ tolower(want) {x=$7+$9/2; y=$8+$10/2} END {if (x) print x, y}'; }
at=$(word_xy 01b-second-card-on-the-list "^[^a-z]?own(pane)?$")   # the ⤴ glyph can read as a stray character, and at other kernings the chip's space closes into "Ownpane"
[[ -z $at ]] && at=$(word_xy_native 01b-second-card-on-the-list "^[^a-z]?own(pane)?$")
if [[ -n $at ]] || text 01b-second-card-on-the-list | grep -qiE 'own ?pane'; then
    # Even when no word box survives for the click (some runs drop the token entirely), the chip
    # still reads on the psm-4 line pass ("4 Ownpane"): that is the check; the drive falls back
    # to Shift+Enter when there is nothing to aim at.
    ok "b1 the list's open page offers \"⤴ Own pane\" (01b-second-card-on-the-list.png)"
    [[ -n $at ]] && click_at ${at% *} ${at#* } || xdotool key --window "$win" Escape shift+Return
else
    bad "b1 no \"Own pane\" button read on the list's page in 01b-second-card-on-the-list.png; Shift+Enter instead"
    xdotool key --window "$win" Escape shift+Return
fi
sleep 6
shot 02-two-card-panes
closes=$(count 02-two-card-panes "^close$")
if (( closes >= 2 )); then ok "b2 two card panes open at once ($closes \"Close pane\" headers in 02-two-card-panes.png)"
else bad "b2 expected two \"Close pane\" headers in 02-two-card-panes.png, read $closes"; fi
hasflat "b3 one is on Alpha" 02-two-card-panes "Alpha first card"
hasflat "b4 the other on Bravo" 02-two-card-panes "Bravo second card"

# ============================================================ (c) Plan in both card panes
# Each card pane's body has an "Issue" heading: click it (the page takes the keyboard), press p.
mapfile -t issues < <(xs 02-two-card-panes "^issue$")
note "issue headings read at: ${issues[*]:-none}"
planned=0
for at in "${issues[@]}"; do
    click_at ${at% *} ${at#* }; sleep 1
    xdotool key --window "$win" p; sleep 2
    planned=$((planned + 1))
done
(( planned >= 2 )) && ok "c1 Plan pressed in $planned card panes" || bad "c1 found $planned card pages to plan in"
running=0
for n in $(seq 1 10); do
    shot 03-both-planning
    # The strip's clock is "Relaying · thinking… · 4 s · Esc stops" (Pane.h's %1 · %2 s · %3): seconds
    # with an "s", never M:SS — the old M:SS grep here could never match anything the product draws.
    # The two panes sit side by side, so psm 6 merges both strips onto one line: count occurrences of
    # the strip's tail ("Esc stops"), one per running turn, not lines.
    busy=$(text 03-both-planning | grep -oiE 'esc stops' | wc -l)
    (( busy >= 2 )) && { running=1; break; }
    sleep 1
done
if (( running )); then ok "c2 both busy strips running at once (03-both-planning.png: $(text 03-both-planning | grep -iE '[0-9]+:[0-9][0-9]' | sed 's/^ *//' | tr '\n' '|'))"
else bad "c2 did not read two running turn clocks in 03-both-planning.png"; fi
awaited "c3 the plans streamed in" 04-both-planned "Read the files" 120
sleep 8; shot 04-both-planned
steps=$(text 04-both-planned | grep -ci "Read the files")
(( steps >= 2 )) && ok "c4 both card panes show their plan turn's answer ($steps in 04-both-planned.png)" \
                 || bad "c4 read the plan answer $steps time(s) in 04-both-planned.png"

# ======================================================= (d) quit and start again: both come back
kill -TERM "$relay_pid"; wait "$relay_pid" 2>/dev/null; relay_pid=; sleep 2
layout=$(grep -rl '"card"' "$XDG_CONFIG_HOME" "$XDG_DATA_HOME" 2>/dev/null | head -1)
if [[ -n $layout ]]; then
    python3 - "$layout" >"$out/layout.json" <<'PY'
import json, sys
found = []
def walk(node):
    if isinstance(node, dict):
        if "card" in node and isinstance(node["card"], dict):
            found.append({"card": node["card"]})
        for value in node.values():
            walk(value)
    elif isinstance(node, list):
        for value in node:
            walk(value)
walk(json.load(open(sys.argv[1])))
print(json.dumps(found, indent=2).replace(sys.argv[1].rsplit("/.config", 1)[0], "$HOME"))
PY
    n=$(grep -c '"id"' "$out/layout.json")
    (( n >= 2 )) && ok "d1 the saved layout holds $n card nodes (layout.json)" || bad "d1 layout.json holds $n card nodes"
else
    bad "d1 no saved layout with a card node under the profile"
fi
launch || exit 1
sleep 6
shot 05-restored
closes=$(count 05-restored "^close$")
if (( closes >= 2 )); then ok "d2 after the restart both card panes are back ($closes \"Close pane\" headers in 05-restored.png)"
else bad "d2 expected two \"Close pane\" headers in 05-restored.png, read $closes"; fi
# The panes are ~330 px wide and the title shares its row with the ⤴ Own pane and Delete chips, so
# "Bravo second card" wraps ("Bravo second" / "card") in a live pane and a restored one alike, and
# the list row truncates once the stage pill widens — the full title phrase is unreadable in either
# shot. The card page's footer path is the per-pane identifier that stays on one line, so the d3/d4
# checks read that instead: each restored pane still shows its own card's file.
has "d3 back on Alpha" 05-restored "alpha-first-card.md"
has "d4 and on Bravo" 05-restored "bravo-second-"

note ""
note "$pass passed, $fail failed"
echo "$pass passed, $fail failed — shots and notes in $out"
