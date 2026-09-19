#!/usr/bin/env bash
# Card #HQ2B, the "Relaying…" line goes left and loses the bold: implementer screenshots under
# Xvfb with an isolated HOME, XDG_CONFIG_HOME, XDG_RUNTIME_DIR and TMPDIR. No provider account:
# the profile points a local model endpoint at stub-provider.py on 127.0.0.1. Harness copied
# from the 2026-09-19 relaying-status-language run (#4E13) — its two lessons are built in here
# from the start: the readiness probe (submit `echo relayqaready`, OCR-poll for the echo, then
# `clear`), and per-pane isolation off in the sandbox relay.conf (the sandbox XDG_RUNTIME_DIR
# has no bus socket, so systemd-run --user cannot spawn the worker there; see that run's
# README). This run does not re-shoot the blink (the #4E13 six-frame method covers it, and
# nothing in #HQ2B touches the clock); it shoots one frame per state and measures edges.
#
#   docs/qa_evidence/2026-09-19-relaying-line-left-normal/drive.sh [build-dir] [scene...]
#
# Scenes (all by default), each into implementer-<scene>-x.png (full window) and
# implementer-<scene>-x-comp.png (the composer, bottom 240 px, at 100 % for measurement and
# 200 % in -comp2x.png for reading):
#   idle      a fresh pane — no busy line above the prompt (the control).
#   relaying  a `*slow` stub turn (60 s) — violet "Relaying thinking… · N s · … · Esc stops".
#   running   `sleep 600` — blue "Relaying sleep…", the terminal-program spelling.
#   spawn     a `*spawn` stub turn that ends leaving a subagent running — violet
#             "Relaying waiting for 1 subagent…".
#   question  a `*stuck` stub turn whose ask_user card is never answered — amber
#             "Relaying waiting for your answer… · N s · Esc stops" while the card is up (#MQ9C).
#   narrow    a `*longrun` stub turn (run_command "sleep 120 && echo padding-…") in a window
#             420 px wide — the line elides from the middle and stays put on the prompt's edge.
#
# Needs Xvfb, xdotool, ImageMagick, tesseract.
set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"
out=$PWD
root=$(cd ../../.. && pwd)
build=${1:-$root/build}; shift || true
scenes=${*:-idle relaying running spawn question narrow}
width=1440 height=900
port=${RELAY_QA_PORT:-8805}

display=
for n in $(seq 160 199); do [[ -e /tmp/.X11-unix/X$n ]] || { display=:$n; break; }; done
[[ -z $display ]] && { echo "no free display"; exit 1; }

sandbox=$(mktemp -d /tmp/relay-hq2b.XXXXXX)
stub_pid= xvfb_pid= relay_pid= scene=current
cleanup() {
    local pid
    for pid in $relay_pid $stub_pid $xvfb_pid; do kill "$pid" 2>/dev/null; done   # never `kill 0`
    rm -rf "$sandbox"
}
trap cleanup EXIT

python3 "$out/stub-provider.py" "$port" >/dev/null 2>&1 &
stub_pid=$!
Xvfb "$display" -screen 0 1600x1000x24 >/dev/null 2>&1 &
xvfb_pid=$!
sleep 2
kill -0 "$xvfb_pid" 2>/dev/null || { echo "Xvfb died on $display"; exit 1; }
export DISPLAY=$display RELAY_KEYRING=off

t() { xdotool type --delay 18 "$1"; }
k() { xdotool key --delay 60 "$@"; }

prepare() {   # prepare <theme-id>
    rm -rf "$sandbox/home" "$sandbox/run" "$sandbox/tmp"
    mkdir -p "$sandbox/home" "$sandbox/run" "$sandbox/tmp"; chmod 700 "$sandbox/run"
    export HOME=$sandbox/home XDG_RUNTIME_DIR=$sandbox/run TMPDIR=$sandbox/tmp
    export XDG_CONFIG_HOME=$HOME/.config XDG_DATA_HOME=$HOME/.local/share XDG_CACHE_HOME=$HOME/.cache
    work=$HOME/project
    mkdir -p "$XDG_CONFIG_HOME/RelayTerminal" "$XDG_CONFIG_HOME/relay" "$XDG_DATA_HOME" "$XDG_CACHE_HOME" "$work"
    printf "PS1='\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\w \\\\\\\\\\\\$ '\nunset PROMPT_COMMAND\n" >"$HOME/.bashrc"
    printf '# project\n' >"$work/README.md"
cat >"$XDG_CONFIG_HOME/RelayTerminal/relay.conf" <<CONF
[instructions]
onboarded=true
[theme]
name=$1
[appearance]
pane_colours=type
[provider]
preset=local:stub
[isolation]
enabled=false
CONF
    printf '{"version": 1, "endpoints": [{"id": "local:stub", "label": "Stub", "base_url": "http://127.0.0.1:%s/v1", "model": "stub", "server": "openai-compatible", "context_window": 131072}]}\n' \
        "$port" >"$XDG_CONFIG_HOME/relay/local-models.json"
}

