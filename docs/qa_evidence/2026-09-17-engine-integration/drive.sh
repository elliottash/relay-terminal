#!/usr/bin/env bash
# Live check of the per-pane terminal engine: one KonsolePart pane and one Relay-engine
# pane in the same window, under Xvfb + xdotool.
#
#   docs/qa_evidence/2026-09-17-engine-integration/drive.sh [build-dir]
#
# Writes NN-*.png next to this script plus relay-stderr.log. Needs Xvfb, xdotool and
# ImageMagick `import`. An isolated XDG_CONFIG_HOME/XDG_DATA_HOME keeps the run out of
# the real profile, so no provider keys exist in it.
set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"
out=$PWD
root=$(cd ../../.. && pwd)
build=${1:-$root/build}
display=:91

export XDG_CONFIG_HOME=$(mktemp -d) XDG_DATA_HOME=$(mktemp -d) XDG_CACHE_HOME=$(mktemp -d)
work=$(mktemp -d)
# No provider keys in this profile, and no first-run dialogs in the way.
mkdir -p "$XDG_CONFIG_HOME/RelayTerminal"
cat >"$XDG_CONFIG_HOME/RelayTerminal/relay.conf" <<'CONF'
[instructions]
onboarded=true
[terminal]
shell_integration=true
CONF
trap 'kill "${relay_pid:-0}" "${xvfb_pid:-0}" 2>/dev/null; rm -rf "$XDG_CONFIG_HOME" "$XDG_DATA_HOME" "$XDG_CACHE_HOME" "$work"' EXIT

seq 1 40000 | sed 's/^/relay engine flood line /' >"$work/big.txt"

Xvfb "$display" -screen 0 1400x900x24 >/dev/null 2>&1 &
xvfb_pid=$!
sleep 1
export DISPLAY=$display

shot() { import -window root "$out/$1.png"; }
# No window manager under Xvfb: put the window back at a known place before any click.
place() { xdotool windowmove "$win" 0 0 windowsize "$win" 1400 900; xdotool windowfocus "$win"; sleep 1; }
t() { xdotool type --delay 12 "$1"; }
k() { xdotool key --delay 40 "$@"; }

"$build/relay" --workspace "$work" >"$out/relay-stderr.log" 2>&1 &
relay_pid=$!
sleep 6
win=$(xdotool search --pid "$relay_pid" --name Relay | tail -1)
[[ -z $win ]] && { echo "no Relay window"; exit 1; }
xdotool windowmove "$win" 0 0 windowsize "$win" 1400 900
xdotool windowfocus "$win"
xdotool mousemove 400 300
sleep 2
shot 01-konsole-pane-only

# Palette -> "New pane (Relay engine)": splits right, so both engines share one window.
k ctrl+shift+a; sleep 1
t 'New pane (Relay'; sleep 1
shot 02-palette-new-engine-pane
k Return; sleep 5
shot 03-two-panes-konsole-left-engine-right

# The new pane has the focus. F12 hands the keyboard to the terminal (native input).
k F12; sleep 1
t 'ls -la --color=always'; k Return; sleep 1.5
t 'printf "CJK \xe6\xbc\xa2\xe5\xad\x97 | emoji \xf0\x9f\x8e\x89 | box \xe2\x94\x8c\xe2\x94\x80\xe2\x94\xac\xe2\x94\x80\xe2\x94\x90 | \033[1mbold\033[0m \033[38;5;208m256\033[0m\n"'; k Return; sleep 1
shot 04-engine-pane-ls

# Alternate screen: vim in the engine pane. Relay hides the composer for it.
t 'vim -u NONE -N engine.txt'; k Return; sleep 2.5
k i; t 'hello from the Relay engine'; sleep 0.5
shot 05-engine-pane-vim-altscreen
k Escape; sleep 0.3; t ':q!'; k Return; sleep 1.5
shot 06-engine-pane-after-vim

# Long output, then Ctrl+C while it runs.
t 'cat big.txt'; k Return; sleep 0.4
k ctrl+c; sleep 1.5
shot 07-engine-pane-cat-interrupted

# Resize the window with both panes open.
xdotool windowsize "$win" 1000 700; sleep 2
shot 08-resized-1000x700
xdotool windowsize "$win" 1400 900; sleep 2

# Select with the mouse in the engine pane and copy (Ctrl+C with a selection copies).
xdotool mousemove 900 300 mousedown 1 mousemove 1250 300 sleep 0.3 mouseup 1
sleep 0.5
k ctrl+c; sleep 0.8
shot 09-engine-pane-select-copy

# Back to the composer, then "New chat": the note is written straight into the emulator
# (writeToDisplay), not typed into the shell, and Readline redraws the prompt after it.
k F12; sleep 1
place
xdotool mousemove 290 27 click 1; sleep 2.5
shot 10-engine-pane-inline-agent-output

# A few commands through the composer (Relay's Bash bridge stages them with Readline).
place
xdotool mousemove 1000 800 click 1; sleep 0.5
t 'echo one; echo two; echo three'; k Return; sleep 2.5

# OSC 133 prompt marks from shell/relay-integration.bash: the palette's "Jump to previous
# prompt" scrolls the engine pane to the mark; without marks it says so in the status bar.
place
k ctrl+shift+a; sleep 1
t 'Jump to previous prompt'; sleep 1
k Return; sleep 1.5
shot 11-engine-pane-prompt-jump

# Back to the Konsole pane on the left: it must be unaffected.
xdotool mousemove 300 500 click 1; sleep 0.8
k F12; sleep 0.8
t 'echo konsole pane still fine; ls | head -3'; k Return; sleep 1.5
shot 12-konsole-pane-still-works

# Both panes side by side, final state.
sleep 1
shot 13-final-both-panes
echo "screenshots in $out"
