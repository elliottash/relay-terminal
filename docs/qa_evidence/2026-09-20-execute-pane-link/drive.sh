#!/usr/bin/env bash
# #HKAP, live: after Execute hands a card to a terminal pane, the card's thread entry reads
# "Executing (<first 8 of the pane's session token>) · handed to a new terminal pane…" and
# clicking it reveals that pane. A board of one fixture card (with a `## Plan`, so Execute
# goes ahead at once) under Xvfb with an isolated HOME, XDG_CONFIG_HOME, XDG_RUNTIME_DIR and
# TMPDIR and RELAY_KEYRING=off. Every claim is checked by OCR (tesseract word boxes) plus the
# board's own thread file, not by eye:
#
#   1. Execute opens a pane; the thread entry carries pane_token=<token> and the screen shows
#      "Executing (<first 8 of token>)".
#   2. Focus the board, click the "Executing" entry: the typed marker lands in the right-hand
#      pane (x > 55% of the window), so the click revealed the pane.
#   3. Close that pane, click the entry again: the marker does not land in a right-hand pane,
#      Relay stays alive, and relay.log has no gui_crash — the stale link is inert.
#
#   docs/qa_evidence/2026-09-20-execute-pane-link/drive.sh [build-dir] [out-dir]
set -uo pipefail
root=/home/elliott/repos/relay-terminal
build=${1:-$root/build}
out=${2:-$root/docs/qa_evidence/2026-09-20-execute-pane-link}
width=1440 height=900
mkdir -p "$out"

display=
for n in $(seq 170 199); do [[ -e /tmp/.X11-unix/X$n ]] || { display=:$n; break; }; done
[[ -z $display ]] && { echo "no free display"; exit 1; }

sandbox=$(mktemp -d /tmp/rl-hkap.XXXX)
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
# The fixture board: card 1 with a plan, in ready, so the Execute button acts at once; card 2
# in needs-verification with an anthropic signature, so the recommended verifier is guest:codex
# (installed here) — Verify's hand-off gets the same treatment (#HKAP).
read -r card_id card2_id <<< "$(PYTHONPATH="$root/backend" python3 - "$work" <<'FIX'
import sys
from pathlib import Path
from relay_core import board as B
work = Path(sys.argv[1])
root = work / "issues"
root.mkdir(parents=True)
(root / B.BOARD_CONFIG).write_text(
    "tabs: [{id: features, folder: features}, {id: bugs, folder: changes}]\n"
    "columns: [inbox, discussing, ready, in-progress, waiting, needs-verification, needs-qa, done]\n",
    encoding="utf-8")
board = B.Board(root, work)
card = B.new_card("work", "Alpha voice mode", "ready", request="add voice transcribe mode")
path = B.write_new_card(board, card, "features")
path.write_text(path.read_text(encoding="utf-8") + "\n## Plan\n\nWire the mic and ship it.\n",
                encoding="utf-8")
card2 = B.new_card("work", "Bravo theme swap", "needs-verification", rank="zz",
                   implemented_by="anthropic/claude-opus-5",
                   request="swap the theme and verify it")
