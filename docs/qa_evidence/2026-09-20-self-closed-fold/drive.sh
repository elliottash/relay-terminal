#!/usr/bin/env bash
# #93WR, live: the cards the agent closed itself are one row of the done list, folded by default.
#
# A fixture board under Xvfb with an isolated HOME, XDG_CONFIG_HOME, XDG_RUNTIME_DIR and TMPDIR,
# and RELAY_KEYRING=off. Nothing here needs a model or the worker's half of the card: the three
# self-closed cards are written with `implemented_by` and `verified_by` already equal, which is
# exactly the marker `board::selfClosed` reads (card #93WR, "Marker, no new field").
#
#   1. Done unfolds to its two ordinary cards and one row, "▸ 3 closed by the agent". The three
#      cards it stands for are nowhere on the page, and the DONE header still counts all five.
#   2. A click on the row shows them as ordinary rows; a second click puts them away.
#   3. The keyboard does the same: End stands on the row, Enter opens it, ← puts it away again.
#
#   docs/qa_evidence/2026-09-20-self-closed-fold/drive.sh [relay-binary] [out-dir]
set -uo pipefail
root=/home/elliott/repos/relay-terminal
relay=${1:-$root/build/relay}
out=${2:-$root/docs/qa_evidence/2026-09-20-self-closed-fold}
width=1440 height=1180
mkdir -p "$out"

display=
for n in $(seq 170 199); do [[ -e /tmp/.X11-unix/X$n ]] || { display=:$n; break; }; done
[[ -z $display ]] && { echo "no free display"; exit 1; }

# A SHORT sandbox path: XDG_RUNTIME_DIR holds sockets and the limit is 108 bytes.
sandbox=$(mktemp -d /tmp/rl-93wr.XXXX)
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
    "tabs: [{id: features, folder: features}]\n"
    "columns: [inbox, discussing, ready, executing, waiting, needs-verification, needs-qa, done]\n",
    encoding="utf-8")
board = B.Board(root, work)
SIG = "anthropic/claude-opus-5"
# One open card, two ordinary closed ones, and three the agent closed itself: `verified_by`
# equal to `implemented_by` is the whole marker.
plan = [("Alpha voice mode", "ready", {}, "i1"),
        ("Bravo release notes", "done", {"implemented_by": SIG}, "i2"),
        ("Charlie old glyph", "dropped", {}, "i3"),
        # Closed by a second model: this one is Verified, not Done, and never folds.
        ("Golf checked by codex", "done", {"implemented_by": SIG, "verified_by": "openai/codex"}, "i7"),
        ("Delta tooltip wording", "done", {"implemented_by": SIG, "verified_by": SIG}, "i4"),
        ("Echo log rotation", "done", {"implemented_by": SIG, "verified_by": SIG}, "i5"),
        ("Foxtrot cache header", "done", {"implemented_by": SIG, "verified_by": SIG}, "i6")]
for title, status, fields, rank in plan:
    card = B.new_card("work", title, status, rank=rank, request=f"{title}, for the fold evidence",
                      **fields)
    B.write_new_card(board, card, "features")
    print(card.id, status, fields.get("verified_by", "-"))
FIX

: >"$out/ocr.txt"
echo "relay binary: $relay ($(stat -c %y "$relay"))" >>"$out/ocr.txt"

