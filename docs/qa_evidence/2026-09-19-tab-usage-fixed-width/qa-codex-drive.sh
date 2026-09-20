#!/usr/bin/env bash
# Independent QA live run (#MERX), adapted from drive.sh by a second model (codex).
# Watches: the suffix's fixed form (both halves, two digits, idle 00/00), the pane count in
# parens only when a tab has more than one pane (e932c7ab), the 5 s clock with real
# wall-clock timestamps, width stability across different readings, the pane chip at idle,
# and appearance/pane_usage off/on clearing and restoring the suffix.
# Differences from the implementer's drive.sh: load processes carry argv[0] tags so this
# run's pkill cannot touch another agent's load; the .bashrc guard is a file, so the shell
# of a second (split) pane does not start a second load; crops are stamped with real
# wall-clock times (the implementer's t+N labels are nominal — the loop's OCR adds seconds).
set -uo pipefail
root=/home/elliott/repos/relay-terminal
build=${1:-$root/build}
out=$root/docs/qa_evidence/2026-09-19-tab-usage-fixed-width
width=1440 height=900
theme=${THEME:-relay-dark}
tag=qa-codex
mkdir -p "$out"

display=
for n in $(seq 170 199); do [[ -e /tmp/.X11-unix/X$n ]] || { display=:$n; break; }; done
[[ -z $display ]] && { echo "no free display"; exit 1; }

sandbox=$(mktemp -d /tmp/rl-mxqa.XXXX)
xvfb_pid= relay_pid=
cleanup() {
    local pid
    for pid in $relay_pid $xvfb_pid; do kill "$pid" 2>/dev/null; done
    pkill -u "$(id -u)" -f 'mxqa-yes' 2>/dev/null
    pkill -u "$(id -u)" -f 'mxqa-mem' 2>/dev/null
    rm -rf "$sandbox"
}
trap cleanup EXIT

Xvfb "$display" -screen 0 $((width + 40))x$((height + 40))x24 >/dev/null 2>&1 &
xvfb_pid=$!
sleep 2
kill -0 "$xvfb_pid" 2>/dev/null || { echo "Xvfb died on $display"; exit 1; }
export DISPLAY=$display RELAY_KEYRING=off

mkdir -p "$sandbox/home" "$sandbox/run" "$sandbox/tmp"; chmod 700 "$sandbox/run"
ln -sfn "/run/user/$(id -u)/bus" "$sandbox/run/bus"
export HOME=$sandbox/home XDG_RUNTIME_DIR=$sandbox/run TMPDIR=$sandbox/tmp
export XDG_CONFIG_HOME=$HOME/.config XDG_DATA_HOME=$HOME/.local/share XDG_CACHE_HOME=$HOME/.cache
work=$HOME/pj      # short, so "pj (2)  ·  cpu NN% · mem NN%" clears the 260 px elide
mkdir -p "$XDG_CONFIG_HOME/RelayTerminal" "$XDG_CONFIG_HOME/relay" "$XDG_DATA_HOME" "$XDG_CACHE_HOME" "$work"
conf=$XDG_CONFIG_HOME/RelayTerminal/relay.conf
# Six tagged `yes` loops ~30 % of this machine's 20 cores; ~6 GB ~5 % of 121 GB. $HOME/.half
# kills three of the six; the guard file means a split pane's fresh shell adds no second load.
cat >"$HOME/.bashrc" <<'RC'
PS1='\w \$ '
unset PROMPT_COMMAND
if [[ ! -e "$HOME/.loaded" ]]; then
    touch "$HOME/.loaded"
    (
        pids=()
        for i in 1 2 3 4 5 6; do bash -c 'exec -a mxqa-yes yes > /dev/null' & pids+=($!); done
        python3 -c 'import sys; sys.argv[0]="mxqa-mem"; a=bytearray(6000*1024*1024); import time; time.sleep(900)' &
        while [[ ! -e "$HOME/.half" ]]; do sleep 0.5; done
        kill "${pids[0]}" "${pids[1]}" "${pids[2]}" 2>/dev/null
        sleep 900
    ) &
    disown -a 2>/dev/null
    clear
fi
RC
cat >"$conf" <<CONF
[instructions]
onboarded=true
[theme]
name=$theme
[appearance]
pane_colours=type
pane_usage=true
CONF

(cd "$work" && exec "$build/relay" --workspace "$work") >"$sandbox/relay.log" 2>&1 &
relay_pid=$!
sleep 9
win= ; best=0
for candidate in $(xdotool search --pid "$relay_pid" 2>/dev/null); do
    eval "$(xdotool getwindowgeometry --shell "$candidate" 2>/dev/null)"
    (( WIDTH * HEIGHT > best )) && { best=$(( WIDTH * HEIGHT )); win=$candidate; }
done
[[ -z $win ]] && { echo "no Relay window"; cat "$sandbox/relay.log"; exit 1; }
xdotool windowmove "$win" 0 0 windowsize "$win" "$width" $height
xdotool windowfocus "$win"; sleep 8

# A first run in a fresh HOME opens panes over the window; click the approvals pane's
# recommended answer as the implementer did, and any instruction-files dialog's "Not now".
xdotool mousemove 906 513 click 1; sleep 3
import -window "$win" "$sandbox/boot.png"
if convert "$sandbox/boot.png" -colorspace Gray -normalize gif:- \
     | tesseract stdin stdout --psm 11 2>/dev/null | grep -qi 'not now'; then
    coords=$(convert "$sandbox/boot.png" -colorspace Gray -normalize gif:- \
        | tesseract stdin stdout --psm 11 tsv 2>/dev/null \
        | awk -F'\t' '$12=="now"{print $7+($9/2), $8+($10/2), NR; exit}')
    if [[ -n ${coords:-} ]]; then
        read -r cx cy _ <<<"$coords"
        xdotool mousemove "$cx" "$cy" click 1; sleep 3
    fi
