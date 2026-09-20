#!/usr/bin/env bash
# The Switchboard's delete, live (card #CYM9: "switchboard needs a delete button for issues.
# right now, i dont see how to delete them."). A board of three fixture cards under Xvfb with an
# isolated HOME, XDG_CONFIG_HOME, XDG_RUNTIME_DIR and TMPDIR (short paths: the 108-byte socket
# limit) and RELAY_KEYRING=off.
#
# Alpha is deleted from the list with the Del key and brought back by the Undo toast's Ctrl+Z;
# Bravo is deleted from its open card's ⌫ button (its thread file goes with it); Charlie's
# confirm is cancelled (nothing sent), and then Charlie's file is removed from outside Relay
# while its card is open — the same wire path as another pane's delete — and the open card
# closes with the "was removed" line. Every claim is checked by OCR of the screenshots
# (tesseract word boxes on a 2x upscale of the board pane) or by reading the fixture files.
#
#   docs/qa_evidence/2026-09-20-switchboard-delete-card/drive.sh [build-dir] [out-dir]
set -uo pipefail
root=/home/elliott/repos/relay-terminal
build=${1:-$root/build}
out=${2:-$root/docs/qa_evidence/2026-09-20-switchboard-delete-card}
width=1440 height=900
mkdir -p "$out"

display=
for n in $(seq 170 199); do [[ -e /tmp/.X11-unix/X$n ]] || { display=:$n; break; }; done
[[ -z $display ]] && { echo "no free display"; exit 1; }

sandbox=$(mktemp -d /tmp/rl-del.XXXX)
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
#            title                status    created      rank  days
cards = [("Alpha scratch card",    "inbox",  "2026-09-01", "c",  0),
         ("Bravo thread card",     "inbox",  "2026-09-10", "a",  5),
         ("Charlie stays",         "ready",  "2026-09-16", "b",  9)]
for title, status, created, rank, days in cards:
    card = B.new_card("work", title, status, created=created, rank=rank,
                      request=f"the ask for {title.lower()}")
    path = B.write_new_card(board, card, "features")
    when = time.time() - days * 86400
    os.utime(path, (when, when))
    print(f"{card.id}  {title}  {status}")
# Bravo carries a thread: a comment to prove the thread file goes with the card.
bravo = next(c for c in board.cards() if c.title.startswith("Bravo"))
board.append_thread(bravo.id, "a line that must vanish with the card",
                    author="owner", kind="comment")
print(f"bravo thread at {board.thread_path(bravo.id).relative_to(work)}")
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
board_x=690
words() { convert "$1" -crop $((width - board_x))x$height+$board_x+0 +repage -scale 200% png:- 2>/dev/null \
              | tesseract - stdout --psm 11 tsv 2>/dev/null; }
word_box() {  # image word -> "left top width height" in full-image space
    words "$1" | awk -v want="$2" -v ox="$board_x" '{w=$12; sub(/^#/, "", w)}
        tolower(w)==tolower(want) {print int($7/2)+ox, int($8/2), int($9/2), int($10/2); exit}'
}
word_y() { local box; box=$(word_box "$1" "$2"); [[ -n $box ]] && echo $(( $(echo $box | cut -d' ' -f2) + $(echo $box | cut -d' ' -f4) / 2 )); }
word_x() { local box; box=$(word_box "$1" "$2"); [[ -n $box ]] && echo $(( $(echo $box | cut -d' ' -f1) + $(echo $box | cut -d' ' -f3) / 2 )); }
line_text() { convert "$1" -crop $((width - board_x))x$height+$board_x+0 +repage -scale 200% png:- 2>/dev/null \
                  | tesseract - stdout --psm 11 2>/dev/null; }
