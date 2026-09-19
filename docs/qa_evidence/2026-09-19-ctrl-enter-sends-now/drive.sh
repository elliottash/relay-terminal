#!/usr/bin/env bash
# Ctrl+Enter sends now, not to the back of the queue (#N8VK): implementer evidence under Xvfb
# with an isolated HOME, XDG_CONFIG_HOME, XDG_RUNTIME_DIR and TMPDIR, in the harness of the
# thinking-fold run (issue T8CN). No provider account: the profile points a local model endpoint
# at stub-provider.py on 127.0.0.1, which logs the epoch time of every ask next to the driver's
# own timeline, so the gap between the keypress and the ask is measured, not eyeballed.
#
#   docs/qa_evidence/2026-09-19-ctrl-enter-sends-now/drive.sh [build-dir] [scene...]
#
# Scenes (all by default), each shot as implementer-<scene>-<step>.png:
#   queue   the card's reproduction: `sleep 8` running, `echo queued-behind` queued behind it,
#           Ctrl+Enter on an agent prompt -> the turn starts at once (ask logged while the sleep
#           still runs), the queued echo keeps its place and runs when the sleep ends.
#   star    the same with the `*` prefix and plain Enter: same submission, same rule.
#   busy    unchanged behaviour: Ctrl+Enter while a turn streams interrupts it and sends now.
#
# Needs Xvfb, xdotool, ImageMagick, tesseract (OCR notes only).
set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"
out=$PWD
root=$(cd ../../.. && pwd)
build=${1:-$root/build}; shift || true
scenes=${*:-queue star busy}
width=1440 height=900
port=${RELAY_QA_PORT:-8823}

display=
for n in $(seq 160 199); do [[ -e /tmp/.X11-unix/X$n ]] || { display=:$n; break; }; done
[[ -z $display ]] && { echo "no free display"; exit 1; }

sandbox=$(mktemp -d /tmp/relay-ctrl-enter.XXXXXX)
stub_pid= xvfb_pid= relay_pid=
cleanup() {
    local pid
    for pid in $relay_pid $stub_pid $xvfb_pid; do kill "$pid" 2>/dev/null; done   # never `kill 0`
    rm -rf "$sandbox"
}
trap cleanup EXIT

: >"$out/timeline.txt"
export RELAY_STUB_LOG=$out/requests.log
: >"$RELAY_STUB_LOG"
python3 "$out/stub-provider.py" "$port" >/dev/null 2>&1 &
stub_pid=$!
Xvfb "$display" -screen 0 $((width + 40))x$((height + 40))x24 >/dev/null 2>&1 &
xvfb_pid=$!
sleep 2
kill -0 "$xvfb_pid" 2>/dev/null || { echo "Xvfb died on $display"; exit 1; }
export DISPLAY=$display RELAY_KEYRING=off

# 40 ms a key: at 18 ms the app dropped characters while a pane was starting up (#YMSR's lesson).
t() { xdotool type --delay 40 "$1"; }
k() { xdotool key --delay 60 "$@"; }
mark() { printf '%s %s\n' "$(date +%s.%N)" "$1" >>"$out/timeline.txt"; }

