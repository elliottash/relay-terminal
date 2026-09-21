#!/usr/bin/env bash
# The AI's pass over the staged scenario (owner, 2026-09-20: "design for an AI to try simulating
# first"; "verification should involve that (typically)"). It plays each scenario in a real Relay
# under Xvfb on an isolated profile and leaves one screenshot per step; ai-pass.md says what the
# AI saw against what scenario.json expects. Input is xdotool only — clicks, chords and text typed
# into the board's own filter box, never into a terminal pane.
#
#   RELAY_BIN=<relay> ./ai-pass.sh <staged project> <out dir> <phase: a|b|c> [coordinates via env]
set -uo pipefail
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
mkdir -p "$sandbox/home/.config/RelayTerminal" "$sandbox/run" "$sandbox/tmp"; chmod 700 "$sandbox/run"
ln -sfn "/run/user/$(id -u)/bus" "$sandbox/run/bus"
export HOME=$sandbox/home XDG_RUNTIME_DIR=$sandbox/run TMPDIR=$sandbox/tmp XDG_CONFIG_HOME=$sandbox/home/.config XDG_DATA_HOME=$sandbox/home/.local/share XDG_CACHE_HOME=$sandbox/home/.cache
printf '[instructions]\nonboarded=true\n[security]\napprovals_chosen=true\n' > "$XDG_CONFIG_HOME/RelayTerminal/relay.conf"
(cd "$work" && exec "$bin" --workspace "$work" --clean-shell --fresh) > "$out/relay-$phase.log" 2>&1 & relay_pid=$!
sleep 12
win=; best=0
for c in $(xdotool search --pid "$relay_pid" 2>/dev/null); do eval "$(xdotool getwindowgeometry --shell "$c" 2>/dev/null)"; (( WIDTH*HEIGHT > best )) && { best=$((WIDTH*HEIGHT)); win=$c; }; done
[[ -z $win ]] && { echo "no window"; tail -5 "$out/relay-$phase.log"; exit 1; }
xdotool windowmove "$win" 0 0 windowsize "$win" $width $height; xdotool windowfocus "$win"; sleep 5
xdotool key --window "$win" ctrl+shift+s; sleep 10
shot() { import -window root "$out/$1.png"; }
shot "$phase-00-board"
case $phase in
a)  # Scenario 1: "the agent says it is done"
    xdotool mousemove ${FILTER_X:-1170} ${FILTER_Y:-102} click 1; sleep 1; xdotool type --delay 40 "$card"; sleep 3
    xdotool mousemove ${ROW_X:-1100} ${ROW_Y:-359} click 1; sleep 6; shot a-10-card
    xdotool mousemove ${CHECK_X:-1548} ${CHECK_Y:-308} click 1; sleep 8; shot a-11-checked
    if [[ -n ${STATUS_X:-} ]]; then xdotool mousemove $STATUS_X $STATUS_Y click 1; sleep 2; shot a-12-status-open
      if [[ -n ${DONE_X:-} ]]; then xdotool mousemove $DONE_X $DONE_Y click 1; sleep 5; shot a-13-gate; fi; fi ;;
b)  # Scenario 2: "which test goes red once a week?"
    xdotool mousemove ${TESTS_X:-1493} ${TESTS_Y:-840} click 1; sleep 10; shot b-10-pane
    if [[ -n ${ROW1_X:-} ]]; then xdotool mousemove $ROW1_X $ROW1_Y click 1; sleep 4; shot b-11-detail; fi ;;
c)  # Scenario 3: "where does the build time go?"
    xdotool mousemove ${PROFILE_X:-1553} ${PROFILE_Y:-840} click 1; sleep 3; shot c-10-menu
    if [[ -n ${BUILD_X:-} ]]; then xdotool mousemove $BUILD_X $BUILD_Y click 1; sleep ${BUILD_WAIT:-60}; shot c-11-result; fi ;;
esac
echo "phase $phase done"
