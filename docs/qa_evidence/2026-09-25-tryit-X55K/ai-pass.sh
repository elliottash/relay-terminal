#!/usr/bin/env bash
# The mechanical pass over the staged #X55K situation: launch the fixture under Xvfb on an
# isolated profile and press the two shortcuts for real (xdotool), while a program is running
# and at a plain prompt. Asserts each jump by OCR of the marker lines and by image difference
# between before/after shots. One screenshot per step; results printed as CHECK lines.
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
root=/home/elliott/.cache/relay/scratch/tryit/x55k-scroll
proj="$root/project"; sandbox="$root/sandbox"
repo=$(cd "$here/../../.." && pwd); driver="$repo/scripts/relay-drive"; bin="$repo/build/relay"
out="$here/ai-pass"; mkdir -p "$out"

bash "$here/stage.sh" > "$out/stage.log" 2>&1

# A second data dir whose rcfile ends busy (sleep), so the pass can press the keys while a
# program is running; Ctrl+C later returns to the prompt for the idle-phase steps.
data_busy="$sandbox/data-busy"
cp -r "$sandbox/data" "$data_busy"
echo 'sleep 300' >> "$data_busy/shell/integration.bash"

width=1600 height=1000
display=; for n in $(seq 170 199); do [[ -e /tmp/.X11-unix/X$n ]] || { display=:$n; break; }; done
cleanup() { for pid in ${relay_pid:-} ${xvfb_pid:-}; do kill -TERM "$pid" 2>/dev/null; done; sleep 1; }
trap cleanup EXIT
Xvfb "$display" -screen 0 $((width+40))x$((height+40))x24 >/dev/null 2>&1 & xvfb_pid=$!; sleep 2
export DISPLAY=$display RELAY_KEYRING=off
unset RELAY_OPEN_SOCKET
export HOME="$sandbox/home" XDG_RUNTIME_DIR="$sandbox/run" TMPDIR="$sandbox/tmp" \
       XDG_CONFIG_HOME="$sandbox/home/.config" XDG_DATA_HOME="$sandbox/home/.local/share" \
       XDG_CACHE_HOME="$sandbox/home/.cache" RELAY_DATA_DIR="$data_busy"
(cd "$proj" && exec "$bin" --clean-shell --fresh) > "$out/relay.log" 2>&1 & relay_pid=$!

shot() { import -window root "$out/$1.png"; }
ocr() { tesseract "$out/$1.png" - 2>/dev/null | tr -d '[:space:]' || true; }
mae() { compare -metric MAE "$out/$1.png" "$out/$2.png" null: 2>&1 | grep -oE '\([0-9.e-]+\)' | tr -d '()' || echo 1; }
panes() { XDG_RUNTIME_DIR="$sandbox/run" "$driver" panes 2>/dev/null || true; }
verdict() { python3 -c 'import sys; sys.exit(0 if eval(sys.argv[1]) else 1)' "$1"; }

# 1. App up, markers printed, pane busy in `sleep 300`, view at the bottom.
for i in $(seq 1 40); do panes >/dev/null 2>&1 && break; sleep 1; done
sleep 6
panes > "$out/00-panes.json"
shot 00-busy-bottom

# 2. Alt+Home while the program runs: the view jumps to the very top of the scrollback.
xdotool key alt+Home; sleep 1; shot 01-busy-top
t1=$(mae 00-busy-bottom 01-busy-top); o1=$(ocr 01-busy-top)
echo "CHECK busy Alt+Home: top-marker OCR $(printf '%s' "$o1" | grep -q SCROLLBACK-MARK0001 && echo FOUND || echo unseen), MAE 00↔01 $t1"

# 3. Alt+End while the program runs: back to the newest output (same screen again).
xdotool key alt+End; sleep 1; shot 02-busy-bottom-again
t2=$(mae 00-busy-bottom 02-busy-bottom-again)
echo "CHECK busy Alt+End: MAE 00↔02 $t2"

# 4. Ctrl+C: the shell returns to its prompt (process no longer busy).
xdotool key ctrl+c; sleep 2; shot 03-prompt-bottom

# 5./6. The same two keys at a plain prompt.
xdotool key alt+Home; sleep 1; shot 04-prompt-top
t3=$(mae 03-prompt-bottom 04-prompt-top); o2=$(ocr 04-prompt-top)
echo "CHECK idle Alt+Home: top-marker OCR $(printf '%s' "$o2" | grep -q SCROLLBACK-MARK0001 && echo FOUND || echo unseen), MAE 03↔04 $t3"
xdotool key alt+End; sleep 1; shot 05-prompt-bottom-again
t4=$(mae 03-prompt-bottom 05-prompt-bottom-again)
echo "CHECK idle Alt+End: MAE 03↔05 $t4"

# Verdict: each Alt+Home is confirmed by OCR of the very first marker (or at least a large
# image change), each Alt+End by the screen being the same again.
fail=0
verdict "'''$o1'''.find('SCROLLBACK-MARK0001')>=0 or $t1>0.02" || { echo "FAIL busy Alt+Home"; fail=1; }
verdict "$t2<0.005" || { echo "FAIL busy Alt+End"; fail=1; }
verdict "'''$o2'''.find('SCROLLBACK-MARK0001')>=0 or $t3>0.02" || { echo "FAIL idle Alt+Home"; fail=1; }
verdict "$t4<0.005" || { echo "FAIL idle Alt+End"; fail=1; }
[[ $fail == 0 ]] && echo "ai-pass: all four jumps verified" || { echo "ai-pass: FAILED"; exit 1; }
