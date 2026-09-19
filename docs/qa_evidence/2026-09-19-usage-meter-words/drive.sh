#!/usr/bin/env bash
# The usage meter in words (#6BGA), live: a busy pane under Xvfb with an isolated HOME,
# XDG_CONFIG_HOME, XDG_RUNTIME_DIR and TMPDIR, all under a short path (108-byte socket limit),
# and RELAY_KEYRING=off. No provider is touched and nothing is typed into the prompt box: the
# load is started by the sandbox's own ~/.bashrc, which the pane's shell sources, so the busy
# processes are children of the pane's shell and land in the meter's tree.
#
# Adapted from docs/qa_evidence/2026-09-19-usage-meter-numbers/mockup-harness/live.sh, which is
# the same recipe against the chip as it was before this card.
#
#   THEME=relay-dark drive.sh [build-dir] [out-dir]
set -uo pipefail
root=/home/elliott/repos/relay-terminal
build=${1:-$root/build}
out=${2:-$root/docs/qa_evidence/2026-09-19-usage-meter-words}
width=1440 height=900
narrow=420          # the header ladder's last rung: the usage meter collapses to CPU alone
theme=${THEME:-relay-dark}
mkdir -p "$out"

display=
for n in $(seq 170 199); do [[ -e /tmp/.X11-unix/X$n ]] || { display=:$n; break; }; done
[[ -z $display ]] && { echo "no free display"; exit 1; }

sandbox=$(mktemp -d /tmp/rl-uw.XXXX)     # short: $XDG_RUNTIME_DIR holds unix sockets
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
cat >"$HOME/.bashrc" <<'RC'
PS1='\w \$ '
unset PROMPT_COMMAND
if [[ -z ${RELAY_QA_LOAD:-} ]]; then
    export RELAY_QA_LOAD=1
    for i in 1 2 3 4; do yes > /dev/null & done
    python3 -c 'a=bytearray(4000*1024*1024); import time; time.sleep(600)' &
    # Stage two, for the warning/error inks: more load, on the drive script's signal, still as
    # children of this shell so the meter counts them.
    ( while [[ ! -e "$HOME/.high" ]]; do sleep 1; done
      for i in $(seq 1 30); do yes > /dev/null & done; sleep 120 ) &
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

shot() { xdotool mousemove $((width + 20)) $((height + 20)); sleep 0.6; import -window "$win" "$out/$1.png"; }

# First run in a fresh HOME opens the approvals pane over half the window; taking its
# recommended answer closes it, so the tab is back to one pane and a short label.
xdotool mousemove 906 513 click 1; sleep 3

# 1. The chip at load: words in the pane header, and the same string on the tab.
shot implementer-header
convert "$out/implementer-header.png" -crop 900x34+8+42 +repage -scale 300% "$out/implementer-header-3x.png"
convert "$out/implementer-header.png" -crop 520x42+0+0 +repage -scale 300% "$out/implementer-tab.png"

# 2. The chip's tooltip: the long form in the same words, and the processes behind the number.
xdotool mousemove 100 56; sleep 2.5
import -window root "$sandbox/tooltip-full.png"
convert "$sandbox/tooltip-full.png" -crop 700x230+20+50 +repage -scale 200% "$out/implementer-tooltip.png"
xdotool mousemove $((width + 20)) $((height + 20)); sleep 1

# 3. The header ladder's last rung: narrow the window until the memory half gives way.
# SWEEP=1 writes one crop per width instead, which is how $narrow was chosen.
for w in ${SWEEP:+700 660 620 580 540 500 460 420 380} $narrow; do
    xdotool windowsize "$win" "$w" $height; sleep 3
    xdotool mousemove $((w + 20)) $((height + 20)); sleep 0.6
    import -window "$win" "$sandbox/narrow-$w.png"
    name=implementer-narrow; [[ -n ${SWEEP:-} ]] && name=sweep-$w
    convert "$sandbox/narrow-$w.png" -crop ${w}x34+0+42 +repage -scale 300% "$out/$name.png"
done
xdotool windowsize "$win" "$width" $height; sleep 3

# 4. The inks: 30 more `yes` loops push the CPU half past the 60 % and 85 % thresholds.
touch "$HOME/.high"; sleep 14
shot implementer-high-full
convert "$out/implementer-high-full.png" -crop 900x34+8+42 +repage -scale 300% "$out/implementer-high.png"
convert "$out/implementer-high-full.png" -crop 520x42+0+0 +repage -scale 300% "$out/implementer-high-tab.png"
rm -f "$out/implementer-high-full.png"
pkill -u "$(id -u)" -f '^yes$' 2>/dev/null; sleep 4

# 5. Idle: the load is stopped, and within a poll the chip and the tab suffix go.
pkill -u "$(id -u)" -f '^yes$' 2>/dev/null
pkill -u "$(id -u)" -f 'bytearray' 2>/dev/null
sleep 5
shot implementer-idle-full
convert "$out/implementer-idle-full.png" -crop 900x34+8+42 +repage -scale 300% "$out/implementer-idle.png"
rm -f "$out/implementer-idle-full.png"

# (relay.log is copied out only when it has something in it)
[[ -s "$sandbox/relay.log" ]] && cp "$sandbox/relay.log" "$out/relay.log"
kill "$relay_pid" 2>/dev/null; wait "$relay_pid" 2>/dev/null
echo "shots in $out"
