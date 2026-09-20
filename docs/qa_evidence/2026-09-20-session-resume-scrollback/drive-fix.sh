#!/usr/bin/env bash
# The fix for #0TJ9, driven: a conversation opened from the sessions manager comes back with the
# terminal text it printed, and with its transcript when no text was ever saved.
#
# `drive.sh` beside this reproduces the bug; this drives what replaced it. Same isolation — Xvfb,
# an isolated HOME with its own XDG_* and TMPDIR under a short path (the 108-byte unix socket
# limit), RELAY_KEYRING=off — and no provider account: the profile points a local model endpoint
# at stub-provider.py on 127.0.0.1, so turns really run and really save.
#
#   docs/qa_evidence/2026-09-20-session-resume-scrollback/drive-fix.sh [build-dir]
#
# Three Relay runs:
#
#   run 1  one pane, two conversations. "alpha …" is answered with ALPHA-MARKER lines, /new
#          starts a second conversation in the same pane, "bravo …" is answered with
#          BRAVO-MARKER lines, and the window is closed. Then, on disk:
#            (c) two <id>.scrollback.txt files beside the two session files, and the second
#                holds BRAVO-MARKER and *not* ALPHA-MARKER — session B's file does not begin
#                with session A's text.
#   run 2  fresh start, Ctrl+Shift+Y, Enter on the newest row: (a) the saved text is above the
#          "Session loaded" line, under the conversation's own rule, and PageUp scrolls into it.
#   run 3  the same conversation's .scrollback.txt is deleted first: (b) the pane prints the
#          transcript instead — the ✦ prompt and the reply, between the same rules.
#
# Shots: fix-01-run1-alpha, fix-02-run1-bravo, fix-03-resumed, fix-04-paged-up,
# fix-05-fallback. Findings in fix-state.txt. Needs Xvfb, xdotool, ImageMagick.
set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"
out=$PWD
root=$(cd ../../.. && pwd)
build=${1:-$root/build}
width=1200 height=900
port=${RELAY_QA_PORT:-8823}

display=
for n in $(seq 120 159); do [[ -e /tmp/.X11-unix/X$n ]] || { display=:$n; break; }; done
[[ -z $display ]] && { echo "no free display"; exit 1; }