fi

strip() {
    xdotool mousemove $((width + 20)) $((height + 20)); sleep 0.4
    import -window "$win" "$sandbox/full-$1.png"
    convert "$sandbox/full-$1.png" -crop 640x40+0+2 +repage "$sandbox/strip-$1.png"
    convert "$sandbox/strip-$1.png" -crop 500x40+140+0 +repage "$sandbox/label-$1.png"
    convert "$sandbox/strip-$1.png" -scale 300% "$out/$tag-tab-$1.png"
}
same() {
    local ae
    ae=$(compare -metric AE "$sandbox/label-$1.png" "$sandbox/label-$2.png" null: 2>&1)
    [[ $ae == 0 ]]
}
# The whole label, title and pane count included (the tab icon's glyph confuses OCR only
# itself; tesseract reads the middle dots as "-").
ocr() {
    convert "$out/$tag-tab-$1.png" -crop 1800x120+150+0 +repage -resize 150% \
            -colorspace Gray -normalize gif:- \
        | tesseract stdin stdout --psm 7 2>/dev/null | tr -d '\f' | sed 's/  */ /g;s/^ //;s/ $//'
}
stamp() { date +%s.%N; }

# 1. Steady load, one pane: no "(1)", two digits on both halves.
strip single
printf '%s\n' "single  $(stamp)  $(ocr single)"

# 2. Split: Ctrl+E gives the tab a second pane -> "pj (2)".
xdotool windowfocus "$win"
xdotool key ctrl+h; sleep 0.5
xdotool key ctrl+e; sleep 6
strip split
printf '%s\n' "split   $(stamp)  $(ocr split)"

# 3. Steady load with two panes: the label stands still between 5 s takes.
run() {   # run <name> <count>
    local name=$1 count=$2 i= state=
    for i in $(seq 0 $((count - 1))); do
        strip "$name-$i"
        sleep 0.85
        if [[ $i -eq 0 ]]; then
            state=first
        elif same "$name-$((i-1))" "$name-$i"; then
            state=identical
        else
            state=CHANGED
        fi
        printf '%-10s %s  %-9s  %s\n' "$name" "$(stamp)" "$state" "$(ocr "$name-$i")"
    done
}
run still 8 | tee "$out/$tag-cadence.log"

# 4. Halve the load: the label must step down through the 5 s window's mean, moves >= 5 s
#    apart in real time, and stand still in between.
touch "$HOME/.half"
run halved 10 | tee -a "$out/$tag-cadence.log"

# Width stability across two different readings: the trimmed bounding box of the label's
# text region must not change width when the number on it does.
w1=$(convert "$sandbox/label-still-7.png" -fuzz 8% -trim -format '%w' info: 2>/dev/null)
last=halved-0; prev=
for i in $(seq 0 9); do [[ -e "$sandbox/label-halved-$i.png" ]] && last=halved-$i; done
w2=$(convert "$sandbox/label-$last.png" -fuzz 8% -trim -format '%w' info: 2>/dev/null)
printf 'width    trimmed label region: still-7=%spx %s last=%spx %s\n' \
        "$w1" "$(ocr still-7)" "$w2" "$(ocr "$last")" | tee -a "$out/$tag-cadence.log"

# 5. Idle: the load dies, the suffix stays at 00/00, the pane chip goes.
pkill -u "$(id -u)" -f 'mxqa-yes' 2>/dev/null
pkill -u "$(id -u)" -f 'mxqa-mem' 2>/dev/null
sleep 14
strip idle
xdotool mousemove $((width + 20)) $((height + 20)); sleep 0.4
import -window "$win" "$sandbox/idle-full.png"
convert "$sandbox/idle-full.png" -crop 700x34+8+42 +repage -scale 300% "$out/$tag-idle-header.png"
printf '%-10s %s  %-9s  %s\n' "idle" "$(stamp)" "reading" "$(ocr idle)" | tee -a "$out/$tag-cadence.log"

# 6. appearance/pane_usage off clears the suffix (the pane count stays); on brings it back.
sed -i 's/pane_usage=true/pane_usage=false/' "$conf"; sleep 4
strip meters-off
printf '%-10s %s  %-9s  %s\n' "off" "$(stamp)" "reading" "$(ocr meters-off)" | tee -a "$out/$tag-cadence.log"
sed -i 's/pane_usage=false/pane_usage=true/' "$conf"; sleep 7
strip meters-on
printf '%-10s %s  %-9s  %s\n' "on" "$(stamp)" "reading" "$(ocr meters-on)" | tee -a "$out/$tag-cadence.log"

# 7. Close the second pane: back to one pane, no "(2)", suffix still there.
xdotool windowfocus "$win"; xdotool key ctrl+w; sleep 4
strip one-again
printf '%-10s %s  %-9s  %s\n' "one" "$(stamp)" "reading" "$(ocr one-again)" | tee -a "$out/$tag-cadence.log"

[[ -s "$sandbox/relay.log" ]] && cp "$sandbox/relay.log" "$out/$tag-relay.log"
rm -f "$out/stdin.txt"
kill "$relay_pid" 2>/dev/null; wait "$relay_pid" 2>/dev/null
echo "done; shots in $out (qa-codex-*)"
