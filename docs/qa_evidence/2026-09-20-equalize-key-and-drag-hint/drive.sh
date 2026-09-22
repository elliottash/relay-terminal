#!/usr/bin/env bash
# #GSJ7, live: resizing a pane teaches Alt+0 ("auto-resize"), Alt+0 equalizes, and the "?" list
# shows the key.
#
#   1. Ctrl+E splits the tab into two side-by-side panes. Ground truth is the app's own record:
#      every splitter move schedules a save of $XDG_DATA_HOME/relay/state/windows.json, whose
#      nodes carry their "split" orientation and "sizes".
#   2. Drag the divider right with the left button held (a real drag: Relay only teaches the key
#      when the button is down, because QSplitter::splitterMoved also fires for its own
#      setSizes()). The sizes must change, and the pane's toast must read
#      "Next time: Alt+0 · equal panes".
#   3. Alt+0 ("auto-resize") puts the splitter back to its equal share.
#   4. F1 opens the "?" list (Actions); typing "auto resize" must find the equalize entry with
#      Alt+0 beside it.
#   5. Nothing crashed: no gui_crash in relay.log.
#
#   docs/qa_evidence/2026-09-20-equalize-key-and-drag-hint/drive.sh [build-dir] [out-dir]
set -uo pipefail
root=/home/elliott/repos/relay-terminal
build=${1:-$root/build}
out=${2:-$root/docs/qa_evidence/2026-09-20-equalize-key-and-drag-hint}
width=1400 height=900
mkdir -p "$out"

display=
for n in $(seq 170 199); do [[ -e /tmp/.X11-unix/X$n ]] || { display=:$n; break; }; done
[[ -z $display ]] && { echo "no free display"; exit 1; }

sandbox=$(mktemp -d /tmp/rl-gsj7.XXXX)
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
# onboarded: no instructions prompt. approvals_chosen + an empty ask list: the first-launch
# Approvals pane stays away, so the two panes are the whole page and the divider is where the
# layout file says it is.
cat >"$XDG_CONFIG_HOME/RelayTerminal/relay.conf" <<'CONF'
[instructions]
onboarded=true

[security]
approvals_chosen=true
approvals_ask=@Invalid()
CONF

work=$HOME/project
mkdir -p "$work"
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
xdotool windowfocus "$win"; sleep 4
for attempt in 1 2 3; do
    eval "$(xdotool getwindowgeometry --shell "$win")"
    (( WIDTH >= width - 2 )) && break
    xdotool windowsize "$win" "$width" "$height"; sleep 2
done
echo "window ${WIDTH}x${HEIGHT} (wanted ${width}x${height})" >>"$out/ocr.txt"
sleep 4

shot() { import -window root "$out/$1.png"; }
ocr() { tesseract "$1" stdout --psm 11 2>/dev/null | tr '\n' ' ' | tr -s ' '; }
# OCR reads the digit 0 in "Alt+0" as an o or a D often enough that the check normalizes first.
flat() { tr -d ' \n' <<<"$1" | tr 'O' '0' | tr 'o' '0'; }
record() { echo "$*" >>"$out/ocr.txt"; }

