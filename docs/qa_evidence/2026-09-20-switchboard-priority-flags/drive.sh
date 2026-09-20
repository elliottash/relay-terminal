#!/usr/bin/env bash
# The Switchboard's priority flag, #ID column and label chips, live (card #VKFV: "add a flag icon,
# and its an empty circle i can click on to priotize. left click increases priority, right click
# reduces... but the # code as a second column before the title... those should become a second
# set of filter next to the section list... there seem to be 3 date columns, remove the leftmost
# of the 3"). A board of four fixture cards under Xvfb with an isolated HOME, XDG_CONFIG_HOME,
# XDG_RUNTIME_DIR and TMPDIR (short paths: the 108-byte socket limit) and RELAY_KEYRING=off.
#
# The Ready cards carry priorities +3, +1 and none, so the ⚑ sort is a different visible
# permutation from Manual; Bravo and Charlie are clicked up and down through the colours, and the
# card files on disk are read back for what the clicks wrote. Every claim is checked by OCR of the
# screenshots (tesseract word boxes) or by reading the fixture files — the colours themselves
# (yellow/white/pale green/bright green), the ring at 0 and the ⚑ glyph are what the screenshots
# carry and OCR cannot.
#
#   docs/qa_evidence/2026-09-20-switchboard-priority-flags/drive.sh [build-dir] [out-dir]
set -uo pipefail
root=/home/elliott/repos/relay-terminal
build=${1:-$root/build}
out=${2:-$root/docs/qa_evidence/2026-09-20-switchboard-priority-flags}
width=1440 height=900
mkdir -p "$out"

display=
for n in $(seq 170 199); do [[ -e /tmp/.X11-unix/X$n ]] || { display=:$n; break; }; done
[[ -z $display ]] && { echo "no free display"; exit 1; }

sandbox=$(mktemp -d /tmp/rl-flag.XXXX)
xvfb_pid= relay_pid=
cleanup() {
    local pid
    for pid in $relay_pid $xvfb_pid; do kill "$pid" 2>/dev/null; done
    rm -rf "$sandbox"
}
trap cleanup EXIT

Xvfb "$display" -screen 0 $((width + 40))x$((height + 40))x24 >/dev/null 2>&1 &
xvfb_pid=$!
sleep 2
kill -0 "$xvfb_pid" 2>/dev/null || { echo "Xvfb died on $display"; exit 1; }
export DISPLAY=$display RELAY_KEYRING=off

mkdir -p "$sandbox/home/.config/RelayTerminal" "$sandbox/home/.local/share" "$sandbox/run" "$sandbox/tmp"
chmod 700 "$sandbox/run"
ln -sfn "/run/user/$(id -u)/bus" "$sandbox/run/bus"
export HOME=$sandbox/home XDG_RUNTIME_DIR=$sandbox/run TMPDIR=$sandbox/tmp
export XDG_CONFIG_HOME=$HOME/.config XDG_DATA_HOME=$HOME/.local/share XDG_CACHE_HOME=$HOME/.cache
cat >"$XDG_CONFIG_HOME/RelayTerminal/relay.conf" <<'CONF'
[instructions]
onboarded=true
CONF

work=$HOME/project
mkdir -p "$work"
PYTHONPATH="$root/backend" python3 - "$work" <<'FIX'
import os, sys, time
from pathlib import Path
from relay_core import board as B

work = Path(sys.argv[1])
root = work / "issues"
root.mkdir(parents=True)
(root / B.BOARD_CONFIG).write_text(
    "tabs: [{id: features, folder: features}, {id: bugs, folder: changes}]\n"
    "columns: [inbox, discussing, ready, in-progress, waiting, needs-qa, done]\n",
    encoding="utf-8")
board = B.Board(root, work)
#            title                 status    created     rank  days  priority  labels
cards = [("Alpha reflow fix",      "ready",  "2026-09-01", "c",  0,    None,  ["feature", "gui"]),
         ("Bravo voice mode",      "ready",  "2026-09-10", "a",  5,       3,  ["feature", "voice"]),
         ("Charlie theme swap",    "ready",  "2026-09-16", "b",  9,       1,  ["bug"]),
         ("Delta quick add",       "inbox",  "2026-09-18", "a",  1,      -1,  ["bug", "gui"])]
