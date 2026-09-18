#!/usr/bin/env bash
# Relay Free on the desktop (Phase 3 of the hosted-inference plan): implementer screenshots under
# Xvfb with an isolated HOME, XDG_*, XDG_RUNTIME_DIR and TMPDIR, RELAY_KEYRING=off and no provider
# key stored. The gateway is fake-gateway.py on 127.0.0.1 (tests/test_hosted.py's FakeGateway),
# reached through RELAY_HOSTED_URL, so nothing here touches api.relay-terminal.ai.
#
#   docs/qa_evidence/2026-09-18-relay-free/drive.sh [build-dir] [scene...]
#
# Scenes (all by default):
#   free   00: a fresh install lands on Relay Free by itself; the disclosure line, the "Free" chip
#          01: an ask streamed a reply; the chip shows the allowance
#          02: Options › Models › API keys…: the Included group first, its row's status and buttons
#          03: the allowance used up: the exhausted line and the add-a-key dialog
#          04: after Cancel: a shell command still runs; the chip reads 0% left
#   byok   05: the same install with a key of its own (RELAY_KIMI_API_KEY): it lands on that
#          provider, not Relay Free, and there is no chip
#
# Needs Xvfb, xdotool and ImageMagick.
set -uo pipefail
build=$(readlink -f "${1:-$(dirname "${BASH_SOURCE[0]}")/../../../build}"); shift || true
cd "$(dirname "${BASH_SOURCE[0]}")"
out=$PWD
root=$(cd ../../.. && pwd)
scenes=${*:-free byok}
width=1280 height=800

display=
for n in $(seq 160 199); do [[ -e /tmp/.X11-unix/X$n ]] || { display=:$n; break; }; done
[[ -z $display ]] && { echo "no free display"; exit 1; }

sandbox=$(mktemp -d "${TMPDIR:-/tmp}/relay-free.XXXXXX")
gateway_pid= xvfb_pid= relay_pid=
cleanup() {
    local pid
    for pid in $relay_pid $gateway_pid $xvfb_pid; do kill "$pid" 2>/dev/null; done   # never `kill 0`
    rm -rf "$sandbox"
}
trap cleanup EXIT

control=$sandbox/gateway
(cd "$root" && PYTHONPATH=backend:. exec python3 "$out/fake-gateway.py" "$control") >"$sandbox/gateway.log" 2>&1 &
gateway_pid=$!
for _ in $(seq 50); do [[ -s $control/gateway.url ]] && break; sleep 0.2; done
[[ -s $control/gateway.url ]] || { echo "fake gateway did not start"; cat "$sandbox/gateway.log"; exit 1; }
export RELAY_HOSTED_URL=$(cat "$control/gateway.url")
Xvfb "$display" -screen 0 $((width + 40))x$((height + 40))x24 >/dev/null 2>&1 &
xvfb_pid=$!
sleep 2
kill -0 "$xvfb_pid" 2>/dev/null || { echo "Xvfb died on $display"; exit 1; }
export DISPLAY=$display RELAY_KEYRING=off

t() { xdotool type --delay 18 "$1"; }
k() { xdotool key --delay 60 "$@"; }

prepare() {
    rm -rf "$sandbox/home" "$sandbox/run" "$sandbox/tmp"
    mkdir -p "$sandbox/home" "$sandbox/run" "$sandbox/tmp"; chmod 700 "$sandbox/run"
    export HOME=$sandbox/home XDG_RUNTIME_DIR=$sandbox/run TMPDIR=$sandbox/tmp
    export XDG_CONFIG_HOME=$HOME/.config XDG_DATA_HOME=$HOME/.local/share XDG_CACHE_HOME=$HOME/.cache XDG_STATE_HOME=$HOME/.local/state
    work=$HOME/project
    mkdir -p "$XDG_CONFIG_HOME/RelayTerminal" "$XDG_DATA_HOME" "$XDG_CACHE_HOME" "$XDG_STATE_HOME" "$work"
    printf "PS1='\\\\w \$ '\nunset PROMPT_COMMAND\n" >"$HOME/.bashrc"
    printf '# project\n' >"$work/README.md"
    # No [provider] section: nothing chosen, nothing stored. The instructions onboarding is skipped
    # so the first screenshot is the pane itself.
    cat >"$XDG_CONFIG_HOME/RelayTerminal/relay.conf" <<CONF
[instructions]
onboarded=true
[theme]
name=relay-dark
CONF
}

start() {
    prepare
    (cd "$work" && exec "$build/relay" --workspace "$work" --clean-shell --fresh) >"$sandbox/relay.log" 2>&1 &
    relay_pid=$!
    sleep 8
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

shot() {   # shot <name>: the whole screen, so a dialog above the window is in it too
    xdotool mousemove $((width + 20)) $((height + 20)); sleep 0.8
    import -window root -crop ${width}x${height}+0+0 +repage "$out/implementer-$1.png"
}

free() {
    rm -f "$control/exhausted" "$control/used"
    start
    shot "00-fresh-install-lands-on-relay-free"
    t '*say hello'; k Return; sleep 6
    echo 61200 >"$control/used"          # what the day's calls add up to, as the gateway would count
    shot "01-an-ask-streamed-and-the-chip-shows-the-allowance"
    k ctrl+shift+a; sleep 1.5; t 'API keys'; sleep 1; k Return; sleep 3
    shot "02-api-keys-modal-with-the-included-group-first"
    k Escape; sleep 1
    touch "$control/exhausted"
    xdotool mousemove 400 725 click 1; sleep 0.5     # the modal took focus; back to the prompt box
    t '*and again'; k Return; sleep 6
    shot "03-allowance-used-up-the-line-and-the-dialog"
    k Escape; sleep 1
    xdotool mousemove 400 725 click 1; sleep 0.5
    t 'echo the shell still runs $((6*7))'; k Return; sleep 3
    shot "04-after-cancel-a-shell-command-still-runs"
    [[ -s $sandbox/relay.log ]] && cp "$sandbox/relay.log" "$out/free-relay.log"
    stop
}

byok() {
    rm -f "$control/exhausted" "$control/used"
    RELAY_KIMI_API_KEY=not-a-real-key start
    shot "05-with-a-key-of-your-own-relay-free-is-not-chosen"
    stop
}

for scene in $scenes; do "$scene"; done
echo done