# The app's own layout record: the horizontal split and its sizes.
sizes_of() {  # -> sizes of the first horizontal split, space-separated
    python3 - "$XDG_DATA_HOME/relay/state/windows.json" <<'PY'
import json, sys
try:
    doc = json.load(open(sys.argv[1]))
except Exception as error:
    print("no layout file yet: %s" % error)
    sys.exit(0)
found = []
def walk(node):
    if "split" in node:
        if node["split"] == "h" and not found:
            found.append(node.get("sizes", []))
        for child in node.get("children", []):
            walk(child)
for window in doc.get("windows", []):
    for tab in window.get("tabs", []):
        walk(tab.get("node", tab))
print(" ".join(str(s) for s in found[0]) if found else "none")
PY
}
equal() { python3 -c 'import sys; a=[int(x) for x in sys.argv[1].split()]; print("yes" if len(a)==2 and abs(a[0]-a[1])<=24 else "no")' "$1"; }
moved() { python3 -c 'import sys; a=[int(x) for x in sys.argv[1].split()]; b=[int(x) for x in sys.argv[2].split()]; print("yes" if len(a)==2 and len(b)==2 and abs(a[0]-b[0])>40 else "no")' "$1" "$2"; }
# The divider of a two-pane row sits just past the first pane's share of the page.
first_share() { local a; a=($1); echo "${a[0]:-0}"; }
# The window's own pixel edges, for the record: strong vertical transitions that span the page.
edges() {
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

: >"$out/ocr.txt"

# 1. Split: two panes side by side.
xdotool key --window "$win" ctrl+e; sleep 3
sleep 2.5
shot 01-split
s0=$(sizes_of)
record "01 after Ctrl+E: split sizes [$s0] (equal: $(equal "$s0")), pixel edges [$(edges "$out/01-split.png")]"
record "01 pane shapes: $(rg -o '"split": ?"[a-z]+"' "$XDG_DATA_HOME/relay/state/windows.json" | sort -u | tr '\n' ' ')"
(( $(first_share "$s0") > 200 )) || { record "01 no two-pane row to drag · FAIL"; tail -8 "$out/ocr.txt"; exit 1; }

# 2. Drag the divider right with the button held. Try the computed position and its neighbours
#    until the sizes actually move (the handle is 3 px wide).
s1=$s0
for offset in 0 -3 3 -6 6 -9; do
    dx=$(( $(first_share "$s0") + offset + 2 ))
    xdotool mousemove "$dx" 450; sleep 0.4
    xdotool mousedown 1; sleep 0.3
    xdotool mousemove $((dx + 60)) 450; sleep 0.3
    xdotool mousemove $((dx + 120)) 450; sleep 0.4
    xdotool mousemove $((dx + 160)) 450; sleep 0.5
    xdotool mouseup 1; sleep 1.2
    s1=$(sizes_of)
    [[ $(moved "$s0" "$s1") == yes ]] && { record "02 dragged from x=$dx (offset $offset)"; break; }
    record "02 drag at x=$dx (offset $offset) did not move the divider; sizing [$s1]"
    sleep 1
done
shot 02-dragged
text=$(ocr "$out/02-dragged.png")
record "02 after the drag: sizes [$s1], moved: $(moved "$s0" "$s1")"
record "02 toast text: $text"
if [[ $(flat "$text") == *"nexttime:alt+0"* ]]; then
    record "02 drag hint: teaches Alt+0 · PASS"
else
    record "02 drag hint: MISSING (want 'Next time: Alt+0 · equal panes') · FAIL"
fi

# 3. Alt+0 puts the splitter back to its equal share. Let the first toast go first.
sleep 6
xdotool key --window "$win" alt+0; sleep 2
shot 03-equalized
s2=$(sizes_of)
record "03 after Alt+0: sizes [$s2]; equal again: $(equal "$s2"); moved from the drag: $(moved "$s1" "$s2")"
if [[ $(equal "$s2") == yes && $(moved "$s1" "$s2") == yes ]]; then
    record "03 Alt+0 equalizes the panes · PASS"
else
    record "03 Alt+0 equalizes the panes · FAIL (want an equal share back, not [$s1])"
fi

# 4. The "?" list: F1, then search for what the owner calls it.
sleep 4
xdotool key --window "$win" F1; sleep 4
shot 04-actions
xdotool type --window "$win" --delay 60 "auto resize"; sleep 3
shot 05-actions-search
text=$(ocr "$out/05-actions-search.png")
record "05 '?' list, searched 'auto resize': $text"
if [[ $(flat "$text") == *"equalizepanesizes"* && $(flat "$text") == *"alt+0"* ]]; then
    record "05 the ? list shows the equalize entry with Alt+0 · PASS"
else
    record "05 the ? list entry with Alt+0 · FAIL"
fi

# 5. Alive, no crash.
if rg -q "gui_crash" "$sandbox/relay.log"; then
    record "05 relay.log: gui_crash PRESENT · FAIL"
else
    record "05 relay.log: no gui_crash · PASS"
fi
kill -0 "$relay_pid" 2>/dev/null && record "05 relay still running · PASS" || record "05 relay exited · FAIL"
rg "PASS|FAIL|sizes|toast text" "$out/ocr.txt"