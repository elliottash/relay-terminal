#!/usr/bin/env bash
# Card #4E13, the "Relaying…" status language: implementer screenshots under Xvfb with an
# isolated HOME, XDG_CONFIG_HOME, XDG_RUNTIME_DIR and TMPDIR. No provider account: the profile
# points a local model endpoint at stub-provider.py on 127.0.0.1. Harness copied from the
# 2026-09-19 pane-live-state run (#V8KT); this one adds the composer crop (the busy line above
# the prompt box), the transcript-colour scenes — and a readiness probe.
#
# The readiness probe is the lesson of the first attempt: a fixed `sleep 7` after launch is not
# enough, and worse, with the #Y4RX per-pane isolation the worker never started at all under
# this sandbox. Isolation defaults on (src/Isolation.h); its probe `systemd-run --user --scope
# true` succeeds because it inherits the desktop session's DBUS_SESSION_BUS_ADDRESS, but the
# real spawn strips that variable (card #Y4RX) and the sandbox XDG_RUNTIME_DIR has no bus
# socket, so `systemd-run --user` exits 1 within ~50 ms and every submit is refused with
# "Local router is not ready" (src/Pane.h, requestRoute) — the text stays in the box, nothing
# runs, and every capture shows an idle pane (relay.log says worker_exit code=1; the worker's
# own stderr is discarded by design, see repro_worker.sh). The sandbox relay.conf therefore
# sets isolation/enabled=false — these scenes do not test isolation — and each scene still
# first submits `echo relayqaready` and OCR-polls the terminal area until the marker echoes
# back (retrying the submit, up to ~80 s), then clears the screen and only then runs the scene.
# relay.log and worker.log are copied per scene into logs/ so a refusal that never ends leaves
# evidence of why.
#
#   docs/qa_evidence/2026-09-19-relaying-status-language/drive.sh [build-dir] [scene...]
#
# Scenes (all by default); the live ones are shot as six frames ~0.7 s apart into
# implementer-<scene>-{a..f}-{bar,head,comp}.png (tab bar, pane header row, composer):
#   idle          a fresh pane — ring glyph, no word, no busy line above the prompt.
#   running       `sleep 600` — bold blue "Relaying sleep…" above the prompt, blinking blue
#                 relay mark in the header, blue dot on the tab.
#   relaying      a `*slow` stub turn (60 s) — bold violet "Relaying thinking… · N s · Esc
#                 stops" above the prompt, blinking violet relay mark, violet tab dot.
#   spawn         a `*spawn` stub turn that ends and leaves a subagent running (90 s) — bold
#                 violet "Relaying waiting for 1 subagent…" once the turn itself is over.
#   labels        a `*show the labels` stub reply — **Done:** green, **Need:** amber,
#                 **Problem:** red, a plain **bold** uncoloured, in the transcript.
#   labels-beige  the same reply under the IBM Beige theme — the colours follow the theme
#                 (indexed SGR resolved at paint time, nothing burnt into the scrollback).
#
# Why six frames and not two (the #V8KT lesson): the blink's steps repeat (phases 0 and 2 paint
# the same image, and so do 1 and 3), so two captures an even number of steps apart are
# pixel-identical while the mark animates fine. Six frames spanning several seconds cannot all
# land on identical phases; the pairwise pixel-difference ranges in implementer-notes.txt are
# the honest "it moves". The composer crop, in contrast, is state and should hold still —
# except the spawn scene, where the prompt placeholder's dots grow on the same 600 ms clock.
#
# Needs Xvfb, xdotool, ImageMagick, tesseract.
set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"
out=$PWD
root=$(cd ../../.. && pwd)
build=${1:-$root/build}; shift || true
scenes=${*:-idle running relaying spawn labels labels-beige}
width=1440 height=900
port=${RELAY_QA_PORT:-8804}

display=
for n in $(seq 160 199); do [[ -e /tmp/.X11-unix/X$n ]] || { display=:$n; break; }; done
[[ -z $display ]] && { echo "no free display"; exit 1; }

sandbox=$(mktemp -d /tmp/relay-4e13.XXXXXX)
stub_pid= xvfb_pid= relay_pid= scene=current
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

t() { xdotool type --delay 18 "$1"; }
k() { xdotool key --delay 60 "$@"; }