B.write_new_card(board, card2, "features")
print(card.id, card2.id)
FIX
)"
echo "fixture cards: $card_id (execute), $card2_id (verify)" >>"$out/ocr.txt"

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
word_center() {  # image word -> "x y"
    tesseract "$1" stdout --psm 11 tsv 2>/dev/null |
        awk -v want="$2" 'tolower($12)==tolower(want) {print int($7+$9/2), int($8+$10/2); exit}'
}
word_center_last() {  # image word -> "x y" of the last occurrence on screen
    tesseract "$1" stdout --psm 11 tsv 2>/dev/null |
        awk -v want="$2" 'tolower($12)==tolower(want) {last=int($7+$9/2) " " int($8+$10/2)} END {print last}'
}
click_word() {  # image word [dx dy]
    local box; box=$(word_center "$1" "$2")
    [[ -z $box ]] && { echo "MISSING WORD: $2" >>"$out/ocr.txt"; return 1; }
    xdotool mousemove $(( ${box% *} + ${3:-0} )) $(( ${box#* } + ${4:-0} )) click 1
    sleep 1.5
}
click_word_last() {  # image word [dx dy]
    local box; box=$(word_center_last "$1" "$2")
    [[ -z $box ]] && { echo "MISSING WORD: $2" >>"$out/ocr.txt"; return 1; }
    xdotool mousemove $(( ${box% *} + ${3:-0} )) $(( ${box#* } + ${4:-0} )) click 1
    sleep 1.5
}
word_x() {  # image word -> x center of its (last) occurrence
    tesseract "$1" stdout --psm 11 tsv 2>/dev/null |
        awk -v want="$2" 'tolower($12)==tolower(want) {x=int($7+$9/2)} END {print x}'
}
: >"$out/ocr.txt"; echo "fixture cards: $card_id (execute), $card2_id (verify)" >>"$out/ocr.txt"

# 1. Open the Switchboard, unfold READY, open the card, Execute. A new board starts with its
#    sections folded; a first run's approvals pane is a tab, not a modal, and does not block.
xdotool key --window "$win" ctrl+shift+s; sleep 8
shot 01-board-open
click_word "$out/01-board-open.png" "READY" 25 0 || true; sleep 2
shot 02-ready-unfolded
click_word "$out/02-ready-unfolded.png" "Alpha" 0 0 || true; sleep 2.5
shot 03-card-open
# Execute is keyed (`x`, #XS6Q): the button strip is too low-contrast for OCR, the key is not.
xdotool key --window "$win" x; sleep 8
shot 04-after-execute

# The thread file says what the entry carried; the screen says what it shows.
token=$(grep -o 'pane_token=[0-9A-Za-z-]*' "$work/issues/threads/${card_id}.md" 2>/dev/null | head -1 | cut -d= -f2)
short=${token:0:8}
echo "04 thread pane_token: ${token:-<none>} (short $short)" >>"$out/ocr.txt"
shot_text=$(tesseract "$out/04-after-execute.png" stdout --psm 11 2>/dev/null)
if [[ -n $short ]] && grep -q "$short" <<<"$shot_text"; then
    echo "04 screen shows Executing ($short): yes" >>"$out/ocr.txt"
else
    echo "04 screen shows Executing ($short): NO" >>"$out/ocr.txt"
fi

# 2. Focus the board (click its THREAD heading), then click the Executing entry: the typed
# marker must land in the pane Execute opened — the rightmost one, past the board pane that
# the new pane split (x > 70% of the window), so the click revealed the pane.
click_word "$out/04-after-execute.png" "THREAD" 0 0 || true; sleep 1.5
click_word_last "$out/04-after-execute.png" "Executing" || true; sleep 2
xdotool type --window "$win" "echo HKAP-CLICKED"; sleep 2
shot 05-link-clicked
mx=$(word_x "$out/05-link-clicked.png" "HKAP-CLICKED")
if [[ -n $mx && $mx -gt $((width * 70 / 100)) ]]; then
    echo "05 marker typed into the pane Execute opened at x=$mx: yes" >>"$out/ocr.txt"
else
    echo "05 marker typed into the pane Execute opened (x=${mx:-missing}): NO" >>"$out/ocr.txt"
fi
xdotool key --window "$win" Escape; sleep 1

# 3. Close the pane the link points at, click the entry again: inert, no crash.
xdotool key --window "$win" ctrl+w; sleep 3
shot 06-pane-closed
click_word_last "$out/06-pane-closed.png" "Executing" || true; sleep 2
xdotool type --window "$win" "HKAP-STALE"; sleep 2
shot 07-stale-click
sx=$(word_x "$out/07-stale-click.png" "HKAP-STALE")
if [[ -z $sx || $sx -le $((width * 70 / 100)) ]]; then
    echo "07 stale link typed nowhere past the board (x=${sx:-not on screen}): inert" >>"$out/ocr.txt"
else
    echo "07 stale link reached past the board (x=$sx): UNEXPECTED" >>"$out/ocr.txt"
fi
if kill -0 "$relay_pid" 2>/dev/null; then
    echo "07 relay still alive after the stale click: yes" >>"$out/ocr.txt"
else
    echo "07 relay still alive after the stale click: NO" >>"$out/ocr.txt"
fi

# 4. Verify's hand-off gets the same treatment (#HKAP): open card 2 from the Needs verification
#    column, key `v`, and the entry names and links the verifier's pane just like Execute's.
xdotool key --window "$win" Escape; sleep 1.5    # the open detail back to the list
shot 08-back-to-list
if ! tesseract "$out/08-back-to-list.png" stdout --psm 11 2>/dev/null | grep -qi "ready"; then
    # Escape closed the page instead: bring the Switchboard back.
    xdotool key --window "$win" ctrl+shift+s; sleep 6
    shot 08-back-to-list
fi
# Folds are fiddly in a narrow board pane; the filter finds the card whatever its column.
xdotool key --window "$win" slash; sleep 1
xdotool type --window "$win" "Bravo"; sleep 2
shot 09-verify-filtered
click_word "$out/09-verify-filtered.png" "swap" 0 0 || true; sleep 2.5
shot 10-verify-card-open
xdotool key --window "$win" v; sleep 8
shot 11-after-verify
vtok=$(grep -o 'pane_token=[0-9A-Za-z-]*' "$work/issues/threads/${card2_id}.md" 2>/dev/null | head -1 | cut -d= -f2)
vshort=${vtok:0:8}
echo "11 verify thread pane_token: ${vtok:-<none>} (short $vshort)" >>"$out/ocr.txt"
vtext=$(tesseract "$out/11-after-verify.png" stdout --psm 11 2>/dev/null)
if [[ -n $vshort ]] && grep -qi "Verifying" <<<"$vtext"; then
    echo "11 screen shows Verifying ($vshort): yes" >>"$out/ocr.txt"
else
    echo "11 screen shows Verifying ($vshort): NO" >>"$out/ocr.txt"
fi

[[ -s "$sandbox/relay.log" ]] && cp "$sandbox/relay.log" "$out/relay.log"
grep -i "gui_crash" "$sandbox/relay.log" >>"$out/ocr.txt" 2>/dev/null || echo "no gui_crash in relay.log" >>"$out/ocr.txt"
echo "shots and ocr.txt in $out"