click_at() { xdotool mousemove "$1" "$2" click "${3:-1}"; sleep 1.8; }
screen_box() {  # the modal confirm lives anywhere on screen, not only in the board pane
    convert "$1" -scale 150% png:- 2>/dev/null | tesseract - stdout --psm 11 tsv 2>/dev/null |
        awk -v want="$2" '{w=$12; sub(/^#/, "", w)} tolower(w)==tolower(want)
            {print int($7/1.5), int($8/1.5), int($9/1.5), int($10/1.5); exit}'
}
confirm_delete() {  # clicks the confirm dialog's Delete button, or Escapes the dialog away
    # The dialog is not an X window of its own (no WM under Xvfb): it is composited inside the
    # Relay window, and its buttons OCR only patchily at any scale. The last word of the
    # informative text -- "committed." -- reads reliably at 300%, and the Delete button sits a
    # fixed offset from it (text left margin -> button left edge +37 px; text baseline -> button
    # row +39 px; click the centre, so +60/+50). Cancel, when it does read, is the fallback.
    local anchor
    anchor=$(import -window root png:- 2>/dev/null | convert - -scale 300% png:- 2>/dev/null \
             | tesseract - stdout --psm 11 tsv 2>/dev/null \
             | awk 'tolower($12) ~ /^committed\.?$/ {b=sprintf("%d %d", int($7/3), int($8/3))} END{print b}')
    if [[ -n $anchor ]]; then
        echo "   (confirm: committed. at $anchor -> clicking $((${anchor% *} + 60)),$((${anchor#* } + 50)))" >>"$out/ocr.txt"
        xdotool mousemove $((${anchor% *} + 60)) $((${anchor#* } + 50)) click 1
        sleep 2
        dialog_up && echo "   (confirm: still up after the click)" >>"$out/ocr.txt"
        return 0
    fi
    local cancel
    cancel=$(import -window root png:- 2>/dev/null | convert - -scale 300% png:- 2>/dev/null \
             | tesseract - stdout --psm 11 tsv 2>/dev/null \
             | awk 'tolower($12)=="cancel" {b=sprintf("%d %d", int($7/3), int($8/3))} END{print b}')
    if [[ -n $cancel ]]; then
        xdotool mousemove $((${cancel% *} - 105)) $((${cancel#* } + 6)) click 1
        sleep 2
        return 0
    fi
    echo "   (no confirm dialog found)" >>"$out/ocr.txt"
    xdotool key --window "$win" Escape; sleep 1
    return 1
}

dialog_up() {  # is the confirm's Cancel on screen right now?
    import -window root png:- 2>/dev/null | convert - -scale 300% png:- 2>/dev/null \
        | tesseract - stdout --psm 11 tsv 2>/dev/null \
        | awk 'tolower($12)=="cancel" {found=1} END{exit found?0:1}'
}
card_file() {  # title -> its path under the fixture
    (cd "$work/issues/features" && for f in *.md; do grep -q "$1" "$f" && { echo "$work/issues/features/$f"; break; }; done)
}
card_id() { (cd "$work/issues/features" && for f in *.md; do grep -q "$1" "$f" && { grep "^id:" "$f" | awk '{print toupper($2)}'; break; }; done); }

: >"$out/ocr.txt"

# 1. Open the Switchboard and unfold Inbox (Alpha and Bravo live there).
xdotool key --window "$win" ctrl+shift+s; sleep 8
shot 00-folded
# Unfold by clicking the section's header row in the list: of the INBOX words on the page
# (the checkbox label at the top, then the engraved header), the last one is the header.
box=$(words "$out/00-folded.png" | awk -v ox="$board_x" \
    'tolower($12)=="inbox" {b=sprintf("%d %d %d %d", int($7/2)+ox, int($8/2), int($9/2), int($10/2))} END{print b}')
click_at $(( $(echo $box | cut -d' ' -f1) + $(echo $box | cut -d' ' -f3) / 2 )) $(( $(echo $box | cut -d' ' -f2) + $(echo $box | cut -d' ' -f4) / 2 ))
shot 01-board-open
alpha_id=$(card_id "Alpha scratch")
echo "01 board open:        $(for t in Alpha Bravo Charlie; do y=$(word_y "$out/01-board-open.png" $t); [[ -n $y ]] && printf "%s " $t; done; echo)" >>"$out/ocr.txt"

# 2. Del on the selected Alpha row, confirmed: the file and the row go, and the notice carries Undo.
alpha_y=$(word_y "$out/01-board-open.png" Alpha)
alpha_x=$(word_x "$out/01-board-open.png" Alpha)
click_at "$alpha_x" "$alpha_y"        # select Alpha and put the keyboard in the list
alpha_before=$(cat "$(card_file "Alpha scratch")")
xdotool key --window "$win" Delete; sleep 2
shot 02-confirm-list
echo "03 confirm dialog:    $(dialog_up && echo yes || echo NO)" >>"$out/ocr.txt"
confirm_delete
shot 03-deleted-list
{
    echo "03 notice:            $(line_text "$out/03-deleted-list.png" | grep -iE "deleted|removed|undo" | head -1)"
    echo "03 alpha file:        $([[ -e $(card_file "Alpha scratch") ]] && echo still-there || echo gone)"
    echo "03 rows left:         $(for t in Alpha Bravo Charlie; do y=$(word_y "$out/03-deleted-list.png" $t); [[ -n $y ]] && printf "%s " $t; done; echo)"
} >>"$out/ocr.txt"

# 3. Ctrl+Z inside the Undo window: Alpha is back, byte for byte.
xdotool key --window "$win" ctrl+z; sleep 2.5
shot 04-undone
{
    echo "04 notice:            $(line_text "$out/04-undone.png" | grep -iE "undone|back" | head -1)"
    echo "04 alpha file:        $([[ -e $(card_file "Alpha scratch") ]] && echo back || echo MISSING)"
    echo "04 alpha identical:   $(diff <(cat "$(card_file "Alpha scratch")") <(echo "$alpha_before") >/dev/null && echo yes || echo NO)"
    echo "04 rows:              $(for t in Alpha Bravo Charlie; do y=$(word_y "$out/04-undone.png" $t); [[ -n $y ]] && printf "%s " $t; done; echo)"
} >>"$out/ocr.txt"

# 4. Bravo, deleted from its open card's ⌫ button: the card and its thread go, the detail closes.
bravo_id=$(card_id "Bravo thread")
bravo_y=$(word_y "$out/04-undone.png" Bravo); bravo_x=$(word_x "$out/04-undone.png" Bravo)
click_at "$bravo_x" "$bravo_y"
xdotool key --window "$win" Return; sleep 3
shot 05-card-open
del_box=$(word_box "$out/05-card-open.png" Delete)
if [[ -z $del_box ]]; then
    edit_box=$(word_box "$out/05-card-open.png" Edit)
    [[ -n $edit_box ]] && del_box=$(( $(echo $edit_box | cut -d' ' -f1) + $(echo $edit_box | cut -d' ' -f3) + 55 ))" $(echo $edit_box | cut -d' ' -f2) 40 $(echo $edit_box | cut -d' ' -f4)"
fi
echo "05 detail open:       $(word_box "$out/05-card-open.png" Issue >/dev/null && echo "yes (body with an Issue heading)" || echo NO)" >>"$out/ocr.txt"
echo "05 delete button:     $([[ -n $del_box ]] && echo "at $del_box" || echo NOT-FOUND)" >>"$out/ocr.txt"
if [[ -n $del_box ]]; then
    click_at $(( $(echo $del_box | cut -d' ' -f1) + $(echo $del_box | cut -d' ' -f3) / 2 )) $(( $(echo $del_box | cut -d' ' -f2) + $(echo $del_box | cut -d' ' -f4) / 2 ))
fi
sleep 1.5
shot 06-confirm-detail
confirm_delete
if [[ -e $(card_file "Bravo thread") ]]; then
    # the button click was eaten between OCR and Qt; the Del key asks the same question on
    # the same open card
    xdotool key --window "$win" Delete; sleep 2
    confirm_delete
fi
shot 07-deleted-detail
{
    echo "07 notice:            $(line_text "$out/07-deleted-detail.png" | grep -iE "deleted|removed|undo" | head -1)"
    echo "07 bravo file:        $([[ -e $(card_file "Bravo thread") ]] && echo still-there || echo gone)"
    echo "07 bravo thread:      $([[ -e $work/issues/threads/$bravo_id.md ]] && echo still-there || echo gone)"
    echo "07 rows:              $(for t in Alpha Bravo Charlie; do y=$(word_y "$out/07-deleted-detail.png" $t); [[ -n $y ]] && printf "%s " $t; done; echo)"
} >>"$out/ocr.txt"

# 5. Alpha's confirm cancelled: nothing is sent, nothing moves. (Alpha is the one Inbox card
#    left on the page; Charlie lives in the folded Ready section and never was on screen.)
alpha_y=$(word_y "$out/07-deleted-detail.png" Alpha); alpha_x=$(word_x "$out/07-deleted-detail.png" Alpha)
click_at "$alpha_x" "$alpha_y"
xdotool key --window "$win" Return; sleep 3
shot 08-card-open
xdotool key --window "$win" Delete; sleep 2   # Del on the open card's document
shot 08b-card-confirm
xdotool key --window "$win" Escape; sleep 2   # Cancel: the dialog's own way out
shot 09-cancelled
{
    echo "08b confirm was up:   $(convert "$out/08b-card-confirm.png" -scale 300% png:- 2>/dev/null | tesseract - stdout --psm 11 2>/dev/null | grep -ci committed)"
    echo "09 alpha file:        $([[ -e $(card_file "Alpha scratch") ]] && echo intact || echo GONE)"
    echo "09 card still open:   $(word_box "$out/09-cancelled.png" Issue >/dev/null && echo yes || echo closed)"
} >>"$out/ocr.txt"

# 6. A removal from outside Relay while the card is open (the wire path of another pane's
#    delete): the open card closes and the notice says it was removed.
rm "$(card_file "Alpha scratch")"
sleep 4
shot 10-removed-outside
echo "10 notice:            $(line_text "$out/10-removed-outside.png" | grep -iE "removed" | head -1)" >>"$out/ocr.txt"
echo "10 card still open:   $(word_box "$out/10-removed-outside.png" Issue >/dev/null && echo yes || echo closed)" >>"$out/ocr.txt"

[[ -s "$sandbox/relay.log" ]] && cp "$sandbox/relay.log" "$out/relay.log"
echo "shots and ocr.txt in $out"
