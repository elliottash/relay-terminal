#!/usr/bin/env bash
# Live pane states (card #V8KT): implementer screenshots under Xvfb with an isolated HOME,
# XDG_CONFIG_HOME, XDG_RUNTIME_DIR and TMPDIR. No provider account: the profile points a local
# model endpoint at stub-provider.py on 127.0.0.1 (copied from the 2026-09-18 pane-types run).
#
#   docs/qa_evidence/2026-09-19-pane-live-state/drive.sh [build-dir] [scene...]
#
# Scenes (all by default); the live ones are shot as six frames half a second apart into
# implementer-<scene>-{a..f}[-bar|-head].png:
#   idle      implementer-idle.png: a fresh pane — ring glyph, no word, plain tab icon.
#   running   implementer-running-*: `sleep 600` — breathing blue triangle, "Command running"
#             beside it, blue dot on the tab.
#   relaying  implementer-relaying-*: a `*slow` stub turn (60 s) — breathing violet star,
#             "Relaying…", violet dot on the tab.
#   mix       implementer-mix-*: a failed turn's news icon in one pane beside a running command
#             in another — the tab shows the failed disc *and* the blue dot.
#
# Why six frames 0.5 s apart and not two: the pulse's four steps include a repeated one
# (pulseScale phases 1 and 3 are both 0.90, so they paint identically) and its periods are
# 1.6 s on the tab (the 400 ms poll) and 2.4 s on the pane glyph (its own 600 ms clock). Two
# frames an exact multiple of a period apart — or two phases apart, onto the repeated step —
# are pixel-identical while the mark animates fine (the first run of this evidence said
# "relaying: 0 pixels" for exactly that reason; probe-output.txt measures every step). Six
# frames spanning >2.5 s cannot all land on identical phases, so the largest pairwise
# pixel-difference count in implementer-notes.txt is an honest "it moves".
#
# Needs Xvfb, xdotool, ImageMagick.
set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"
out=$PWD
root=$(cd ../../.. && pwd)
build=${1:-$root/build}; shift || true
scenes=${*:-idle running relaying mix}
width=1440 height=900
port=${RELAY_QA_PORT:-8803}

display=
for n in $(seq 160 199); do [[ -e /tmp/.X11-unix/X$n ]] || { display=:$n; break; }; done
[[ -z $display ]] && { echo "no free display"; exit 1; }

sandbox=$(mktemp -d /tmp/relay-live-state.XXXXXX)
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

t() { xdotool type --delay 18 "$1"; }
k() { xdotool key --delay 60 "$@"; }
click() { xdotool mousemove "$1" "$2" click 1; sleep 0.4; }

prepare() {
    rm -rf "$sandbox/home" "$sandbox/run" "$sandbox/tmp"
    mkdir -p "$sandbox/home" "$sandbox/run" "$sandbox/tmp"; chmod 700 "$sandbox/run"
    export HOME=$sandbox/home XDG_RUNTIME_DIR=$sandbox/run TMPDIR=$sandbox/tmp
    export XDG_CONFIG_HOME=$HOME/.config XDG_DATA_HOME=$HOME/.local/share XDG_CACHE_HOME=$HOME/.cache
    work=$HOME/project
    mkdir -p "$XDG_CONFIG_HOME/RelayTerminal" "$XDG_CONFIG_HOME/relay" "$XDG_DATA_HOME" "$XDG_CACHE_HOME" "$work"
    printf "PS1='\\\\\\\\w \\$ '\nunset PROMPT_COMMAND\n" >"$HOME/.bashrc"
    printf '# project\n' >"$work/README.md"
    cat >"$XDG_CONFIG_HOME/RelayTerminal/relay.conf" <<CONF
[instructions]
onboarded=true
[theme]
name=relay-dark
[appearance]
pane_colours=type
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

stop() { [[ -n $relay_pid ]] && { kill "$relay_pid"; wait "$relay_pid"; } 2>/dev/null; relay_pid=; }

# Six frames 0.5 s apart (see above), each with the tab bar (top) and the header row crops
# at 200 %. Only the pulse may differ between tab-bar frames: notes gets the smallest and the
# largest pairwise pixel-difference count among all 15 pairs.
pair() {   # pair <name>
    local n i j ae min max
    xdotool mousemove $((width + 20)) $((height + 20)); sleep 0.8
    for n in a b c d e f; do
        import -window "$win" "$out/implementer-$1-$n.png"
        convert "$out/implementer-$1-$n.png" -crop ${width}x60+0+0 +repage -scale 200% "$out/implementer-$1-$n-bar.png"
        convert "$out/implementer-$1-$n.png" -crop 900x60+0+36 +repage -scale 200% "$out/implementer-$1-$n-head.png"
        sleep 0.5
    done
    min= max=0
    local frames=(a b c d e f)
    for i in "${!frames[@]}"; do
        for ((j = i + 1; j < ${#frames[@]}; j++)); do
            ae=$(compare -metric AE "$out/implementer-$1-${frames[$i]}-bar.png" \
                                      "$out/implementer-$1-${frames[$j]}-bar.png" null: 2>&1)
            [[ -z $min || $ae -lt $min ]] && min=$ae
            ((ae > max)) && max=$ae
        done
    done
    printf '%s: tab-bar frames differ in %s..%s pixels across all pairs (0 = identical)\n' "$1" "$min" "$max" \
        >>"$out/implementer-notes.txt"
}

idle() {
    start
    xdotool mousemove $((width + 20)) $((height + 20)); sleep 0.8
    import -window "$win" "$out/implementer-idle.png"
    convert "$out/implementer-idle.png" -crop ${width}x60+0+0 +repage -scale 200% "$out/implementer-idle-bar.png"
    convert "$out/implementer-idle.png" -crop 900x60+0+36 +repage -scale 200% "$out/implementer-idle-head.png"
    stop
}

running() {
    start
    t 'sleep 600'; k Return; sleep 2
    pair running
    stop
}

relaying() {
    start
    t '*slow task'; k Return; sleep 6
    pair relaying
    stop
}

mix() {
    start
    k ctrl+e; sleep 2.5                          # split right: the new pane takes the keyboard
    t '*fail please'; k Return; sleep 2
    click 200 400; sleep 1                       # the left pane: a plain command in the shell
    t 'sleep 600'; k Return; sleep 8             # the failed turn's news is up by now
    pair mix
    stop
}

rm -f "$out/implementer-notes.txt"
for scene in $scenes; do $scene; done
printf 'done: %s\n' "$out"
