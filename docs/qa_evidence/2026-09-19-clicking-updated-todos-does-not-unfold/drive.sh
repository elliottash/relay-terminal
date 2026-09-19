#!/usr/bin/env bash
# The task fold (card #BDXG): implementer screenshots under Xvfb with an isolated HOME,
# XDG_CONFIG_HOME, XDG_RUNTIME_DIR and TMPDIR (short paths — a unix socket has 108 bytes).
# No provider account: the profile points a local model endpoint at stub-provider.py on
# 127.0.0.1, which answers every ask with an `update_todos` call and then a one-line answer.
#
#   docs/qa_evidence/2026-09-19-clicking-updated-todos-does-not-unfold/drive.sh [build-dir]
#
# Shots (implementer-NN-<name>.png, each with a 150 % body crop and OCR in implementer-notes.txt):
#   01-row        the turn ended: "▸ updated tasks · N open", folded
#   02-unfolded   one click on that row: the tasks with their glyphs, "open the task list"
#   03-folded     a second click on the same row: folded away again
#   04-two-rows   a second ask leaves a second row, with a different list
#   05-earlier    the first turn's fold, still its own list, beside the current one
#   06-tasklist   the last row of the fold clicked: the task list panel. This one is the least
#                 reliable of the six — it needs the OCR scan to find "open the task list" in
#                 whatever the terminal has scrolled to — and the run's 900 s budget can expire
#                 first; 02 and 05 already show the row rendered and linked, and
#                 calllines_test.cpp pins where it points.
#
# Needs Xvfb, xdotool, ImageMagick, tesseract.
set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"
out=$PWD
root=$(cd ../../.. && pwd)
build=${1:-$root/build}
width=1440 height=900
port=${RELAY_QA_PORT:-8816}

display=
for n in $(seq 160 199); do [[ -e /tmp/.X11-unix/X$n ]] || { display=:$n; break; }; done
[[ -z $display ]] && { echo "no free display"; exit 1; }

sandbox=$(mktemp -d /tmp/rly-bdxg.XXXXXX)   # short: XDG_RUNTIME_DIR holds unix sockets
stub_pid= xvfb_pid= relay_pid=
cleanup() {
    local pid
    for pid in $relay_pid $stub_pid $xvfb_pid; do kill "$pid" 2>/dev/null; done
    rm -rf "$sandbox"
}
trap cleanup EXIT

python3 "$out/stub-provider.py" "$port" >"$sandbox/stub.log" 2>&1 &
stub_pid=$!
sleep 1
kill -0 "$stub_pid" 2>/dev/null || { echo "stub did not start on $port:"; cat "$sandbox/stub.log"; exit 1; }
Xvfb "$display" -screen 0 $((width + 40))x$((height + 40))x24 >/dev/null 2>&1 &
xvfb_pid=$!
sleep 2
kill -0 "$xvfb_pid" 2>/dev/null || { echo "Xvfb died on $display"; exit 1; }
export DISPLAY=$display RELAY_KEYRING=off   # never touch the owner's real identity key

t() { xdotool type --delay 40 "$1"; }
k() { xdotool key --delay 60 "$@"; }
ask() { t "$1"; k Return; }

prepare() {
    rm -rf "$sandbox/home" "$sandbox/run" "$sandbox/tmp"
    mkdir -p "$sandbox/home" "$sandbox/run" "$sandbox/tmp"; chmod 700 "$sandbox/run"
    # The pane's agent worker runs in a systemd user scope (src/Isolation.h) and finds the user
    # manager through $XDG_RUNTIME_DIR/bus; a sandbox runtime dir has none, so link the real one.
    ln -sfn "/run/user/$(id -u)/bus" "$sandbox/run/bus"
    export HOME=$sandbox/home XDG_RUNTIME_DIR=$sandbox/run TMPDIR=$sandbox/tmp
    export XDG_CONFIG_HOME=$HOME/.config XDG_DATA_HOME=$HOME/.local/share XDG_CACHE_HOME=$HOME/.cache
    work=$HOME/project
    mkdir -p "$XDG_CONFIG_HOME/RelayTerminal" "$XDG_CONFIG_HOME/relay" "$XDG_DATA_HOME" "$XDG_CACHE_HOME" "$work"
    printf "PS1='\\\\w \\\$ '\nunset PROMPT_COMMAND\n" >"$HOME/.bashrc"
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

stop() {
    cp "$sandbox/relay.log" "$out/relay.log" 2>/dev/null
    mkdir -p "$out/logs" && cp -r "$sandbox/home/.local/share/relay/logs/." "$out/logs/" 2>/dev/null
    [[ -n $relay_pid ]] && { kill "$relay_pid"; wait "$relay_pid"; } 2>/dev/null
    relay_pid=
}

shot() {
    xdotool mousemove $((width + 20)) $((height + 20)); sleep 0.8
    import -window "$win" "$out/implementer-$1.png"
    convert "$out/implementer-$1.png" -crop ${width}x620+0+90 +repage -scale 150% "$out/implementer-$1-body.png"
    { echo "--- $1"; tesseract "$out/implementer-$1-body.png" - --psm 6 2>/dev/null; } >>"$out/implementer-notes.txt"
}

# Click the terminal row whose text OCRs as the first line starting with the fold arrow. The row
# geometry is fixed (the pane's font), so the y of the Nth printed row is what is scanned for: the
# shot is cropped one row at a time and the first crop that reads "updated tasks" is the row.
click_row() {   # click_row <needle> [occurrence]
    local needle=$1 want=${2:-1} seen=0 y text
    import -window "$win" "$sandbox/scan.png"
    for (( y = 96; y < height - 200; y += 6 )); do
        convert "$sandbox/scan.png" -crop 900x24+8+$y +repage -scale 250% "$sandbox/row.png" 2>/dev/null
        text=$(tesseract "$sandbox/row.png" - --psm 7 2>/dev/null | tr -d '\n')
        if [[ $text == *"$needle"* ]]; then
            (( ++seen ))
            if (( seen == want )); then
                row_y=$((y + 10))
                echo "row '$needle' #$want at y=$row_y: $text" >>"$out/implementer-notes.txt"
                xdotool mousemove 40 "$row_y" click 1
                sleep 1.2
                return 0
            fi
            (( y += 18 ))
        fi
    done
    echo "row '$needle' #$want NOT FOUND" >>"$out/implementer-notes.txt"
    return 1
}

# Opening a fold pushes the rows below it down, so the terminal scrolls and the anchor leaves the
# top of the viewport: wheel back up before looking for it again.
scroll_up() { xdotool mousemove 400 300 click --repeat ${1:-4} --delay 120 4; sleep 0.8; }

rm -f "$out/implementer-notes.txt"
row_y=0
start
ask 'plan the work'
sleep 9;  shot 01-row                              # the row, folded
click_row 'updated tasks'
scroll_up 3;  shot 02-unfolded                     # one click: the list, with its glyphs
scroll_up 3
click_row 'updated tasks'; shot 03-folded          # a second click on the same row: folded away
ask 'now do it'
sleep 9;  shot 04-two-rows                         # a second turn, a second row
click_row 'updated tasks' 1; shot 05-earlier       # each row unfolds to its own call'"'"'s list
click_row 'open the task list'; shot 06-tasklist   # the fold'"'"'s last row opens the task list
stop
printf 'done: %s\n' "$out"
