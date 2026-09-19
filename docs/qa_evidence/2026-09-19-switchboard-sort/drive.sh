#!/usr/bin/env bash
# The Switchboard's sort menu, live: a board of four fixture cards under Xvfb with an isolated
# HOME, XDG_CONFIG_HOME, XDG_RUNTIME_DIR and TMPDIR (short paths: the 108-byte socket limit) and
# RELAY_KEYRING=off. The card titles start with Alpha/Bravo/Charlie and their `created` dates and
# file mtimes disagree, so each sort is a different visible permutation:
#
#   Manual (rank a,b,c)      Bravo, Charlie, Alpha
#   Newest first (created)   Charlie, Bravo, Alpha
#   Recently updated (mtime) Alpha, Bravo, Charlie
#
# Every claim below is checked by OCR of the screenshots (tesseract word boxes), not by eye:
# the button's label, the menu's entries, the order of the titles on the list (their y
# coordinates), and the refusal notice Alt+Shift+↓ answers under a time sort.
#
#   docs/qa_evidence/2026-09-19-switchboard-sort/drive.sh [build-dir] [out-dir]
set -uo pipefail
root=/home/elliott/repos/relay-terminal
build=${1:-$root/build}
out=${2:-$root/docs/qa_evidence/2026-09-19-switchboard-sort}
width=1440 height=900
mkdir -p "$out"

display=
for n in $(seq 170 199); do [[ -e /tmp/.X11-unix/X$n ]] || { display=:$n; break; }; done
[[ -z $display ]] && { echo "no free display"; exit 1; }

sandbox=$(mktemp -d /tmp/rl-sort.XXXX)
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
# The fixture board: four cards whose rank, created and mtime orders all differ.
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
#            title                 status   created     rank  mtime days ago
cards = [("Alpha reflow fix",      "ready", "2026-09-01", "c",  0),
         ("Bravo voice mode",      "ready", "2026-09-10", "a",  5),
         ("Charlie theme swap",    "ready", "2026-09-16", "b",  9),
         ("Delta quick add",       "inbox", "2026-09-18", "a",  1)]
for title, status, created, rank, days in cards:
    card = B.new_card("work", title, status, created=created, rank=rank,
                      request=f"the ask for {title.lower()}")
    path = B.write_new_card(board, card, "features")
    when = time.time() - days * 86400
    os.utime(path, (when, when))
    print(f"{card.id}  {title}  {status}  created {created}  rank {rank}  mtime -{days}d")
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
# OCR helpers: tesseract word boxes on a screenshot.
word_center() {  # image word -> "x y"
    tesseract "$1" stdout --psm 11 tsv 2>/dev/null |
        awk -v want="$2" 'tolower($12)==tolower(want) {print int($7+$9/2), int($8+$10/2); exit}'
}
word_y() {  # image word -> its y center (the visual row it is on)
    tesseract "$1" stdout --psm 11 tsv 2>/dev/null |
        awk -v want="$2" 'tolower($12)==tolower(want) {print int($8+$10/2); exit}'
}
click_word() {  # image word [dx dy] -- click a word's center, nudged
    local box; box=$(word_center "$1" "$2")
    [[ -z $box ]] && { echo "MISSING WORD: $2" >>"$out/ocr.txt"; return 1; }
    xdotool mousemove $(( ${box% *} + ${3:-0} )) $(( ${box#* } + ${4:-0} )) click 1
    sleep 1.5
}

: >"$out/ocr.txt"
order_of() {  # png -> the three titles as their y order says, highest on screen first
    local a b c
    a=$(word_y "$1" Alpha); b=$(word_y "$1" Bravo); c=$(word_y "$1" Charlie)
    for t in "Alpha:$a" "Bravo:$b" "Charlie:$c"; do echo "${t#*:} ${t%%:*}"; done | sort -n |
        awk '{printf "%s ", $2}' ; echo
}

# 1. Open the Switchboard, then unfold Ready by clicking its header (a new pane starts folded).
xdotool key --window "$win" ctrl+shift+s; sleep 8
shot 01-board-open
echo "01 board open; button label: $(tesseract "$out/01-board-open.png" stdout --psm 11 2>/dev/null | grep -i "sort" | head -1)" >>"$out/ocr.txt"
click_word "$out/01-board-open.png" "READY" 20 0 || true

# 2. Manual (the default): rank order Bravo, Charlie, Alpha.
shot 02-manual
echo "02 manual order:      $(order_of "$out/02-manual.png")" >>"$out/ocr.txt"

# 3. The menu: open it, choose Newest first.
click_word "$out/02-manual.png" "Sort:" 30 0 || true
shot 03-menu-open
echo "03 menu has:          $(tesseract "$out/03-menu-open.png" stdout --psm 11 2>/dev/null | grep -iE 'manual|newest|oldest|updated' | tr '\n' '|')" >>"$out/ocr.txt"
click_word "$out/03-menu-open.png" "Newest" 30 0 || true
sleep 1
shot 04-newest
echo "04 newest order:      $(order_of "$out/04-newest.png")" >>"$out/ocr.txt"
echo "04 button label:      $(tesseract "$out/04-newest.png" stdout --psm 11 2>/dev/null | grep -i "sort" | head -1)" >>"$out/ocr.txt"

# 4. Recently updated: the touched-now Alpha is on top whatever its created date.
click_word "$out/04-newest.png" "Sort:" 30 0 || true
sleep 1
shot 05-menu-again
click_word "$out/05-menu-again.png" "Recently" 40 0 || true
sleep 1
shot 06-updated
echo "06 updated order:     $(order_of "$out/06-updated.png")" >>"$out/ocr.txt"

# 5. Under a time sort Alt+Shift+↓ answers with the notice instead of writing a rank. Clicking
# the row also opens the card (the open card follows the selection), so Esc comes back to the
# list with the card still selected before the key.
click_word "$out/06-updated.png" "Bravo" 0 0 || true
sleep 2.5
xdotool key --window "$win" Escape; sleep 1.5
xdotool key --window "$win" alt+shift+Down; sleep 1.5
shot 07-notice
echo "07 notice says:       $(tesseract "$out/07-notice.png" stdout --psm 11 2>/dev/null | grep -i "reordering is off" | head -1)" >>"$out/ocr.txt"

# 6. Narrow: the sort button drops to the wrapped row with the other two, out of the filter's way.
xdotool windowsize "$win" 620 "$height"; sleep 3
shot 08-narrow
convert "$out/08-narrow.png" -crop 620x150+0+30 +repage -scale 200% "$out/08-narrow-tools.png"
xdotool windowsize "$win" "$width" "$height"; sleep 2

[[ -s "$sandbox/relay.log" ]] && cp "$sandbox/relay.log" "$out/relay.log"
echo "shots and ocr.txt in $out"
