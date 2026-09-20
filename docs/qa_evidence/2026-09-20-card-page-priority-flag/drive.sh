#!/usr/bin/env bash
# The priority flag on the card page, live (card #DPJB: "allow priority flag shifts in the card
# page. and allow agents to set priority as well."). The row's flag has been clickable since
# #VKFV; this run drives the one the card *detail* grew — the same ring/disc at the head of the
# card page's header — and reads the card file back for what each click wrote.
#
# A board of three fixture cards under Xvfb with an isolated HOME, XDG_CONFIG_HOME, XDG_RUNTIME_DIR
# and TMPDIR (short paths: the 108-byte socket limit) and RELAY_KEYRING=off. Bravo starts at +2 so
# the page's first left click is +3 and the clamp is visible; Alpha starts unflagged, so the ring
# at 0 and the first click's +1 are the ordinary case.
#
#   docs/qa_evidence/2026-09-20-card-page-priority-flag/drive.sh [build-dir] [out-dir]
set -uo pipefail
root=/home/elliott/repos/relay-terminal
build=${1:-$root/build}
out=${2:-$root/docs/qa_evidence/2026-09-20-card-page-priority-flag}
width=1440 height=900
mkdir -p "$out"

display=
for n in $(seq 170 199); do [[ -e /tmp/.X11-unix/X$n ]] || { display=:$n; break; }; done
[[ -z $display ]] && { echo "no free display"; exit 1; }

sandbox=$(mktemp -d /tmp/rl-cpflag.XXXX)
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
import sys
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
#            title               status    created     rank  priority  labels
cards = [("Alpha reflow fix",   "ready",  "2026-09-01", "a",    None,  ["feature", "gui"]),
         ("Bravo voice mode",   "ready",  "2026-09-10", "b",       2,  ["feature", "voice"]),
         ("Charlie theme swap", "inbox",  "2026-09-16", "c",      -1,  ["bug"])]
for title, status, created, rank, priority, labels in cards:
    extra = {"labels": labels}
    if priority:
        extra["priority"] = priority
    card = B.new_card("work", title, status, created=created, rank=rank,
                      request=f"the ask for {title.lower()}", **extra)
    path = B.write_new_card(board, card, "features")
    print(f"{card.id}  {title}  {status}  priority {priority}")
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
# The board pane is the right half of the window; its engraved 9pt rows are too small for
# tesseract at full scale, so words are read from a 2x upscale with the boxes mapped back into
# full-image space, which is where xdotool clicks.
board_x=690
words() { convert "$1" -crop $((width - board_x))x$height+$board_x+0 +repage -scale 200% png:- 2>/dev/null \
              | tesseract - stdout --psm 11 tsv 2>/dev/null; }
word_box() {  # image word -> "left top width height"; a leading # is stripped, so "#AB12" is "AB12"
    words "$1" | awk -v want="$2" -v ox="$board_x" '{w=$12; sub(/^#/, "", w)}
        tolower(w)==tolower(want) {print int($7/2)+ox, int($8/2), int($9/2), int($10/2); exit}'
}
word_x() { local b; b=$(word_box "$1" "$2"); [[ -n $b ]] && echo $(( $(echo $b | cut -d' ' -f1) + $(echo $b | cut -d' ' -f3) / 2 )); }
word_y() { local b; b=$(word_box "$1" "$2"); [[ -n $b ]] && echo $(( $(echo $b | cut -d' ' -f2) + $(echo $b | cut -d' ' -f4) / 2 )); }
word_left() { local b; b=$(word_box "$1" "$2"); [[ -n $b ]] && echo $(( $(echo $b | cut -d' ' -f1) )); }
# The card page's `#ID` label. The engraved id OCRs badly run to run ("#xX952" for #X9S2), so the
# label is found by position instead: the leftmost word starting with `#` on the `#ID` button's own
# row — that button's label is stable, and nothing else on that row begins with a hash.
ref_box() {  # png -> "left top width height" of the card page's ref label
    local row
    row=$(words "$1" | awk -v ox="$board_x" '$12=="#ID" {print int($8/2); exit}')
    words "$1" | awk -v ox="$board_x" -v y="${row:-0}" '
        NF>=12 && substr($12,1,1)=="#" && $12!="#ID" && int($8/2)>y-14 && int($8/2)<y+14 {
            x=int($7/2)+ox; if (best=="" || x<best) {best=x; top=int($8/2); h=int($10/2)} }
        END{if (best!="") print best, top, 0, h}'
}
ref_left() { local b; b=$(ref_box "$1"); [[ -n $b ]] && echo "$(echo $b | cut -d' ' -f1)"; }
ref_y() { local b; b=$(ref_box "$1"); [[ -n $b ]] && echo $(( $(echo $b | cut -d' ' -f2) + $(echo $b | cut -d' ' -f4) / 2 )); }
line_text() { convert "$1" -crop $((width - board_x))x$height+$board_x+0 +repage -scale 200% png:- 2>/dev/null \
                  | tesseract - stdout --psm 11 2>/dev/null; }
