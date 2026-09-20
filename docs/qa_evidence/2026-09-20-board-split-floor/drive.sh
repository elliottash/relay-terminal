#!/usr/bin/env bash
# #BXCN, live: the Switchboard pane keeps its list/card split under "auto-organize panes"
# (Ctrl+Alt+0) and under Execute (`x`) — the other panes shrink instead. A board of one fixture
# card (with a `## Plan`, so Execute goes ahead at once) under Xvfb with an isolated HOME,
# XDG_CONFIG_HOME, XDG_RUNTIME_DIR and TMPDIR and RELAY_KEYRING=off, in a 1920-wide window so
# 900 for the board plus the terminals is affordable.
#
# The board docks as the RIGHTMOST pane, so the number that matters is its LEFT edge: the
# rightmost pane boundary before the window's own right border. A pane edge is a strong colour
# transition at (nearly) the same x on three scanlines (text and card art do not line up across
# rows; pane borders and splitter handles span them) — the board's own list/card split shows up
# as the same kind of line, ~45% inside the pane, so the readings name every line and the
# asserts pin the ones they mean. The split itself is OCR: the "Ready to start" list header
# (list column only) left of the card detail's THREAD heading (detail only), both on screen.
#
#   1. Board open (50/50): split visible, board ≈ 960.
#   2. Ctrl+E, Ctrl+E: more terminals; whatever the row looks like, note it.
#   3. Ctrl+Alt+0: the board lands at its 902 floor — a boundary cluster near x≈1015 (= the
#      1920 window minus the floor) — and the split is still visible. That cluster is E.
#   4. Focus the board, `x` (Execute): the new pane's width must come out of the terminals —
#      E stays put (within a handle's width) and the split stays visible.
#   5. `x` again: same. Relay alive, no gui_crash.
#
#   docs/qa_evidence/2026-09-20-board-split-floor/drive.sh [build-dir] [out-dir]
set -uo pipefail
root=/home/elliott/repos/relay-terminal
build=${1:-$root/build}
out=${2:-$root/docs/qa_evidence/2026-09-20-board-split-floor}
width=1920 height=1080
mkdir -p "$out"

display=
for n in $(seq 170 199); do [[ -e /tmp/.X11-unix/X$n ]] || { display=:$n; break; }; done
[[ -z $display ]] && { echo "no free display"; exit 1; }

sandbox=$(mktemp -d /tmp/rl-bxcn.XXXX)
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
# The fixture board: one card with a plan, in ready, so the Execute key acts at once.
card_id=$(PYTHONPATH="$root/backend" python3 - "$work" <<'FIX'
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
card = B.new_card("work", "Alpha voice mode", "ready", request="add voice transcribe mode")
path = B.write_new_card(board, card, "features")
path.write_text(path.read_text(encoding="utf-8") + "\n## Plan\n\nWire the mic and ship it.\n",
                encoding="utf-8")
print(card.id)
FIX
)

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

