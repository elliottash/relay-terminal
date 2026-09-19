#!/usr/bin/env bash
# The open task list under the prompt (card #TKS9): implementer screenshots under Xvfb with an
# isolated HOME, XDG_CONFIG_HOME, XDG_DATA_HOME, XDG_CACHE_HOME, XDG_RUNTIME_DIR and TMPDIR, and
# RELAY_KEYRING=off. No provider account and no contact with a running Relay: the sandbox profile
# points a local model endpoint at stub-provider.py on 127.0.0.1 (the harness of the 2026-09-19
# subagent-badge run, with the task scripts this card needs).
#
#   docs/qa_evidence/2026-09-19-open-task-list-under-the-prompt/drive.sh [build-dir] [scene...]
#
# Scenes (all by default), each shot as implementer-<scene>.png plus the 300 % crop
# implementer-<scene>-strip.png of the bottom of the window, where the strip lives:
#   none    no tasks and no subagents: the strip is absent, not an empty card.
#   tasks   3 open tasks, no subagents: the strip full width under the prompt, the `main` row
#           above the task rows, no overflow line.
#   window  7 tasks with T5 in progress: 5 rows, T3..T7, T5 third of the five, and the overflow
#           line counting the 2 that did not fit. T1 and T2 are off the top — the window slid.
#   both    the same 7 tasks and 2 background subagents, on T5 and T6: the rows split at half
#           width, agents left and tasks right, the two pairs aligned on their own rows.
#   keys    the same 7 tasks and one subagent on T5. Three shots:
#             -handing ↓ → ↓↓↓↓ S on T7 (plain and delegable), caught at once: the pane's own
#                      status line, "Handing T7 to a subagent…"
#             -run    the same press 3 s later: the worker has answered, a2 is live on T7 and the
#                     row has become ◐ with the violet connector
#             -enter  ↓ → ↓↓ Enter on T5 (it has a subagent): that subagent's tab opens
#             -prompt Esc: the focus is back in the prompt and the strip is unfocused
#
# Needs Xvfb, xdotool, ImageMagick.
set -uo pipefail
invoked_from=$PWD
cd "$(dirname "${BASH_SOURCE[0]}")"
out=$PWD
root=$(cd ../../.. && pwd)
build=${1:-$root/build}; shift || true
# start() runs relay from the sandbox workspace, so a relative build dir would not resolve there;
# and this script has already cd'd to its own folder, so resolve it against the caller's cwd.
build=$(cd "$invoked_from" && cd "$build" && pwd) || { echo "no such build dir: $build"; exit 1; }
scenes=${*:-none tasks window both keys}
width=1440 height=900
port=${RELAY_QA_PORT:-8805}

display=
for n in $(seq 160 199); do [[ -e /tmp/.X11-unix/X$n ]] || { display=:$n; break; }; done
[[ -z $display ]] && { echo "no free display"; exit 1; }

sandbox=$(mktemp -d /tmp/relay-open-tasks.XXXXXX)
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

# 40 ms a key: at 18 ms the app dropped characters while a pane was starting up.
t() { xdotool type --delay 40 "$1"; }
k() { xdotool key --delay 60 "$@"; }

prepare() {
    rm -rf "$sandbox/home" "$sandbox/run" "$sandbox/tmp"
    mkdir -p "$sandbox/home" "$sandbox/run" "$sandbox/tmp"; chmod 700 "$sandbox/run"
    # The pane's agent worker runs in a systemd user scope (src/Isolation.h) unless isolation is
    # off; this harness turns isolation off below, but the link is kept so the run behaves the
    # same if someone flips it back on.
    ln -sfn "/run/user/$(id -u)/bus" "$sandbox/run/bus"
    export HOME=$sandbox/home XDG_RUNTIME_DIR=$sandbox/run TMPDIR=$sandbox/tmp
    export XDG_CONFIG_HOME=$HOME/.config XDG_DATA_HOME=$HOME/.local/share XDG_CACHE_HOME=$HOME/.cache
    work=$HOME/project
    mkdir -p "$XDG_CONFIG_HOME/RelayTerminal" "$XDG_CONFIG_HOME/relay" "$XDG_DATA_HOME" "$XDG_CACHE_HOME" "$work"
    printf "PS1='\\\\w \\$ '\nunset PROMPT_COMMAND\n" >"$HOME/.bashrc"
    printf '# project\n' >"$work/README.md"
    cat >"$XDG_CONFIG_HOME/RelayTerminal/relay.conf" <<CONF
[instructions]
onboarded=true
[theme]
name=relay-dark
[appearance]
pane_colours=type
[isolation]
enabled=false
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
    # The app's own stderr, only when it said something; then the pane's worker logs, without the
    # lock files, which are runtime state and not evidence.
    [[ -s $sandbox/relay.log ]] && cp "$sandbox/relay.log" "$out/relay-$scene.log"
    mkdir -p "$out/logs-$scene"
    for log in "$sandbox/home/.local/share/relay/logs/"*.log; do
        [[ -f $log ]] && cp "$log" "$out/logs-$scene/"
    done
    [[ -n $relay_pid ]] && { kill "$relay_pid"; wait "$relay_pid"; } 2>/dev/null
    relay_pid=
}

shot() {   # shot <name>
    xdotool mousemove $((width + 20)) $((height + 20)); sleep 0.8
    import -window "$win" "$out/implementer-$1.png"
    # The strip sits between the terminal and the bottom of the window, under the prompt box:
    # the last 300 px of the frame hold the composer, the strip and the status row.
    convert "$out/implementer-$1.png" -crop ${width}x300+0+$((height - 300)) +repage -scale 200% \
        "$out/implementer-$1-strip.png"
}

none()   { scene=none;   start; shot none; stop; }

tasks()  { scene=tasks;  start; t 'tasks3 please'; k Return; sleep 14; shot tasks; stop; }

window() { scene=window; start; t 'tasks7 please'; k Return; sleep 14; shot window; stop; }

both()   { scene=both;   start; t 'both7 please'; k Return; sleep 22; shot both; stop; }

# One subagent, on T5, so the window still reads T3..T7 and the rows below T5 are plain, open and
# delegable. ↓ enters the strip, → crosses to the task column, then ↓ walks the five rows.
keys()   {
    scene=keys; start
    t 'keys7 please'; k Return; sleep 20
    k Down; sleep 0.5; k Right; sleep 0.5          # into the strip, then into the task column
    k Down Down Down Down; sleep 0.6               # row 1 (T3) -> row 5 (T7), plain and pending
    k s; sleep 0.35; shot keys-handing             # Pane's own status: "Handing T7 to a subagent…"
    sleep 2.5; shot keys-run                       # the worker's reply, and a2 live on T7
    k Escape; sleep 1
    k Down; sleep 0.5; k Right; sleep 0.5
    k Down Down; sleep 0.6                         # row 3: T5, the one with a subagent
    k Return; sleep 3; shot keys-enter             # its tab opens
    k Escape; sleep 1.5; shot keys-prompt          # back in the prompt, the strip unfocused
    stop
}

for scene in $scenes; do $scene; done
printf 'done: %s\n' "$out"