click_at() { xdotool mousemove "$1" "$2" click "${3:-1}"; sleep 1.6; }
# What the flag itself is painted with: the three commonest colours in its 24x24 box, so the disc's
# colour (and the ring's stroke at 0) is measured rather than asserted.
flag_colours() {  # png x y -> "colour:count colour:count colour:count"
    convert "$1" -crop 24x24+$(( $2 - 12 ))+$(( $3 - 12 )) +repage txt:- 2>/dev/null |
        awk -F'#' 'NR>1 {print substr($2,1,6)}' | sort | uniq -c | sort -rn | head -3 |
        awk '{printf "%s:%s ", $2, $1}'
}

card_id() {  # title -> the card's #ID, from the file the fixture wrote
    (cd "$work/issues/features" && for f in *.md; do
        grep -q "$1" "$f" && { grep "^id:" "$f" | awk '{print toupper($2)}'; break; }
     done)
}
card_front() {  # title -> its `priority:` line, or "(no priority key)"
    (cd "$work/issues/features" && for f in *.md; do
        grep -q "$1" "$f" && { grep -E "^priority:" "$f" || echo "(no priority key)"; break; }
     done)
}

alpha=$(card_id "Alpha reflow fix")
bravo=$(card_id "Bravo voice mode")

: >"$out/ocr.txt"

# 1. Open the Switchboard and the card page. A new pane starts folded, so unfold Ready, select a
#    row and press Enter: the detail takes the whole pane at this width (below ~900 px of pane).
xdotool key --window "$win" ctrl+shift+s; sleep 8
shot 00-folded
box=$(word_box "$out/00-folded.png" "READY")
[[ -n $box ]] && click_at $(( $(echo $box | cut -d' ' -f1) + $(echo $box | cut -d' ' -f3) / 2 )) \
                       $(( $(echo $box | cut -d' ' -f2) + $(echo $box | cut -d' ' -f4) / 2 ))
shot 00b-ready-open
bravo_y=$(word_y "$out/00b-ready-open.png" "Bravo")
[[ -n $bravo_y ]] && click_at "$(word_x "$out/00b-ready-open.png" "Bravo")" "$bravo_y"
xdotool key --window "$win" Return; sleep 2
shot 01-card-open

# 2. The flag is the first control of the detail's header, 16 px left of the `#ID` label's left
#    edge (12 px margin + a 20 px button + 6 px spacing puts the id at 38, the button's centre at
#    22). The id is found by OCR; the button's own pixel colour is what the screenshots carry.
ref_left=$(ref_left "$out/01-card-open.png")
ref_y=$(ref_y "$out/01-card-open.png")
flag_x=$(( ref_left - 16 ))
flag_y=$ref_y
{
    echo "01 card page open:     #$bravo, ref label at x=$ref_left y=$ref_y"
    echo "01 flag click point:   x=$flag_x y=$flag_y (16 px left of the id)"
    echo "01 file on open:       $(card_front 'Bravo voice mode')  (the fixture's +2)"
    echo "01 flag colours at +2: $(flag_colours "$out/01-card-open.png" "$flag_x" "$flag_y")"
    echo "01 header words:       $(convert "$out/01-card-open.png" -crop 750x30+690+133 +repage -scale 300% png:- 2>/dev/null | tesseract - stdout --psm 7 2>/dev/null)"
} >>"$out/ocr.txt"

