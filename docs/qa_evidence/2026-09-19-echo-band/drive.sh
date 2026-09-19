#!/usr/bin/env bash
# Reproduce the owner's "too dark" text: Dark Copper, an agent turn that runs echo and replies with
# a bold path, a "(22 projects):" tail and a listing, twice (fenced, then plain). Screenshots to $out.
set -uo pipefail
S=/tmp/claude-1000/-home-elliott-repos-relay-terminal/8070d90d-903b-49da-a9b3-9fc3c5a3117e/scratchpad
root=/home/elliott/repos/relay-terminal
relay=$root/build/relay
out=$S/dark; mkdir -p "$out"
width=1200 height=760
port=8861
display=
for n in $(seq 170 199); do [[ -e /tmp/.X11-unix/X$n ]] || { display=:$n; break; }; done
sandbox=$(mktemp -d /tmp/rd.XXXXXX)
stub_pid= xvfb_pid= relay_pid=
cleanup() { local pid; for pid in $relay_pid $stub_pid $xvfb_pid; do kill "$pid" 2>/dev/null; done; cp -r "$HOME/.local/share/relay" "$out/data" 2>/dev/null; cp -r "$HOME/.cache/relay" "$out/cache" 2>/dev/null; rm -rf "$sandbox"; }
trap cleanup EXIT
python3 "$S/dark-fake.py" "$port" >"$out/fake.log" 2>&1 &
stub_pid=$!
Xvfb "$display" -screen 0 $((width + 40))x$((height + 40))x24 >/dev/null 2>&1 &
xvfb_pid=$!
sleep 2
export DISPLAY=$display RELAY_KEYRING=off
mkdir -p "$sandbox/home" "$sandbox/run" "$sandbox/tmp"; chmod 700 "$sandbox/run"
export HOME=$sandbox/home XDG_RUNTIME_DIR=$sandbox/run TMPDIR=$sandbox/tmp
export XDG_CONFIG_HOME=$HOME/.config XDG_DATA_HOME=$HOME/.local/share XDG_CACHE_HOME=$HOME/.cache
work=$HOME/project
mkdir -p "$XDG_CONFIG_HOME/RelayTerminal" "$XDG_CONFIG_HOME/relay" "$XDG_DATA_HOME" "$XDG_CACHE_HOME" "$work"
printf "PS1='\\\\w \$ '\nunset PROMPT_COMMAND\n" >"$HOME/.bashrc"
cat >"$XDG_CONFIG_HOME/RelayTerminal/relay.conf" <<CONF
[instructions]
onboarded=true
[theme]
name=${THEME:-dark-copper}
[provider]
preset=local:fake
[isolation]
enabled=false
[agent]
show_tool_output=${TOOLOUT:-false}
[terminal]
echo_band=${BAND:-channel}
CONF
printf '{"version": 1, "endpoints": [{"id": "local:fake", "label": "Fake", "base_url": "http://127.0.0.1:%s/v1", "model": "fake", "server": "openai-compatible", "context_window": 131072}]}\n' "$port" >"$XDG_CONFIG_HOME/relay/local-models.json"
(cd "$work" && exec "$relay" --workspace "$work") >"$out/relay.log" 2>&1 &
relay_pid=$!
sleep 7
win=; best=0
for w in $(xdotool search --pid "$relay_pid" 2>/dev/null); do
    eval "$(xdotool getwindowgeometry --shell "$w" 2>/dev/null)"
    (( WIDTH * HEIGHT > best )) && { best=$(( WIDTH * HEIGHT )); win=$w; }
done
[[ -z $win ]] && { echo "no Relay window"; cat "$out/relay.log"; exit 1; }
xdotool windowmove "$win" 0 0 windowsize "$win" $width $height
xdotool windowfocus "$win"; sleep 1.5
if [[ -n ${SWITCH:-} ]]; then xdotool type --delay 15 '/light'; xdotool key Return; sleep 2.5; fi
xdotool type --delay 15 '*show it'; xdotool key Return; sleep 9
if [[ -n ${RESTART:-} ]]; then
    xdotool key ctrl+q; sleep 4; kill $relay_pid 2>/dev/null; sleep 1
    (cd "$work" && exec "$relay" --workspace "$work") >>"$out/relay.log" 2>&1 &
    relay_pid=$!; sleep 9
    win=; best=0
    for w in $(xdotool search --pid "$relay_pid" 2>/dev/null); do
        eval "$(xdotool getwindowgeometry --shell "$w" 2>/dev/null)"
        (( WIDTH * HEIGHT > best )) && { best=$(( WIDTH * HEIGHT )); win=$w; }
    done
    xdotool windowmove "$win" 0 0 windowsize "$win" $width $height; xdotool windowfocus "$win"; sleep 2
    import -window "$win" "$out/${SHOT:-dark-copper}-restored.png"
fi
if [[ -n ${SWITCH:-} ]]; then xdotool type --delay 15 '/dark'; xdotool key Return; sleep 2.5; fi
xdotool type --delay 15 '*show it again'; xdotool key Return; sleep 9
xdotool mousemove $((width + 20)) $((height + 20)); sleep 0.8
import -window "$win" "$out/${SHOT:-dark-copper}.png"
echo "shot: $out/${SHOT:-dark-copper}.png"
