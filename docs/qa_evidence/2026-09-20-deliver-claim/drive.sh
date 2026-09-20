#!/usr/bin/env bash
# #R9G7, live: Execute claims a card for the pane it opens, and the Switchboard shows who holds it.
#
# A board of one fixture card (with a `## Plan`, so Execute goes ahead at once) under Xvfb with an
# isolated HOME, XDG_CONFIG_HOME, XDG_RUNTIME_DIR and TMPDIR, and RELAY_KEYRING=off. Nothing here
# needs a model: `board_claim` is a write, not a turn. Every claim is checked against the card file
# the worker wrote and by OCR (tesseract word boxes), not by eye:
#
#   1. `x` on the card sends one `board_claim`. The card's front matter gains `session: <token>`,
#      the token of the pane Execute opened, and the thread's entry reads "Claimed (<first 8>)".
#   2. The card page and the row wear the chip: the session glyph and those same eight characters.
#   3. Close that pane and the chip says `closed` — the claim is still the record of who took the
#      card, but nothing links to a pane that has gone.
#
#   docs/qa_evidence/2026-09-20-deliver-claim/drive.sh [build-dir] [out-dir]
set -uo pipefail
root=/home/elliott/repos/relay-terminal
build=${1:-$root/build}
out=${2:-$root/docs/qa_evidence/2026-09-20-deliver-claim}
width=1440 height=900
mkdir -p "$out"

display=
for n in $(seq 170 199); do [[ -e /tmp/.X11-unix/X$n ]] || { display=:$n; break; }; done
[[ -z $display ]] && { echo "no free display"; exit 1; }

# A SHORT sandbox path: XDG_RUNTIME_DIR holds sockets and the limit is 108 bytes.
sandbox=$(mktemp -d /tmp/rl-r9g7.XXXX)
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
card_id=$(PYTHONPATH="$root/backend" python3 - "$work" <<'FIX'
import sys
from pathlib import Path
from relay_core import board as B
work = Path(sys.argv[1])
root = work / "issues"
root.mkdir(parents=True)
(root / B.BOARD_CONFIG).write_text(
    "tabs: [{id: features, folder: features}]\n"
    "columns: [inbox, discussing, ready, executing, waiting, needs-verification, needs-qa, done]\n",
    encoding="utf-8")
board = B.Board(root, work)
card = B.new_card("work", "Alpha voice mode", "ready", request="add voice transcribe mode")
path = B.write_new_card(board, card, "features")
path.write_text(path.read_text(encoding="utf-8") + "\n## Plan\n\nWire the mic and ship it.\n",
                encoding="utf-8")
print(card.id)
FIX
)
: >"$out/ocr.txt"
echo "fixture card: $card_id" >>"$out/ocr.txt"

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