prepare() {   # prepare <theme-id>
    rm -rf "$sandbox/home" "$sandbox/run" "$sandbox/tmp"
    mkdir -p "$sandbox/home" "$sandbox/run" "$sandbox/tmp"; chmod 700 "$sandbox/run"
    export HOME=$sandbox/home XDG_RUNTIME_DIR=$sandbox/run TMPDIR=$sandbox/tmp
    export XDG_CONFIG_HOME=$HOME/.config XDG_DATA_HOME=$HOME/.local/share XDG_CACHE_HOME=$HOME/.cache
    work=$HOME/project
    mkdir -p "$XDG_CONFIG_HOME/RelayTerminal" "$XDG_CONFIG_HOME/relay" "$XDG_DATA_HOME" "$XDG_CACHE_HOME" "$work"
    printf "PS1='\\\\\\\\\\\\\\\\w \\\\\\$ '\nunset PROMPT_COMMAND\n" >"$HOME/.bashrc"
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

# Submit `echo relayqaready` and OCR the terminal area until the marker echoes back. A submit
# before the router is ready leaves the text in the box (requestRoute returns early), so each
# attempt clears the editor first. Ends with `clear` so the scene starts from a clean screen.
wait_ready() {   # wait_ready <scene>
    local marker=relayqaready tries=0 probe text
    while ((tries < 20)); do
        k ctrl+a Delete
        t "echo $marker"; k Return
        sleep 2
        probe=$(mktemp /tmp/4e13-probe.XXXXXX.png)
        import -window "$win" "$probe"
        convert "$probe" -crop ${width}x704+0+96 +repage png:"$probe"
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

start() {   # start <theme-id> <scene>
    scene=$2
    prepare "$1"
    (cd "$work" && exec "$build/relay" --workspace "$work") >"$sandbox/relay-stderr.log" 2>&1 &
    relay_pid=$!
    sleep 7
    win= ; local best=0 w
    for w in $(xdotool search --pid "$relay_pid" 2>/dev/null); do
        eval "$(xdotool getwindowgeometry --shell "$w" 2>/dev/null)"
        (( WIDTH * HEIGHT > best )) && { best=$(( WIDTH * HEIGHT )); win=$w; }
    done
    [[ -z $win ]] && { echo "no Relay window"; cat "$sandbox/relay-stderr.log"; exit 1; }
    xdotool windowmove "$win" 0 0 windowsize "$win" $width $height
    xdotool windowfocus "$win"; sleep 1.5
    wait_ready "$2" || { logs "$2"; stop "$2"; exit 1; }
}

stop() {   # stop <scene>
    [[ -n $relay_pid ]] && { kill "$relay_pid"; wait "$relay_pid"; } 2>/dev/null; relay_pid=
    logs "$1"
}

# The three crops of one frame: tab bar (top), the pane header row, and the composer (bottom
# 240 px: queue strip, the busy line above the prompt box, the prompt, the status strip).
shoot() {   # shoot <scene> <frame-letter>
    import -window "$win" "$out/implementer-$1-$2.png"
    convert "$out/implementer-$1-$2.png" -crop ${width}x60+0+0 +repage -scale 200% "$out/implementer-$1-$2-bar.png"
    convert "$out/implementer-$1-$2.png" -crop 900x60+0+36 +repage -scale 200% "$out/implementer-$1-$2-head.png"
    convert "$out/implementer-$1-$2.png" -crop ${width}x240+0+$((height - 240)) +repage -scale 200% "$out/implementer-$1-$2-comp.png"
}

# Six frames ~0.7 s apart (see above). Notes gets the smallest and largest pairwise
# pixel-difference count among all 15 pairs, for the tab bar and the header row (both blink)
# and for the composer (state: expected still, except the spawn scene's placeholder dots).
pair() {   # pair <name>
    local n i j ae min max hmin hmax cmin cmax
    xdotool mousemove $((width + 20)) $((height + 20)); sleep 0.8
    for n in a b c d e f; do
        shoot "$1" "$n"
        sleep 0.5
    done
    min= max=0 hmin= hmax=0 cmin= cmax=0
    local frames=(a b c d e f)
    for i in "${!frames[@]}"; do
        for ((j = i + 1; j < ${#frames[@]}; j++)); do
            ae=$(compare -metric AE "$out/implementer-$1-${frames[$i]}-bar.png" \
                                      "$out/implementer-$1-${frames[$j]}-bar.png" null: 2>&1)
            [[ -z $min || $ae -lt $min ]] && min=$ae
            ((ae > max)) && max=$ae
            ae=$(compare -metric AE "$out/implementer-$1-${frames[$i]}-head.png" \
                                      "$out/implementer-$1-${frames[$j]}-head.png" null: 2>&1)
            [[ -z $hmin || $ae -lt $hmin ]] && hmin=$ae
            ((ae > hmax)) && hmax=$ae
            ae=$(compare -metric AE "$out/implementer-$1-${frames[$i]}-comp.png" \
                                      "$out/implementer-$1-${frames[$j]}-comp.png" null: 2>&1)
            [[ -z $cmin || $ae -lt $cmin ]] && cmin=$ae
            ((ae > cmax)) && cmax=$ae
        done
    done
    printf '%s: tab-bar frames differ in %s..%s px, header in %s..%s px, composer in %s..%s px (0 = identical)\n' \
        "$1" "$min" "$max" "$hmin" "$hmax" "$cmin" "$cmax" >>"$out/implementer-notes.txt"
}

idle() {
    start relay-dark idle
    xdotool mousemove $((width + 20)) $((height + 20)); sleep 0.8
    shoot idle x
    stop idle
}

running() {
    start relay-dark running
    t 'sleep 600'; k Return; sleep 3
    pair running
    stop running
}

relaying() {
    start relay-dark relaying
    t '*slow task'; k Return; sleep 6
    pair relaying
    stop relaying
}

spawn() {
    start relay-dark spawn
    t '*spawn demo'; k Return; sleep 10
    pair spawn
    stop spawn
}

labels() {
    start relay-dark labels
    t '*show the labels'; k Return; sleep 9
    xdotool mousemove $((width + 20)) $((height + 20)); sleep 0.8
    shoot labels x
    stop labels
}

labels-beige() {
    start ibm-beige labels-beige
    t '*show the labels'; k Return; sleep 9
    xdotool mousemove $((width + 20)) $((height + 20)); sleep 0.8
    shoot labels-beige x
    stop labels-beige
}

rm -f "$out/implementer-notes.txt"
for scene in $scenes; do $scene; done
printf 'done: %s\n' "$out"
