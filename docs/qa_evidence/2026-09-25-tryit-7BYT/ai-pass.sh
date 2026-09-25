#!/usr/bin/env bash
# The AI's mechanical pass over the #7BYT Try-it fixture: opens Relay under Xvfb on an isolated
# profile, produces the clickable output, then plays every chord except the one judgement left to
# the person. Keyboard chords go through the link walk (Ctrl+Shift+L, Alt+Enter, Ctrl+Enter) and
# the Alt+drag is a real pointer drag over output text. One screenshot per step.
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
root=/home/elliott/.cache/relay/scratch/tryit/7byt
bin=$root/build/relay; proj=$root/proj
width=1600 height=1000
display=; for n in $(seq 170 199); do [[ -e /tmp/.X11-unix/X$n ]] || { display=:$n; break; }; done
sandbox=$(mktemp -d /tmp/rl-7byt.XXXX)
cleanup() { for pid in ${relay_pid:-} ${xvfb_pid:-}; do kill -TERM "$pid" 2>/dev/null; done; sleep 2; rm -rf "$sandbox"; }
trap cleanup EXIT
Xvfb "$display" -screen 0 $((width+40))x$((height+40))x24 >/dev/null 2>&1 & xvfb_pid=$!; sleep 2
export DISPLAY=$display RELAY_KEYRING=off
unset RELAY_OPEN_SOCKET
mkdir -p "$sandbox/home/.config/RelayTerminal" "$sandbox/run" "$sandbox/tmp"; chmod 700 "$sandbox/run"
ln -sfn "/run/user/$(id -u)/bus" "$sandbox/run/bus"
export HOME=$sandbox/home XDG_RUNTIME_DIR=$sandbox/run TMPDIR=$sandbox/tmp XDG_CONFIG_HOME=$sandbox/home/.config XDG_DATA_HOME=$sandbox/home/.local/share XDG_CACHE_HOME=$sandbox/home/.cache
printf '[instructions]\nonboarded=true\n[security]\napprovals_chosen=true\n' > "$XDG_CONFIG_HOME/RelayTerminal/relay.conf"
(cd "$proj" && exec "$bin" --workspace "$proj" --clean-shell --fresh) > "$here/relay.log" 2>&1 & relay_pid=$!
sleep 14
shot() { import -window root "$here/$1.png"; }
wid=$(xdotool search --onlyvisible --class relay | head -1)
xdotool windowfocus --sync "$wid" || true
xdotool mousemove --window "$wid" 800 700 click 1
sleep 1
xdotool type --delay 60 'python3 src/boom.py && ls src'
xdotool key Return
sleep 3
shot 10-output
xdotool key ctrl+shift+l
sleep 1
shot 20-walk
xdotool key alt+Return
sleep 1
shot 30-mention
xdotool key ctrl+shift+l
sleep 1
xdotool key ctrl+Return
sleep 2
shot 40-navigated
xdotool keydown alt
xdotool mousemove --window "$wid" 250 330 mousedown 1
xdotool mousemove --window "$wid" 900 470
sleep 0.4
xdotool mouseup 1
xdotool keyup alt
sleep 1
shot 50-selection
echo "ai pass done"