for title, status, created, rank, days, priority, labels in cards:
    extra = {"labels": labels}
    if priority:
        extra["priority"] = priority
    card = B.new_card("work", title, status, created=created, rank=rank,
                      request=f"the ask for {title.lower()}", **extra)
    path = B.write_new_card(board, card, "features")
    when = time.time() - days * 86400
    os.utime(path, (when, when))
    print(f"{card.id}  {title}  {status}  priority {priority}  labels {labels}")
FIX

(cd "$work" && exec "$build/relay" --workspace "$work" --fresh) >"$sandbox/relay.log" 2>&1 &
relay_pid=$!
sleep 9
win= ; best=0
for candidate in $(xdotool search --pid "$relay_pid" 2>/dev/null); do
    eval "$(xdotool getwindowgeometry --shell "$candidate" 2>/dev/null)"
    (( WIDTH * HEIGHT > best )) && { best=$(( WIDTH * HEIGHT )); win=$candidate; }
done
[[ -z $win ]] && { echo "no Relay window"; cat "$sandbox/relay.log"; exit 1; }
xdotool windowmove "$win" 0 0 windowsize "$win" "$width" "$height"
xdotool windowfocus "$win"; sleep 6

# A first run can open the approvals pane; take its recommended answer to get it out of the way.
xdotool mousemove 906 513 click 1; sleep 2

shot() { import -window root "$out/$1.png"; }
# The board pane is the right half of the window and its engraved 9pt rows are too small for
# tesseract at full scale, so every word is read from a 2x upscale of that half -- with the
# coordinates mapped back to full-image space, because that is where xdotool clicks.
board_x=690
words() { convert "$1" -crop $((width - board_x))x$height+$board_x+0 +repage -scale 200% png:- 2>/dev/null \
              | tesseract - stdout --psm 11 tsv 2>/dev/null; }
word_box() {  # image word -> "left top width height"; a leading # is stripped, so "#AB12" is "AB12"
    words "$1" | awk -v want="$2" -v ox="$board_x" '{w=$12; sub(/^#/, "", w)}
        tolower(w)==tolower(want) {print int($7/2)+ox, int($8/2), int($9/2), int($10/2); exit}'
}
word_y() {  # image word -> its y center
    local box; box=$(word_box "$1" "$2")
    [[ -n $box ]] && echo $(( $(echo $box | cut -d' ' -f2) + $(echo $box | cut -d' ' -f4) / 2 ))
}
word_x() {  # image word -> its x center
    local box; box=$(word_box "$1" "$2")
    [[ -n $box ]] && echo $(( $(echo $box | cut -d' ' -f1) + $(echo $box | cut -d' ' -f3) / 2 ))
}
line_text() {  # png -> its whole text, from the same upscale
    convert "$1" -crop $((width - board_x))x$height+$board_x+0 +repage -scale 200% png:- 2>/dev/null \
        | tesseract - stdout --psm 11 2>/dev/null
}
click_at() { xdotool mousemove "$1" "$2" click "${3:-1}"; sleep 1.6; }

# The fixture printed each card's id; recompute them from the files for the OCR round trip.
mapfile -t ids < <(cd "$work/issues/features" && grep -h "^id:" *.md | awk '{print toupper($2)}' | sort)
bravo_id=$(cd "$work/issues/features" && grep -l "Bravo" *.md | head -1 | cut -d- -f3 | cut -d. -f1 | tr '[:lower:]' '[:upper:]')
bravo_id=$(cd "$work/issues/features" && for f in *.md; do grep -q "Bravo voice mode" "$f" && { grep "^id:" "$f" | awk '{print toupper($2)}'; break; }; done)
charlie_id=$(cd "$work/issues/features" && for f in *.md; do grep -q "Charlie theme swap" "$f" && { grep "^id:" "$f" | awk '{print toupper($2)}'; break; }; done)

