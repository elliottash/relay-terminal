#!/usr/bin/env bash
# The model box swaps Main <-> Flash and the gear opens the model options modal
# (owner report, 2026-09-18). Implementer screenshots under Xvfb with an isolated profile.
#
#   docs/qa_evidence/2026-09-18-model-dropdown-selection/drive.sh [build-dir]
#
#   implementer-a-list-open.png     the open list: the preset, the Main/Flash rows, the gear
#   implementer-b-flash.png         the Flash row picked: the chip reads "Flash agent · <model>"
#   implementer-c-main.png          the Main row picked: the chip is back on the preset's model
#   implementer-d-flash-again.png   Flash again, to show the swap works both ways more than once
#   implementer-e-still-flash.png   the same pane later: the role survived, chip unchanged
#   implementer-f-model-options.png the gear row: the Main / Flash / Lite modal is open
#
# No provider account and no network turn: the profile stores a literal non-key string in the
# environment (RELAY_GLM_CODING_API_KEY) so one preset shows up as "stored", and the script never
# submits a prompt, so nothing is ever sent to a provider. Needs Xvfb, xdotool, ImageMagick.
set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"
out=$PWD
root=$(cd ../../.. && pwd)
build=${1:-$root/build}
width=1400 height=880

display=
for n in $(seq 160 199); do [[ -e /tmp/.X11-unix/X$n ]] || { display=:$n; break; }; done
[[ -z $display ]] && { echo "no free display"; exit 1; }

sandbox=${RELAY_SHOT_HOME:-/tmp/relay-modelbox-shots}
xvfb_pid= relay_pid=
cleanup() { kill "${relay_pid:-0}" "${xvfb_pid:-0}" 2>/dev/null; rm -rf "$sandbox"; }
trap cleanup EXIT

Xvfb "$display" -screen 0 $((width + 40))x$((height + 40))x24 >/dev/null 2>&1 &
xvfb_pid=$!
sleep 2
kill -0 "$xvfb_pid" 2>/dev/null || { echo "Xvfb died on $display"; exit 1; }
export DISPLAY=$display
export RELAY_KEYRING=off
# A literal non-key string: it only makes the preset list one stored provider. No turn is run.
export RELAY_GLM_CODING_API_KEY=xvfb-not-a-real-key

k() { xdotool key --delay 60 "$@"; }

rm -rf "$sandbox"
export HOME=$sandbox
export XDG_CONFIG_HOME=$HOME/.config XDG_DATA_HOME=$HOME/.local/share XDG_CACHE_HOME=$HOME/.cache
work=$HOME/project
mkdir -p "$XDG_CONFIG_HOME/RelayTerminal" "$XDG_DATA_HOME" "$XDG_CACHE_HOME" "$work"
printf "PS1='\\\\w \$ '\nunset PROMPT_COMMAND\n" >"$HOME/.bashrc"
printf '# project\n' >"$work/README.md"
cat >"$XDG_CONFIG_HOME/RelayTerminal/relay.conf" <<CONF
[instructions]
onboarded=true
[theme]
name=relay-dark
[provider]
preset=glm-coding
[suggestions]
next_command=false
next_prompt=false
CONF

largest_window() {
    win= ; local best=0 w
    for w in $(xdotool search --pid "$relay_pid" 2>/dev/null); do
        eval "$(xdotool getwindowgeometry --shell "$w" 2>/dev/null)"
        (( WIDTH * HEIGHT > best )) && { best=$(( WIDTH * HEIGHT )); win=$w; }
    done
}

shot() {   # shot <name> [root]
    sleep 0.6
    if [[ ${2:-} == root ]]; then import -window root "$out/implementer-$1.png"
    else xdotool mousemove $((width + 20)) $((height + 20)); sleep 0.4; import -window "$win" "$out/implementer-$1.png"; fi
}

"$build/relay" --workspace "$work" >"$out/relay-stderr.log" 2>&1 &
relay_pid=$!
sleep 7
largest_window
[[ -z $win ]] && { echo "no Relay window"; exit 1; }
xdotool windowmove "$win" 0 0 windowsize "$win" $width $height
xdotool windowfocus "$win"; sleep 2

# The model box sits in the composer's right-hand chip strip. COMBO_X/COMBO_Y were read off a
# screenshot of this very layout (1400x880, one pane, no plan chip).
COMBO_X=${COMBO_X:-1195} COMBO_Y=${COMBO_Y:-844}
open_list() { xdotool mousemove "$COMBO_X" "$COMBO_Y" click 1; sleep 1.2; }

# a. the list, with the two role rows and the gear under the preset
open_list
shot a-list-open root
# b. Main -> Flash (Down past the separator to Main, once more to Flash)
k Down Down Return; sleep 1.5
shot b-flash
# c. Flash -> Main (the list now sits on the Flash row; Up is the Main row)
open_list
k Up Return; sleep 1.5
shot c-main
# d. and back to Flash a second time, from Main
open_list
k Down Down Return; sleep 1.5
shot d-flash-again
# e. the pane stays open and does other things; the role is still Flash
xdotool windowfocus "$win"; xdotool type --delay 14 'echo the pane is still open'; sleep 1
shot e-still-flash
xdotool key ctrl+a; xdotool key BackSpace; sleep 0.5
# f. the gear row opens the Main / Flash / Lite modal
open_list
k Down Down Down Return; sleep 2.5
shot f-model-options root
printf 'done: %s\n' "$out"
