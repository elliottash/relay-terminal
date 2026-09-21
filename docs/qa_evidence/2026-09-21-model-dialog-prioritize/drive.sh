#!/usr/bin/env bash
# The Ctrl+Alt+M dialog as the place models are picked *and* prioritized (card #MDL1 t:a7; owner,
# 2026-09-21: "the model priority chooser is crtical, and currently its too hard to find -- model
# options, then scroll down. i think we should beef up the ctrl alt m dialogue to be the main way
# to select / prioritize models"). Under Xvfb with an isolated profile: fill the five lists, open
# the dialog, walk the tabs, reorder, add, remove, undo, fold three providers into one row, and
# find the door to it at the top of Options › Models.
#
#   docs/qa_evidence/2026-09-21-model-dialog-prioritize/drive.sh [build-dir]
#
#   0-filled.png      Options › Models with the five lists filled, so the tabs have something in them
#   1-main.png        the dialog on the main tab: numbered, "via", rank 1 "new panes start here"
#   2-flash.png       one Right: the flash list, in its own order
#   3-moved.png       Alt+Down on rank 1 — the row and "new panes start here" both moved
#   4-not-in-list.png typing searches every model: this list's matches, then "not in this list"
#   5-added.png       Ctrl+Enter put it at the end of the list
#   6-deleted.png     Delete took a row out, no confirmation
#   7-undone.png      Ctrl+Z put it back, where it was, with its level
#   8-all.png         the all tab: one row per model, "via … +1" where two presets serve one model
#   9-via.png         → opened that row's providers beside the levels
#   10-options.png    Options › Models: "prioritize models… (Ctrl+Alt+M)" as its first row
#
# No provider account and no network turn: three literal non-key strings make three presets
# "stored" (glm and glm-coding serve the same model on purpose — that is the folded row), and
# nothing is ever submitted. Needs Xvfb, xdotool, ImageMagick.
set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"
out=$PWD
root=$(cd ../../.. && pwd)
build=${1:-$root/build}
width=1400 height=880

display=
for n in $(seq 420 450); do [[ -e /tmp/.X11-unix/X$n ]] || { display=:$n; break; }; done
[[ -z $display ]] && { echo "no free display"; exit 1; }

# Short paths: the XDG runtime dir holds sockets and a sun_path is 108 bytes (the
# xvfb-test-isolation note). Isolating it keeps this run out of the owner's live session.
sandbox=${RELAY_SHOT_HOME:-/tmp/claude-1000/dg}
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
export RELAY_GLM_API_KEY=xvfb-not-a-real-key-two
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
# isolation off: the worker's sandbox refuses to start under a fake XDG_RUNTIME_DIR, and with no
# worker there is no catalog and the dialog has nothing to show.
cat >"$XDG_CONFIG_HOME/RelayTerminal/relay.conf" <<CONF
[instructions]
onboarded=true
[provider]
preset=glm-coding
[isolation]
enabled=false
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

shot() { sleep 0.8; xdotool mousemove $((width + 20)) $((height + 20)); sleep 0.3; import -window root "$out/$1.png"; }

"$build/relay" --workspace "$work" >"$out/relay-stderr.log" 2>&1 &
relay_pid=$!
sleep 10
largest_window
[[ -z $win ]] && { echo "no Relay window"; exit 1; }
xdotool windowmove "$win" 0 0 windowsize "$win" $width $height
xdotool windowfocus "$win"; sleep 3

prompt() { xdotool mousemove 300 812 click 1; sleep 0.6; }

# ----- the lists, so the tabs have something in them -----------------------------------------
prompt
xdotool type --delay 60 "/models"; sleep 0.5; k Return; sleep 6
xdotool mousemove 1040 110 click 1; sleep 0.5
xdotool type --delay 60 "fill the lists"; sleep 2.5
xdotool mousemove 769 248 click 1; sleep 3
shot 0-filled
k ctrl+shift+m; sleep 2            # close the Options pane again

# ----- the dialog ------------------------------------------------------------------------------
prompt
k ctrl+alt+m; sleep 3
shot 1-main
k Right; sleep 1.5                 # → the flash list
shot 2-flash
k Left; sleep 1.5                  # back to main
k alt+Down; sleep 1.5              # rank 1 becomes rank 2
shot 3-moved
k alt+Up; sleep 1.2                # put it back before the rest of the run

xdotool type --delay 60 "flash"; sleep 2
shot 4-not-in-list
k ctrl+Return; sleep 2             # the first "not in this list" row is already the highlighted one
shot 5-added
k Delete; sleep 1.5
shot 6-deleted
k ctrl+z; sleep 1.5
shot 7-undone

# ----- the flat tab, one row per model ---------------------------------------------------------
k ctrl+Tab; sleep 1; k ctrl+Tab; sleep 1; k ctrl+Tab; sleep 1.5   # main → flash → lite → all
shot 8-all
xdotool type --delay 60 "glm-5.3"; sleep 1.5
# ←/→ in the filter are the tabs', so Tab is what hands the focus to the rows; → then opens the
# providers of a row two presets serve ("via … +1").
k Tab; sleep 0.8
k Right; sleep 1.5
shot 9-via
k Escape; sleep 2

# ----- the door on Options › Models ------------------------------------------------------------
prompt
xdotool type --delay 60 "/models"; sleep 0.5; k Return; sleep 5
shot 10-options

echo "shots written to $out"
