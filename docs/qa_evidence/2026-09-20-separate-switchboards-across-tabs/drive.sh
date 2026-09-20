#!/usr/bin/env bash
# #TTYB, live: two Switchboard panes in two tabs of one window must not move each other. A card
# opened in tab A must leave tab B on its list with its own selection; the data broadcasts must
# still reach both panes. A board of three fixture cards under Xvfb with an isolated HOME,
# XDG_CONFIG_HOME, XDG_RUNTIME_DIR and TMPDIR (short paths: the 108-byte socket limit) and
# RELAY_KEYRING=off, in the shape of docs/qa_evidence/2026-09-20-switchboard-delete-card/drive.sh.
#
#   1. Tab A: board open, click Alpha — its card opens ("Back" header on screen).
#   2. Ctrl+T, Ctrl+Shift+S: tab B's own board. Click Bravo (selection only). B must show the
#      list — no "Back" — while A has Alpha's card open: the replication the card is about.
#   3. In A: Esc to the list, Alt+Shift+Right twice (Alpha inbox → discussing → ready).
#   4. B: still the list, and Alpha's file on disk says ready (the move reached every pane's
#      board through the worker — data stays shared).
#   5. B opens Alpha itself (click + Enter): a pane that never asked must still open cards.
#   6. A re-opens Alpha too — the same card open in both tabs — and comments on it from A.
#   7. B, still on Alpha, shows the comment (the change flowed) without anything being pushed
#      into its navigation: it asked for this card itself.
#
# Every claim is OCR of the screenshots (tesseract word boxes on a 2x upscale of the board pane,
# which docks right of the terminal at x≈690) or a read of the fixture files.
#
#   docs/qa_evidence/2026-09-20-separate-switchboards-across-tabs/drive.sh [build-dir] [out-dir]
set -uo pipefail
root=/home/elliott/repos/relay-terminal
build=${1:-$root/build}
out=${2:-$root/docs/qa_evidence/2026-09-20-separate-switchboards-across-tabs}
width=1440 height=900
mkdir -p "$out"

display=
for n in $(seq 170 199); do [[ -e /tmp/.X11-unix/X$n ]] || { display=:$n; break; }; done
[[ -z $display ]] && { echo "no free display"; exit 1; }

sandbox=$(mktemp -d /tmp/rl-ttyb.XXXX)
xvfb_pid= relay_pid=
cleanup() {
    local pid
    for pid in $relay_pid $xvfb_pid; do kill "$pid" 2>/dev/null; done
    rm -rf "$sandbox" || true
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
    "tabs: [{id: features, folder: features}]\n"
    "columns: [inbox, discussing, ready, in-progress, waiting, needs-qa, done]\n",
    encoding="utf-8")
board = B.Board(root, work)
cards = [("Alpha scratch card", "inbox", "2026-09-01", "c", 0),
         ("Bravo thread card",  "inbox", "2026-09-10", "a", 5),
         ("Charlie stays",      "ready", "2026-09-16", "b", 9)]
for title, status, created, rank, days in cards:
    card = B.new_card("work", title, status, created=created, rank=rank,
                      request=f"the ask for {title.lower()}")
    path = B.write_new_card(board, card, "features")
    when = time.time() - days * 86400
    os.utime(path, (when, when))
    print(f"{card.id}  {title}  {status}")
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
has_word() { words "$1" | awk -v want="$2" '{w=$12; sub(/^#/, "", w)} tolower(w)==tolower(want){found=1} END{exit found?0:1}'; }
lacks_word() { ! has_word "$1" "$2"; }
word_box() {  # image word [which=last|1] — the section name shows twice (checkbox label at
    # the top, engraved header in the list); the LAST one is the list header, as in the
    # switchboard-delete-card drive.
    words "$1" | awk -v want="$2" -v ox="$board_x" -v which="${3:-1}" '{w=$12; sub(/^#/, "", w)}
        tolower(w)==tolower(want) {b=sprintf("%d %d %d %d", int($7/2)+ox, int($8/2), int($9/2), int($10/2)); n++}
        END{if (n == 0) exit 1; print b}'
}
click_word() {  # image word [which] — click the middle of the word's box; fails loudly
    local box; box=$(word_box "$1" "$2" "${3:-1}")
    [[ -z $box ]] && { echo "NO BOX for $2 in $1"; return 1; }
    xdotool mousemove $(( $(echo $box | cut -d' ' -f1) + $(echo $box | cut -d' ' -f3) / 2 )) \
                         $(( $(echo $box | cut -d' ' -f2) + $(echo $box | cut -d' ' -f4) / 2 )) click 1
    sleep 1.8
}
open_card() {  # image word [which] — select the row, then open it with Enter
    click_word "$1" "$2" "${3:-1}" || { echo "FAIL  could not click $2 in $1"; fails=$((fails + 1)); return 1; }
    key Return
}
seen() {  # image — the board pane's first words, so the log says which page each shot is
    convert "$1" -crop $((width - board_x))x200+$board_x+60 +repage -scale 200% png:- 2>/dev/null \
        | tesseract - stdout --psm 11 2>/dev/null | tr -s '\n ' ' ' | head -c 90; echo
}
key() { xdotool key --delay 120 "$@"; sleep 1.6; }

