#!/usr/bin/env bash
# Folds (#TK9C): drive relay-vterm-spike --folds under Xvfb + xdotool and capture
# the states the card asks for.
#
#   DISPLAY=:79 engine/scripts/gui/folds.sh BIN CORE OUTDIR
#
# Produces OUTDIR/<core>-NN-*.png and OUTDIR/<core>-dump.txt (Ctrl+Shift+D dumps,
# which include the fold layer's own state). Check them by eye; the script only
# drives the clicks.
set -uo pipefail
bin=$1 core=$2 out=$3
mkdir -p "$out"
dump="$out/$core-dump.txt"
rm -f "$dump"

shot() { import -window root "$out/$core-$1.png"; }
k() { xdotool key --delay 30 "$@"; }
cw=9; ch=17   # DejaVu Sans Mono 10pt under Xvfb
rowy() { echo $((2 + $1 * ch + ch / 2)); }
colx() { echo $((2 + $1 * cw + cw / 2)); }

"$bin" --core "$core" --dump "$dump" --size 100x30 --folds -e /bin/cat >"$out/$core-stderr.log" 2>&1 &
pid=$!
sleep 1.5
win=$(xdotool search --pid "$pid" | tail -1)
xdotool windowmove "$win" 0 0
xdotool windowfocus "$win"
xdotool mousemove 700 400
sleep 1.0

# Rows: 0 "relay agent turn 1", 1..3 the three tool-call lines, 4 "done."
shot 01-folded
k ctrl+shift+d; sleep 0.3

# 1. The short run unfolds under its own line.
xdotool mousemove "$(colx 10)" "$(rowy 1)" click 1; sleep 0.6
xdotool mousemove 700 500
shot 02-unfolded-run
k ctrl+shift+d; sleep 0.3

# 2. The coloured diff, under the line that is still where it was.
xdotool mousemove "$(colx 10)" "$(rowy 16)" click 1; sleep 0.6
xdotool mousemove 700 500
shot 03-unfolded-diff
k ctrl+shift+d; sleep 0.3

# 3. Selection dragged from a real row, through the fold, onto a real row.
xdotool mousemove "$(colx 2)" "$(rowy 1)" mousedown 1
xdotool mousemove "$(colx 30)" "$(rowy 6)"; sleep 0.3
xdotool mouseup 1; sleep 0.4
shot 04-selection-across-the-fold

# 4. The 300-line listing, and scrolling inside it row by row.
xdotool mousemove "$(colx 2)" "$(rowy 0)" click 1; sleep 0.3   # drop the selection
xdotool mousemove "$(colx 10)" "$(rowy 23)" click 1; sleep 0.8
xdotool mousemove 700 400
shot 05-unfolded-300-lines
for _ in $(seq 1 45); do xdotool click 4; done
sleep 0.5
shot 06-scrolled-inside-the-long-fold
k ctrl+shift+d; sleep 0.3

# 5. The find bar over a scrollback that has folds open in it. "python" is in a
#    tool-call line and inside the run fold, so the count covers both and the
#    walk visits them in the order they are on screen.
k ctrl+shift+Home; sleep 0.4
k ctrl+shift+f; sleep 0.4
xdotool type --delay 20 'python'
sleep 0.8
shot 07-search-while-folds-are-open
k ctrl+shift+d; sleep 0.3

# 6. A needle only the inside of a fold has: the block is scrolled into view and
#    the hit is highlighted exactly as one in a real row is.
k ctrl+a; sleep 0.2
xdotool type --delay 20 'sum(range'
sleep 0.9
shot 08-search-hit-inside-a-fold
k ctrl+shift+d; sleep 0.3

kill "$pid" 2>/dev/null
wait "$pid" 2>/dev/null
echo "screenshots in $out"