# 3. Left click raises it (+2 -> +3, the clamp), the notice names the value and the file agrees.
click_at "$flag_x" "$flag_y" 1
shot 02-click-up
{
    echo "02 file after up:      $(card_front 'Bravo voice mode')  (expect priority: 3)"
    echo "02 flag colours at +3: $(flag_colours "$out/02-click-up.png" "$flag_x" "$flag_y")"
    echo "02 notice after up:    $(line_text "$out/02-click-up.png" | grep -E "Flagged #" | head -1)"
} >>"$out/ocr.txt"
# A second left click at +3 stays +3 (the clamp), and the file is untouched.
click_at "$flag_x" "$flag_y" 1
shot 02b-click-clamped
echo "02b file after clamp:  $(card_front 'Bravo voice mode')  (expect priority: 3, unchanged)" >>"$out/ocr.txt"

# 4. Right clicks walk it down: +3 -> +2 -> +1 -> 0 (the key goes) -> −1 (yellow).
for n in 1 2 3 4; do
    click_at "$flag_x" "$flag_y" 3
    shot "03-click-down-$n"
    echo "03 after right click $n: $(card_front 'Bravo voice mode')  colours: $(flag_colours "$out/03-click-down-$n.png" "$flag_x" "$flag_y")" >>"$out/ocr.txt"
done
{
    echo "03 notice at 0 (3rd):  $(line_text "$out/03-click-down-3.png" | grep -iE "cleared the flag" | head -1)"
    echo "03 notice at -1 (4th): $(line_text "$out/03-click-down-4.png" | grep -E "Flagged #" | head -1)"
    echo "03 thread lines:       $(line_text "$out/03-click-down-4.png" | grep -ciE "flagged this card|cleared this card") worker entries on the card"
} >>"$out/ocr.txt"

# 6. Ctrl+Z in the pane undoes the last of those writes, the way it undoes any other.
xdotool key --window "$win" ctrl+z; sleep 2
shot 06-undo
echo "06 file after Ctrl+Z:  $(card_front 'Bravo voice mode')  (expect the -1 undone: no key)" >>"$out/ocr.txt"

# 5. The page follows the row's card: click Alpha's row flag in the list (left = +1), open the
#    card, and the page's flag is that card's +1 (the disc, not the ring) with the same value in
#    the file — one flag, two places to shift it.
xdotool key --window "$win" Escape; sleep 1.5
shot 04-list
alpha_y=$(word_y "$out/04-list.png" "Alpha")
if [[ -n $alpha_y ]]; then
    click_at $(( $(word_left "$out/04-list.png" "Alpha") - 60 )) "$alpha_y"
    echo "05 alpha row flag:     $(card_front 'Alpha reflow fix')  (expect priority: 1)" >>"$out/ocr.txt"
    click_at "$(word_x "$out/04-list.png" "Alpha")" "$alpha_y"
    xdotool key --window "$win" Return; sleep 2
    shot 05-alpha-page
    a_ref=$(ref_left "$out/05-alpha-page.png"); a_y=$(ref_y "$out/05-alpha-page.png")
    echo "05 alpha page ref:     x=$a_ref y=$a_y, flag colours at +1: $(flag_colours "$out/05-alpha-page.png" $(( a_ref - 16 )) "$a_y")" >>"$out/ocr.txt"
    # The page's flag is the row's flag: one right click here takes Alpha back to 0, and the row's
    # own colour follows in the list behind it.
    click_at $(( a_ref - 16 )) "$a_y" 3
    shot 05b-alpha-page-down
    echo "05b alpha after down:  $(card_front 'Alpha reflow fix')  (expect the +1 cleared)" >>"$out/ocr.txt"
fi

echo "run done: $out/ocr.txt"
