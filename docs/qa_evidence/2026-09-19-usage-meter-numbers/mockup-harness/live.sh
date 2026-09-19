#!/usr/bin/env bash
# The CURRENT pane usage chip, live: a busy pane under Xvfb with an isolated HOME,
# XDG_CONFIG_HOME, XDG_RUNTIME_DIR and TMPDIR, all under a short path (108-byte socket limit),
# and RELAY_KEYRING=off. No provider is touched and nothing is typed into the prompt box: the
# load is started by the sandbox's own ~/.bashrc, which the pane's shell sources, so the busy
# processes are children of the pane's shell and land in the meter's tree.
#
#   THEME=relay-dark live.sh [build-dir] [out-dir]
set -uo pipefail
root=/home/elliott/repos/relay-terminal
build=${1:-$root/build}
out=${2:-$root/docs/qa_evidence/2026-09-19-usage-meter-numbers}
width=1440 height=900
theme=${THEME:-relay-dark}
mkdir -p "$out"

display=
for n in $(seq 170 199); do [[ -e /tmp/.X11-unix/X$n ]] || { display=:$n; break; }; done
[[ -z $display ]] && { echo "no free display"; exit 1; }

sandbox=$(mktemp -d /tmp/rl-um.XXXX)     # short: $XDG_RUNTIME_DIR holds unix sockets
xvfb_pid= relay_pid=
cleanup() {
    local pid
    for pid in $relay_pid $xvfb_pid; do kill "$pid" 2>/dev/null; done
    pkill -u "$(id -u)" -f '^yes$' 2>/dev/null
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
    python3 -c 'a=bytearray(1500*1024*1024); import time; time.sleep(300)' &
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
xdotool windowfocus "$win"; sleep 6

shot() { xdotool mousemove $((width + 20)) $((height + 20)); sleep 0.6; import -window "$win" "$out/$1.png"; }

shot "live-$theme-busy"
convert "$out/live-$theme-busy.png" -crop 500x44+0+0 +repage -scale 300% "$out/live-$theme-tab-3x.png"
convert "$out/live-$theme-busy.png" -crop 760x30+8+44 +repage -scale 300% "$out/live-$theme-header-3x.png"

# The idle form: the load is stopped, and within a poll the chip and the tab suffix go.
pkill -u "$(id -u)" -f '^yes$' 2>/dev/null
pkill -u "$(id -u)" -f 'bytearray' 2>/dev/null
sleep 4
shot "live-$theme-idle"
convert "$out/live-$theme-idle.png" -crop 500x44+0+0 +repage -scale 300% "$out/live-$theme-idle-tab-3x.png"
convert "$out/live-$theme-idle.png" -crop 760x30+8+44 +repage -scale 300% "$out/live-$theme-idle-header-3x.png"

kill "$relay_pid" 2>/dev/null; wait "$relay_pid" 2>/dev/null
echo "shots in $out"
