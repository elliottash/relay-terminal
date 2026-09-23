#!/usr/bin/env bash
# #1MGS, relay side: an agent reply holding Markdown images, and a prompt carrying an `@` image,
# printed into a pane under Xvfb with an isolated HOME, XDG_CONFIG_HOME, XDG_RUNTIME_DIR and
# TMPDIR. No provider account: the profile points a local model endpoint at stub-provider.py.
#
#   docs/qa_evidence/2026-09-22-1MGS-relay-images/drive.sh [build-dir]
#
# Until the engine reads kitty graphics the escapes are swallowed, so the shot shows the layout
# around them: the alt lines, the text after a picture on a row of its own, the web image as a
# link, the missing file as alt text and path, and no escape body leaking into the grid.
#
# Needs Xvfb, xdotool and ImageMagick.
set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"
out=$PWD
root=$(cd ../../.. && pwd)
build=${1:-$root/build}
width=1200 height=760
port=${RELAY_QA_PORT:-8821}
prompt='what does @chart.png show'

display=
for n in $(seq 520 559); do [[ -e /tmp/.X11-unix/X$n ]] || { display=:$n; break; }; done
[[ -z $display ]] && { echo "no free display"; exit 1; }

sandbox=$(mktemp -d /tmp/relay-1mgs.XXXXXX)
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
convert -size 600x300 gradient:steelblue-orange "$work/chart.png"
convert -size 1600x200 gradient:green-white "$work/wide.png"
echo "# notes" >"$work/notes.md"
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
}

# A reply first: the stub is no vision model, so a prompt carrying an image is refused before it
# reaches the provider, and its thumbnail is what the second shot is for.
t 'show me the chart'; sleep 0.5
k ctrl+Return; sleep 12; shot reply
t "$prompt"; sleep 0.5
k ctrl+Return; sleep 8; shot attachment
printf 'done: %s\n' "$out"
