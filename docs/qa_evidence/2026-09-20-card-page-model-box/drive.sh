#!/usr/bin/env bash
# The Switchboard card page's model box (#BRD3), for the owner's 2026-09-20 report: "did we lose
# the model picker in the switchboard agent … it's the one in the cards, it's different and doesn't
# have the model picker." Drives a real Relay under Xvfb on a one-card fixture board: open the
# Switchboard, open the card, shoot the reply strip with the box on it, open its list, then squeeze
# the window until the pane is ~350 px and shoot the row again.
#
# Isolated HOME/XDG/TMPDIR under a short path (the 108-byte socket limit), RELAY_KEYRING=off so a
# run never touches the owner's real identity key.
set -uo pipefail
root=/home/elliott/repos/relay-terminal
# $RELAY_BIN is the binary to drive; the shots here were taken with the one land.py built from the
# exact tree it committed (/tmp/claude-1000/land/<me>/verify/build/relay), because this shared
# checkout's build/relay carries other sessions' in-flight edits.
bin=${RELAY_BIN:-$root/build/relay}
out=${1:-$root/docs/qa_evidence/2026-09-20-card-page-model-box}
width=1440 height=900
narrow=${NARROW:-730}          # window width at which each of the two panes is ~355 px
mkdir -p "$out"

display=
for n in $(seq 170 199); do [[ -e /tmp/.X11-unix/X$n ]] || { display=:$n; break; }; done
[[ -z $display ]] && { echo "no free display"; exit 1; }

sandbox=$(mktemp -d /tmp/rl-cmb.XXXX)
xvfb_pid= relay_pid=
cleanup() { local pid; for pid in $relay_pid $xvfb_pid; do kill "$pid" 2>/dev/null; done; rm -rf "$sandbox"; }
trap cleanup EXIT

Xvfb "$display" -screen 0 $((width + 40))x$((height + 40))x24 >/dev/null 2>&1 &
xvfb_pid=$!
sleep 2
export DISPLAY=$display RELAY_KEYRING=off
mkdir -p "$sandbox/home/.config/RelayTerminal" "$sandbox/home/.local/share" "$sandbox/run" "$sandbox/tmp"
chmod 700 "$sandbox/run"
ln -sfn "/run/user/$(id -u)/bus" "$sandbox/run/bus"
export HOME=$sandbox/home XDG_RUNTIME_DIR=$sandbox/run TMPDIR=$sandbox/tmp
export XDG_CONFIG_HOME=$HOME/.config XDG_DATA_HOME=$HOME/.local/share XDG_CACHE_HOME=$HOME/.cache
printf '[instructions]\nonboarded=true\n' >"$XDG_CONFIG_HOME/RelayTerminal/relay.conf"

work=$HOME/project
mkdir -p "$work"
PYTHONPATH="$root/backend" python3 - "$work" <<'FIX'
import sys
from pathlib import Path
from relay_core import board as B
work = Path(sys.argv[1]); root = work / "issues"; root.mkdir(parents=True)
(root / B.BOARD_CONFIG).write_text(
    "tabs: [{id: features, folder: features}, {id: bugs, folder: changes}]\n"
    "columns: [inbox, discussing, ready, in-progress, waiting, needs-qa, done]\n", encoding="utf-8")
b = B.Board(root, work)
c = B.new_card("work", "Alpha plain card", "inbox", created="2026-09-01", rank="c",
               request="a plain card", labels=["feature"])
B.write_new_card(b, c, "features")
FIX

(cd "$work" && exec "$bin" --workspace "$work" --fresh) >"$sandbox/relay.log" 2>&1 &
relay_pid=$!
sleep 9
win= ; best=0
for candidate in $(xdotool search --pid "$relay_pid" 2>/dev/null); do
    eval "$(xdotool getwindowgeometry --shell "$candidate" 2>/dev/null)"
    (( WIDTH * HEIGHT > best )) && { best=$(( WIDTH * HEIGHT )); win=$candidate; }
done
[[ -z $win ]] && { echo "no Relay window"; tail -40 "$sandbox/relay.log"; exit 1; }
xdotool windowmove "$win" 0 0 windowsize "$win" "$width" "$height"; xdotool windowfocus "$win"; sleep 6
xdotool mousemove 906 513 click 1; sleep 2         # dismiss a first-run approvals pane
xdotool key --window "$win" ctrl+shift+s; sleep 8  # the Switchboard

# Open the one card. The sections come up folded and the focus is not on the list, so click the
# INBOX header to unfold it and focus the list, then click the card's row.
xdotool mousemove 900 245 click 1; sleep 2
import -window root "$out/00-list.png"
xdotool mousemove 900 283 click 1; sleep 4
import -window root "$out/01-card-page.png"

# The box itself, on the reply strip left of Plan / Execute. The coordinates are read off
# 01-card-page.png: the strip is at the bottom of the card page, the box just left of "Plan".
box_x=${BOX_X:-1120} box_y=${BOX_Y:-835}
xdotool mousemove "$box_x" "$box_y" click 1; sleep 3
import -window root "$out/02-picker-open.png"
xdotool key --window "$win" Escape; sleep 2

# ~350 px of pane: the list page is hidden and the card has the whole width, which is where the
# three buttons would be clipped if the box did not give its width back. The *window* is left
# window holds two panes side by side, so it is half as wide as the pane is meant to be.
xdotool windowsize "$win" "$narrow" "$height"; sleep 5
import -window root "$out/03-narrow-pane.png"
echo "shots in $out"
