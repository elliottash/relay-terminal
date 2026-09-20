#!/usr/bin/env bash
# The Switchboard's column header, live (owner, 2026-09-19: "change switchboard sorting from a sort
# button to adding header columns that you click on", "add a 'created' and 'updated' column", "and
# sorting is within section", and the section list's own order). A board of four fixture cards
# under Xvfb with an isolated HOME, XDG_CONFIG_HOME, XDG_RUNTIME_DIR and TMPDIR (short paths: the
# 108-byte socket limit) and RELAY_KEYRING=off.
#
# The Ready cards' rank, created and mtime orders all differ, so each click is a different visible
# permutation:
#
#   Manual (rank a,b,c)      Bravo, Charlie, Alpha
#   Newest first (created)   Charlie, Bravo, Alpha
#   Oldest first (created)   Alpha, Bravo, Charlie
#   Recently updated (mtime) Alpha, Bravo, Charlie   (Alpha's file was touched now)
#
# Every claim below is checked by OCR of the screenshots (tesseract word boxes), not by eye: the
# header's own words and arrow, the order of the titles on the list (their y coordinates), the dates
# in the two cells, the refusal notice Alt+Shift+↓ answers under a column sort, the section list's
# own page, and the drop of the date columns in a narrow pane. The screenshots carry what OCR cannot
# read — the ▲ ▼ glyphs, the drag handle, the arrow on the active cell.
#
#   docs/qa_evidence/2026-09-19-switchboard-column-header/drive.sh [build-dir] [out-dir]
set -uo pipefail
root=/home/elliott/repos/relay-terminal
build=${1:-$root/build}
out=${2:-$root/docs/qa_evidence/2026-09-19-switchboard-column-header}
width=1440 height=900
mkdir -p "$out"

display=
for n in $(seq 170 199); do [[ -e /tmp/.X11-unix/X$n ]] || { display=:$n; break; }; done
[[ -z $display ]] && { echo "no free display"; exit 1; }

sandbox=$(mktemp -d /tmp/rl-cols.XXXX)
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
rightmost_x() {  # image y -> the x the rightmost word on that row ends at
    tesseract "$1" stdout --psm 11 tsv 2>/dev/null |
        awk -v y="$2" 'NF>=12 && $12 != "" && $8+$10/2 > y-12 && $8+$10/2 < y+12 {if ($7+$9 > m) m = $7+$9} END {print int(m)}'
}

: >"$out/ocr.txt"
order_of() {  # png -> the three titles as their y order says, highest on screen first
    local a b c
    a=$(word_y "$1" Alpha); b=$(word_y "$1" Bravo); c=$(word_y "$1" Charlie)
    for t in "Alpha:$a" "Bravo:$b" "Charlie:$c"; do echo "${t#*:} ${t%%:*}"; done | sort -n |
        awk '{printf "%s ", $2}' ; echo
}
# The header's own row: every word whose y is within 14 px of CREATED's, left to right.
header_line() {
    local y; y=$(word_y "$1" CREATED)
    [[ -z $y ]] && { echo "(no CREATED found)"; return; }
    tesseract "$1" stdout --psm 11 tsv 2>/dev/null |
        awk -v y="$y" 'NF>=12 && $12 != "" && $8+$10/2 > y-14 && $8+$10/2 < y+14 {print $7, $12}' |
        sort -n | awk '{printf "%s ", $2}'
    echo
}

# 1. Open the Switchboard. The header row is over the list; there is no sort button any more.
xdotool key --window "$win" ctrl+shift+s; sleep 8
shot 01-board-open
{
    echo "01 header row:        $(header_line "$out/01-board-open.png")"
    echo "01 'sort:' controls:  $(tesseract "$out/01-board-open.png" stdout --psm 11 2>/dev/null | grep -ci 'sort:')"
    echo "01 dates in the cells: $(tesseract "$out/01-board-open.png" stdout --psm 11 2>/dev/null | grep -c '2026-')"
} >>"$out/ocr.txt"
click_word "$out/01-board-open.png" "READY" 20 0 || true

# 2. Manual: the board's own rank order.
shot 02-manual
echo "02 manual order:      $(order_of "$out/02-manual.png")" >>"$out/ocr.txt"

# 3. A click on CREATED: newest first, the arrow on that cell.
click_word "$out/02-manual.png" "CREATED" 0 0 || true
shot 03-created-newest
{
    echo "03 newest order:      $(order_of "$out/03-created-newest.png")"
    echo "03 header row:        $(header_line "$out/03-created-newest.png")"
} >>"$out/ocr.txt"

