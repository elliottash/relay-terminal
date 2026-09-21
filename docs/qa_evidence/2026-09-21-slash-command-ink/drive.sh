#!/usr/bin/env bash
# #SQ3D: the `/command` of a prompt the pane echoes, photographed under Xvfb with an isolated
# HOME, XDG_CONFIG_HOME, XDG_RUNTIME_DIR and TMPDIR. No provider account: the profile points a
# local model endpoint at stub-provider.py on 127.0.0.1.
#
#   docs/qa_evidence/2026-09-21-slash-command-ink/drive.sh [build-dir]
#
# Types a prompt that opens with /deliver, sends it to the agent, and shoots the row the pane
# prints back — on Relay Dark, whose agent band is light, and then on Relay Light, whose band is
# dark, with nothing retyped in between. analyse.py reads the inks out of the two shots.
#
# Needs Xvfb, xdotool and ImageMagick.
set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"
out=$PWD
root=$(cd ../../.. && pwd)
build=${1:-$root/build}
width=1200 height=760
port=${RELAY_QA_PORT:-8821}
prompt='/deliver trace and fix the bug. it affects claude as well'

display=
for n in $(seq 520 559); do [[ -e /tmp/.X11-unix/X$n ]] || { display=:$n; break; }; done
[[ -z $display ]] && { echo "no free display"; exit 1; }

sandbox=$(mktemp -d /tmp/relay-slq3.XXXXXX)
stub_pid= xvfb_pid= relay_pid=
cleanup() {
    local pid
    for pid in $relay_pid $stub_pid $xvfb_pid; do kill "$pid" 2>/dev/null; done
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

t() { xdotool type --delay 30 "$1"; }
k() { xdotool key --delay 60 "$@"; }

mkdir -p "$sandbox/home" "$sandbox/run" "$sandbox/tmp"; chmod 700 "$sandbox/run"
export HOME=$sandbox/home XDG_RUNTIME_DIR=$sandbox/run TMPDIR=$sandbox/tmp
export XDG_CONFIG_HOME=$HOME/.config XDG_DATA_HOME=$HOME/.local/share XDG_CACHE_HOME=$HOME/.cache
work=$HOME/project
mkdir -p "$XDG_CONFIG_HOME/RelayTerminal" "$XDG_CONFIG_HOME/relay" "$XDG_DATA_HOME" "$XDG_CACHE_HOME" "$work"
printf "PS1='\\\\w \\\$ '\nunset PROMPT_COMMAND\n" >"$HOME/.bashrc"
cat >"$XDG_CONFIG_HOME/RelayTerminal/relay.conf" <<CONF
[instructions]
onboarded=true
[theme]
name=relay-dark
[provider]
preset=local:stub
[models]
tier\\main=local:stub|stub|high
tier\\high=local:stub|stub|high
tier\\flash=local:stub|stub|low
tier\\lite=local:stub|stub|low
[security]
approvals_chosen=true
[url_handler]
announced=true
CONF
printf '{"version": 1, "endpoints": [{"id": "local:stub", "label": "Stub", "base_url": "http://127.0.0.1:%s/v1", "model": "stub", "server": "openai-compatible", "context_window": 131072}]}\n' \
    "$port" >"$XDG_CONFIG_HOME/relay/local-models.json"

(cd "$work" && exec "$build/relay" --workspace "$work") >"$sandbox/relay.log" 2>&1 &
relay_pid=$!
sleep 8
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
    import -window "$win" "$out/$1.png"
    convert "$out/$1.png" -crop $((width - 40))x130+20+140 +repage -scale 200% "$out/$1-row.png"
}

# One ordinary prompt first: the pane configures the agent on its first agent submission, and the
# skill list — which is what makes `/deliver` a command rather than an unknown one — arrives with
# it. Then the prompt this card is about.
t 'hello'; k ctrl+Return; sleep 14; shot warmup
t "$prompt"; sleep 0.5; shot composer
k ctrl+Return; sleep 14; shot dark
# The same row on a theme whose agent band is dark. Nothing is retyped: the view repaints what is
# already in the scrollback, which is what a *role* on the row buys (and a written colour cannot).
t '/light'; k Return; sleep 4; shot light
printf 'done: %s\n' "$out"
