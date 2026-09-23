#!/usr/bin/env bash
# #MDA7 read aloud, driven once under Xvfb with an isolated HOME, XDG_CONFIG_HOME, XDG_RUNTIME_DIR
# and TMPDIR (adapted from docs/qa_evidence/2026-09-21-slash-command-ink/drive.sh). No provider
# account: a local model endpoint points at stub-provider.py on 127.0.0.1.
#
#   docs/qa_evidence/2026-09-22-read-aloud/drive.sh [build-dir]
#
# "Read replies aloud automatically" is on in the profile, so: a prompt's reply is read by itself
# (shot auto), Esc stops it (shot esc), /speak reads it again (shot speak), /speak stop stops it
# (shot speakstop). After each step the speaker processes Relay started are listed in drive.log.
set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"
out=$PWD
root=$(cd ../../.. && pwd)
build=${1:-$root/build}
width=1200 height=760
port=${RELAY_QA_PORT:-8822}
log=$out/drive.log
: >"$log"

display=
for n in $(seq 560 599); do [[ -e /tmp/.X11-unix/X$n ]] || { display=:$n; break; }; done
[[ -z $display ]] && { echo "no free display"; exit 1; }

sandbox=$(mktemp -d /tmp/relay-mda7.XXXXXX)
stub_pid= xvfb_pid= relay_pid=
cleanup() {
    local pid
    for pid in $relay_pid $stub_pid $xvfb_pid; do kill "$pid" 2>/dev/null; done
    cp "$sandbox/relay.log" "$out/relay-stdout.log" 2>/dev/null
    # spd-say autospawns a speech-dispatcher on the sandbox's own runtime directory: it goes too.
    pkill -KILL -f "$sandbox/run/speech-dispatcher" 2>/dev/null   # it ignores SIGTERM
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
note() { printf '%s %s\n' "$(date +%H:%M:%S.%3N)" "$*" | tee -a "$log"; }
speakers() {
    local found
    found=$(pgrep -af 'spd-say -w -N relay|espeak-ng --stdin' | grep -v pgrep)
    note "  speaker processes: ${found:-(none)}"
}

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
[speech]
auto_read=true
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
    xdotool mousemove $((width + 20)) $((height + 20)); sleep 0.5
    import -window "$win" "$out/$1.png"
    note "shot $1.png"
}

speakers
note "prompt: hello (auto-read is on)"
t 'hello'; k ctrl+Return
for i in $(seq 1 40); do sleep 0.5; pgrep -f 'spd-say -w -N relay|espeak-ng --stdin' >/dev/null && break; done
sleep 1.5; shot auto; speakers
note "key: Escape"
k Escape; sleep 1.0; shot esc; speakers
note "prompt: /speak"
t '/speak'; k Return
for i in $(seq 1 20); do sleep 0.25; pgrep -f 'spd-say -w -N relay|espeak-ng --stdin' >/dev/null && break; done
sleep 1.5; shot speak; speakers
note "prompt: /speak stop"
t '/speak stop'; k Return; sleep 1.0; shot speakstop; speakers
note "key: Escape with nothing being read (Esc keeps its old meaning: nothing is stopped, no error)"
k Escape; sleep 1.0; speakers
sleep 2.5
note "after the 2 s stop-helper deadline:"
lingering=$(pgrep -af 'spd-say' | grep -v pgrep)
note "  spd-say processes left: ${lingering:-(none)}"
note "done: $out"