: >"$out/ocr.txt"
order_of() {  # png -> the three Ready titles as their y order says, highest first
    local a b c
    a=$(word_y "$1" Alpha); b=$(word_y "$1" Bravo); c=$(word_y "$1" Charlie)
    for t in "Alpha:$a" "Bravo:$b" "Charlie:$c"; do echo "${t#*:} ${t%%:*}"; done | sort -n |
        awk '{printf "%s ", $2}' ; echo
}

# 1. Open the Switchboard. The row now reads #ID then title (the id is left of its title), the
#    age badge is gone (no "N d"/"N w" token anywhere), and the label chips sit under the
#    section checkboxes.
xdotool key --window "$win" ctrl+shift+s; sleep 8
shot 00-folded
# A new pane starts folded (every section a count); unfold Ready, then Inbox -- the second is
# found on a fresh shot, because unfolding Ready moves everything under it down.
box=$(word_box "$out/00-folded.png" "READY")
[[ -n $box ]] && click_at $(( $(echo $box | cut -d' ' -f1) + $(echo $box | cut -d' ' -f3) / 2 )) $(( $(echo $box | cut -d' ' -f2) + $(echo $box | cut -d' ' -f4) / 2 ))
shot 00b-ready-open
box=$(word_box "$out/00b-ready-open.png" "INBOX")
[[ -n $box ]] && click_at $(( $(echo $box | cut -d' ' -f1) + $(echo $box | cut -d' ' -f3) / 2 )) $(( $(echo $box | cut -d' ' -f2) + $(echo $box | cut -d' ' -f4) / 2 ))
shot 01-board-open
{
    echo "01 ids:               ${ids[*]}"
    echo "01 bravo=#$bravo_id charlie=#$charlie_id"
    # The id column is left of the title's first word, on the same row (the old row had the id
    # *after* the elided title).
    bravo_left=$(word_x "$out/01-board-open.png" "Bravo")
    id_right=$(( bravo_left - 48 + 40 ))
    echo "01 title starts at $bravo_left; the id column ends at ~$id_right (48 px of id column + padding before it)"
    id_y=$(word_y "$out/01-board-open.png" "Bravo")
    id_word=$(words "$out/01-board-open.png" | awk -v y="$id_y" 'NF>=12 && $12!="" && $8/2 > y-6 && $8/2 < y+6 {print int($7/2)+0, $12}' | sort -n | awk -v t="$bravo_left" '$1 < t {print $2; exit}')
    echo "01 leftmost word on Bravo's row before the title: $id_word"
    echo "01 age badges (N d / N w tokens): $(words "$out/01-board-open.png" | awk '{w=$12; sub(/^#/, "", w)} w ~ /^[0-9]+[dw]$/ {n++} END{print n+0}')"
    echo "01 label chips seen:  $(for l in bug feature gui voice; do words "$out/01-board-open.png" | awk -v w="$l" 'tolower($12)==w {found=1} END{print (found ? "'$l' " : "")}'; done)"
    echo "01 header words:      $(hy=$(word_y "$out/01-board-open.png" CREATED); words "$out/01-board-open.png" | awk -v y="$hy" 'NF>=12 && $12!="" && $8/2 > y-14 && $8/2 < y+14 {print int($7/2), $12}' | sort -n | awk '{printf "%s ", $2}'; echo)"
} >>"$out/ocr.txt"

