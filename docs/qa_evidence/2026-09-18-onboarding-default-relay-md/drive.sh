#!/usr/bin/env bash
# Onboarding with no instruction files (#ZYRB): the first-launch dialog must NOT appear when the
# scan finds nothing — Relay writes the starter relay.md, selects it and opens it instead. With
# files present the dialog still shows (regression guard). Implementer run under Xvfb with an
# isolated HOME, XDG_CONFIG_HOME, XDG_RUNTIME_DIR and TMPDIR; the profile points a local model
# endpoint at stub-provider.py (no provider account).
#
#   docs/qa_evidence/2026-09-18-onboarding-default-relay-md/drive.sh [build-dir] [scene...]
#
# Scenes (all by default):
#   fresh    no instruction files anywhere: no dialog, relay.md created + selected + opened
#   control  ~/.claude/CLAUDE.md present: the dialog still shows
#
# Needs Xvfb, xdotool and ImageMagick.
set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"
out=$PWD
root=$(cd ../../.. && pwd)
build=${1:-$root/build}; shift || true
scenes=${*:-fresh control}
width=1440 height=900
port=${RELAY_QA_PORT:-8801}

display=
for n in $(seq 160 199); do [[ -e /tmp/.X11-unix/X$n ]] || { display=:$n; break; }; done
[[ -z $display ]] && { echo "no free display"; exit 1; }

sandbox=$(mktemp -d /tmp/relay-onboarding.XXXXXX)
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

fail=0
prepare() {   # prepare [extra setup as $@ shell]
    kill "$relay_pid" 2>/dev/null; wait "$relay_pid" 2>/dev/null; relay_pid=
    rm -rf "$sandbox/home" "$sandbox/run" "$sandbox/tmp"
    mkdir -p "$sandbox/home" "$sandbox/run" "$sandbox/tmp"; chmod 700 "$sandbox/run"
    export HOME=$sandbox/home XDG_RUNTIME_DIR=$sandbox/run TMPDIR=$sandbox/tmp
    export XDG_CONFIG_HOME=$HOME/.config XDG_DATA_HOME=$HOME/.local/share XDG_CACHE_HOME=$HOME/.cache
    work=$HOME/project
    mkdir -p "$XDG_CONFIG_HOME/RelayTerminal" "$XDG_CONFIG_HOME/relay" "$XDG_DATA_HOME" "$XDG_CACHE_HOME" "$work"
    printf "PS1='\\\\w \\$ '\nunset PROMPT_COMMAND\n" >"$HOME/.bashrc"
    printf '# project\n' >"$work/README.md"
    # No onboarding mark: the [instructions] section is absent on purpose.
    cat >"$XDG_CONFIG_HOME/RelayTerminal/relay.conf" <<CONF
[provider]
preset=local:stub
CONF
    printf '{"version": 1, "endpoints": [{"id": "local:stub", "label": "Stub", "base_url": "http://127.0.0.1:%s/v1", "model": "stub", "server": "openai-compatible", "context_window": 131072}]}\n' \
        "$port" >"$XDG_CONFIG_HOME/relay/local-models.json"
    "$@"
}

start() {   # start <scene>
    prepare "${@:2}"
    (cd "$work" && exec "$build/relay" --workspace "$work") >"$sandbox/relay-$1.log" 2>&1 &
    relay_pid=$!
    sleep 8   # session configures, the 400 ms onboarding timer fires, scan + dialog/init land
    win=
    local best=0 w
    for w in $(xdotool search --pid "$relay_pid" 2>/dev/null); do
        eval "$(xdotool getwindowgeometry --shell "$w" 2>/dev/null)"
        (( WIDTH * HEIGHT > best )) && { best=$(( WIDTH * HEIGHT )); win=$w; }
    done
    [[ -z $win ]] && { echo "[$1] no Relay window"; cat "$sandbox/relay-$1.log"; exit 1; }
    xdotool windowmove "$win" 0 0 windowsize "$win" $width $height
    xdotool windowfocus "$win"; sleep 1.5
}

shot() {   # shot <name>
    xdotool mousemove $((width + 20)) $((height + 20)); sleep 0.8
    import -window "$win" "$out/implementer-$1.png"
    echo "[$1] implementer-$1.png"
}

dialogs() { xdotool search --name 'Agent instructions' 2>/dev/null | wc -l; }

fresh() {
    start fresh
    local d; d=$(dialogs)
    shot fresh-no-files
    if [[ "$d" != 0 ]]; then echo "[fresh] FAIL: dialog shown with no instruction files"; fail=1; else echo "[fresh] ok: no dialog with no instruction files"; fi
    local md=$XDG_CONFIG_HOME/relay/relay.md
    if [[ -f $md ]] && grep -q '^## Rules$' "$md"; then echo "[fresh] ok: starter relay.md written ($(wc -c <"$md") bytes)"; else echo "[fresh] FAIL: starter relay.md missing/incomplete"; head -c 200 "$md" 2>/dev/null; fail=1; fi
    if grep -q '^files=.*relay/relay.md$' "$XDG_CONFIG_HOME/RelayTerminal/relay.conf" 2>/dev/null; then echo "[fresh] ok: relay.md selected as the instructions file"; else echo "[fresh] FAIL: instructions/files not set"; fail=1; fi
    if grep -q '^onboarded=true$' "$XDG_CONFIG_HOME/RelayTerminal/relay.conf"; then echo "[fresh] ok: onboarding marked done"; else echo "[fresh] FAIL: onboarded not set"; fail=1; fi
}

control_run() {
    prepare
    mkdir -p "$HOME/.claude" && printf '# Claude rules\nUse 2-space indent.\n' >"$HOME/.claude/CLAUDE.md"
    (cd "$work" && exec "$build/relay" --workspace "$work") >"$sandbox/relay-control.log" 2>&1 &
    relay_pid=$!
    sleep 8
    win=
    local best=0 w
    for w in $(xdotool search --pid "$relay_pid" 2>/dev/null); do
        eval "$(xdotool getwindowgeometry --shell "$w" 2>/dev/null)"
        (( WIDTH * HEIGHT > best )) && { best=$(( WIDTH * HEIGHT )); win=$w; }
    done
    [[ -z $win ]] && { echo "[control] no Relay window"; cat "$sandbox/relay-control.log"; exit 1; }
    xdotool windowmove "$win" 0 0 windowsize "$win" $width $height
    xdotool windowfocus "$win"; sleep 1.5
    local d; d=$(dialogs)
    shot control-with-claude-md
    if [[ "$d" != 0 ]]; then echo "[control] ok: dialog still shows with a CLAUDE.md present"; else echo "[control] FAIL: dialog did not show"; fail=1; fi
    if [[ ! -f $XDG_CONFIG_HOME/relay/relay.md ]]; then echo "[control] ok: relay.md not auto-created when files exist"; else echo "[control] FAIL: relay.md created although the dialog should decide"; fail=1; fi
    xdotool key Escape; sleep 1   # Not now: nothing selected, nothing created
}

for scene in $scenes; do
    case $scene in
        fresh) fresh ;;
        control) control_run ;;
        *) echo "unknown scene $scene"; exit 1 ;;
    esac
done
exit $fail
