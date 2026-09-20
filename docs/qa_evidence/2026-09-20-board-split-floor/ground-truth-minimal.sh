#!/usr/bin/env bash
# Ground truth, minimal scenario for #BXCN: no Ctrl+E at all, so the splitter is flat and every
# expected size is computable. Board + one terminal, then:
#   Ctrl+Alt+0  -> the board is pinned at its floor (902 + chrome) and the terminal has the rest
#   x (Execute) -> the newcomer's half comes out of the terminal, the board keeps the floor
#   x again     -> same, one more pane
# Every reading is the layout Relay itself saved: $XDG_DATA_HOME/relay/state/windows.json, whose
# nodes carry "split" and "sizes", dumped after each step.
#   ground-truth-minimal.sh [build-dir] [out-dir]
set -uo pipefail
root=/home/elliott/repos/relay-terminal
build=${1:-$root/build}
out=${2:-$root/docs/qa_evidence/2026-09-20-board-split-floor}
width=2560 height=1080
mkdir -p "$out"

display=
for n in $(seq 170 199); do [[ -e /tmp/.X11-unix/X$n ]] || { display=:$n; break; }; done
[[ -z $display ]] && { echo "no free display"; exit 1; }

sandbox=$(mktemp -d /tmp/rl-bxcn-min.XXXX)
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
xdotool windowmove "$win" 0 0; xdotool windowsize "$win" "$width" "$height"
xdotool windowfocus "$win"; sleep 2
for attempt in 1 2 3; do
    eval "$(xdotool getwindowgeometry --shell "$win")"
    (( WIDTH >= width - 2 )) && break
    xdotool windowsize "$win" "$width" "$height"; sleep 2
done
sleep 4

word_center() {
    tesseract "$1" stdout --psm 11 tsv 2>/dev/null |
        awk -v want="$2" 'tolower($12)==tolower(want) {print int($7+$9/2), int($8+$10/2); exit}'
}
click_word() {
    local box; box=$(word_center "$1" "$2")
    [[ -z $box ]] && { echo "MISSING WORD: $2"; return 1; }
    xdotool mousemove $(( ${box% *} + ${3:-0} )) $(( ${box#* } + ${4:-0} )) click 1
    sleep 1.5
}
gt="$out/ground-truth-minimal.txt"
dump_layout() {  # tag
    sleep 12   # let the debounced save land (shorter reads caught the previous state)
    python3 - "$XDG_DATA_HOME/relay/state/windows.json" "$1" "$gt" <<'PY'
import json, sys
path, tag, out = sys.argv[1:4]
line = open(out, "a")
print(f"== {tag} ==", file=line)
try:
    doc = json.load(open(path))
except Exception as error:
    print(f"  no layout file yet: {error}", file=line)
    sys.exit(0)
def walk(node, depth=0):
    pad = "  " * depth
    if "split" in node:
        sizes = node.get("sizes", [])
        print(f"{pad}{node['split']} {sizes} (sum {sum(sizes)})", file=line)
        for child in node.get("children", []):
            walk(child, depth + 1)
    else:
        label = "BOARD" if "board" in node else "pane"
        print(f"{pad}{label}", file=line)
for window in doc.get("windows", []):
    for tab in window.get("tabs", []):
        walk(tab.get("node", tab), 1)
PY
}
: >"$gt"
echo "fixture card: $card_id; window asked ${width}x${height}" >>"$gt"

xdotool key --window "$win" ctrl+shift+s; sleep 8
import -window root "$out/gtm-01-board-open.png"
click_word "$out/gtm-01-board-open.png" "READY" 25 0 || true; sleep 2
import -window root "$out/gtm-02-unfolded.png"
click_word "$out/gtm-02-unfolded.png" "Alpha" 0 0 || true; sleep 2.5
dump_layout "board open beside the one terminal, card open"

xdotool key --window "$win" ctrl+alt+0; sleep 13
import -window root "$out/gtm-03-equalized.png"
dump_layout "after Ctrl+Alt+0 (equalize)"

click_word "$out/gtm-03-equalized.png" "THREAD" 0 0 || true; sleep 1.5
xdotool key --window "$win" x; sleep 10
import -window root "$out/gtm-04-executed.png"
dump_layout "after first Execute (x)"

click_word "$out/gtm-04-executed.png" "THREAD" 0 0 || true; sleep 1.5
xdotool key --window "$win" x; sleep 10
import -window root "$out/gtm-05-executed-twice.png"
dump_layout "after second Execute (x)"

tokens=$(grep -c 'pane_token=' "$work/issues/threads/${card_id}.md" 2>/dev/null || true)
echo "thread entries with pane_token: ${tokens:-0} (want 2)" >>"$gt"
if kill -0 "$relay_pid" 2>/dev/null; then echo "relay alive at the end: yes" >>"$gt"; else echo "relay alive at the end: NO" >>"$gt"; fi
grep -i "gui_crash" "$sandbox/relay.log" >>"$gt" 2>/dev/null || echo "no gui_crash in relay.log" >>"$gt"
[[ -s "$sandbox/relay.log" ]] && cp "$sandbox/relay.log" "$out/ground-truth-minimal-relay.log"
echo "ground truth in $gt"
