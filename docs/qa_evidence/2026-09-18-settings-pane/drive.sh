#!/usr/bin/env bash
# The Settings pane (card SP4N, owner 2026-09-18): implementer screenshots under Xvfb with an
# isolated profile. No provider account is needed: the profile points at stub-provider.py on
# 127.0.0.1 (the recipe from docs/qa_evidence/2026-09-17-website-update/).
#
#   docs/qa_evidence/2026-09-18-settings-pane/drive.sh [build-dir]
#
#   implementer-a-general.png    Ctrl+Shift+A: the pane beside the terminal, General tab
#   implementer-b-search.png     "think" typed: settings rows and actions in one list
#   implementer-c-actions.png    the Actions tab: Recent, then every action with its keys
#   implementer-d-agent.png      the Agent tab with its headings
#   implementer-e-closed.png     Esc: the pane is gone and the prompt box has focus again
#   implementer-f-light.png      the same pane in Relay Light
#
# Needs Xvfb, xdotool, ImageMagick. Captures name the largest window of the relay pid.
set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"
out=$PWD
root=$(cd ../../.. && pwd)
build=${1:-$root/build}
width=1400 height=880
port=${RELAY_QA_PORT:-8794}

display=
for n in $(seq 160 199); do [[ -e /tmp/.X11-unix/X$n ]] || { display=:$n; break; }; done
[[ -z $display ]] && { echo "no free display"; exit 1; }

sandbox=${RELAY_SHOT_HOME:-/tmp/relay-settings-shots}
stub_pid= xvfb_pid= relay_pid=
cleanup() { kill "${relay_pid:-0}" "${stub_pid:-0}" "${xvfb_pid:-0}" 2>/dev/null; rm -rf "$sandbox"; }
trap cleanup EXIT

python3 "$out/stub-provider.py" "$port" >/dev/null 2>&1 &
stub_pid=$!
Xvfb "$display" -screen 0 $((width + 40))x$((height + 40))x24 >/dev/null 2>&1 &
xvfb_pid=$!
sleep 2
kill -0 "$xvfb_pid" 2>/dev/null || { echo "Xvfb died on $display"; exit 1; }
export DISPLAY=$display
export RELAY_KEYRING=off

t() { xdotool type --delay 14 "$1"; }
k() { xdotool key --delay 45 "$@"; }

prepare() {   # prepare <theme-id>
    rm -rf "$sandbox"
    export HOME=$sandbox
    export XDG_CONFIG_HOME=$HOME/.config XDG_DATA_HOME=$HOME/.local/share XDG_CACHE_HOME=$HOME/.cache
    work=$HOME/project
    mkdir -p "$XDG_CONFIG_HOME/RelayTerminal" "$XDG_DATA_HOME" "$XDG_CACHE_HOME" "$work/src"
    printf "PS1='\\\\w \$ '\nunset PROMPT_COMMAND\n" >"$HOME/.bashrc"
    printf '# project\n' >"$work/README.md"
    printf '#include <stdio.h>\nint main(void) { printf("hi\\n"); return 0; }\n' >"$work/src/main.c"
    cat >"$XDG_CONFIG_HOME/RelayTerminal/relay.conf" <<CONF
[instructions]
onboarded=true
[theme]
name=$1
[provider]
preset=custom
base=http://127.0.0.1:$port/v1
model=glm-5.3
extra={}
max_tokens=1024
CONF
}

largest_window() {
    win= ; local best=0 w
    for w in $(xdotool search --pid "$relay_pid" 2>/dev/null); do
        eval "$(xdotool getwindowgeometry --shell "$w" 2>/dev/null)"
        (( WIDTH * HEIGHT > best )) && { best=$(( WIDTH * HEIGHT )); win=$w; }
    done
}

shot() {
    xdotool mousemove $((width + 20)) $((height + 20)); sleep 0.6
    import -window "$win" "$out/implementer-$1.png"
}

start() {   # start <theme-id>
    prepare "$1"
    "$build/relay" --workspace "$work" >"$out/relay-stderr-$1.log" 2>&1 &
    relay_pid=$!
    sleep 7
    largest_window
    [[ -z $win ]] && { echo "no Relay window"; exit 1; }
    xdotool windowmove "$win" 0 0 windowsize "$win" $width $height
    xdotool windowfocus "$win"; sleep 2
    t 'ls -la src'; k Return; sleep 1.5
}

start relay-dark
k ctrl+shift+a; sleep 2
shot a-general
t 'think'; sleep 1.5
shot b-search
k ctrl+a BackSpace; sleep 0.5
k Left; sleep 1.2                 # wraps from General to Actions
shot c-actions
k Right Right Right Right Right; sleep 1.2   # General → … → Agent
shot d-agent
k Escape; sleep 1.2
t 'echo back in the prompt box'; sleep 1
shot e-closed
kill "$relay_pid" 2>/dev/null; wait "$relay_pid" 2>/dev/null; relay_pid=

start relay-light
k ctrl+shift+a; sleep 2
shot f-light
kill "$relay_pid" 2>/dev/null; wait "$relay_pid" 2>/dev/null; relay_pid=
printf 'done: %s\n' "$out"
