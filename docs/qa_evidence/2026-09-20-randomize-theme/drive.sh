#!/usr/bin/env bash
# Options › Appearance › Randomize, and /theme random (card #R4ND). Implementer screenshots under
# Xvfb with an isolated profile: the button on the page, the window after each press, the picker's
# Random row, and the command.
#
#   docs/qa_evidence/2026-09-20-randomize-theme/drive.sh [build-dir]
#
#   implementer-a-start.png        the window as it opens, on the default theme (Dark Copper)
#   implementer-b-appearance.png   Options › Appearance: the Randomize row under Theme
#   implementer-c-search.png       the same page, searched for "randomize" (the row's aliases)
#   implementer-d-press-1.png      after one press: another theme, and the notice naming it
#   implementer-e-press-2.png      after a second press: a different theme again
#   implementer-f-picker.png       /theme: the picker, with Random as its last row
#   implementer-g-command.png      /theme random: the same switch from the prompt box
#
# No provider account and no network turn. Needs Xvfb, xdotool, ImageMagick.
set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"
out=$PWD
root=$(cd ../../.. && pwd)
build=${1:-$root/build}
width=1400 height=880

display=
for n in $(seq 240 270); do [[ -e /tmp/.X11-unix/X$n ]] || { display=:$n; break; }; done
[[ -z $display ]] && { echo "no free display"; exit 1; }

sandbox=${RELAY_SHOT_HOME:-/tmp/rnd-shots}
xvfb_pid= relay_pid=
cleanup() { kill "${relay_pid:-0}" "${xvfb_pid:-0}" 2>/dev/null; rm -rf "$sandbox"; }
trap cleanup EXIT

Xvfb "$display" -screen 0 $((width + 40))x$((height + 40))x24 >/dev/null 2>&1 &
xvfb_pid=$!
sleep 2
kill -0 "$xvfb_pid" 2>/dev/null || { echo "Xvfb died on $display"; exit 1; }
export DISPLAY=$display
export RELAY_KEYRING=off

k() { xdotool key --delay 60 "$@"; }

rm -rf "$sandbox"
export HOME=$sandbox
export XDG_CONFIG_HOME=$HOME/.config XDG_DATA_HOME=$HOME/.local/share XDG_CACHE_HOME=$HOME/.cache
export XDG_RUNTIME_DIR=$HOME/run TMPDIR=$HOME/tmp
work=$HOME/project
mkdir -p "$XDG_CONFIG_HOME/RelayTerminal" "$XDG_DATA_HOME" "$XDG_CACHE_HOME" "$XDG_RUNTIME_DIR" "$TMPDIR" "$work"
chmod 700 "$XDG_RUNTIME_DIR"
printf "PS1='\\\\w \\$ '\nunset PROMPT_COMMAND\n" >"$HOME/.bashrc"
printf '# project\n' >"$work/README.md"
cat >"$XDG_CONFIG_HOME/RelayTerminal/relay.conf" <<CONF
[instructions]
onboarded=true
[suggestions]
next_command=false
next_prompt=false
[security]
approvals_chosen=true
approvals_ask=@Invalid()
CONF

largest_window() {
    win= ; local best=0 w
    for w in $(xdotool search --pid "$relay_pid" 2>/dev/null); do
        eval "$(xdotool getwindowgeometry --shell "$w" 2>/dev/null)"
        (( WIDTH * HEIGHT > best )) && { best=$(( WIDTH * HEIGHT )); win=$w; }
    done
}

shot() {   # the whole screen, so popups and dialogs are in the picture
    sleep 0.8
    xdotool mousemove $((width + 20)) $((height + 20)); sleep 0.3
    import -window root "$out/implementer-$1.png"
}

theme_now() { grep -a '^name=' "$XDG_CONFIG_HOME/RelayTerminal/relay.conf" 2>/dev/null; }

"$build/relay" --workspace "$work" >"$out/relay-stderr.log" 2>&1 &
relay_pid=$!
sleep 8
largest_window
[[ -z $win ]] && { echo "no Relay window"; exit 1; }
xdotool windowmove "$win" 0 0 windowsize "$win" $width $height
xdotool windowfocus "$win"; sleep 3

shot a-start

# Options (Ctrl+Shift+O) › Appearance. The search box has the focus, so Right walks the tabs.
k ctrl+shift+o; sleep 3
k Right; sleep 1.5
shot b-appearance

# The row answers to "randomize" although its label is "Randomize" and its aliases carry the rest.
xdotool type --delay 80 "randomize"; sleep 1.5
shot c-search

# Enter on the highlighted row presses its button. Twice, for two different themes.
k Down; sleep 0.5
k Return; sleep 2
echo "after press 1: $(theme_now)" | tee -a "$out/themes.txt"
shot d-press-1
k Return; sleep 2
echo "after press 2: $(theme_now)" | tee -a "$out/themes.txt"
shot e-press-2

# Back to the prompt box: the picker, then the command.
k Escape; sleep 0.5; k Escape; sleep 1.5
xdotool mousemove 300 812 click 1; sleep 0.8
xdotool type --delay 60 "/theme"; sleep 0.5; k Return; sleep 2.5
shot f-picker
k Escape; sleep 1.5
xdotool mousemove 300 812 click 1; sleep 0.8
xdotool type --delay 60 "/theme random"; sleep 0.5; k Return; sleep 2.5
echo "after /theme random: $(theme_now)" | tee -a "$out/themes.txt"
shot g-command

echo "shots written to $out"
