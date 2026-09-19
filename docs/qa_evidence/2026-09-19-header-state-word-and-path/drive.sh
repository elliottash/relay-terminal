#!/usr/bin/env bash
# Card #0STR, the pane header loses the state word and the right-hand label becomes just the
# path: implementer screenshots under Xvfb with an isolated HOME, XDG_CONFIG_HOME,
# XDG_RUNTIME_DIR and TMPDIR under a short /tmp path (the 108-byte unix-socket limit),
# RELAY_KEYRING=off, per-pane isolation off, and no provider account: the profile points a
# local model endpoint at stub-provider.py on 127.0.0.1. Harness copied from #RR0G / #HQ2B.
#
#   docs/qa_evidence/2026-09-19-header-state-word-and-path/drive.sh [build-dir] [scene...]
#
# RELAY_QA_PREFIX (default "implementer") names the files, so the same script shoots the
# BEFORE set against a build of the old code and the AFTER set against the new one.
#
# Each scene writes <prefix>-<scene>.png (the whole window) and <prefix>-<scene>-hdr2x.png
# (the pane header band, top 100 px, at 200 %). What to look at is the title row: the state
# word beside the glyph, and the right-hand label.
#   same      the pane renamed to "project", the folder it is in — the path must not be repeated
#   diff      the pane renamed to "Header cleanup" — the path is shown, as the bare path
#   busy      "diff" with a `*slow` turn running — the live state word used to sit by the glyph
#   workspace a pane whose terminal has cd'd below its agent workspace — the two paths
#   tooltip   "workspace" with the pointer resting on the title — the header tooltip, which
#             is where the words and the agent workspace live now (root capture: a tooltip is
#             its own X window and never lands in an `import -window $win` frame)
#   narrow    "diff" in a 560 px window — the ladder without its word rung
#
# Needs Xvfb, xdotool, ImageMagick, tesseract.
set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"
out=$PWD
root=$(cd ../../.. && pwd)
build=${1:-$root/build}; shift || true
scenes=${*:-same diff busy workspace tooltip narrow}
width=1440 height=900
port=${RELAY_QA_PORT:-8825}
prefix=${RELAY_QA_PREFIX:-implementer}

display=
for n in $(seq 160 199); do [[ -e /tmp/.X11-unix/X$n ]] || { display=:$n; break; }; done
[[ -z $display ]] && { echo "no free display"; exit 1; }

sandbox=$(mktemp -d /tmp/relay-0str.XXXXXX)
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
        probe=$(mktemp /tmp/0str-probe.XXXXXX.png)
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

# One frame of a scene: the whole window, and the pane header band (top 100 px) at 200 %.
shoot() {   # shoot <scene> [park]
    [[ ${2:-} == park ]] || { xdotool mousemove 1580 980; sleep 0.6; }
    import -window "$win" "$out/$prefix-$1.png"
    convert "$out/$prefix-$1.png" -crop ${width}x100+0+36 +repage -scale 200% "$out/$prefix-$1-hdr2x.png"
}

# The whole screen, for a frame that has to hold a window Relay does not own (a tooltip).
shoot_root() {   # shoot_root <scene>
    import -window root "$out/$prefix-$1.png"
    convert "$out/$prefix-$1.png" -crop ${width}x300+0+36 +repage -scale 150% "$out/$prefix-$1-hdr2x.png"
}

rename() {   # rename <title>
    k ctrl+a Delete
    t "/rename $1"; k Return; sleep 2
}

same() {
    start relay-dark same
    rename project
    shoot same
    stop same
}

diff_() {
    start relay-dark diff
    rename "Header cleanup"
    shoot diff
    stop diff
}

busy() {
    start relay-dark busy
    rename "Header cleanup"
    t '*slow task'; k Return; sleep 6
    shoot busy
    stop busy
}

workspace() {
    start relay-dark workspace
    rename "Header cleanup"
    k ctrl+a Delete
    t 'mkdir -p sub && cd sub'; k Return; sleep 3
    shoot workspace
    stop workspace
}

tooltip() {
    start relay-dark tooltip
    rename "Header cleanup"
    k ctrl+a Delete
    t 'mkdir -p sub && cd sub'; k Return; sleep 3
    xdotool mousemove 120 56; sleep 3        # rest on the title: the header tooltip comes up
    shoot_root tooltip
    stop tooltip
}

narrow() {
    start relay-dark narrow 560 760
    rename "Header cleanup"
    shoot narrow
    stop narrow
}

for scene in $scenes; do case $scene in diff) diff_ ;; *) $scene ;; esac; done
printf 'done: %s\n' "$out"