fails=0
check() { # description command...
    local desc=$1; shift
    if "$@"; then echo "PASS  $desc"; else echo "FAIL  $desc"; fails=$((fails + 1)); fi
}

# 1 — tab A: board, unfold Inbox (a pane starts with every section folded), open Alpha.
key ctrl+shift+s; sleep 3
shot 01-tab-a-board
echo "  01-tab-a-board: $(seen "$out/01-tab-a-board.png")"
check "tab A board shows the list" has_word "$out/01-tab-a-board.png" Inbox
click_word "$out/01-tab-a-board.png" INBOX last
shot 01b-tab-a-unfolded
echo "  01b-tab-a-unfolded: $(seen "$out/01b-tab-a-unfolded.png")"
click_word "$out/01b-tab-a-unfolded.png" Alpha
shot 02-tab-a-card-open
echo "  02-tab-a-card-open: $(seen "$out/02-tab-a-card-open.png")"
check "tab A has Alpha's card open (Back header)" has_word "$out/02-tab-a-card-open.png" Back

# 2 — tab B: its own board, its own selection (Bravo), no card pushed into it.
key ctrl+t
key ctrl+shift+s; sleep 3
shot 03-tab-b-list
echo "  03-tab-b-list: $(seen "$out/03-tab-b-list.png")"
check "tab B shows its list, not A's card (no Back header)" lacks_word "$out/03-tab-b-list.png" Back
click_word "$out/03-tab-b-list.png" INBOX last
shot 03b-tab-b-unfolded
echo "  03b-tab-b-unfolded: $(seen "$out/03b-tab-b-unfolded.png")"
check "tab B lists its own rows (Bravo)" has_word "$out/03b-tab-b-unfolded.png" Bravo
click_word "$out/03b-tab-b-unfolded.png" Bravo   # B selects Bravo — B's own navigation

# 3 — in A: move Alpha to ready (inbox → discussing → ready). Ctrl+PgUp is the board pane's own
# previous-tab key (Ctrl+Tab does not reliably leave its editors).
key ctrl+Prior; sleep 2
shot 04-tab-a-still-open
echo "  04-tab-a-still-open: $(seen "$out/04-tab-a-still-open.png")"
check "tab A still has its card after B selected Bravo" has_word "$out/04-tab-a-still-open.png" Back
key Escape
key alt+shift+Right
key alt+shift+Right
sleep 3
alpha_md=$(grep -l "Alpha scratch card" "$work"/issues/features/*.md)
check "Alpha's file says ready (the move wrote through the worker)" \
    grep -qE "^status: '?ready'?" "$alpha_md"
alpha_id=$(sed -n "s/^id: //p" "$alpha_md" | head -1)

# 4 — B: still its list, rows updated with Alpha's move, no card pushed in.
key ctrl+Next; sleep 2
shot 05-tab-b-after-move
echo "  05-tab-b-after-move: $(seen "$out/05-tab-b-after-move.png")"
check "tab B still on its list after A's move (no Back header)" lacks_word "$out/05-tab-b-after-move.png" Back
check "tab B still shows its rows (Bravo)" has_word "$out/05-tab-b-after-move.png" Bravo

# 5 — B opens Alpha itself: a pane that never asked must still open cards.
open_card "$out/05-tab-b-after-move.png" Alpha
shot 06-tab-b-own-card
echo "  06-tab-b-own-card: $(seen "$out/06-tab-b-own-card.png")"
check "tab B opened Alpha itself (Back header)" has_word "$out/06-tab-b-own-card.png" Back

# 6 — A re-opens Alpha too: the same card open in both tabs, then A comments on it.
key ctrl+Prior; sleep 2
key Escape
shot 07-tab-a-list
echo "  07-tab-a-list: $(seen "$out/07-tab-a-list.png")"
open_card "$out/07-tab-a-list.png" Alpha
shot 08-tab-a-also-open
echo "  08-tab-a-also-open: $(seen "$out/08-tab-a-also-open.png")"
check "tab A re-opened Alpha (Back header)" has_word "$out/08-tab-a-also-open.png" Back
key c
xdotool type --delay 40 "both tabs see this"
sleep 1
key Return
sleep 4

# 7 — B, still on Alpha, shows the comment: the change flowed into a card B asked for itself.
key ctrl+Next; sleep 3
shot 09-tab-b-sees-comment
echo "  09-tab-b-sees-comment: $(seen "$out/09-tab-b-sees-comment.png")"
check "tab B's open Alpha shows A's comment" has_word "$out/09-tab-b-sees-comment.png" tabs

check "the comment is in Alpha's thread file" \
    grep -q "both tabs see this" "$work/issues/threads/$alpha_id.md"

if grep -q "gui_crash" "$sandbox/relay.log" 2>/dev/null; then
    echo "FAIL  relay logged a gui_crash"; fails=$((fails + 1))
else
    echo "PASS  no gui_crash in relay.log"
fi
cp "$sandbox/relay.log" "$out/logs/relay-live.log" 2>/dev/null || true

echo
echo "failures: $fails"
exit $(( fails > 0 ))