(cd "$work" && exec "$relay" --workspace "$work" --fresh) >"$sandbox/relay.log" 2>&1 &
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
# The fold row reads "▸ 3 closed by the agent", and at the row's font tesseract runs the
# count and the word together ("3closed"), so the row is found by a token that *contains* the
# word rather than by an exact match.
token_center_last() {
    tesseract "$1" stdout --psm 11 tsv 2>/dev/null |
        awk -v want="$2" 'tolower($12) ~ tolower(want) {last=int($7+$9/2) " " int($8+$10/2)} END {print last}'
}
click_token_last() {
    local box; box=$(token_center_last "$1" "$2")
    [[ -z ${box// /} ]] && { echo "MISSING TOKEN: $2" >>"$out/ocr.txt"; return 1; }
    xdotool mousemove $(( ${box% *} + ${3:-0} )) $(( ${box#* } + ${4:-0} )) click 1
    sleep 1.5
}

word_center_first() {
    tesseract "$1" stdout --psm 11 tsv 2>/dev/null |
        awk -v want="$2" 'tolower($12)==tolower(want) {print int($7+$9/2), int($8+$10/2); exit}'
}
click_word_first() {
    local box; box=$(word_center_first "$1" "$2")
    [[ -z $box ]] && { echo "MISSING WORD: $2" >>"$out/ocr.txt"; return 1; }
    xdotool mousemove $(( ${box% *} + ${3:-0} )) $(( ${box#* } + ${4:-0} )) click 1
    sleep 1.5
}
says() {  # image needle label
    if grep -qi -- "$2" <<<"$(text_of "$1")"; then echo "$3: yes" >>"$out/ocr.txt"
    else echo "$3: NO" >>"$out/ocr.txt"; fi
}
says_not() {  # image needle label
    if grep -qi -- "$2" <<<"$(text_of "$1")"; then echo "$3: NO" >>"$out/ocr.txt"
    else echo "$3: yes" >>"$out/ocr.txt"; fi
}
# The DONE header read on its own at 4x: its name and the count beside it, which is the whole
# section's card count whatever is folded inside it.
header_line() {  # image word
    local tsv y h x
    tsv=$(tesseract "$1" stdout --psm 11 tsv 2>/dev/null)
    read -r y h x <<<"$(awk -v want="$2" 'tolower($12)==tolower(want) {y=$8; h=$10; x=$7} END {print y, h, x}' <<<"$tsv")"
    [[ -z ${y:-} ]] && return 1
    convert "$1" -crop 360x$((h + 14))+$((x > 40 ? x - 40 : 0))+$((y - 7)) +repage -resize 400% -colorspace Gray \
            -normalize "$sandbox/header.png" 2>/dev/null || return 1
    tesseract "$sandbox/header.png" stdout --psm 7 2>/dev/null | tr -d '\f' | tr -s ' \n' ' '
}

# The first run opens an Approvals pane; close it so the board has the window.
shot 00-first-run
click_word_first "$out/00-first-run.png" "Approvals" 0 0 || true; sleep 1
xdotool key --window "$win" ctrl+w; sleep 2

# 1. The Switchboard, every section folded as a new pane opens it, then DONE unfolded.
xdotool key --window "$win" ctrl+shift+s; sleep 8
shot 01-board-open
click_word_last "$out/01-board-open.png" "DONE" 25 0 || true; sleep 2
# The list is taller than its viewport with every section drawn, and the fold row is the last of
# Done's rows: scroll to the end of the list so the shots hold it.
xdotool mousemove $(( width * 75 / 100 )) $(( height * 40 / 100 ))
for _ in 1 2 3 4 5 6; do xdotool click 5; done; sleep 1.5
shot 02-done-unfolded
says     "$out/02-done-unfolded.png" "closed by the agent" "02 the fold row says 'closed by the agent'"
says     "$out/02-done-unfolded.png" "Bravo"   "02 the ordinary closed card is an ordinary row"
says_not "$out/02-done-unfolded.png" "Golf"    "02 the cross-verified card is not in Done at all"
says     "$out/02-done-unfolded.png" "Charlie" "02 and so is the dropped one"
says_not "$out/02-done-unfolded.png" "Delta"   "02 the self-closed cards are away: Delta"
says_not "$out/02-done-unfolded.png" "Echo"    "02 the self-closed cards are away: Echo"
says_not "$out/02-done-unfolded.png" "Foxtrot" "02 the self-closed cards are away: Foxtrot"
echo "02 the DONE header reads: $(header_line "$out/02-done-unfolded.png" DONE)" >>"$out/ocr.txt"

# 2. A click on the row shows its cards; a second click puts them away.
click_token_last "$out/02-done-unfolded.png" "closed" 0 0 || true; sleep 2
shot 03-agent-cards-shown
says "$out/03-agent-cards-shown.png" "Delta"   "03 a click on the row shows Delta"
says "$out/03-agent-cards-shown.png" "Echo"    "03 a click on the row shows Echo"
says "$out/03-agent-cards-shown.png" "Foxtrot" "03 a click on the row shows Foxtrot"
says "$out/03-agent-cards-shown.png" "closed by the agent" "03 and the row is still there"
echo "03 the DONE header reads: $(header_line "$out/03-agent-cards-shown.png" DONE)" >>"$out/ocr.txt"
click_token_last "$out/03-agent-cards-shown.png" "closed" 0 0 || true; sleep 2
shot 04-folded-again
says_not "$out/04-folded-again.png" "Foxtrot" "04 a second click puts them away again"
says     "$out/04-folded-again.png" "closed by the agent" "04 and the row stays"

# 3. The keyboard: open a card and Escape back, so the list has the focus; End stands on the last
#    row of the list, which is the fold row; Enter opens it and ← puts it away.
click_word_last "$out/04-folded-again.png" "Bravo" 0 0 || true; sleep 2.5
shot 05-card-open
xdotool key --window "$win" Escape; sleep 3
xdotool key --window "$win" End; sleep 1.5
xdotool key --window "$win" Return; sleep 2
shot 06-keyboard-open
says "$out/06-keyboard-open.png" "Foxtrot" "06 End then Enter on the row shows its cards"
xdotool key --window "$win" Left; sleep 2
shot 07-keyboard-folded
says_not "$out/07-keyboard-folded.png" "Foxtrot" "07 and ← puts them away"
says     "$out/07-keyboard-folded.png" "closed by the agent" "07 with the row still standing"

if kill -0 "$relay_pid" 2>/dev/null; then echo "relay still alive at the end: yes" >>"$out/ocr.txt"
else echo "relay still alive at the end: NO" >>"$out/ocr.txt"; fi
[[ -s "$sandbox/relay.log" ]] && cp "$sandbox/relay.log" "$out/relay.log"
grep -i "gui_crash" "$sandbox/relay.log" >>"$out/ocr.txt" 2>/dev/null ||
    echo "no gui_crash in relay.log" >>"$out/ocr.txt"
# The closed cards live in their status subfolder (`features/done/`), so the whole tree is read.
: >"$out/fixture-cards.txt"
for card in $(find "$work/issues/features" -name '*.md' | sort); do
    printf '%s: status=%s implemented_by=%s verified_by=%s\n' "$(basename "$card")" \
        "$(sed -n 's/^status: *//p' "$card" | head -1)" \
        "$(sed -n 's/^implemented_by: *//p' "$card" | head -1)" \
        "$(sed -n 's/^verified_by: *//p' "$card" | head -1)" >>"$out/fixture-cards.txt"
done
echo "shots, fixture-cards.txt and ocr.txt in $out"
