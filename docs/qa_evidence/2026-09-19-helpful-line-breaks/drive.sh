#!/usr/bin/env bash
# Blank lines between transcript blocks (#5AWD): implementer screenshots under Xvfb with an
# isolated HOME, XDG_CONFIG_HOME, XDG_RUNTIME_DIR and TMPDIR. No provider account: the profile
# points a local model endpoint at stub-provider.py on 127.0.0.1 (the harness of the thinking-fold
# run, T8CN, with the scenes this card needs).
#
#   docs/qa_evidence/2026-09-19-helpful-line-breaks/drive.sh [build-dir]
#
# One session, three prompts, shot after each as implementer-<n>-<name>.png plus a 150 % crop of
# the pane body and OCR in implementer-notes.txt:
#   01-walk     prose · two reads · a command · prose · an edit · prose: a gap at every change of
#               kind, none between the three rows, the ▸ model line directly on top of the prose
#   02-second   a second ✦ line: set off from the previous turn's last prose line by a blank row
#   03-tools    a turn whose first step is a tool call: ▸ model directly on top of the row
#   04-recap    /recap after a turn: the Recap · line is set off from the ✦ N tool calls link
#
# Needs Xvfb, xdotool, ImageMagick, tesseract.
set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"
out=$PWD
root=$(cd ../../.. && pwd)
build=${1:-$root/build}
width=1200 height=900
port=${RELAY_QA_PORT:-8816}

display=
for n in $(seq 160 199); do [[ -e /tmp/.X11-unix/X$n ]] || { display=:$n; break; }; done
[[ -z $display ]] && { echo "no free display"; exit 1; }

sandbox=$(mktemp -d /tmp/relay-gaps.XXXXXX)
stub_pid= xvfb_pid= relay_pid=
cleanup() {
    local pid
    for pid in $relay_pid $stub_pid $xvfb_pid; do kill "$pid" 2>/dev/null; done   # never `kill 0`
    rm -rf "$sandbox"
}
trap cleanup EXIT

python3 "$out/stub-provider.py" "$port" >/dev/null 2>&1 &
stub_pid=$!
Xvfb "$display" -screen 0 $((width + 40))x$((height + 40))x24 >/dev/null 2>&1 &
xvfb_pid=$!
sleep 2
kill -0 "$xvfb_pid" 2>/dev/null || { echo "Xvfb died on $display"; exit 1; }
export DISPLAY=$display RELAY_KEYRING=off

t() { xdotool type --delay 40 "$1"; }
k() { xdotool key --delay 60 "$@"; }

mkdir -p "$sandbox/home" "$sandbox/run" "$sandbox/tmp"; chmod 700 "$sandbox/run"
ln -sfn "/run/user/$(id -u)/bus" "$sandbox/run/bus"
export HOME=$sandbox/home XDG_RUNTIME_DIR=$sandbox/run TMPDIR=$sandbox/tmp
export XDG_CONFIG_HOME=$HOME/.config XDG_DATA_HOME=$HOME/.local/share XDG_CACHE_HOME=$HOME/.cache
work=$HOME/project
mkdir -p "$XDG_CONFIG_HOME/RelayTerminal" "$XDG_CONFIG_HOME/relay" "$XDG_DATA_HOME" "$XDG_CACHE_HOME" "$work"
printf "PS1='\\\\w \\\$ '\nunset PROMPT_COMMAND\n" >"$HOME/.bashrc"
printf 'SMALL = 1\nkeep = True\n' >"$work/alpha.py"
printf '# beta\n%s\n' "$(seq 1 12)" >"$work/beta.py"
cat >"$XDG_CONFIG_HOME/RelayTerminal/relay.conf" <<CONF
[instructions]
onboarded=true
[theme]
name=relay-dark
[appearance]
pane_colours=type
[provider]
preset=local:stub
CONF
printf '{"version": 1, "endpoints": [{"id": "local:stub", "label": "Stub", "base_url": "http://127.0.0.1:%s/v1", "model": "stub", "server": "openai-compatible", "context_window": 131072}]}\n' \
    "$port" >"$XDG_CONFIG_HOME/relay/local-models.json"

(cd "$work" && exec "$build/relay" --workspace "$work") >"$sandbox/relay.log" 2>&1 &
relay_pid=$!
sleep 7
win= ; best=0
for w in $(xdotool search --pid "$relay_pid" 2>/dev/null); do
    eval "$(xdotool getwindowgeometry --shell "$w" 2>/dev/null)"
    (( WIDTH * HEIGHT > best )) && { best=$(( WIDTH * HEIGHT )); win=$w; }
done
[[ -z $win ]] && { echo "no Relay window"; cat "$sandbox/relay.log"; exit 1; }
xdotool windowmove "$win" 0 0 windowsize "$win" $width $height
xdotool windowfocus "$win"; sleep 1.5

shot() {
    xdotool mousemove $((width + 20)) $((height + 20)); sleep 0.8
    import -window "$win" "$out/implementer-$1.png"
    convert "$out/implementer-$1.png" -crop ${width}x640+0+90 +repage -scale 150% "$out/implementer-$1-body.png"
    { echo "--- $1 (body)"; tesseract "$out/implementer-$1-body.png" - --psm 6 2>/dev/null; } >>"$out/implementer-notes.txt"
}
ask() { t "$1"; k Return; }

rm -f "$out/implementer-notes.txt"
ask 'walk through the files'; sleep 30; shot 01-walk
ask 'second prompt, no tools'; sleep 8; shot 02-second
ask 'tools first please'; sleep 14; shot 03-tools
ask '/recap'; sleep 12; shot 04-recap
cp "$sandbox/relay.log" "$out/relay.log" 2>/dev/null
mkdir -p "$out/logs" && cp -r "$sandbox/home/.local/share/relay/logs/." "$out/logs/" 2>/dev/null
printf 'done: %s\n' "$out"
