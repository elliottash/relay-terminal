#!/usr/bin/env bash
# `/todos` after card #SHE3: hidden from the `/` popup, never completed from a prefix, still run
# when typed in full. Implementer screenshots under Xvfb with an isolated HOME, XDG_CONFIG_HOME,
# XDG_RUNTIME_DIR and TMPDIR (short paths — a unix socket has 108 bytes), against the same
# loopback stub provider #BDXG's scenes use, so the task list has something in it.
#
#   docs/qa_evidence/2026-09-19-call-todos-tasks-everywhere-a-person-reads/drive-todos.sh [build-dir]
#
# Shots (implementer-NN-<name>.png, OCR in implementer-todos-notes.txt):
#   01-popup      "/" typed: the popup offers /tasks and never /todos
#   02-prefix     "/to" typed: nothing completes to todos (no ghost, no /todos row)
#   03-panel      "/todos" typed in full and sent: the Tasks panel opens
#
# The OCR of each shot is checked here, so the run itself says PASS or FAIL per scene.
# Needs Xvfb, xdotool, ImageMagick, tesseract.
set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"
out=$PWD
root=$(cd ../../.. && pwd)
build=${1:-$root/build}
stub=$root/docs/qa_evidence/2026-09-19-clicking-updated-todos-does-not-unfold/stub-provider.py
width=1440 height=900
port=${RELAY_QA_PORT:-8817}

display=
for n in $(seq 160 199); do [[ -e /tmp/.X11-unix/X$n ]] || { display=:$n; break; }; done
[[ -z $display ]] && { echo "no free display"; exit 1; }

sandbox=$(mktemp -d /tmp/rly-she3.XXXXXX)   # short: XDG_RUNTIME_DIR holds unix sockets
stub_pid= xvfb_pid= relay_pid=
cleanup() {
    local pid
    for pid in $relay_pid $stub_pid $xvfb_pid; do kill "$pid" 2>/dev/null; done
    rm -rf "$sandbox"
}
trap cleanup EXIT

python3 "$stub" "$port" >"$sandbox/stub.log" 2>&1 &
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
    ln -sfn "/run/user/$(id -u)/bus" "$sandbox/run/bus"   # the worker's systemd user scope
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
    cp "$sandbox/relay.log" "$out/relay-todos.log" 2>/dev/null
    [[ -n $relay_pid ]] && { kill "$relay_pid"; wait "$relay_pid"; } 2>/dev/null
    relay_pid=
}

notes=$out/implementer-todos-notes.txt
# shot <name>: the whole window, plus a 150 % crop of the lower half (the composer, its popup and
# the panel all sit there), OCR'd into the notes. The OCR text is left in $ocr for the checks.
shot() {
    xdotool mousemove $((width + 20)) $((height + 20)); sleep 0.8
    import -window "$win" "$out/implementer-$1.png"
    convert "$out/implementer-$1.png" -crop ${width}x520+0+$((height - 520)) +repage -scale 150% "$sandbox/lower.png"
    ocr=$(tesseract "$sandbox/lower.png" - --psm 6 2>/dev/null)
    { echo "--- $1"; echo "$ocr"; } >>"$notes"
}
fails=0
check() {   # check <scene> <what> <expect: has|lacks> <needle>
    local ok
    case $3 in
        has)   [[ $ocr == *"$4"* ]]; ok=$? ;;
        lacks) [[ $ocr != *"$4"* ]]; ok=$? ;;
    esac
    if (( ok == 0 )); then echo "PASS $1: $2" | tee -a "$notes"
    else echo "FAIL $1: $2" | tee -a "$notes"; (( ++fails )); fi
}

rm -f "$notes"
start
ask 'plan the work'                       # one update_todos call, so the Tasks panel is not empty
sleep 9
t '/';   sleep 1.2; shot 01-popup         # the popup: every command Relay teaches
check 01-popup 'the popup offers /tasks' has '/tasks'
check 01-popup 'the popup never offers /todos' lacks '/todos'
t 'to';  sleep 1.2; shot 02-prefix        # "/to": nothing completes to the hidden name
check 02-prefix 'no /todos row for the prefix' lacks '/todos'
check 02-prefix 'no "todos" completion at all' lacks 'todos'
t 'dos'; sleep 0.6; k Return; sleep 2.5   # "/todos" in full: still runs
shot 03-panel
check 03-panel 'the Tasks panel opened' has 'Tasks'
check 03-panel 'and it holds the tasks the turn left' has 'make the row a fold'
stop
echo "done: $out ($fails failure(s))"
exit $(( fails > 0 ))
