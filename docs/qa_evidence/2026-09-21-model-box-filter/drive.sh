#!/usr/bin/env bash
# The model box and the level box, driven live (owner, 2026-09-21: "when you use alt+m or alt+e,
# your current selection should be highlighted. then you should be able to select with up/down
# arrows, and also filter with text typing (like warp's model picker)").
#
#   docs/qa_evidence/2026-09-21-model-box-filter/drive.sh [build-dir]
#
#   a-strip.png            the composer strip before anything is opened
#   b-altm-open.png        Alt+M: the list, with the pane's current row highlighted and the
#                          filter line above it
#   c-down-down.png        after Down Down: the highlight has moved, stepping over a separator
#   d-typed-kimi.png       after typing "kimi": the typed text is on screen and the rows are
#                          filtered to what matches
#   e-picked.png           after Enter: the collapsed box names the model that was picked
#   f-no-match.png         a filter nothing matches: a quiet "no match" line, not an empty box
#   g-alte-open.png        Alt+E: the level list, with the pane's current level highlighted
#   h-alte-typed.png       Alt+E with "hi" typed: the same filter over the four levels
#   i-escaped.png          Escape, then typing: the words land in the prompt box, not in the
#                          model box that was just open
#   j-mouse-open.png       the same list opened by clicking the box, current row highlighted
#   k-altm-again.png       Alt+M pressed a second time closes the list again (owner, 2026-09-20)
#
# No provider account and no network turn: two literal non-key strings make two presets read as
# stored, and nothing is ever submitted. `isolation/enabled=false` is only so the worker starts at
# all under an isolated XDG_RUNTIME_DIR, where systemd --user cannot be reached and the transient
# scope fails — with no worker there is no model catalog and the list has nothing to filter.
# Needs Xvfb, xdotool, ImageMagick.
set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"
out=$PWD
root=$(cd ../../.. && pwd)
build=${1:-$root/build}
width=1400 height=880

display=
for n in $(seq 340 370); do [[ -e /tmp/.X11-unix/X$n ]] || { display=:$n; break; }; done
[[ -z $display ]] && { echo "no free display"; exit 1; }

# Short paths: the XDG runtime dir holds sockets and a sun_path is 108 bytes.
sandbox=${RELAY_SHOT_HOME:-/tmp/claude-1000/am}
xvfb_pid= relay_pid=
cleanup() { kill "${relay_pid:-0}" "${xvfb_pid:-0}" 2>/dev/null; sleep 1; rm -rf "$sandbox"; }
trap cleanup EXIT

Xvfb "$display" -screen 0 $((width + 40))x$((height + 40))x24 >/dev/null 2>&1 &
xvfb_pid=$!
sleep 2
kill -0 "$xvfb_pid" 2>/dev/null || { echo "Xvfb died on $display"; exit 1; }
export DISPLAY=$display
export RELAY_KEYRING=off
export RELAY_GLM_CODING_API_KEY=xvfb-not-a-real-key
export RELAY_KIMI_CODE_API_KEY=xvfb-not-a-real-key-either

k() { xdotool key --delay 60 "$@"; }

rm -rf "$sandbox"
export HOME=$sandbox
export XDG_CONFIG_HOME=$HOME/.config XDG_DATA_HOME=$HOME/.local/share XDG_CACHE_HOME=$HOME/.cache
export XDG_RUNTIME_DIR=$sandbox/run TMPDIR=$sandbox/tmp
work=$HOME/project
mkdir -p "$XDG_CONFIG_HOME/RelayTerminal" "$XDG_DATA_HOME" "$XDG_CACHE_HOME" "$XDG_RUNTIME_DIR" "$TMPDIR" "$work"
chmod 700 "$XDG_RUNTIME_DIR"
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
[security]
approvals_chosen=true
approvals_ask=@Invalid()
[isolation]
enabled=false
CONF

largest_window() {
    win= ; local best=0 w
    for w in $(xdotool search --pid "$relay_pid" 2>/dev/null); do
        eval "$(xdotool getwindowgeometry --shell "$w" 2>/dev/null)"
        (( WIDTH * HEIGHT > best )) && { best=$(( WIDTH * HEIGHT )); win=$w; }
    done
}

shot() { sleep 0.8; xdotool mousemove $((width + 20)) $((height + 20)); sleep 0.3; import -window root "$out/$1.png"; }

"$build/relay" --workspace "$work" >"$out/relay-stderr.log" 2>&1 &
relay_pid=$!
sleep 9
largest_window
[[ -z $win ]] && { echo "no Relay window"; exit 1; }
xdotool windowmove "$win" 0 0 windowsize "$win" $width $height
xdotool windowfocus "$win"; sleep 3
shot a-strip

# Alt+M: the current row is the highlighted one, and there is a filter line above the list.
k alt+m; sleep 1.6
shot b-altm-open
k Down; sleep 0.4; k Down; sleep 0.6
shot c-down-down
# Typing filters, and what was typed is on the screen.
xdotool type --delay 90 "kimi"; sleep 1.2
shot d-typed-kimi
k Return; sleep 1.6
shot e-picked

# A filter nothing matches says so rather than showing an empty box.
k alt+m; sleep 1.4
xdotool type --delay 90 "zzzq"; sleep 1.2
shot f-no-match
k Escape; sleep 0.8

# Alt+E: the same list over the reasoning levels, the pane's own level highlighted.
k alt+e; sleep 1.4
shot g-alte-open
xdotool type --delay 90 "hi"; sleep 1.0
shot h-alte-typed
k Escape; sleep 0.8
# Escape left the caret in the prompt box: what is typed next lands there.
xdotool type --delay 60 "back in the prompt box"; sleep 1.0
shot i-escaped
k ctrl+a; k BackSpace; sleep 0.5

# Alt+M again closes the list, as it has since 2026-09-20: the chord still reaches the pane.
k alt+m; sleep 1.2
k alt+m; sleep 1.0
shot k-altm-again

# And by mouse, which is how the box has always been opened: the same list, the same highlighted
# row, and the row under the pointer takes the highlight as the pointer moves.
xdotool mousemove 1180 845 click 1; sleep 1.4
import -window root "$out/j-mouse-open.png"
k Escape; sleep 0.5

echo "shots written to $out"