shot() { import -window root "$out/$1.png"; }
text_of() { tesseract "$1" stdout --psm 11 2>/dev/null; }
word_center() {
    tesseract "$1" stdout --psm 11 tsv 2>/dev/null |
        awk -v want="$2" 'tolower($12)==tolower(want) {print int($7+$9/2), int($8+$10/2); exit}'
}
click_word() {
    local box; box=$(word_center "$1" "$2")
    [[ -z $box ]] && { echo "MISSING WORD: $2" >>"$out/ocr.txt"; return 1; }
    xdotool mousemove $(( ${box% *} + ${3:-0} )) $(( ${box#* } + ${4:-0} )) click 1
    sleep 1.5
}
# The section names appear twice: once as a checkbox at the top of the list page, once as the
# section header over its cards. The header is the lower one, so the *last* box is the one to
# click — clicking the first only unticks the section and takes it off the page.
word_center_last() {
    tesseract "$1" stdout --psm 11 tsv 2>/dev/null |
        awk -v want="$2" 'tolower($12)==tolower(want) {last=int($7+$9/2) " " int($8+$10/2)} END {print last}'
}
click_word_last() {
    local box; box=$(word_center_last "$1" "$2")
    [[ -z ${box// /} ]] && { echo "MISSING WORD (last): $2" >>"$out/ocr.txt"; return 1; }
    xdotool mousemove $(( ${box% *} + ${3:-0} )) $(( ${box#* } + ${4:-0} )) click 1
    sleep 1.5
}
# The card row, read on its own at 4x. A badge is drawn at 0.85 of the pane's font and tesseract
# misses it at page scale, so the strip the card's title sits on is cropped out of the board pane
# and enlarged. The last "Alpha" on screen is the row; the first is the claimed pane's title.
row_line() {
    local tsv y h x right left
    tsv=$(tesseract "$1" stdout --psm 11 tsv 2>/dev/null)
    read -r y h x <<<"$(awk 'tolower($12)=="alpha" {y=$8; h=$10; x=$7} END {print y, h, x}' <<<"$tsv")"
    [[ -z ${y:-} ]] && return 1
    # How far the row runs: the rightmost word tesseract did see on the same line — the `updated`
    # date — so the crop ends with the row and never reaches the pane beside it.
    right=$(awk -v ry="$y" '$8+0 >= ry-6 && $8+0 <= ry+6 && $7+$9 > m {m=$7+$9} END {print m+0}' <<<"$tsv")
    left=$(( x > 80 ? x - 80 : 0 ))
    (( right <= left + 60 )) && right=$(( left + 620 ))
    convert "$1" -crop $((right - left + 20))x$((h + 16))+$left+$((y - 8)) +repage \
            -resize 400% -colorspace Gray -normalize "$sandbox/row.png" 2>/dev/null || return 1
    tesseract "$sandbox/row.png" stdout --psm 7 2>/dev/null
}

says() {  # image needle label
    if grep -qi -- "$2" <<<"$(text_of "$1")"; then echo "$3: yes" >>"$out/ocr.txt"
    else echo "$3: NO" >>"$out/ocr.txt"; fi
}

# The first run opens an Approvals pane; close it so the board has room. A narrow row drops its
# badges (board::fitBadges), and the claim chip is the point of these shots.
shot 00-first-run
click_word "$out/00-first-run.png" "Approvals" 0 0 || true; sleep 1
xdotool key --window "$win" ctrl+w; sleep 2

# 1. Open the Switchboard, unfold READY, open the card, Execute (`x`, #XS6Q).
xdotool key --window "$win" ctrl+shift+s; sleep 8
shot 01-board-open
click_word_last "$out/01-board-open.png" "READY" 25 0 || true; sleep 2
shot 02-ready-unfolded
click_word "$out/02-ready-unfolded.png" "Alpha" 0 0 || true; sleep 2.5
shot 03-card-open
xdotool key --window "$win" x; sleep 9
shot 04-claimed

# What the worker wrote: the claim is `session` in the card's own front matter (#R9G7).
cardfile=$(ls "$work"/issues/features/*.md 2>/dev/null | head -1)
session=$(grep -m1 '^session:' "$cardfile" 2>/dev/null | sed 's/^session: *//' | tr -d "\"' ")
short=${session:0:8}
echo "04 card front matter session: ${session:-<none>} (chip ${short:-<none>})" >>"$out/ocr.txt"
echo "04 card status: $(sed -n 's/^status: *//p' "$cardfile" | head -1)" >>"$out/ocr.txt"
echo "04 card assignee: $(sed -n 's/^assignee: *//p' "$cardfile" | head -1)" >>"$out/ocr.txt"
grep -o 'Claimed ([0-9A-Za-z]*)' "$work/issues/threads/${card_id}.md" 2>/dev/null |
    head -1 | sed 's/^/04 thread entry: /' >>"$out/ocr.txt"
if [[ -n $short ]] && grep -q "$short" <<<"$(text_of "$out/04-claimed.png")"; then
    echo "04 the card page shows the chip $short: yes" >>"$out/ocr.txt"
else
    echo "04 the card page shows the chip ${short:-<none>}: NO (OCR of 8 hex characters is lossy;" \
         "compare 04-claimed.png by eye)" >>"$out/ocr.txt"
fi
says "$out/04-claimed.png" "session" "04 the card page labels it 'session'"

# 2. Back to the list: the row wears the same chip. The board has to be the focused pane for
#    Escape to close the card, so click its own words first, and the list comes back folded.
click_word "$out/04-claimed.png" "THREAD" 0 0 || click_word "$out/04-claimed.png" "Issue" 0 0 || true
sleep 1.5
xdotool key --window "$win" Escape; sleep 2
shot 04b-list-folded
click_word_last "$out/04b-list-folded.png" "EXECUTING" 30 0 || true; sleep 2
shot 05-row-chip
live_row=$(row_line "$out/05-row-chip.png")
echo "05 the row reads: ${live_row:-<not found>}" >>"$out/ocr.txt"
if [[ -n $short ]] && grep -q "$short" <<<"$live_row"; then
    echo "05 the row shows the whole chip $short: yes" >>"$out/ocr.txt"
else
    echo "05 the row shows the whole chip ${short:-<none>}: NO" >>"$out/ocr.txt"
fi
if grep -qi "closed" <<<"$live_row"; then
    echo "05 the live chip says 'closed' (it must not): NO" >>"$out/ocr.txt"
else
    echo "05 the live chip leaves 'closed' off, the pane being open: yes" >>"$out/ocr.txt"
fi

# 3. Close the pane the claim names: the chip goes muted and says `closed`, on the row and on the
#    card page, and nothing links to it any more.
xdotool mousemove $(( width * 88 / 100 )) $(( height * 30 / 100 )) click 1; sleep 1.5
xdotool key --window "$win" ctrl+w; sleep 4
shot 06-pane-closed
gone_row=$(row_line "$out/06-pane-closed.png")
echo "06 the row reads: ${gone_row:-<not found>}" >>"$out/ocr.txt"
if grep -qi "closed" <<<"$gone_row" && { [[ -z $short ]] || grep -q "$short" <<<"$gone_row"; }; then
    echo "06 the row's chip keeps the token and says 'closed': yes" >>"$out/ocr.txt"
else
    echo "06 the row's chip keeps the token and says 'closed': NO" >>"$out/ocr.txt"
fi
says "$out/06-pane-closed.png" "Switchboard" "06 the Switchboard is still the pane on screen"
click_word "$out/06-pane-closed.png" "Alpha" 0 0 || true; sleep 2.5
shot 07-card-page-closed
says "$out/07-card-page-closed.png" "closed" "07 the card page's chip says 'closed' too"
if [[ -n $short ]] && grep -q "$short" <<<"$(text_of "$out/07-card-page-closed.png")"; then
    echo "07 and still shows the token $short: yes" >>"$out/ocr.txt"
else
    echo "07 and still shows the token ${short:-<none>}: NO (lossy OCR; see the shot)" >>"$out/ocr.txt"
fi

if kill -0 "$relay_pid" 2>/dev/null; then echo "relay still alive at the end: yes" >>"$out/ocr.txt"
else echo "relay still alive at the end: NO" >>"$out/ocr.txt"; fi
[[ -s "$sandbox/relay.log" ]] && cp "$sandbox/relay.log" "$out/relay.log"
grep -i "gui_crash" "$sandbox/relay.log" >>"$out/ocr.txt" 2>/dev/null ||
    echo "no gui_crash in relay.log" >>"$out/ocr.txt"
cp "$cardfile" "$out/claimed-card.md" 2>/dev/null
echo "shots, claimed-card.md and ocr.txt in $out"