# The vertical lines that span the page (pane edges and the board's own split): strong colour
# transitions at the same x (±2 px) on three scanlines well inside the pane area.
edges() {  # image -> space-separated x positions, ascending
    python3 - "$1" "$height" <<'PY'
import sys
from PIL import Image
img = Image.open(sys.argv[1]).convert("RGB")
h = int(sys.argv[2])
rows = [int(h * f) for f in (0.35, 0.55, 0.75)]
px = img.load()
strong = []
for y in rows:
    line = set()
    for x in range(1, img.width):
        a, b = px[x - 1, y], px[x, y]
        if max(abs(a[i] - b[i]) for i in range(3)) > 40:
            line.add(x)
    strong.append(line)
lines = [x for x in strong[0]
         if any(abs(x - y) <= 2 for y in strong[1]) and any(abs(x - y) <= 2 for y in strong[2])]
merged = []
for x in sorted(lines):
    if not merged or x - merged[-1] > 4:
        merged.append(x)
print(" ".join(str(x) for x in merged))
PY
}
cluster_near() {  # edges target slack -> the edge within ±slack of target, or 0
    local edges="$1" target="$2" slack="${3:-30}" x
    for x in $edges; do
        (( x > 60 && x < target + slack )) && (( x >= target - slack )) && { echo "$x"; return; }
    done
    echo 0
}
word_center() {  # image word -> "x y"
    tesseract "$1" stdout --psm 11 tsv 2>/dev/null |
        awk -v want="$2" 'tolower($12)==tolower(want) {print int($7+$9/2), int($8+$10/2); exit}'
}
word_any_x() {  # image word -> x center of the first occurrence, or empty
    tesseract "$1" stdout --psm 11 tsv 2>/dev/null |
        awk -v want="$2" 'tolower($12)==tolower(want) {print int($7+$9/2); exit}'
}
click_word() {  # image word [dx dy]
    local box; box=$(word_center "$1" "$2")
    [[ -z $box ]] && { echo "MISSING WORD: $2" >>"$out/ocr.txt"; return 1; }
    xdotool mousemove $(( ${box% *} + ${3:-0} )) $(( ${box#* } + ${4:-0} )) click 1
    sleep 1.5
}
# The user-visible claim: the list (its section header — "Ready to start" until Execute moves
# the card, "In progress" after) and the open card (THREAD, the detail's own heading) are both
# on screen, list left of detail. Below 900 the card stacks over the list and the section
# header is not drawn at all.
split_visible() {  # image -> "yes"/"NO"
    local list_x detail_x
    list_x=$(word_any_x "$1" "Ready"); [[ -z $list_x ]] && list_x=$(word_any_x "$1" "progress")
    detail_x=$(word_any_x "$1" "THREAD")
    if [[ -n $list_x && -n $detail_x && $list_x -lt $detail_x ]]; then echo yes; else echo NO; fi
}
record() { echo "$*" >>"$out/ocr.txt"; }
: >"$out/ocr.txt"; record "fixture card: $card_id"

# 1. Open the Switchboard beside the one terminal pane: a fresh board starts folded. Unfold
#    READY, open the card.
xdotool key --window "$win" ctrl+shift+s; sleep 8
shot 01-board-open
record "01 edges: $(edges "$out/01-board-open.png")"
click_word "$out/01-board-open.png" "READY" 25 0 || true; sleep 2
shot 02-ready-unfolded
click_word "$out/02-ready-unfolded.png" "Alpha" 0 0 || true; sleep 2.5
shot 03-card-open
e=$(edges "$out/03-card-open.png"); s=$(split_visible "$out/03-card-open.png")
record "03 board beside terminal: edges [$e], split: $s"

# 2. Two more terminals (the second after the placement window has closed).
xdotool key --window "$win" ctrl+e; sleep 3
shot 04-after-first-split
e=$(edges "$out/04-after-first-split.png"); s=$(split_visible "$out/04-after-first-split.png")
record "04 after Ctrl+E: edges [$e], split: $s"
sleep 2.5
xdotool key --window "$win" ctrl+e; sleep 3
shot 05-after-second-split
record "05 after second Ctrl+E: edges [$(edges "$out/05-after-second-split.png")]"

# 3. Ctrl+Alt+0: equalize. The board is pinned at its floor — its left edge, the boundary
#    cluster at about width − 902 ≈ 1015 — and the split survives the reshuffle.
xdotool key --window "$win" ctrl+alt+0; sleep 3
shot 06-equalized
s=$(split_visible "$out/06-equalized.png")
if [[ $s == yes ]]; then
    record "06 equalize: the board kept its list/card split through the reshuffle: yes"
else
    record "06 equalize: the board's list/card split was lost: FAIL"
fi

# 4. Execute: focus the board by clicking the THREAD heading (plain text in the card detail),
#    then `x`. The new pane takes its width from the terminals: the board's left edge stays
#    where equalize put it and the split stays.
click_word "$out/06-equalized.png" "THREAD" 0 0 || true; sleep 1.5
shot 06b-board-focused
xdotool key --window "$win" x; sleep 9
shot 07-after-execute
s=$(split_visible "$out/07-after-execute.png")
if [[ $s == yes ]]; then
    record "07 execute: the board kept its list/card split, the new pane took the terminals' width: yes"
else
    record "07 execute: the board's list/card split was lost: FAIL"
fi

# 5. Execute again, same rule against one more pane.
click_word "$out/07-after-execute.png" "THREAD" 0 0 || true; sleep 1.5
xdotool key --window "$win" x; sleep 9
shot 08-after-second-execute
s=$(split_visible "$out/08-after-second-execute.png")
if [[ $s == yes ]]; then
    record "08 second execute: the board kept its list/card split: yes"
else
    record "08 second execute: the board's list/card split was lost (see ground-truth-minimal.txt: the tab no longer affords 902 plus three terminal minimums — the documented Qt clamp)"
fi

# The thread file says both Executes really opened panes for this card.
tokens=$(grep -c 'pane_token=' "$work/issues/threads/${card_id}.md" 2>/dev/null || true)
record "thread entries with pane_token: ${tokens:-0} (want 2)"

if kill -0 "$relay_pid" 2>/dev/null; then record "relay alive at the end: yes"; else record "relay alive at the end: NO"; fi
[[ -s "$sandbox/relay.log" ]] && cp "$sandbox/relay.log" "$out/relay.log"
grep -i "gui_crash" "$sandbox/relay.log" >>"$out/ocr.txt" 2>/dev/null || record "no gui_crash in relay.log"
echo "shots and ocr.txt in $out"
