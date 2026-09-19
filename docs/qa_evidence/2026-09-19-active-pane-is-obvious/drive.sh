#!/usr/bin/env bash
# "Which pane is active" — implementer screenshots under Xvfb with an isolated HOME,
# XDG_CONFIG_HOME, XDG_DATA_HOME, XDG_RUNTIME_DIR and TMPDIR.
#
#   drive.sh [build-dir] [tag] [theme]
#
# Splits into three panes, then clicks into each one's *terminal output* (a Qt::NoFocus
# surface: the click that used to leave the old pane active) and shoots the window each time.
set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"
out=$PWD
root=$(cd ../../.. && pwd)
build=${1:-build}; [[ $build == /* ]] || build=$root/$build
tag=${2:-now}
theme=${3:-relay-dark}
width=1440 height=900

display=
for n in $(seq 160 199); do [[ -e /tmp/.X11-unix/X$n ]] || { display=:$n; break; }; done
[[ -z $display ]] && { echo "no free display"; exit 1; }

sandbox=$(mktemp -d /tmp/relay-active-pane.XXXXXX)
xvfb_pid= relay_pid=
cleanup() { local p; for p in $relay_pid $xvfb_pid; do kill "$p" 2>/dev/null; done; rm -rf "$sandbox"; }
trap cleanup EXIT

Xvfb "$display" -screen 0 $((width + 40))x$((height + 40))x24 >/dev/null 2>&1 &
xvfb_pid=$!
sleep 2
kill -0 "$xvfb_pid" 2>/dev/null || { echo "Xvfb died on $display"; exit 1; }
export DISPLAY=$display RELAY_KEYRING=off

mkdir -p "$sandbox/home" "$sandbox/run" "$sandbox/tmp"; chmod 700 "$sandbox/run"
export HOME=$sandbox/home XDG_RUNTIME_DIR=$sandbox/run TMPDIR=$sandbox/tmp
export XDG_CONFIG_HOME=$HOME/.config XDG_DATA_HOME=$HOME/.local/share XDG_CACHE_HOME=$HOME/.cache
work=$HOME/project
mkdir -p "$XDG_CONFIG_HOME/RelayTerminal" "$XDG_CONFIG_HOME/relay" "$XDG_DATA_HOME" "$XDG_CACHE_HOME" \
         "$work/issues/features"
# A board in the project, so the Switchboard opens as a tool pane (scenes 07-09).
cp "$root/issues/board.yaml" "$work/issues/"
cp "$root"/issues/features/2026-09-17-*.md "$work/issues/features/" 2>/dev/null
printf "PS1='\\\\w \$ '\nunset PROMPT_COMMAND\n" >"$HOME/.bashrc"
cat >"$XDG_CONFIG_HOME/RelayTerminal/relay.conf" <<CONF
[instructions]
onboarded=true
[theme]
name=$theme
CONF

k() { xdotool key --delay 60 "$@"; }
t() { xdotool type --delay 18 "$1"; }
click() { xdotool mousemove "$1" "$2" click 1; sleep 0.8; }
shot() { xdotool mousemove $((width + 20)) $((height + 20)); sleep 0.8; import -window "$win" "$out/implementer-$tag-$1.png"; }

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

k ctrl+e; sleep 4            # new pane on the right (Ctrl+E)
k ctrl+e; sleep 0.4; k Down; sleep 4   # another, docked beneath it (#78BN placement)
shot 01-three-panes

# Click into each pane's terminal output area (not its prompt box).
click 300 300;  shot 02-clicked-left
click 1050 200; shot 03-clicked-top-right
click 1050 650; shot 04-clicked-bottom-right
click 300 300;  shot 05-back-to-left

# A press on a control that opens a popup: the pane becomes active and the popup stays open.
click 1289 436; shot 06-model-box-of-an-inactive-pane
k Escape; sleep 1

# A tool pane has no prompt box and no title label, so its frame is the whole of the cue.
click 300 300; sleep 1
k ctrl+shift+s; sleep 5         # the Switchboard, in a pane of its own
shot 07-switchboard-pane-active
click 300 300;  shot 08-clicked-a-terminal-with-a-tool-pane-open
click 715 600;  shot 09-clicked-back-into-the-tool-pane
click 1150 300; shot 10-clicked-a-third-pane
printf 'done: %s (tag %s, theme %s)\n' "$out" "$tag" "$theme"
