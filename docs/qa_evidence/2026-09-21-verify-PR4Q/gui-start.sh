#!/usr/bin/env bash
# Start Relay from the clean export under its own Xvfb on an isolated profile, and leave it
# running. Writes $S/gui.env with DISPLAY, the window id and the pids.
set -uo pipefail
bin=${RELAY_BIN:?}; work=${WORK:?}; out=${OUT:?}; S=${S:?}
mkdir -p "$out"; width=1600 height=1000
display=; for n in $(seq 170 199); do [[ -e /tmp/.X11-unix/X$n ]] || { display=:$n; break; }; done
sandbox=$(mktemp -d /tmp/rl-v4q.XXXX)
Xvfb "$display" -screen 0 $((width+40))x$((height+40))x24 >/dev/null 2>&1 & xvfb_pid=$!
sleep 2
export DISPLAY=$display RELAY_KEYRING=off
mkdir -p "$sandbox/home/.config/RelayTerminal" "$sandbox/run" "$sandbox/tmp"; chmod 700 "$sandbox/run"
ln -sfn "/run/user/$(id -u)/bus" "$sandbox/run/bus"
export HOME=$sandbox/home XDG_RUNTIME_DIR=$sandbox/run TMPDIR=$sandbox/tmp \
       XDG_CONFIG_HOME=$sandbox/home/.config XDG_DATA_HOME=$sandbox/home/.local/share \
       XDG_CACHE_HOME=$sandbox/home/.cache
printf '[instructions]\nonboarded=true\n[security]\napprovals_chosen=true\n' > "$XDG_CONFIG_HOME/RelayTerminal/relay.conf"
(cd "$work" && exec "$bin" --workspace "$work" --clean-shell --fresh) > "$out/relay.log" 2>&1 & relay_pid=$!
sleep 14
win=; best=0
for c in $(xdotool search --pid "$relay_pid" 2>/dev/null); do
  eval "$(xdotool getwindowgeometry --shell "$c" 2>/dev/null)"; (( WIDTH*HEIGHT > best )) && { best=$((WIDTH*HEIGHT)); win=$c; }
done
[[ -z $win ]] && { echo "no window"; tail -20 "$out/relay.log"; exit 1; }
xdotool windowmove "$win" 0 0 windowsize "$win" $width $height; xdotool windowfocus "$win"; sleep 4
xdotool key --window "$win" ctrl+shift+s; sleep 10
cat > "$S/gui.env" <<ENV
export DISPLAY=$display WIN=$win RELAY_PID=$relay_pid XVFB_PID=$xvfb_pid SANDBOX=$sandbox OUT=$out
ENV
import -window root "$out/00-board.png"
echo "up: display=$display win=$win relay=$relay_pid"
