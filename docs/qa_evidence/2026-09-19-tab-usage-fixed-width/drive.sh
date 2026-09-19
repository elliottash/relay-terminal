#!/usr/bin/env bash
# The tab label's fixed-width usage suffix (#MERX), live: a busy pane under Xvfb with an
# isolated HOME, XDG_CONFIG_HOME, XDG_RUNTIME_DIR and TMPDIR, all under a short path
# (108-byte socket limit), and RELAY_KEYRING=off. Nothing is typed into the prompt box: the
# load is started by the sandbox's own ~/.bashrc, which the pane's shell sources, so the busy
# processes are children of the pane's shell and land in the meter's tree. The recipe is
# docs/qa_evidence/2026-09-19-usage-meter-words/drive.sh (card #6BGA), cut down to the tab.
#
# What this one watches:
#   1. the label's shape — both halves always, two digits each, idle included;
#   2. the label's clock — crops 1.25 s apart compared pixel for pixel. A steady load first
#      (the strip cannot move at all), then the load halves on a signal: the number must step
#      down at 5 s-ish intervals and stand still in between, through the window's mean, not
#      live wobble;
#   3. idle — the load is killed and the suffix stays, reading 00/00, while the pane chip
#      (whose rules are unchanged) goes.
set -uo pipefail
root=/home/elliott/repos/relay-terminal
build=${1:-$root/build}
out=${2:-$root/docs/qa_evidence/2026-09-19-tab-usage-fixed-width}
width=1440 height=900
theme=${THEME:-relay-dark}
mkdir -p "$out"

display=
for n in $(seq 170 199); do [[ -e /tmp/.X11-unix/X$n ]] || { display=:$n; break; }; done
[[ -z $display ]] && { echo "no free display"; exit 1; }

