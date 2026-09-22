#!/usr/bin/env bash
# Verifier's driver for the #7BM4 staged scenario (Kimi K3, 2026-09-22).
# Plays the mechanical steps of scenario.json in a real Relay under Xvfb on an isolated
# profile, xdotool only. One screenshot per step; tesseract OCR is used afterwards to read
# what was on screen. Coordinates were discovered by OCR on this fixture (the implementer's
# ai-pass.sh defaults missed: this board has fewer expanded sections, so rows sit higher).
#
#   RELAY_BIN=<relay> ./drive.sh <staged project> <out dir> <step: discover|card|check|gate|tests|detail|profile|profilerun>
set -uo pipefail
bin=${RELAY_BIN:?set RELAY_BIN}
work=${1:?staged project}; out=${2:?out dir}; step=${3:?step}
mkdir -p "$out"; width=1600 height=1000
display=; for n in $(seq 170 199); do [[ -e /tmp/.X11-unix/X$n ]] || { display=:$n; break; }; done
sandbox=$(mktemp -d /tmp/rl-v7bm4.XXXX)
cleanup() { for pid in ${relay_pid:-} ${xvfb_pid:-}; do kill -TERM "$pid" 2>/dev/null; done; sleep 2; rm -rf "$sandbox"; }
trap cleanup EXIT
Xvfb "$display" -screen 0 $((width+40))x$((height+40))x24 >/dev/null 2>&1 & xvfb_pid=$!; sleep 2
export DISPLAY=$display RELAY_KEYRING=off
mkdir -p "$sandbox/home/.config/RelayTerminal" "$sandbox/run" "$sandbox/tmp"; chmod 700 "$sandbox/run"
ln -sfn "/run/user/$(id -u)/bus" "$sandbox/run/bus"
export HOME=$sandbox/home XDG_RUNTIME_DIR=$sandbox/run TMPDIR=$sandbox/tmp XDG_CONFIG_HOME=$sandbox/home/.config XDG_DATA_HOME=$sandbox/home/.local/share XDG_CACHE_HOME=$sandbox/home/.cache
printf '[instructions]\nonboarded=true\n[security]\napprovals_chosen=true\n' > "$XDG_CONFIG_HOME/RelayTerminal/relay.conf"
(cd "$work" && exec "$bin" --workspace "$work" --clean-shell --fresh) > "$out/relay-$step.log" 2>&1 & relay_pid=$!
sleep 12
win=; best=0
for c in $(xdotool search --pid "$relay_pid" 2>/dev/null); do eval "$(xdotool getwindowgeometry --shell "$c" 2>/dev/null)"; (( WIDTH*HEIGHT > best )) && { best=$((WIDTH*HEIGHT)); win=$c; }; done
[[ -z $win ]] && { echo "no window"; tail -5 "$out/relay-$step.log"; exit 1; }
xdotool windowmove "$win" 0 0 windowsize "$win" $width $height; xdotool windowfocus "$win"; sleep 5
xdotool key --window "$win" ctrl+shift+s; sleep 10
shot() { import -window root "$out/$1.png"; }
click() { xdotool mousemove "$1" "$2" click 1; sleep "${3:-2}"; }
# open the SYCG card: filter, then click its row
click 1170 102 1; xdotool type --delay 40 SYCG; sleep 3
click 1000 265 6
shot "$step-10-card"
back() { xdotool key --window "$win" Escape; sleep 3; }
case $step in
discover) : ;;
check)
    click "${CHECK_X:-1180}" "${CHECK_Y:-178}" 10
    shot "$step-11-checked" ;;
gate)
    click "${CHECK_X:-1180}" "${CHECK_Y:-178}" 10; shot "$step-11-checked"
    click "${STATUS_X:-918}" "${STATUS_Y:-150}" 3; shot "$step-12-status-open"
    if [[ -n ${DONE_X:-} ]]; then click "$DONE_X" "${DONE_Y:-430}" 6; shot "$step-13-gate"; fi ;;
tests)
    back
    click "${TESTS_X:-1088}" "${TESTS_Y:-856}" 12
    shot "$step-20-pane" ;;
detail)
    back
    click "${TESTS_X:-1088}" "${TESTS_Y:-856}" 12; shot "$step-20-pane"
    click "${ROW1_X:-800}" "${ROW1_Y:-432}" 5
    shot "$step-21-detail" ;;
profile)
    back
    click "${PROFILE_X:-1168}" "${PROFILE_Y:-856}" 4
    shot "$step-30-menu" ;;
profilerun)
    back
    click "${PROFILE_X:-1168}" "${PROFILE_Y:-856}" 4; shot "$step-30-menu"
    click "${BUILD_X:-1132}" "${BUILD_Y:-890}" "${BUILD_WAIT:-45}"
    shot "$step-31-result" ;;
esac
echo "step $step done"
