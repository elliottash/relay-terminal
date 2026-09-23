#!/usr/bin/env bash
# #1MGS: inline images, photographed in a real Relay under Xvfb with an isolated HOME,
# XDG_CONFIG_HOME, XDG_RUNTIME_DIR, TMPDIR and RELAY_KEYRING=off. No provider account: the
# profile points a local model endpoint at stub-provider.py on 127.0.0.1.
#
#   docs/qa_evidence/2026-09-22-inline-images/drive.sh [build-dir]
#
# 01  the shell prints a kitty, an iTerm2 and a sixel picture (demo.sh, run from ~/.bashrc, so
#     nothing is typed into the terminal)
# 02  a prompt with an image attached (@attached.png), and the agent's reply with a Markdown image
#     (and a remote one that must stay a link)
# 03  quit and start Relay again on the same profile: the saved pane draws its pictures again
# 04  `clear`: every picture is gone with its text
#
# Needs Xvfb, xdotool, ImageMagick and python3-PIL.
set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"
out=$PWD
root=$(cd ../../.. && pwd)
build=${1:-$root/build}
width=1400 height=1000
port=${RELAY_QA_PORT:-8831}

display=
for n in $(seq 560 599); do [[ -e /tmp/.X11-unix/X$n ]] || { display=:$n; break; }; done
[[ -z $display ]] && { echo "no free display"; exit 1; }
sandbox=$(mktemp -d /tmp/rl-1mgs.XXXX)
stub_pid= xvfb_pid= relay_pid=
cleanup() { local pid; for pid in $relay_pid $stub_pid $xvfb_pid; do kill "$pid" 2>/dev/null; done; rm -rf "$sandbox"; }
trap cleanup EXIT

python3 "$out/stub-provider.py" "$port" >/dev/null 2>&1 & stub_pid=$!
Xvfb "$display" -screen 0 $((width + 40))x$((height + 40))x24 >/dev/null 2>&1 & xvfb_pid=$!
sleep 2
kill -0 "$xvfb_pid" 2>/dev/null || { echo "Xvfb died on $display"; exit 1; }
export DISPLAY=$display RELAY_KEYRING=off
unset RELAY_OPEN_SOCKET
mkdir -p "$sandbox/home" "$sandbox/run" "$sandbox/tmp"; chmod 700 "$sandbox/run"
export HOME=$sandbox/home XDG_RUNTIME_DIR=$sandbox/run TMPDIR=$sandbox/tmp
export XDG_CONFIG_HOME=$HOME/.config XDG_DATA_HOME=$HOME/.local/share XDG_CACHE_HOME=$HOME/.cache
work=$HOME/project
mkdir -p "$XDG_CONFIG_HOME/RelayTerminal" "$XDG_CONFIG_HOME/relay" "$XDG_DATA_HOME" "$XDG_CACHE_HOME" "$work"
python3 "$out/make-images.py" "$work" || exit 1
cp "$out/demo.sh" "$work/"
cat >"$HOME/.bashrc" <<RC
PS1='\\w \\\$ '
unset PROMPT_COMMAND
[[ -e ~/.demo-ran ]] || { touch ~/.demo-ran; bash "$work/demo.sh" "$work"; }
RC
cat >"$XDG_CONFIG_HOME/RelayTerminal/relay.conf" <<CONF
[instructions]
onboarded=true
[theme]
name=relay-dark
[provider]
preset=local:stub
[models]
tier\\main=local:stub|llava-stub|high
tier\\high=local:stub|llava-stub|high
tier\\flash=local:stub|llava-stub|low
tier\\lite=local:stub|llava-stub|low
[security]
approvals_chosen=true
[url_handler]
announced=true
CONF
printf '{"version": 1, "endpoints": [{"id": "local:stub", "label": "Stub", "base_url": "http://127.0.0.1:%s/v1", "model": "llava-stub", "server": "openai-compatible", "context_window": 131072}]}\n' \
    "$port" >"$XDG_CONFIG_HOME/relay/local-models.json"

launch() {  # $1: "--workspace <dir> --fresh" the first time; nothing after, so the saved layout
            # reopens (an explicit --workspace also means a new window, src/main.cpp)
    (cd "$work" && exec "$build/relay" $1 --engine-core libvterm) >>"$out/relay.log" 2>&1 & relay_pid=$!
    sleep 8
    win= ; best=0
    for w in $(xdotool search --pid "$relay_pid" 2>/dev/null); do
        eval "$(xdotool getwindowgeometry --shell "$w" 2>/dev/null)"
        (( WIDTH * HEIGHT > best )) && { best=$(( WIDTH * HEIGHT )); win=$w; }
    done
    [[ -z $win ]] && { echo "no Relay window"; cat "$out/relay.log"; exit 1; }
    xdotool windowmove "$win" 0 0 windowsize "$win" $width $height
    xdotool windowfocus "$win"; sleep 2
}
: >"$out/relay.log"
launch "--workspace $work --fresh"
t() { xdotool type --delay 25 "$1"; }
k() { xdotool key --delay 60 "$@"; }
shot() { xdotool mousemove $((width + 20)) $((height + 20)); sleep 0.8; import -window "$win" "$out/$1.png"; }

shot 01-protocols
t 'what is in this picture? @attached.png'; sleep 0.5; k ctrl+Return; sleep 12
shot 02-conversation
kill -TERM "$relay_pid"; wait "$relay_pid" 2>/dev/null; sleep 1
launch ""
shot 03-restored
t 'clear'; k Return; sleep 2
shot 04-cleared
printf 'done: %s\n' "$out"