# 4. A second click on the same cell: oldest first.
click_word "$out/03-created-newest.png" "CREATED" 0 0 || true
shot 04-created-oldest
{
    echo "04 oldest order:      $(order_of "$out/04-created-oldest.png")"
    echo "04 header row:        $(header_line "$out/04-created-oldest.png")"
} >>"$out/ocr.txt"

# 5. A click on UPDATED: the file touched now is on top, whatever its created date.
click_word "$out/04-created-oldest.png" "UPDATED" 0 0 || true
shot 05-updated
{
    echo "05 updated order:     $(order_of "$out/05-updated.png")"
    echo "05 header row:        $(header_line "$out/05-updated.png")"
} >>"$out/ocr.txt"

# 6. Under a column sort Alt+Shift+↓ answers with the notice instead of writing a rank. Clicking a
# row also opens its card, so Esc comes back to the list with the card still selected.
click_word "$out/05-updated.png" "Bravo" 0 0 || true
sleep 2.5
xdotool key --window "$win" Escape; sleep 1.5
xdotool key --window "$win" alt+shift+Down; sleep 1.5
shot 06-notice
echo "06 notice says:       $(tesseract "$out/06-notice.png" stdout --psm 11 2>/dev/null | grep -i "reordering is off" | head -1)" >>"$out/ocr.txt"

# 7. The section list's own order: the gear at the end of the checkbox row opens the page, one row
# per section with a drag handle and ▲ ▼ beside it, and ▲ on Ready moves it up one place. The gear
# and the arrows are glyphs OCR cannot read, so they are clicked by position and the receipt is the
# page's own footer line — "Nothing changed yet." before the click, "Will write: … moved above …"
# after it.
# The gear is the last cell of the checkbox flow row, which wraps after Done — and it is a glyph,
# so OCR cannot find it. The row is swept to the right of Done (past every checkbox, so a stray
# click can only land on the gear) until the section page's own words appear.
done_y=$(word_y "$out/06-notice.png" "DONE")
echo "07 gear sweep on y=$done_y: $(
    for x in $(seq 786 14 940); do
        xdotool mousemove "$x" "$done_y" click 1
        sleep 1.2
        import -window root "$sandbox/sweep.png"
        if tesseract "$sandbox/sweep.png" stdout --psm 6 2>/dev/null | grep -qi "save sections"; then
            cp "$sandbox/sweep.png" "$out/07-sections.png"; echo "opened at x=$x"; break
        fi
    done)" >>"$out/ocr.txt"
{
    echo "07 page intro:        $(tesseract "$out/07-sections.png" stdout --psm 6 2>/dev/null | grep -i "view of the statuses" | head -1)"
    echo "07 rows (name,statuses,Merge…,buttons): $(tesseract "$out/07-sections.png" stdout --psm 6 2>/dev/null | grep -i "Merge" | tr '\n' '|')"
    echo "07 footer:            $(tesseract "$out/07-sections.png" stdout --psm 6 2>/dev/null | grep -io "Nothing[^|]*" | head -1)"
} >>"$out/ocr.txt"
# The ▲ ▼ buttons and the drag handle are glyphs too, so what the page itself does is left to the
# unit test (tests/boardsections_test.cpp drives the real ▲ and the row's drop). The screenshot is
# the visual record of the row: handle, ▲ ▼, Merge… and ✕.
xdotool key --window "$win" Escape; sleep 1.5

# 8. Narrow: the two date columns and their labels go together, and the list is all that is left.
xdotool windowsize "$win" 560 "$height"; sleep 3
shot 09-narrow
convert "$out/09-narrow.png" -crop 560x260+0+30 +repage -scale 200% "$out/09-narrow-header.png"
{
    echo "09 narrow header words: $(tesseract "$out/09-narrow.png" stdout --psm 11 2>/dev/null | grep -ciE 'CREATED|UPDATED')"
    echo "09 narrow dates:        $(tesseract "$out/09-narrow.png" stdout --psm 11 2>/dev/null | grep -c '2026-')"
} >>"$out/ocr.txt"
xdotool windowsize "$win" "$width" "$height"; sleep 2

[[ -s "$sandbox/relay.log" ]] && cp "$sandbox/relay.log" "$out/relay.log"
echo "shots and ocr.txt in $out"
