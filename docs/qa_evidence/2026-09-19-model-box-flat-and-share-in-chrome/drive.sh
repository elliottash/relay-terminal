#!/usr/bin/env bash
# The model box is one flat list — "model (main)" / "model (flash)" first, then the other
# presets — and the collapsed chip hugs the current row's text while the open list may be wider;
# the share button moved from the composer strip to the pane's chrome row (owner, 2026-09-19).
# Implementer screenshots under Xvfb with an isolated profile.
#
#   docs/qa_evidence/2026-09-19-model-box-flat-and-share-in-chrome/drive.sh [build-dir]
#
#   implementer-a-collapsed.png     the strip: the chip is only as wide as "glm-5.3 (main)";
#                                   the share button sits in the chrome row, top right
#   implementer-b-list-open.png     the open list: the two role rows, then the gear — the popup
#                                   is wider than the collapsed chip it hangs from
#   implementer-c-flash.png         the flash row picked: the chip reads "glm-5.3-flash (flash)"
#                                   and is exactly that much wider
#   implementer-d-main.png          the main row picked again: the chip is back to "glm-5.3 (main)"
#   implementer-e-share-tooltip.png hovering the chrome row's share button: its tooltip
#   implementer-f-share-dialog.png  clicking it opens the share dialog (QR) as the strip's chip did
#   implementer-g-share-lit.png     sharing started: the chrome row's share button wears the
#                                   agent's violet
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
for n in $(seq 200 230); do [[ -e /tmp/.X11-unix/X$n ]] || { display=:$n; break; }; done
[[ -z $display ]] && { echo "no free display"; exit 1; }

sandbox=${RELAY_SHOT_HOME:-/tmp/relay-flatbox-shots}
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
# The pairing URL is written out only under this QA hook; it proves the sidecar started.
export RELAY_REMOTE_PAIR_FILE="$sandbox/pair-url.txt"

k() { xdotool key --delay 60 "$@"; }

rm -rf "$sandbox"
export HOME=$sandbox
export XDG_CONFIG_HOME=$HOME/.config XDG_DATA_HOME=$HOME/.local/share XDG_CACHE_HOME=$HOME/.cache
work=$HOME/project
mkdir -p "$XDG_CONFIG_HOME/RelayTerminal" "$XDG_DATA_HOME" "$XDG_CACHE_HOME" "$work"
printf "PS1='\\\\w \\$ '\nunset PROMPT_COMMAND\n" >"$HOME/.bashrc"
printf '# project\n' >"$work/README.md"
cat >"$XDG_CONFIG_HOME/RelayTerminal/relay.conf" <<CONF
[instructions]
onboarded=true
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

# The model box sits in the composer's right-hand chip strip; now that the chip hugs its text its
# right edge is fixed and its left edge moves. SHARE_X/SHARE_Y is the share button in the chrome
# row at the pane's top right. Both were read off a probe screenshot of this very layout
# (1400x880, one pane); override them if the strip moves.
COMBO_X=${COMBO_X:-1140} COMBO_Y=${COMBO_Y:-844}
SHARE_X=${SHARE_X:-1305} SHARE_Y=${SHARE_Y:-88}
open_list() { xdotool mousemove "$COMBO_X" "$COMBO_Y" click 1; sleep 1.2; }

# a. collapsed: the chip hugs "glm-5.3 (main)"; the share button is in the chrome row
shot a-collapsed root
[[ ${PROBE_ONLY:-} == 1 ]] && { echo "probe shot written"; exit 0; }

# b. the flat list: role rows first, then the gear (guest rows only when a guest is on PATH)
open_list
shot b-list-open root
# c. Main -> Flash (the collapsed box sits on the live row; Down is the flash row)
k Down Return; sleep 1.5
shot c-flash
# d. Flash -> Main (the box now sits on the flash row; Up is the main row)
open_list
k Up Return; sleep 1.5
shot d-main

# e. hovering the chrome row's share button shows its tooltip
xdotool mousemove "$SHARE_X" "$SHARE_Y"; sleep 2.2
shot e-share-tooltip root
# f. clicking it opens the share dialog, as the strip's chip did
xdotool mousemove "$SHARE_X" "$SHARE_Y" click 1; sleep 10
dlg=$(xdotool search --onlyvisible --name "Share this pane" | head -1)
[[ -n $dlg ]] && import -window "$dlg" "$out/implementer-f-share-dialog.png"
url=$(cat "$RELAY_REMOTE_PAIR_FILE" 2>/dev/null)
echo "pairing url: ${url:0:60}${url:+...}"
# g. sharing is on: the dialog closed, the chrome row's share button is the agent's violet
[[ -n $dlg ]] && { xdotool windowfocus "$dlg"; k Escape; sleep 1.5; }
xdotool windowfocus "$win"
shot g-share-lit root
printf 'done: %s\n' "$out"
