#!/usr/bin/env bash
# Pane types (#SPBN), pane states (#XM0T) and the remote-session header: implementer screenshots
# under Xvfb with an isolated HOME, XDG_CONFIG_HOME, XDG_RUNTIME_DIR and TMPDIR. No provider account:
# the profile points a local model endpoint at stub-provider.py on 127.0.0.1, and `ssh` on PATH is
# fake-ssh.c (see its header) because there is no sshd to log in to.
#
#   docs/qa_evidence/2026-09-18-actions-red-orange-and-lit-buttons/drive.sh [build-dir] [scene...]
#
# Scenes (all by default):
#   layout   implementer-layout-<theme>-<mode>.png: a local terminal whose agent has a subagent
#            running, an ssh terminal, the Switchboard, Options and the subagent pane, side by side;
#            Relay Dark and Relay Light, pane colours by type and by group, and Dark with them off.
#   tabs     implementer-tabs-<theme>.png and its -bar crop: background tabs that need you, finished,
#            failed, are working, running a command, or are in an ssh session; the bell's list.
#
# Needs Xvfb, xdotool, ImageMagick, gcc and script(1).
set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"
out=$PWD
root=$(cd ../../.. && pwd)
build=${1:-$root/build}; shift || true
scenes=${*:-buttons}
width=1440 height=900
port=${RELAY_QA_PORT:-8797}

display=
for n in $(seq 160 199); do [[ -e /tmp/.X11-unix/X$n ]] || { display=:$n; break; }; done
[[ -z $display ]] && { echo "no free display"; exit 1; }

sandbox=$(mktemp -d /tmp/relay-pane-types.XXXXXX)
stub_pid= xvfb_pid= relay_pid=
cleanup() {
    local pid
    for pid in $relay_pid $stub_pid $xvfb_pid; do kill "$pid" 2>/dev/null; done   # never `kill 0`
    rm -rf "$sandbox"
}
trap cleanup EXIT

mkdir -p "$sandbox/bin"
gcc -O2 -o "$sandbox/bin/ssh" "$out/fake-ssh.c" || exit 1
python3 "$out/stub-provider.py" "$port" >/dev/null 2>&1 &
stub_pid=$!
Xvfb "$display" -screen 0 $((width + 40))x$((height + 40))x24 >/dev/null 2>&1 &
xvfb_pid=$!
sleep 2
kill -0 "$xvfb_pid" 2>/dev/null || { echo "Xvfb died on $display"; exit 1; }
export DISPLAY=$display RELAY_KEYRING=off

t() { xdotool type --delay 18 "$1"; }
k() { xdotool key --delay 60 "$@"; }
click() { xdotool mousemove "$1" "$2" click 1; sleep 0.4; }

prepare() {   # prepare <theme-id> <pane-colours>
    rm -rf "$sandbox/home" "$sandbox/run" "$sandbox/tmp"
    mkdir -p "$sandbox/home" "$sandbox/run" "$sandbox/tmp"; chmod 700 "$sandbox/run"
    export HOME=$sandbox/home XDG_RUNTIME_DIR=$sandbox/run TMPDIR=$sandbox/tmp
    export XDG_CONFIG_HOME=$HOME/.config XDG_DATA_HOME=$HOME/.local/share XDG_CACHE_HOME=$HOME/.cache
    work=$HOME/project
    mkdir -p "$XDG_CONFIG_HOME/RelayTerminal" "$XDG_CONFIG_HOME/relay" "$XDG_DATA_HOME" "$XDG_CACHE_HOME" "$work/issues/features"
    printf "PS1='\\\\w \$ '\nunset PROMPT_COMMAND\nexport PATH=%s:\$PATH\n" "$sandbox/bin" >"$HOME/.bashrc"
    printf '# project\n' >"$work/README.md"
    cp "$root/issues/board.yaml" "$work/issues/"
    cp "$root"/issues/features/2026-09-18-*.md "$work/issues/features/" 2>/dev/null
    cat >"$XDG_CONFIG_HOME/RelayTerminal/relay.conf" <<CONF
[instructions]
onboarded=true
[theme]
name=$1
[appearance]
pane_colours=$2
[provider]
preset=local:stub
CONF
    printf '{"version": 1, "endpoints": [{"id": "local:stub", "label": "Stub", "base_url": "http://127.0.0.1:%s/v1", "model": "stub", "server": "openai-compatible", "context_window": 131072}]}\n' \
        "$port" >"$XDG_CONFIG_HOME/relay/local-models.json"
}

