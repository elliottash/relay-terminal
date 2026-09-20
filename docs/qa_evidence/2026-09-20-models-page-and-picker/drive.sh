#!/usr/bin/env bash
# The model picker and Options › Models (owner, 2026-09-20): one catalog behind the pane's model
# box, the Ctrl+Shift+M picker and the Models page. Implementer screenshots under Xvfb with an
# isolated profile.
#
#   docs/qa_evidence/2026-09-20-models-page-and-picker/drive.sh [build-dir]
#
#   implementer-a-box-open.png    the model box open: role rows, then one row per catalog model
#                                 in rank order, lower-case, then "more models…" and "customize…"
#   implementer-b-picker.png      Ctrl+Alt+M: the picker — filter, sort menu, columns, the
#                                 reasoning buttons for the highlighted row, use / cancel
#   implementer-c-picker-filter.png  "flash" typed: one flat list of the rows that match
#   implementer-d-picker-sort.png the sort menu open
#   implementer-e-options.png     /models: Options › Models — providers, the checklist, priority
#   implementer-g-options-checklist.png the checklist group, with the per-model "continue on
#                                 openrouter" switch under a model OpenRouter also serves
#   implementer-h-options-priority.png the priority group: ranks with ↑ ↓ and the movable
#                                 'fallbacks end here' line after rank 2
#   implementer-f-options-down.png the same page scrolled to the end of the priority list and defaults
#
# No provider account and no network turn: two literal non-key strings in the environment make
# two presets "stored", and nothing is ever submitted. Needs Xvfb, xdotool, ImageMagick.
set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"
out=$PWD
root=$(cd ../../.. && pwd)
build=${1:-$root/build}
width=1400 height=880

display=
for n in $(seq 200 230); do [[ -e /tmp/.X11-unix/X$n ]] || { display=:$n; break; }; done
[[ -z $display ]] && { echo "no free display"; exit 1; }

sandbox=${RELAY_SHOT_HOME:-/tmp/relay-models-shots}
xvfb_pid= relay_pid=
cleanup() { kill "${relay_pid:-0}" "${xvfb_pid:-0}" 2>/dev/null; rm -rf "$sandbox"; }
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

shot() {   # shot <name>: the whole screen, so popups and dialogs are in the picture
    sleep 0.8
    xdotool mousemove $((width + 20)) $((height + 20)); sleep 0.3
    import -window root "$out/implementer-$1.png"
}

"$build/relay" --workspace "$work" >"$out/relay-stderr.log" 2>&1 &
relay_pid=$!
sleep 8
largest_window
[[ -z $win ]] && { echo "no Relay window"; exit 1; }
xdotool windowmove "$win" 0 0 windowsize "$win" $width $height
xdotool windowfocus "$win"; sleep 3

# The model box sits in the composer's right-hand chip strip (read off a probe of this layout).
COMBO_X=${COMBO_X:-1180} COMBO_Y=${COMBO_Y:-845}   # the model box; the level box sits right of it
[[ ${PROBE_ONLY:-} == 1 ]] && { shot probe; echo "probe shot written"; exit 0; }

# a. the box open
xdotool mousemove "$COMBO_X" "$COMBO_Y" click 1; sleep 1.5
shot a-box-open
k Escape; sleep 0.5

# b. the picker
k ctrl+alt+m; sleep 2
shot b-picker
# c. filtered
xdotool type --delay 80 "flash"; sleep 1
shot c-picker-filter
k ctrl+a BackSpace; sleep 0.5
# d. the sort menu
dlg=$(xdotool search --onlyvisible --name "^model$" | head -1)
if [[ -n $dlg ]]; then
    eval "$(xdotool getwindowgeometry --shell "$dlg")"
    xdotool mousemove $((X + WIDTH - 60)) $((Y + 28)) click 1; sleep 1.2
    shot d-picker-sort
    k Escape; sleep 0.4
fi
k Escape; sleep 1

# e. Options › Models via /models (the prompt box takes the focus back first)
xdotool mousemove 300 812 click 1; sleep 0.6
xdotool type --delay 60 "/models"; sleep 0.5; k Return; sleep 3
shot e-options
# g. the checklist group: each model's row and, under one OpenRouter also serves, the opt-in switch
for _ in 1 2 3; do k Page_Down; done; sleep 1
shot g-options-checklist
# h. the priority group: ranks with ↑ ↓, the fallback line after rank 2
for _ in 1 2 3; do k Page_Down; done; sleep 1
shot h-options-priority
# f. scrolled down to priority and defaults
for _ in 1 2 3 4 5 6; do k Page_Down; done; sleep 1
shot f-options-down

echo "shots written to $out"