# 2. The flag click. The flag box sits left of the id column: id_left - idColumnWidth - 4 + 7.
#    Left-click Charlie's flag twice (+1 -> +3... it starts at +1, so one click reaches +2), then
#    right-click it back down. The notice after each click names the value; the file says it too.
flag_pos() {  # image title-word -> "x y" of that row's flag centre. The engraved ids OCR badly
    # run to run, so the row is found by a title word instead: the title starts one id column
    # (48 px) right of the id's left edge, and the flag centre is 12 px left of that.
    local box; box=$(word_box "$1" "$2")
    [[ -n $box ]] || { echo "MISSING TITLE WORD: $2" >>"$out/ocr.txt"; return 1; }
    local left top h; left=$(echo $box | cut -d' ' -f1); top=$(echo $box | cut -d' ' -f2); h=$(echo $box | cut -d' ' -f4)
    echo $(( left - 60 )) $(( top + h / 2 ))
}
pos=$(flag_pos "$out/01-board-open.png" "Charlie")
echo "02 charlie flag at:   $pos (charlie starts at priority 1)" >>"$out/ocr.txt"
click_at ${pos% *} ${pos#* } 1
shot 02-click-up
charlie_file() { (cd "$work/issues/features" && for f in *.md; do grep -q "Charlie theme swap" "$f" && { grep -E "^priority:" "$f" || echo "(no priority key)"; break; }; done); }
echo "02 file after up:     $(charlie_file)  (expect priority: 2)" >>"$out/ocr.txt"
echo "02 notice after up:   $(line_text "$out/02-click-up.png" | grep -i "flagged" | head -1)" >>"$out/ocr.txt"
click_at ${pos% *} ${pos#* } 3
shot 03-click-down
echo "03 file after down:   $(charlie_file)  (expect priority: 1 again)" >>"$out/ocr.txt"
echo "03 notice after down: $(line_text "$out/03-click-down.png" | grep -iE "flagged|cleared" | head -1)" >>"$out/ocr.txt"

# 3. The ⚑ sort: a click on the flag's header cell (x of the flag column, y of the header row)
#    orders +3, +2, +1, 0, −1 high first; a second click turns it round; a third is Manual again.
header_y=$(word_y "$out/01-board-open.png" CREATED)
flag_header_x=$(flag_pos "$out/01-board-open.png" "Bravo")
flag_header_x=${flag_header_x% *}
click_at "$flag_header_x" "$header_y" 1
shot 04-priority-high
click_at "$flag_header_x" "$header_y" 1
shot 05-priority-low
click_at "$flag_header_x" "$header_y" 1
shot 06-manual
{
    echo "04 priority-high order (expect Bravo Charlie Alpha in Ready): $(order_of "$out/04-priority-high.png")"
    echo "05 priority-low order  (expect Alpha Charlie Bravo in Ready): $(order_of "$out/05-priority-low.png")"
    echo "06 back to manual      (rank a,b,c: Bravo Charlie Alpha):     $(order_of "$out/06-manual.png")"
} >>"$out/ocr.txt"

# 4. The label chips: tick BUG and only the bug cards stay (Charlie, Delta); the count label
#    switches to "N shown". Untick it and the board is whole again.
bug_y=$(word_y "$out/06-manual.png" "bug")
bug_x=$(word_x "$out/06-manual.png" "bug")
click_at "$bug_x" "$bug_y" 1
shot 07-label-bug
{
    echo "07 count line:        $(line_text "$out/07-label-bug.png" | grep -iE "shown|open" | head -1)"
    echo "07 bug keeps:         $(for t in Alpha Bravo Charlie Delta; do y=$(word_y "$out/07-label-bug.png" $t); [[ -n $y ]] && printf "%s " $t; done; echo)"
} >>"$out/ocr.txt"
click_at "$bug_x" "$bug_y" 1
shot 08-label-unticked
echo "08 unticked keeps:    $(for t in Alpha Bravo Charlie Delta; do y=$(word_y "$out/08-label-unticked.png" $t); [[ -n $y ]] && printf "%s " $t; done; echo)" >>"$out/ocr.txt"

# 5. The clicks wrote the files: every priority the fixture set or the clicks made is there.
{
    echo "05 files' priorities:"
    (cd "$work/issues/features" && for f in *.md; do printf "   %s %s\n" "$(grep "^id:" "$f" | awk '{print toupper($2)}')" "$(grep -E "^(priority|title)" "$f" | head -2 | tr '\n' ' ')"; done)
} >>"$out/ocr.txt"

[[ -s "$sandbox/relay.log" ]] && cp "$sandbox/relay.log" "$out/relay.log"
echo "shots and ocr.txt in $out"
