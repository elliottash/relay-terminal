#!/usr/bin/env bash
# The board's list page carries its own tools (#EVW1) and a checkbox per section (#T7BQ), live
# under Xvfb + xdotool.
#
#   docs/qa_evidence/2026-09-18-board-list-page-tools/drive.sh [build-dir]
#
# Writes implementer-NN-*.png next to this script plus relay-stderr.log. An isolated
# XDG_CONFIG_HOME/XDG_DATA_HOME/XDG_CACHE_HOME/XDG_RUNTIME_DIR keeps the run out of the real
# profile, and the workspace is a throwaway git repo holding a *copy* of this repository's issues/
# tree, so the repository's own cards are never touched and no provider key is used.
#
# The click co-ordinates below are for a 1600x950 window on this screen; a different font or theme
# moves them. Every shot is `import -window <id>` with the pointer parked in the title bar: a root
# grab comes back black on this host, and a pointer left over a row raises a tooltip that a
# window grab paints as a black box.
set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"
out=$PWD
root=$(cd ../../.. && pwd)
build=${1:-$root/build}

display=
for n in $(seq 140 199); do [[ -e /tmp/.X11-unix/X$n ]] || { display=:$n; break; }; done
[[ -z $display ]] && { echo "no free X display"; exit 1; }
echo "display $display"

export XDG_CONFIG_HOME=$(mktemp -d) XDG_DATA_HOME=$(mktemp -d) XDG_CACHE_HOME=$(mktemp -d)
export XDG_RUNTIME_DIR=$(mktemp -d) RELAY_KEYRING=off
chmod 700 "$XDG_RUNTIME_DIR"
work=$(mktemp -d)
mkdir -p "$XDG_CONFIG_HOME/RelayTerminal"
printf '[instructions]\nonboarded=true\n' >"$XDG_CONFIG_HOME/RelayTerminal/relay.conf"
trap 'kill "${relay_pid:-0}" "${xvfb_pid:-0}" 2>/dev/null; rm -rf "$XDG_CONFIG_HOME" "$XDG_DATA_HOME" "$XDG_CACHE_HOME" "$XDG_RUNTIME_DIR" "$work"' EXIT

cp -r "$root/issues" "$work/issues"
( cd "$work" && git init -q . && git add -A >/dev/null 2>&1 &&
  git -c user.email=qa@example.com -c user.name=QA commit -qm cards >/dev/null 2>&1 )
echo "workspace $work ($(find "$work/issues" -name '*.md' | wc -l) card files)"

Xvfb "$display" -screen 0 1700x1000x24 >/dev/null 2>&1 &
xvfb_pid=$!
sleep 2
export DISPLAY=$display

"$build/relay" --workspace "$work" >"$out/relay-stderr.log" 2>&1 &
relay_pid=$!
sleep 13
win=$(xdotool search --onlyvisible --pid "$relay_pid" --name "^Relay" | head -1)
[[ -z $win ]] && { echo "no Relay window"; exit 1; }
echo "window $win"

park() { xdotool mousemove 1650 20; sleep 2; }
shot() { import -window "$win" "$out/implementer-$1.png"; }
wide() { xdotool windowmove "$win" 0 0 windowsize "$win" 1600 950; sleep 3; }

wide
xdotool windowfocus "$win"; xdotool mousemove 800 500; sleep 1
xdotool key --delay 60 ctrl+shift+s; sleep 6      # the board, split right of the terminal

park; shot 01-list-page-tools-wide
# The READY checkbox, second row of the tools block.
xdotool mousemove 978 91 click 1; sleep 2; park; shot 02-a-section-unticked
xdotool mousemove 978 91 click 1; sleep 2         # back on
# "Clean up": the notice on the board and the window's status bar.
xdotool mousemove 1367 62 click 1; sleep 2; park; shot 03-clean-up-not-wired-yet
# A card, in a pane too narrow (790 px) to keep the list beside it.
xdotool mousemove 1000 183 click 1; sleep 4; park; shot 04-open-card-back-to-board
# The same card with the board in a tab of its own (1600 px): the list and its tools stay.
xdotool mousemove 1534 60 click 1; sleep 4; park; shot 05-wide-pane-card-beside-the-list
# A ~415 px pane: the buttons drop to their own line and the checkboxes wrap.
xdotool mousemove 81 62 click 1; sleep 3          # ← Back to board
xdotool windowsize "$win" 420 950; sleep 3
xdotool mousemove 200 940; sleep 2; shot 06-narrow-pane-wrapping
xdotool mousemove 150 240 click 1; sleep 4        # the first card, in the narrow pane
xdotool mousemove 200 940; sleep 2; shot 07-narrow-pane-open-card
wide
echo "done"