sandbox=$(mktemp -d /tmp/rl-mx.XXXX)
xvfb_pid= relay_pid=
cleanup() {
    local pid
    for pid in $relay_pid $xvfb_pid; do kill "$pid" 2>/dev/null; done
    pkill -u "$(id -u)" -f '^yes$' 2>/dev/null
    pkill -u "$(id -u)" -f 'bytearray' 2>/dev/null
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
work=$HOME/project
mkdir -p "$XDG_CONFIG_HOME/RelayTerminal" "$XDG_CONFIG_HOME/relay" "$XDG_DATA_HOME" "$XDG_CACHE_HOME" "$work"
# Six `yes` loops is ~30 % of this machine's 20 cores; 6 GB is ~5 % of its 121 GB — a reading
# with two digits worth showing on both axes. On $HOME/.half the shell kills three of the six,
# so the reading halves on the drive script's signal, still from inside the pane's tree.
cat >"$HOME/.bashrc" <<'RC'
PS1='\w \$ '
unset PROMPT_COMMAND
if [[ -z ${RELAY_QA_LOAD:-} ]]; then
    export RELAY_QA_LOAD=1
    (
        pids=()
        for i in 1 2 3 4 5 6; do yes > /dev/null & pids+=($!); done
        python3 -c 'a=bytearray(6000*1024*1024); import time; time.sleep(600)' &
        while [[ ! -e "$HOME/.half" ]]; do sleep 0.5; done
        kill "${pids[0]}" "${pids[1]}" "${pids[2]}" 2>/dev/null
        sleep 600
    ) &
    disown -a 2>/dev/null
    clear
fi
RC
cat >"$XDG_CONFIG_HOME/RelayTerminal/relay.conf" <<CONF
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

# First run in a fresh HOME opens the approvals pane over half the window; taking its
# recommended answer closes it, so the tab is back to one pane and a short label.
xdotool mousemove 906 513 click 1; sleep 4

# The tab strip: the label lives in the top 40 px. mousemove parks the pointer off-window so
# no hover state disturbs the pixels. $sandbox/label-*.png is what the shots are compared on —
# the label's region of the strip, right of the tab icon, which pulses while the pane is busy
# and would otherwise mark every shot changed however still the number stood.
strip() {
    xdotool mousemove $((width + 20)) $((height + 20)); sleep 0.4
    import -window "$win" "$sandbox/full-$1.png"
    convert "$sandbox/full-$1.png" -crop 640x40+0+2 +repage "$sandbox/strip-$1.png"
    convert "$sandbox/strip-$1.png" -crop 500x40+140+0 +repage "$sandbox/label-$1.png"
    convert "$sandbox/strip-$1.png" -scale 300% "$out/implementer-tab-$1.png"
}
# Two labels are the same reading when not one pixel of their region differs.
same() {
    local ae
    ae=$(compare -metric AE "$sandbox/label-$1.png" "$sandbox/label-$2.png" null: 2>&1)
    [[ $ae == 0 ]]
}
# The label's text is right of the tab icon; the middle dot and the padding digits need the
# grey, enlarged cut tesseract reads reliably.
ocr() {
    convert "$out/implementer-tab-$1.png" -crop 1100x120+420+0 +repage -resize 150% \
            -colorspace Gray -normalize gif:- \
        | tesseract stdin stdout --psm 7 2>/dev/null | tr -d '\f' | sed 's/  */ /g;s/^ //;s/ $//'
}

# 1. Steady load: ten crops 1.25 s apart. The reading has nothing to do but hold still; the
#    strip is compared pixel for pixel, so this says the label is not repainting at the poll's
#    2.5 Hz with a wobbling number on it.
run() {   # run <tag> <count>
    local tag=$1 count=$2 i= state=
    for i in $(seq 0 $((count - 1))); do
        strip "$tag-$i"
        sleep 0.85   # 0.4 hover + 0.85 = ~1.25 s between shots
        if [[ $i -eq 0 ]]; then
            state=first
        elif same "$tag-$((i-1))" "$tag-$i"; then
            state=identical
        else
            state=CHANGED
        fi
        printf '%-7s t+%5.2fs  %-9s  %s\n' "$tag" "$(echo "$i * 1.25" | bc)" "$state" "$(ocr "$tag-$i")"
    done
}
run busy 10 | tee "$out/cadence.log"

# 2. The load halves on the signal: the number must step down through the window's mean —
#    not at once, and not continuously — with the steps 5 s-ish apart.
touch "$HOME/.half"
run halved 10 | tee -a "$out/cadence.log"

# Keep one crop per distinct reading for the sheet; drop the rest.
kept=()
for tag in busy halved; do
    prev=
    for i in $(seq 0 9); do
        [[ -e "$sandbox/label-$tag-$i.png" ]] || continue
        if [[ -z $prev ]] || ! same "$tag-$prev" "$tag-$i"; then
            kept+=("implementer-tab-$tag-$i.png")
        fi
        prev=$i
    done
done
for f in "$out"/implementer-tab-*.png; do
    case "$(basename "$f")" in *cadence*|*idle*) continue ;; esac
    [[ " ${kept[*]-} " == *" $(basename "$f") "* ]] || rm -f "$f"
done
montage "${kept[@]/#/$out/}" -tile 1x -geometry +0+4 -background black "$out/implementer-tab-cadence.png"

# 3. Idle: the load dies, the suffix stays — 00/00, the same width — while the pane chip goes.
pkill -u "$(id -u)" -f '^yes$' 2>/dev/null
pkill -u "$(id -u)" -f 'bytearray' 2>/dev/null
sleep 14   # long enough for the next 5 s take to pick the window's mean up
strip idle
xdotool mousemove $((width + 20)) $((height + 20)); sleep 0.4
import -window "$win" "$sandbox/idle-full.png"
convert "$sandbox/idle-full.png" -crop 900x34+8+42 +repage -scale 300% "$out/implementer-idle-header.png"
printf 'idle    t+ 0.00s  %-9s  %s\n' reading "$(ocr idle)" | tee -a "$out/cadence.log"

[[ -s "$sandbox/relay.log" ]] && cp "$sandbox/relay.log" "$out/relay.log"
rm -f "$out/stdin.txt"   # tesseract leaves one behind when it reads from stdin
kill "$relay_pid" 2>/dev/null; wait "$relay_pid" 2>/dev/null
echo "shots in $out"
