#!/usr/bin/env bash
# Live check of the pane button row's tile and outline (#0T2R) under Xvfb. The row is permanent, so
# there is no hover state of the row itself to capture: what the shots have to show is that the
# three buttons sit on a raised, outlined tile at rest, in a dark theme and in a light square one,
# and that a hovered button still reads as lifted now that it can no longer take a ground of its own.
#
#   docs/qa_evidence/2026-09-18-pane-buttons-brighter-outline/drive.sh <build-dir> <prefix>
#   RELAY_QA_DISPLAY=:91 ... drive.sh /tmp/outline-qa/build implementer-after
#
# Run it twice against two builds — one at HEAD, one carrying the change — to get the before and
# after pairs. Writes <prefix>-*.png next to this script. Needs Xvfb, xdotool and ImageMagick.
set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"
out=$PWD
build=${1:?build dir}
prefix=${2:-implementer}
display=${RELAY_QA_DISPLAY:-:91}
width=1400 height=820

export XDG_CONFIG_HOME=$(mktemp -d) XDG_DATA_HOME=$(mktemp -d) XDG_CACHE_HOME=$(mktemp -d)
work=$(mktemp -d)
mkdir -p "$XDG_CONFIG_HOME/RelayTerminal"
cat >"$XDG_CONFIG_HOME/RelayTerminal/relay.conf" <<'CONF'
[instructions]
onboarded=true
[terminal]
shell_integration=true
CONF
trap 'kill "${relay_pid:-0}" "${xvfb_pid:-0}" 2>/dev/null; rm -rf "$XDG_CONFIG_HOME" "$XDG_DATA_HOME" "$XDG_CACHE_HOME" "$work"' EXIT

Xvfb "$display" -screen 0 ${width}x${height}x24 >/dev/null 2>&1 &
xvfb_pid=$!
sleep 1
# Another agent may already own that display number; a leaked run would type into their session.
if ! DISPLAY=$display xdotool getdisplaygeometry >/dev/null 2>&1 || ! kill -0 "$xvfb_pid" 2>/dev/null; then
    echo "display $display is not ours (set RELAY_QA_DISPLAY to a free one)"; exit 1
fi
export DISPLAY=$display

shot() { import -window root "$out/$prefix-$1.png"; }
# The button row sits at the top right of the pane; crop it large so the outline is countable.
# The band is the same in every shot, so before and after crop identically and can be compared.
crop() { convert "$out/$prefix-$1.png" -crop 150x40+1248+38 +repage -scale 400% "$out/$prefix-crop-$1.png"; }
t() { xdotool type --delay 12 "$1"; }
k() { xdotool key --delay 40 "$@"; }

RELAY_KEYRING=off "$build/relay" --workspace "$work" >"$out/$prefix-relay-stderr.log" 2>&1 &
relay_pid=$!
sleep 8
win=$(xdotool search --pid "$relay_pid" --name Relay | tail -1)
[[ -z $win ]] && { echo "no Relay window"; exit 1; }
xdotool windowmove "$win" 0 0 windowsize "$win" $width $height
xdotool windowfocus "$win"
xdotool mousemove 700 600      # pointer well away from every button row
sleep 2

# ----- one pane, the default dark theme ----------------------------------------------------------
shot 01-one-pane-dark
crop 01-one-pane-dark

# ----- two panes: both rows look the same, and neither reacts to the pointer ----------------------
k ctrl+e; sleep 7
xdotool mousemove 700 600; sleep 1.5
shot 03-two-panes-pointer-away
xdotool mousemove 350 400; sleep 1.5          # pointer inside the LEFT pane's body
shot 04-two-panes-pointer-in-left-pane

# ----- a hovered button: it lifts by ink and outline, not by a ground --------------------------
xdotool mousemove 1337 46; sleep 1.5          # the right pane's × button
shot 05-pointer-on-the-close-button
crop 05-pointer-on-the-close-button

# ----- IBM Beige: a light, square-cornered theme -------------------------------------------------
xdotool mousemove 700 600; sleep 1
t '/theme ibm-beige'; k Return; sleep 4
shot 07-one-pane-ibm-beige
crop 07-one-pane-ibm-beige

# 1b270ef's acceptance, which this change must not undo: the rows do not react to the pointer.
for s in 03-two-panes-pointer-away 04-two-panes-pointer-in-left-pane; do
    convert "$out/$prefix-$s.png" -crop 1400x40+0+38 +repage "$work/row-$s.png"
done
echo -n "$prefix button rows, pointer away vs pointer in a pane: "
compare -metric AE "$work/row-03-two-panes-pointer-away.png" "$work/row-04-two-panes-pointer-in-left-pane.png" null: 2>&1
echo " differing pixels"
echo "screenshots in $out ($prefix-*)"
