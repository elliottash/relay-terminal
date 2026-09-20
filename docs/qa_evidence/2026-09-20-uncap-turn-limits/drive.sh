#!/usr/bin/env bash
# Uncapped turn limits and loop detection (#2CZP): implementer screenshots under Xvfb with an
# isolated HOME, XDG_CONFIG_HOME, XDG_RUNTIME_DIR and TMPDIR. No provider account and no network:
# the profile points a local model endpoint at stub-provider.py on 127.0.0.1.
#
#   docs/qa_evidence/2026-09-20-uncap-turn-limits/drive.sh [build-dir]
#
#   implementer-a-loop.png     a turn that repeats one failing call: two ↻ nudge lines, then
#                              "‖ Stopped: repeating itself (…)" and ▸ Continue — a stop that
#                              names the repetition rather than a count nowhere near its limit
#   implementer-b-sweep.png    30 distinct reads in one turn: past the old 256/150 caps' reach,
#                              no stop, the cadence recitation passing through the status line
#   implementer-c-options.png  Options › Security: the two turn-bound rows at 500 and 2000, worded
#                              as a backstop for a runaway turn
#
# Needs Xvfb, xdotool, ImageMagick, tesseract.
set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"
out=$PWD
root=$(cd ../../.. && pwd)
build=${1:-$root/build}
width=1300 height=900
port=${RELAY_QA_PORT:-8821}

display=
for n in $(seq 170 199); do [[ -e /tmp/.X11-unix/X$n ]] || { display=:$n; break; }; done
[[ -z $display ]] && { echo "no free display"; exit 1; }

sandbox=$(mktemp -d /tmp/relay-2czp.XXXXXX)
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

t() { xdotool type --delay 35 "$1"; }
k() { xdotool key --delay 60 "$@"; }

mkdir -p "$sandbox/home" "$sandbox/run" "$sandbox/tmp"; chmod 700 "$sandbox/run"
ln -sfn "/run/user/$(id -u)/bus" "$sandbox/run/bus"
export HOME=$sandbox/home XDG_RUNTIME_DIR=$sandbox/run TMPDIR=$sandbox/tmp
export XDG_CONFIG_HOME=$HOME/.config XDG_DATA_HOME=$HOME/.local/share XDG_CACHE_HOME=$HOME/.cache
work=$HOME/project
mkdir -p "$XDG_CONFIG_HOME/RelayTerminal" "$XDG_CONFIG_HOME/relay" "$XDG_DATA_HOME" "$XDG_CACHE_HOME" "$work"
printf "PS1='\\\\w \\\$ '\nunset PROMPT_COMMAND\n" >"$HOME/.bashrc"
for i in $(seq 0 39); do printf 'file %s\n' "$i" >"$work/f$i.txt"; done
cat >"$XDG_CONFIG_HOME/RelayTerminal/relay.conf" <<CONF
[instructions]
onboarded=true
[security]
approvals_chosen=true
approvals_ask=@Invalid()
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
# Click into the prompt box and wait before the first prompt: typing into a pane that is still
# wiring up its worker loses the first characters, and the stub's scene is keyed on a word in them.
xdotool mousemove 300 $((height - 90)) click 1; sleep 3

shot() {
    xdotool mousemove $((width + 20)) $((height + 20)); sleep 0.8
    import -window "$win" "$out/implementer-$1.png"
    convert "$out/implementer-$1.png" -crop ${width}x700+0+80 +repage -scale 130% "$out/implementer-$1-body.png"
    { echo "--- $1 (body)"; tesseract "$out/implementer-$1-body.png" - --psm 6 2>/dev/null; } >>"$out/implementer-notes.txt"
}
ask() { sleep 0.5; t "$1"; sleep 0.5; k Return; }

rm -f "$out/implementer-notes.txt"
ask 'loop please: read that file'; sleep 35; shot a-loop
ask 'sweep please: read every file and tell me what changed'; sleep 60; shot b-sweep
k ctrl+shift+o; sleep 3; t 'step limit'; sleep 2; shot c-options
cp "$sandbox/relay.log" "$out/relay.log" 2>/dev/null
mkdir -p "$out/logs" && cp "$sandbox/home/.local/share/relay/logs/worker.log" "$out/logs/gui-worker.log" 2>/dev/null
printf 'done: %s\n' "$out"
