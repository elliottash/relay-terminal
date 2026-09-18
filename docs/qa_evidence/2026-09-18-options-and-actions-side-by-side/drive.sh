#!/usr/bin/env bash
# Options and Actions open side by side (owner: "you cant have the options menu and actions menu
# both open simultaneously"), and IBM Beige's destination pair at its second, louder setting.
# Implementer screenshots under Xvfb with an isolated HOME, XDG_CONFIG_HOME, XDG_RUNTIME_DIR and
# TMPDIR. No provider account and no agent turn is needed: every scene is chrome.
#
#   docs/qa_evidence/2026-09-18-options-and-actions-side-by-side/drive.sh [build-dir] [scene...]
#
# Scenes (all by default):
#   both     implementer-both-<theme>.png and its -bar crop: Options opened, then Actions; both
#            panes on screen, both title-bar buttons lit in their own header colour.
#   close    implementer-close-<theme>-N-*.png: with both open, the Actions key closes only
#            Actions, then the Options key closes only Options.
#   beige    implementer-beige-*.png: the composer in shell mode and in agent mode, so the two
#            destination colours are on a beige ground next to ordinary text.
#
# Needs Xvfb, xdotool and ImageMagick.
set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"
out=$PWD
root=$(cd ../../.. && pwd)
build=${1:-$root/build}; shift || true
scenes=${*:-both close beige}
width=1440 height=900

display=
for n in $(seq 160 199); do [[ -e /tmp/.X11-unix/X$n ]] || { display=:$n; break; }; done
[[ -z $display ]] && { echo "no free display"; exit 1; }

sandbox=$(mktemp -d /tmp/relay-two-panes.XXXXXX)
xvfb_pid= relay_pid=
cleanup() {
    local pid
    for pid in $relay_pid $xvfb_pid; do kill "$pid" 2>/dev/null; done   # never `kill 0`
    rm -rf "$sandbox"
}
trap cleanup EXIT

Xvfb "$display" -screen 0 $((width + 40))x$((height + 40))x24 >/dev/null 2>&1 &
xvfb_pid=$!
sleep 2
kill -0 "$xvfb_pid" 2>/dev/null || { echo "Xvfb died on $display"; exit 1; }
export DISPLAY=$display RELAY_KEYRING=off

t() { xdotool type --delay 18 "$1"; }
k() { xdotool key --delay 60 "$@"; }
click() { xdotool mousemove "$1" "$2" click 1; sleep 0.4; }

prepare() {   # prepare <theme-id>
    rm -rf "$sandbox/home" "$sandbox/run" "$sandbox/tmp"
    mkdir -p "$sandbox/home" "$sandbox/run" "$sandbox/tmp"; chmod 700 "$sandbox/run"
    export HOME=$sandbox/home XDG_RUNTIME_DIR=$sandbox/run TMPDIR=$sandbox/tmp
    export XDG_CONFIG_HOME=$HOME/.config XDG_DATA_HOME=$HOME/.local/share XDG_CACHE_HOME=$HOME/.cache
    work=$HOME/project
    mkdir -p "$XDG_CONFIG_HOME/RelayTerminal" "$XDG_DATA_HOME" "$XDG_CACHE_HOME" "$work"
    printf "PS1='\\\\w \$ '\nunset PROMPT_COMMAND\n" >"$HOME/.bashrc"
    printf '# project\n' >"$work/README.md"
    cat >"$XDG_CONFIG_HOME/RelayTerminal/relay.conf" <<CONF
[instructions]
onboarded=true
[theme]
name=$1
[appearance]
pane_colours=type
CONF
}

start() {   # start <theme-id>
    prepare "$1"
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

both() {   # both <theme>: Options, then Actions; neither replaces the other
    start "$1"
    k ctrl+comma; sleep 3                   # Options
    shot "both-$1-1-options" 480x44+960+0
    k ctrl+shift+a; sleep 3                 # Actions, with Options still there
    shot "both-$1-2-both" 480x44+960+0
    stop
}

close() {   # close <theme>: each key closes only its own pane
    start "$1"
    k ctrl+comma; sleep 3
    k ctrl+shift+a; sleep 3
    k ctrl+shift+a; sleep 2                 # Actions has the focus: this closes Actions only
    shot "close-$1-1-actions-gone" 480x44+960+0
    k ctrl+comma; sleep 2                   # Options has the focus now: this closes Options
    shot "close-$1-2-both-gone" 480x44+960+0
    stop
}

beige() {   # the destination pair on the beige ground, shell mode then agent mode
    start ibm-beige
    t 'grep -rn "shell = " data/theme/themes/*.toml'; sleep 1.5
    shot "beige-1-shell" 900x150+0+750
    k ctrl+a; k BackSpace; sleep 0.5
    t '*summarise what changed in the light themes today'; sleep 1.5   # * is the agent prefix
    shot "beige-2-agent" 900x150+0+750
    k ctrl+comma; sleep 3
    k ctrl+shift+a; sleep 3
    shot "beige-3-both-panes"
    stop
}

for scene in $scenes; do
    case $scene in
    both) both ibm-beige; both relay-dark ;;
    close) close ibm-beige ;;
    beige) beige ;;
    esac
done
printf 'done: %s\n' "$out"
