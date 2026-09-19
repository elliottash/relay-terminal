#!/usr/bin/env bash
# The planner asks the user a question (#MQ9C): implementer screenshots under Xvfb with an
# isolated HOME, XDG_CONFIG_HOME, XDG_RUNTIME_DIR and TMPDIR. No provider account: the profile
# points a local model endpoint at stub-provider.py on 127.0.0.1, whose planner answers the first
# call with `ask_user` and only writes its plan once the pane has answered.
#
#   docs/qa_evidence/2026-09-19-the-planner-asks-questions/drive.sh [build-dir]
#
# Shots:
#   implementer-card.png        the amber card for question 1, in plan mode, the turn blocked on it
#   implementer-card-tab.png    the tab bar, cropped: the background tab says "needs you"
#   implementer-second.png      question 2 after "1" answered the first: the multiple-choice one
#   implementer-answered.png    both answered, the plan written from them
#
# Isolation is off in the sandbox: the worker is normally launched under `systemd-run --user`, and
# XDG_RUNTIME_DIR points at the sandbox here, so there is no user bus to place the scope on and the
# worker would exit before it configured itself.
#
# Needs Xvfb, xdotool and ImageMagick.
set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"
out=$PWD
root=$(cd ../../.. && pwd)
build=${1:-$root/build}
width=1280 height=860
port=${RELAY_QA_PORT:-8799}

display=
for n in $(seq 160 199); do [[ -e /tmp/.X11-unix/X$n ]] || { display=:$n; break; }; done
[[ -z $display ]] && { echo "no free display"; exit 1; }

sandbox=$(mktemp -d /tmp/relay-ask-user.XXXXXX)
stub_pid= xvfb_pid= relay_pid=
cleanup() {
    local pid
    for pid in $relay_pid $stub_pid $xvfb_pid; do kill "$pid" 2>/dev/null; done   # never `kill 0`
    # The app's own stderr is the first thing to read when a shot comes out wrong; keep it.
    [[ -f $sandbox/relay.log ]] && cp "$sandbox/relay.log" "${RELAY_QA_LOG:-$sandbox/kept.log}"
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

t() { xdotool type --delay 18 "$1"; }
k() { xdotool key --delay 60 "$@"; }

rm -rf "$sandbox/home" "$sandbox/run" "$sandbox/tmp"
mkdir -p "$sandbox/home" "$sandbox/run" "$sandbox/tmp"; chmod 700 "$sandbox/run"
export HOME=$sandbox/home XDG_RUNTIME_DIR=$sandbox/run TMPDIR=$sandbox/tmp
export XDG_CONFIG_HOME=$HOME/.config XDG_DATA_HOME=$HOME/.local/share XDG_CACHE_HOME=$HOME/.cache
work=$HOME/project
mkdir -p "$XDG_CONFIG_HOME/RelayTerminal" "$XDG_CONFIG_HOME/relay" "$XDG_DATA_HOME" "$XDG_CACHE_HOME" "$work"
printf "PS1='\\\\w \$ '\nunset PROMPT_COMMAND\n" >"$HOME/.bashrc"
printf 'def parse(text):\n    return text\n' >"$work/parser.py"
cat >"$XDG_CONFIG_HOME/RelayTerminal/relay.conf" <<CONF
[instructions]
onboarded=true
[theme]
name=relay-dark
[provider]
preset=local:stub
[isolation]
enabled=false
CONF
printf '{"version": 1, "endpoints": [{"id": "local:stub", "label": "Stub", "base_url": "http://127.0.0.1:%s/v1", "model": "stub", "server": "openai-compatible", "context_window": 131072}]}\n' \
    "$port" >"$XDG_CONFIG_HOME/relay/local-models.json"

(cd "$work" && exec "$build/relay" --workspace "$work") >"$sandbox/relay.log" 2>&1 &
relay_pid=$!
sleep 7
win= ; best=0
for w in $(xdotool search --pid "$relay_pid" 2>/dev/null); do
    eval "$(xdotool getwindowgeometry --shell "$w" 2>/dev/null)"
    (( WIDTH * HEIGHT > best )) && { best=$(( WIDTH * HEIGHT )); win=$w; }
done
[[ -z $win ]] && { echo "no Relay window"; cat "$sandbox/relay.log"; exit 1; }
xdotool windowmove "$win" 0 0 windowsize "$win" $width $height windowfocus "$win"; sleep 1.5

shot() {   # shot <name> [crop]
    xdotool mousemove $((width + 20)) $((height + 20)); sleep 0.8
    import -window "$win" "$out/implementer-$1.png"
    [[ -n ${2:-} ]] && convert "$out/implementer-$1.png" -crop "$2" +repage -scale 200% "$out/implementer-$1.png"
    return 0
}

# A second tab, so the first one's "needs you" glyph can be seen from outside it.
k ctrl+shift+Tab 2>/dev/null
k shift+Tab; sleep 1                     # plan mode
t 'rename parse() in parser.py'; k Return
sleep 6                                  # the turn blocks on the card
shot card
k ctrl+t; sleep 3                        # a new tab: the first one is now in the background
shot card-tab 900x64+0+0
k ctrl+w; sleep 2                        # back to the pane that is asking
t '1'; k Return; sleep 3
shot second
t '1,2'; k Return; sleep 6
shot answered
echo "shots in $out"
