#!/usr/bin/env bash
# Second AI pass for #7BYT: same fixture, but the Alt+drag now sweeps the plain-text prompt rows
# (the first pass pressed on a path link, which is an Alt+click and added a mention instead).
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
root=/home/elliott/.cache/relay/scratch/tryit/7byt
bin=$root/build/relay; proj=$root/proj
display=; for n in $(seq 170 199); do [[ -e /tmp/.X11-unix/X$n ]] || { display=:$n; break; }; done
sandbox=$(mktemp -d /tmp/rl-7byt2.XXXX)
cleanup() { for pid in ${relay_pid:-} ${xvfb_pid:-}; do kill -TERM "$pid" 2>/dev/null; done; sleep 2; rm -rf "$sandbox"; }
trap cleanup EXIT
Xvfb "$display" -screen 0 1640x1040x24 >/dev/null 2>&1 & xvfb_pid=$!; sleep 2
export DISPLAY=$display RELAY_KEYRING=off
unset RELAY_OPEN_SOCKET
mkdir -p "$sandbox/home/.config/RelayTerminal" "$sandbox/run" "$sandbox/tmp"; chmod 700 "$sandbox/run"
ln -sfn "/run/user/$(id -u)/bus" "$sandbox/run/bus"
export HOME=$sandbox/home XDG_RUNTIME_DIR=$sandbox/run TMPDIR=$sandbox/tmp XDG_CONFIG_HOME=$sandbox/home/.config XDG_DATA_HOME=$sandbox/home/.local/share XDG_CACHE_HOME=$sandbox/home/.cache
printf '[instructions]\nonboarded=true\n[security]\napprovals_chosen=true\n' > "$XDG_CONFIG_HOME/RelayTerminal/relay.conf"
(cd "$proj" && exec "$bin" --workspace "$proj" --clean-shell --fresh) > "$here/relay2.log" 2>&1 & relay_pid=$!
sleep 14
shot() { import -window root "$here/$1.png"; }
wid=$(xdotool search --onlyvisible --class relay | head -1)
xdotool windowfocus --sync "$wid" || true
xdotool mousemove --window "$wid" 800 700 click 1
sleep 1
xdotool type --delay 60 'python3 src/boom.py && ls src'
xdotool key Return
sleep 3
# The fresh prompt sits on the last terminal row (~y 414 in a 1600x1000 window): a blank row
# above it plus the prompt row are plain text, so Alt+drag there can only be a selection.
xdotool keydown alt
xdotool mousemove --window "$wid" 250 390 mousedown 1
xdotool mousemove --window "$wid" 900 420
sleep 0.4
xdotool mouseup 1
xdotool keyup alt
sleep 1
shot 50-selection
echo "drag pass done"
