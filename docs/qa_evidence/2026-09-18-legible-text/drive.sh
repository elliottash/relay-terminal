#!/usr/bin/env bash
# Legibility pass (2026-09-18): the same layouts before and after, in Relay Dark and Relay Light,
# under Xvfb with an isolated HOME, XDG_CONFIG_HOME, XDG_RUNTIME_DIR and TMPDIR. No provider
# account: the model is a local endpoint pointed at the subagents card's fake-provider.py on
# 127.0.0.1, which starts three background subagents that each run one command.
#
#   docs/qa_evidence/2026-09-18-legible-text/drive.sh <relay-binary> <prefix> [theme...]
#
# Writes <prefix>-<theme>-layout.png (a terminal with agent notes, the subagent pane on a tab with
# its transcript, the Switchboard, Options scrolled to Diagnostics) and <prefix>-<theme>-bell.png
# (the notification list). Needs Xvfb, xdotool and ImageMagick.
set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"
out=$PWD
root=$(cd ../../.. && pwd)
relay=${1:?relay binary}; prefix=${2:?prefix}; shift 2
themes=${*:-relay-dark relay-light}
width=1440 height=900
port=${RELAY_QA_PORT:-8813}

display=
for n in $(seq 170 199); do [[ -e /tmp/.X11-unix/X$n ]] || { display=:$n; break; }; done
[[ -z $display ]] && { echo "no free display"; exit 1; }

sandbox=$(mktemp -d "${TMPDIR:-/tmp}/relay-legible.XXXXXX")
stub_pid= xvfb_pid= relay_pid=
cleanup() {
    local pid
    for pid in $relay_pid $stub_pid $xvfb_pid; do kill "$pid" 2>/dev/null; done   # never `kill 0`
    rm -rf "$sandbox"
}
trap cleanup EXIT

python3 "$root/docs/qa_evidence/2026-09-18-subagents-tabbed-pane/fake-provider.py" "$port" >/dev/null 2>&1 &
stub_pid=$!
Xvfb "$display" -screen 0 $((width + 40))x$((height + 40))x24 >/dev/null 2>&1 &
xvfb_pid=$!
sleep 2
kill -0 "$xvfb_pid" 2>/dev/null || { echo "Xvfb died on $display"; exit 1; }
[[ -e /tmp/.X11-unix/X${display#:} ]] || { echo "Xvfb is not up on $display"; exit 1; }
export DISPLAY=$display RELAY_KEYRING=off

t() { xdotool type --delay 18 "$1"; }
k() { xdotool key --delay 60 "$@"; }
click() { xdotool mousemove "$1" "$2" click 1; sleep 0.4; }

start() {   # start <theme-id>
    rm -rf "$sandbox/home" "$sandbox/run" "$sandbox/tmp"
    mkdir -p "$sandbox/home" "$sandbox/run" "$sandbox/tmp"; chmod 700 "$sandbox/run"
    export HOME=$sandbox/home XDG_RUNTIME_DIR=$sandbox/run TMPDIR=$sandbox/tmp
    export XDG_CONFIG_HOME=$HOME/.config XDG_DATA_HOME=$HOME/.local/share XDG_CACHE_HOME=$HOME/.cache
    work=$HOME/project
    mkdir -p "$XDG_CONFIG_HOME/RelayTerminal" "$XDG_CONFIG_HOME/relay" "$XDG_DATA_HOME" "$XDG_CACHE_HOME" "$work/issues/features"
    printf "PS1='\\\\w \$ '\nunset PROMPT_COMMAND\n" >"$HOME/.bashrc"
    printf '# project\n' >"$work/README.md"
    cp "$root/issues/board.yaml" "$work/issues/"
    cp "$root"/issues/features/2026-09-18-*.md "$work/issues/features/" 2>/dev/null
    cat >"$XDG_CONFIG_HOME/RelayTerminal/relay.conf" <<CONF
[instructions]
onboarded=true
[theme]
name=$1
[appearance]
pane_colours=type
[provider]
preset=local:fake
CONF
    printf '{"version": 1, "endpoints": [{"id": "local:fake", "label": "Fake", "base_url": "http://127.0.0.1:%s/v1", "model": "fake", "server": "openai-compatible", "context_window": 131072}]}\n' \
        "$port" >"$XDG_CONFIG_HOME/relay/local-models.json"
    (cd "$work" && exec "$relay" --workspace "$work") >"$sandbox/relay.log" 2>&1 &
    relay_pid=$!
    sleep 7
    win= ; local best=0 w
    for w in $(xdotool search --pid "$relay_pid" 2>/dev/null); do
        eval "$(xdotool getwindowgeometry --shell "$w" 2>/dev/null)"
        (( WIDTH * HEIGHT > best )) && { best=$(( WIDTH * HEIGHT )); win=$w; }
    done
    [[ -z $win ]] && { echo "no Relay window"; cat "$sandbox/relay.log"; exit 1; }
    xdotool windowmove "$win" 0 0 windowsize "$win" $width $height
    xdotool windowfocus "$win"; sleep 1.5
}

stop() { [[ -n $relay_pid ]] && { kill "$relay_pid"; wait "$relay_pid"; } 2>/dev/null; relay_pid=; }

shot() { xdotool mousemove $((width + 20)) $((height + 20)); sleep 0.8; import -window "$win" "$out/$prefix-$1.png"; }

scene() {   # scene <theme>
    start "$1"
    t '*Please start three subagents on this folder'; k Return; sleep 1.5
    k ctrl+t; sleep 12                       # a background tab, so the turn's end is notified
    k ctrl+shift+Tab; sleep 2
    click 80 870; sleep 3                    # the subagents strip: the pane opens on the right
    click 300 500; k ctrl+shift+s; sleep 2.5
    click 150 500; k ctrl+shift+o; sleep 2.5
    # Options: scroll its page to the end, where Diagnostics is.
    xdotool mousemove 1270 600; for _ in $(seq 1 25); do xdotool click 5; done; sleep 0.8
    click 150 300; sleep 1
    shot "$1-layout"
    import -window root -crop ${width}x${height}+0+0 +repage /dev/null 2>/dev/null
    click "${BELL_X:-1205}" 27; sleep 1.2     # the bell: a popup, so grab the screen
    xdotool mousemove $((width + 20)) $((height + 20)); sleep 0.5
    import -window root -crop ${width}x${height}+0+0 +repage "$out/$prefix-$1-bell.png"
    k Escape
    stop
}

for theme in $themes; do scene "$theme"; done
printf 'done: %s\n' "$out"
