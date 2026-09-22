#!/usr/bin/env bash
# The AI's pass over the staged scenario (owner, 2026-09-20: "design for an AI to try simulating
# first"; "verification should involve that (typically)"). It plays each scenario in a real Relay
# under Xvfb on an isolated profile and leaves one screenshot per step; ai-pass.md says what the
# AI saw against what scenario.json expects. Named controls use scripts/relay-drive;
# no coordinate input and no terminal-pane input.
#
#   RELAY_BIN=<relay> ./ai-pass.sh <staged project> <out dir> <phase: a|b|c> [RELAY_DRIVE=/path/to/relay-drive]
set -euo pipefail
driver=${RELAY_DRIVE:-$(cd "$(dirname "$0")/../../../.." && pwd)/scripts/relay-drive}
bin=${RELAY_BIN:?set RELAY_BIN to a relay binary that has #7BM4}
work=${1:?staged project}; out=${2:?out dir}; phase=${3:-a}
card=$(python3 -c "import json,sys; print(json.load(open('$work/STAGED.json'))['cards']['done'])")
mkdir -p "$out"; width=1600 height=1000
display=; for n in $(seq 170 199); do [[ -e /tmp/.X11-unix/X$n ]] || { display=:$n; break; }; done
sandbox=$(mktemp -d /tmp/rl-ai.XXXX)
cleanup() { for pid in ${relay_pid:-} ${xvfb_pid:-}; do kill -TERM "$pid" 2>/dev/null; done; sleep 2; rm -rf "$sandbox"; }
trap cleanup EXIT
Xvfb "$display" -screen 0 $((width+40))x$((height+40))x24 >/dev/null 2>&1 & xvfb_pid=$!; sleep 2
export DISPLAY=$display RELAY_KEYRING=off
unset RELAY_OPEN_SOCKET  # discover only this disposable instance, never the caller's app
mkdir -p "$sandbox/home/.config/RelayTerminal" "$sandbox/run" "$sandbox/tmp"; chmod 700 "$sandbox/run"
ln -sfn "/run/user/$(id -u)/bus" "$sandbox/run/bus"
export HOME=$sandbox/home XDG_RUNTIME_DIR=$sandbox/run TMPDIR=$sandbox/tmp XDG_CONFIG_HOME=$sandbox/home/.config XDG_DATA_HOME=$sandbox/home/.local/share XDG_CACHE_HOME=$sandbox/home/.cache
printf '[instructions]\nonboarded=true\n[security]\napprovals_chosen=true\n' > "$XDG_CONFIG_HOME/RelayTerminal/relay.conf"
(cd "$work" && exec "$bin" --workspace "$work" --clean-shell --fresh) > "$out/relay-$phase.log" 2>&1 & relay_pid=$!
sleep 12
drive() { "$driver" "$@"; }
wait_read() {
    local i
    for i in $(seq 1 60); do drive read "$1" > "$out/$phase-read.json" && return 0; sleep 1; done
    echo "named control did not become readable: $1" >&2; return 1
}
shot() { import -window root "$out/$1.png"; }
drive open "$card"
wait_read sections
shot "$phase-00-card"
case $phase in
a)
    drive press boardTestsCheck
    wait_read boardTestsFindings
    drive read boardTestsFindings > "$out/a-findings.json"
    shot a-11-checked
    drive press boardCardDone
    sleep 2
    drive read notice > "$out/a-gate.json"
    shot a-13-gate ;;
b)
    drive action tests.open
    sleep 3
    drive panes > "$out/b-panes.json"
    shot b-10-pane ;;
c)
    # Return to the list so its tool row is visible.
    drive press boardCardClose
    wait_read boardProfile
    drive press boardProfile
    sleep 1
    shot c-10-menu
    drive press profileTarget:build
    sleep "${BUILD_WAIT:-10}"
    drive panes > "$out/c-panes.json"
    shot c-11-result ;;
esac
echo "phase $phase done"