# Short path: $XDG_RUNTIME_DIR holds unix sockets, and the whole path must fit in 108 bytes.
sandbox=$(mktemp -d /tmp/r0tj9.XXXXXX)
stub_pid= xvfb_pid= relay_pid=
cleanup() {
    local pid
    # Never `kill 0`: that is the whole process group, this script included. quit() leaves
    # relay_pid empty rather than zero for the same reason.
    for pid in $relay_pid $stub_pid $xvfb_pid; do [ -n "$pid" ] && [ "$pid" != 0 ] && kill "$pid" 2>/dev/null; done
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

t() { xdotool type --delay 30 "$1"; }
k() { xdotool key --delay 60 "$@"; }

mkdir -p "$sandbox/home" "$sandbox/run" "$sandbox/tmp"; chmod 700 "$sandbox/run"
ln -sfn "/run/user/$(id -u)/bus" "$sandbox/run/bus"
export HOME=$sandbox/home XDG_RUNTIME_DIR=$sandbox/run TMPDIR=$sandbox/tmp
export XDG_CONFIG_HOME=$HOME/.config XDG_DATA_HOME=$HOME/.local/share
export XDG_CACHE_HOME=$HOME/.cache XDG_STATE_HOME=$HOME/.local/state
work=$HOME/project
mkdir -p "$XDG_CONFIG_HOME/RelayTerminal" "$XDG_CONFIG_HOME/relay" "$XDG_DATA_HOME" "$XDG_CACHE_HOME" "$work"
printf "PS1='\\\\w \\\$ '\nunset PROMPT_COMMAND\n" >"$HOME/.bashrc"
cat >"$XDG_CONFIG_HOME/RelayTerminal/relay.conf" <<CONF
[instructions]
onboarded=true
[security]
approvals_chosen=true
[theme]
name=relay-dark
[provider]
preset=local:stub
CONF
printf '{"version": 1, "endpoints": [{"id": "local:stub", "label": "Stub", "base_url": "http://127.0.0.1:%s/v1", "model": "stub", "server": "openai-compatible", "context_window": 131072}]}\n' \
    "$port" >"$XDG_CONFIG_HOME/relay/local-models.json"

win=
start() {
    (cd "$work" && exec "$build/relay" --workspace "$work") >>"$sandbox/relay.log" 2>&1 &
    relay_pid=$!
    sleep 9
    win= ; local best=0 w
    for w in $(xdotool search --pid "$relay_pid" 2>/dev/null); do
        eval "$(xdotool getwindowgeometry --shell "$w" 2>/dev/null)"
        (( WIDTH * HEIGHT > best )) && { best=$(( WIDTH * HEIGHT )); win=$w; }
    done
    [[ -z $win ]] && { echo "no Relay window"; tail -20 "$sandbox/relay.log"; exit 1; }
    xdotool windowmove "$win" 0 0 windowsize "$win" $width $height
    xdotool windowfocus "$win"; sleep 2
}
quit() {   # Ctrl+W on the last pane asks before closing; Left picks Close.
    k ctrl+w; sleep 1.5
    k Left; sleep 0.4
    k Return
    for _ in $(seq 1 24); do kill -0 "$relay_pid" 2>/dev/null || break; sleep 0.5; done
    # A window that will not take the dialog must still not hang the run: the layout and the
    # conversations' text are written on the way out of the close, which has already happened.
    if kill -0 "$relay_pid" 2>/dev/null; then
        echo "run did not quit on its own; terminating" >&2
        kill "$relay_pid" 2>/dev/null
        for _ in $(seq 1 10); do kill -0 "$relay_pid" 2>/dev/null || break; sleep 0.5; done
        kill -9 "$relay_pid" 2>/dev/null
    fi
    # Polled, not `wait`: the worker Relay spawns holds the shell's job open long after Relay
    # itself is gone, and waiting on it hangs the run with every artifact already written.
    for _ in $(seq 1 10); do kill -0 "$relay_pid" 2>/dev/null || break; sleep 0.5; done
    relay_pid=
    sleep 1
}
shot() {
    xdotool mousemove $((width + 20)) $((height + 20)); sleep 0.8
    import -window "$win" "$out/fix-$1.png"
}
ask() { t "$1"; k Return; }

sessions=$XDG_DATA_HOME/relay/sessions
: >"$sandbox/relay.log"

# ----- run 1: two conversations in one pane -------------------------------------------------
start
ask 'alpha: say your markers'; sleep 24
shot 01-run1-alpha
ask '/new'; sleep 4                      # a second conversation in the same pane
ask 'bravo: say your markers'; sleep 24
shot 02-run1-bravo
quit

{
    echo "== session files and their terminal text, after run 1 =="
    find "$sessions" -type f \( -name '*.json' -o -name '*.scrollback.txt' \) -printf '%s\t%p\n' | sort -k2
    echo
    for f in $(find "$sessions" -name '*.scrollback.txt' | sort); do
        echo "-- $f"
        echo "   ALPHA-MARKER lines: $(grep -c ALPHA-MARKER "$f")   BRAVO-MARKER lines: $(grep -c BRAVO-MARKER "$f")"
    done
} >"$out/fix-state.txt" 2>&1

newest=$(find "$sessions" -name '*.scrollback.txt' -printf '%T@ %p\n' | sort -n | tail -1 | cut -d' ' -f2-)
older=$(find "$sessions" -name '*.scrollback.txt' -printf '%T@ %p\n' | sort -n | head -1 | cut -d' ' -f2-)
{
    echo
    echo "== (c) session B's file does not begin with session A's text =="
    echo "A (older): $older"
    echo "B (newer): $newest"
    if [ "$newest" != "$older" ] && grep -q BRAVO-MARKER "$newest" && ! grep -q ALPHA-MARKER "$newest"; then
        echo "PASS: two files; the newer holds BRAVO-MARKER and no ALPHA-MARKER"
    else
        echo "FAIL: see the counts above"
    fi
} >>"$out/fix-state.txt" 2>&1

# ----- run 2: resume the newest conversation, with its file -----------------------------------
start
k ctrl+shift+y; sleep 5
k Down; sleep 1
k Return; sleep 10          # Enter: resume here
shot 03-resumed
k Prior Prior; sleep 1
shot 04-paged-up
quit

# ----- run 3: the same row, with every saved text deleted -------------------------------------
# All of them, not just one: the row the manager opens is whichever this key sequence lands on,
# and the point is that a conversation with no file at all still comes back.
find "$sessions" -name '*.scrollback.txt' -delete
{
    echo
    echo "== (b) every *.scrollback.txt deleted; run 3 has only the transcript to go on =="
    find "$sessions" -name '*.scrollback.txt' | wc -l | sed 's/^/   files left: /'
} >>"$out/fix-state.txt" 2>&1
start
k ctrl+shift+y; sleep 5
k Down; sleep 1
k Return; sleep 12
shot 05-fallback
quit

cp "$sandbox/relay.log" "$out/relay-fix.log" 2>/dev/null
printf 'done: %s\n' "$out"