start() {   # start <theme-id> <pane-colours>
    prepare "$1" "$2"
    (cd "$work" && exec "$build/relay" --workspace "$work") >"$sandbox/relay.log" 2>&1 &
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

shot() {   # shot <name> [crop]
    xdotool mousemove $((width + 20)) $((height + 20)); sleep 0.8
    import -window "$win" "$out/implementer-$1.png"
    [[ -n ${2:-} ]] && convert "$out/implementer-$1.png" -crop "$2" +repage -scale 200% "$out/implementer-$1-bar.png"
}

layout() {   # layout <theme> <mode>
    start "$1" "$2"
    t '*spawn a helper'; k Return; sleep 9
    click 80 870; sleep 2.5                 # the subagent's row: its pane opens on the right
    click 300 500; k ctrl+shift+s; sleep 2.5
    click 150 500; k ctrl+shift+o; sleep 2.5
    click 212 60; sleep 3                   # ⬓+ on the terminal: a new terminal below it
    t 'ssh demo@build-box'; k Return; sleep 3
    click 150 250; sleep 2                  # focus back on the agent's terminal
    shot "layout-$1-$2"
    stop
}

tabs() {   # tabs <theme>
    start "$1" type
    k ctrl+t; sleep 2.5; t '*ask me which fix'; k Return; sleep 1
    k ctrl+t; sleep 2.5; t '*quick one'; k Return; sleep 1
    k ctrl+t; sleep 2.5; t '*fail please'; k Return; sleep 1
    k ctrl+t; sleep 2.5; t '*slow task'; k Return; sleep 1
    k ctrl+t; sleep 2.5; t 'sleep 600'; k Return; sleep 1
    k ctrl+t; sleep 2.5; t 'ssh demo@build-box'; k Return; sleep 1.5
    for _ in 1 2 3 4 5 6; do k ctrl+shift+Tab; sleep 0.25; done   # back to the first tab, in passing
    sleep 5
    shot "tabs-$1" 1000x80+0+0
    click 1295 27; sleep 1.2                # the bell: what was notified (a popup: grab the screen)
    import -window root -crop ${width}x${height}+0+0 +repage "$out/implementer-tabs-$1-bell.png"
    k Escape
    stop
}


buttons() {   # buttons <theme> <pane-colours>: the lit title-bar buttons and the Actions band
    start "$1" "$2"
    shot "buttons-$1-$2-none" 480x44+960+0          # nothing open: no button is lit
    k ctrl+shift+a; sleep 2.5                        # Actions
    shot "buttons-$1-$2-actions" 480x44+960+0
    k ctrl+shift+s; sleep 2.5                        # Switchboard
    k ctrl+shift+y; sleep 2.5                        # Sessions
    k ctrl+comma; sleep 2.5                          # Options
    shot "buttons-$1-$2-all" 480x44+960+0
    stop
}

toggle() {   # toggle <theme> <pane-colours>: the button opens its pane, and a second click closes it
    start "$1" "$2"
    click 1240 27; sleep 2.5                         # the bolt: Actions opens
    shot "toggle-$1-$2-1-open"
    click 1240 27; sleep 2                           # the same button again: it closes
    shot "toggle-$1-$2-2-closed"
    click 1296 27; sleep 2.5                         # the Switchboard button
    shot "toggle-$1-$2-3-board" 480x44+960+0
    stop
}

info() {   # info <theme>: Alt+I opens the conversation-info pane (owner, 2026-09-18)
    start "$1" type
    k alt+i; sleep 3
    shot "info-$1-alt-i"
    stop
}

for scene in $scenes; do
    case $scene in
    layout)
        layout relay-dark type; layout relay-dark group; layout relay-dark off
        layout relay-light type; layout relay-light group ;;
    tabs) tabs relay-dark; tabs relay-light ;;
    buttons) buttons relay-dark type; buttons relay-light type; buttons relay-dark off ;;
    toggle) toggle relay-dark type; toggle relay-dark off ;;
    info) info relay-dark ;;
    esac
done
printf 'done: %s\n' "$out"