prepare() {
    rm -rf "$sandbox/home" "$sandbox/run" "$sandbox/tmp"
    mkdir -p "$sandbox/home" "$sandbox/run" "$sandbox/tmp"; chmod 700 "$sandbox/run"
    # The pane's agent worker runs in a systemd user scope (src/Isolation.h) and the app removes
    # DBUS_SESSION_BUS_ADDRESS from that worker's environment, so systemd-run finds the user
    # manager through $XDG_RUNTIME_DIR/bus. A sandbox runtime dir has no bus of its own: without
    # this link the scope fails and the pane shows "The agent worker exited."
    ln -sfn "/run/user/$(id -u)/bus" "$sandbox/run/bus"
    export HOME=$sandbox/home XDG_RUNTIME_DIR=$sandbox/run TMPDIR=$sandbox/tmp
    export XDG_CONFIG_HOME=$HOME/.config XDG_DATA_HOME=$HOME/.local/share XDG_CACHE_HOME=$HOME/.cache
    work=$HOME/project
    mkdir -p "$XDG_CONFIG_HOME/RelayTerminal" "$XDG_CONFIG_HOME/relay" "$XDG_DATA_HOME" "$XDG_CACHE_HOME" "$work"
    printf "PS1='\\\\w \\\$ '\nunset PROMPT_COMMAND\n" >"$HOME/.bashrc"
    printf '# project\n' >"$work/README.md"
    cat >"$XDG_CONFIG_HOME/RelayTerminal/relay.conf" <<'CONF'
[instructions]
onboarded=true
[provider]
preset=local:stub
CONF
    printf '{"version": 1, "endpoints": [{"id": "local:stub", "label": "Stub", "base_url": "http://127.0.0.1:%s/v1", "model": "stub", "server": "openai-compatible", "context_window": 131072}]}\n' \
        "$port" >"$XDG_CONFIG_HOME/relay/local-models.json"
}

start() {
    prepare
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

stop() {
    cp "$sandbox/relay.log" "$out/relay-$scene.log" 2>/dev/null
    [[ -n $relay_pid ]] && { kill "$relay_pid"; wait "$relay_pid"; } 2>/dev/null
    relay_pid=
}

shot() {   # shot <name> — the pane body (terminal + queue strip) and the whole window
    scene=${scene:-$1}
    xdotool mousemove $((width + 20)) $((height + 20)); sleep 0.8
    import -window "$win" "$out/implementer-$1.png"
    convert "$out/implementer-$1.png" -crop ${width}x680+0+90 +repage -scale 150% "$out/implementer-$1-body.png"
    { echo "--- $1"; tesseract "$out/implementer-$1-body.png" - --psm 6 2>/dev/null; } >>"$out/implementer-notes.txt"
}

# The card's reproduction: sleep running, echo queued behind it, Ctrl+Enter on an agent prompt.
queue() {
    scene=queue; start
    t 'sleep 8'; k Return;    mark 'queue: sleep entered'
    sleep 1.2                 # the sleep is running; the terminal is not free
    t 'echo queued-behind'; k Return;   mark 'queue: echo queued behind the sleep'
    sleep 0.6
    t 'the agent was asked while the shell slept'; k ctrl+Return;   mark 'queue: Ctrl+Enter on the agent prompt'
    sleep 2.2; shot queue-mid      # the turn runs; the queue strip still holds $ echo queued-behind
    sleep 6.5;  shot queue-after   # the sleep ended; the queued echo ran, in its place
    stop
}

# The `*` prefix and plain Enter are the same submission: same rule.
star() {
    scene=star; start
    t 'sleep 6'; k Return;    mark 'star: sleep entered'
    sleep 1.2
    t 'echo star-tail'; k Return;   mark 'star: echo queued behind the sleep'
    sleep 0.6
    t '*the star prefix asked too'; k Return;   mark 'star: Enter on a *-prefixed prompt'
    sleep 2.2; shot star-mid
    sleep 5.5;  shot star-after
    stop
}

# While a turn streams, Ctrl+Enter still interrupts it and sends now (unchanged).
busy() {
    scene=busy; start
    t 'a slow turn that streams for a while'; k Return;   mark 'busy: slow turn submitted'
    sleep 2.5                 # mid-stream
    t 'interrupting prompt'; k ctrl+Return;   mark 'busy: Ctrl+Enter during the turn'
    sleep 2.2; shot busy-mid  # "Interrupting the current turn…" and the new prompt's answer
    stop
}

rm -f "$out/implementer-notes.txt"
for scene in $scenes; do $scene; done
cp -r "$sandbox/home/.local/share/relay/logs/." "$out/logs/" 2>/dev/null || {
    mkdir -p "$out/logs" && cp -r "$sandbox/home/.local/share/relay/logs/." "$out/logs/" 2>/dev/null; }
printf 'done: %s\n' "$out"
