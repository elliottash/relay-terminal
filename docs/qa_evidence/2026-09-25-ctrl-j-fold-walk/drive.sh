#!/usr/bin/env bash
# Ctrl+J, the keyboard walk over tool calls and reasoning (card #XPEB): implementer screenshots
# under Xvfb with an isolated HOME, XDG_CONFIG_HOME, XDG_RUNTIME_DIR and TMPDIR. No provider
# account: a local endpoint points at stub-provider.py (copied from #QT8C), whose one turn is a
# reasoning block, a run_command and two read_file calls merged into one row.
#
#   docs/qa_evidence/2026-09-25-ctrl-j-fold-walk/drive.sh [build-dir]
#
# Shots: 01-turn-done (the turn at rest), 02-ctrl-j (the walk on the newest line, the ✦ turn
# line), 03-up (Up: the merged read row), 04-enter-unfolds (Enter unfolds it in place, walk
# still on it), 05-up-right (Up to the run row, Right unfolds it), 06-up-thinking (Up to the
# reasoning row), 07-shift-enter (Shift+Enter folds every fold of the turn), 08-esc (Esc
# leaves: no highlight, no toast), 09-ctrl-j-esc (a fresh walk left at once). Needs Xvfb, xdotool, ImageMagick, tesseract.
set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"
out=$PWD
root=$(cd ../../.. && pwd)
build=${1:-$root/build}
width=1440 height=900
port=${RELAY_QA_PORT:-8847}

display=
for n in $(seq 160 199); do [[ -e /tmp/.X11-unix/X$n ]] || { display=:$n; break; }; done
[[ -z $display ]] && { echo "no free display"; exit 1; }

sandbox=$(mktemp -d /tmp/relay-xpeb.XXXXXX)
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

t() { xdotool type --delay 40 "$1"; }
k() { xdotool key --delay 60 "$@"; }

mkdir -p "$sandbox/home" "$sandbox/run" "$sandbox/tmp"; chmod 700 "$sandbox/run"
ln -sfn "/run/user/$(id -u)/bus" "$sandbox/run/bus"   # the worker's systemd scope (T8CN's lesson)
export HOME=$sandbox/home XDG_RUNTIME_DIR=$sandbox/run TMPDIR=$sandbox/tmp
export XDG_CONFIG_HOME=$HOME/.config XDG_DATA_HOME=$HOME/.local/share XDG_CACHE_HOME=$HOME/.cache
work=$HOME/project
mkdir -p "$XDG_CONFIG_HOME/RelayTerminal" "$XDG_CONFIG_HOME/relay" "$XDG_DATA_HOME" "$XDG_CACHE_HOME" "$work"
printf "PS1='\\\\w \\\$ '\nunset PROMPT_COMMAND\n" >"$HOME/.bashrc"
printf '# project\n\nA readme with a few lines.\n' >"$work/README.md"
printf '# notes\n\n- one\n- two\n' >"$work/notes.md"
cat >"$XDG_CONFIG_HOME/RelayTerminal/relay.conf" <<CONF
[instructions]
onboarded=true
[theme]
name=relay-dark
[appearance]
pane_colours=type
[provider]
preset=local:stub
[models]
tier\\main="local:stub|stub||rank=1"
[approvals]
mode=allow
[terminal]
persistLocal=false
CONF
printf '{"version": 1, "endpoints": [{"id": "local:stub", "label": "Stub", "base_url": "http://127.0.0.1:%s/v1", "model": "stub", "server": "openai-compatible", "context_window": 131072}]}\n' \
    "$port" >"$XDG_CONFIG_HOME/relay/local-models.json"

(cd "$work" && exec "$build/relay" --fresh --workspace "$work") >"$sandbox/relay.log" 2>&1 &
relay_pid=$!
sleep 7
win= ; best=0
for candidate in $(xdotool search --pid "$relay_pid" 2>/dev/null); do
    eval "$(xdotool getwindowgeometry --shell "$candidate" 2>/dev/null)"
    (( WIDTH * HEIGHT > best )) && { best=$(( WIDTH * HEIGHT )); win=$candidate; }
done
[[ -z $win ]] && { echo "no Relay window"; cat "$sandbox/relay.log"; exit 1; }
xdotool windowmove "$win" 0 0 windowsize "$win" "$width" $height
xdotool windowfocus "$win"; sleep 1.5

: >"$out/implementer-notes.txt"
shot() {
    xdotool mousemove $((width + 20)) $((height + 20)); sleep 0.6
    import -window "$win" "$out/$1.png"
    convert "$out/$1.png" -scale 150% "$sandbox/ocr.png"
    { echo "--- $1"; tesseract "$sandbox/ocr.png" - --psm 6 2>/dev/null; } >>"$out/implementer-notes.txt"
}

t 'please check the stable'; k Return
sleep 20;              shot 01-turn-done
# The prompt box has the focus after an ask; Ctrl+J acts from there only.
k ctrl+j;   sleep 0.8; shot 02-ctrl-j
k Up;       sleep 0.8; shot 03-up
k Return;   sleep 1.5; shot 04-enter-unfolds
k Up;       sleep 0.5; k Right; sleep 1.5; shot 05-up-right
k Up;       sleep 0.8; shot 06-up-thinking
k shift+Return; sleep 1.5; shot 07-shift-enter
k Escape;   sleep 0.8; shot 08-esc
k ctrl+j;   sleep 0.8; k Escape; sleep 0.8; shot 09-ctrl-j-esc
