#!/usr/bin/env bash
# The machine pass for #0FBB: stage.sh under an Xvfb, then drive the pane to the state the card
# describes and screenshot it. The tab attaches to the project through the Board (`open`), the
# pane's worker configures on its first prompt, and ` #` in the prompt box loads the card index —
# which is what makes the claims chip appear. Screenshots land beside this script.
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
work=/tmp/claude-1000/tryit/0fbb
env -u DISPLAY "$here/stage.sh"
export DISPLAY=$(sed -n 's/.*DISPLAY=\(:[0-9]*\).*/\1/p' <<<"$(ps -o args= -p "$(cat "$work/xvfb.pid")")" | head -1)
[[ -n $DISPLAY ]] || DISPLAY=$(ps -o args= -p "$(cat "$work/xvfb.pid")" | awk '{print $2}')
export HOME=$work/sandbox/home XDG_RUNTIME_DIR=$work/sandbox/run
unset RELAY_OPEN_SOCKET
repo=$(cd "$here/../../.." && pwd)
drive() { python3 "$repo/scripts/relay-drive" "$@"; }
shot() { import -window root "$here/$1.png"; }

drive open '#K7Q2' >/dev/null            # attaches the tab to the project and opens the card
sleep 3
xdotool mousemove 120 735 click 1; sleep 0.3               # the terminal pane's prompt box
xdotool type --delay 60 'look at #K7Q2'; xdotool key Return   # the first prompt configures the worker
sleep 10
xdotool mousemove 120 735 click 1; sleep 0.3
xdotool type --delay 80 ' #'; sleep 4      # the picker asks for the card index
xdotool key Escape; sleep 0.3; xdotool key ctrl+a BackSpace; sleep 1
shot 03-narrow-pane-full                   # beside the Board: the chip, then the title on two lines
xdotool mousemove 1229 58 click 1; sleep 1.5               # close the Board pane
shot 01-header-chip-full                   # "#K7Q2 (2)  Log in to service"
xdotool mousemove 75 57 click 1; sleep 1.5                 # the chip
shot 02-chip-menu-full                     # both claims, latest first, with their stage
xdotool key Escape
convert "$here/01-header-chip-full.png" -crop 1260x60+0+38 +repage "$here/01-header-chip.png"
convert "$here/02-chip-menu-full.png" -crop 1260x110+0+38 +repage "$here/02-chip-menu.png"
convert "$here/03-narrow-pane-full.png" -crop 450x60+0+38 +repage "$here/03-narrow-pane.png"
rm -f "$here"/0[13]-*-full.png
mv "$here/02-chip-menu-full.png" "$here/04-full-window-menu.png"
"$here/stage.sh" --stop