# Copy this run's relay.log and worker.log out of the sandbox HOME before the next prepare()
# wipes it, so a scene that never reached "ready" leaves the reason on disk.
logs() {   # logs <scene>
    mkdir -p "$out/logs"
    for name in relay worker; do
        [[ -f $XDG_DATA_HOME/relay/logs/$name.log ]] && cp "$XDG_DATA_HOME/relay/logs/$name.log" "$out/logs/$1-$name.log"
    done
}

# Submit `echo relayqaready` and OCR the terminal area until the marker echoes back (the #4E13
# lesson: a submit before the router is ready leaves the text in the box). Ends with `clear`.
wait_ready() {   # wait_ready <scene>
    local marker=relayqaready tries=0 probe text w h
    w=$width h=$height
    while ((tries < 20)); do
        k ctrl+a Delete
        t "echo $marker"; k Return
        sleep 2
        probe=$(mktemp /tmp/hq2b-probe.XXXXXX.png)
        import -window "$win" "$probe"
        convert "$probe" -crop ${w}x704+0+96 +repage png:"$probe"
        text=$(tesseract "$probe" - --psm 6 2>/dev/null | tr -cd '[:alnum:]')
        rm -f "$probe"
        [[ $text == *$marker* ]] && {
            t clear; k Return; sleep 1
            return 0
        }
        ((tries++))
    done
    echo "$1: the router never accepted a submit (see logs/$1-{relay,worker}.log)"
    return 1
}

start() {   # start <theme-id> <scene> [width] [height]
    scene=$2
    width=${3:-1440} height=${4:-900}
    prepare "$1"
    relay_pid=
    for attempt in 1 2; do
        # One retry: the first full run of this harness saw a one-off "Permission denied" from
        # exec on the third launch of a binary that had already run twice — nothing a re-exec
        # did not cure, and a scene that dies there takes the whole run with it.
        (cd "$work" && exec "$build/relay" --workspace "$work") >"$sandbox/relay-stderr.log" 2>&1 &
        relay_pid=$!
        sleep 7
        win= ; local best=0 w
        for w in $(xdotool search --pid "$relay_pid" 2>/dev/null); do
            eval "$(xdotool getwindowgeometry --shell "$w" 2>/dev/null)"
            (( WIDTH * HEIGHT > best )) && { best=$(( WIDTH * HEIGHT )); win=$w; }
        done
        [[ -n $win ]] && break
        kill "$relay_pid" 2>/dev/null; wait "$relay_pid" 2>/dev/null
        ((attempt == 2)) && { echo "no Relay window"; cat "$sandbox/relay-stderr.log"; exit 1; }
    done
    xdotool windowmove "$win" 0 0 windowsize "$win" $width $height
    xdotool windowfocus "$win"; sleep 1.5
    wait_ready "$2" || { logs "$2"; stop "$2"; exit 1; }
}

stop() {   # stop <scene>
    [[ -n $relay_pid ]] && { kill "$relay_pid"; wait "$relay_pid"; } 2>/dev/null; relay_pid=
    logs "$1"
}

# One frame of a scene: the whole window, and the composer (bottom 240 px) at 100 % (what
# measure.py reads) and 200 % (what a person reads).
shoot() {   # shoot <scene>
    xdotool mousemove 1580 980; sleep 0.6
    import -window "$win" "$out/implementer-$1-x.png"
    convert "$out/implementer-$1-x.png" -crop ${width}x240+0+$((height - 240)) +repage "$out/implementer-$1-x-comp.png"
    convert "$out/implementer-$1-x-comp.png" -scale 200% "$out/implementer-$1-x-comp2x.png"
}

idle() {
    start relay-dark idle
    shoot idle
    stop idle
}

relaying() {
    start relay-dark relaying
    t '*slow task'; k Return; sleep 6
    shoot relaying
    stop relaying
}

running() {
    start relay-dark running
    t 'sleep 600'; k Return; sleep 3
    shoot running
    stop running
}

spawn() {
    start relay-dark spawn
    t '*spawn demo'; k Return; sleep 10
    shoot spawn
    stop spawn
}

question() {
    start relay-dark question
    t '*stuck please'; k Return; sleep 8
    shoot question
    stop question
}

narrow() {
    start relay-dark narrow 420 760
    t '*longrun demo'; k Return; sleep 6
    shoot narrow
    stop narrow
}

for scene in $scenes; do $scene; done
printf 'done: %s\n' "$out"
