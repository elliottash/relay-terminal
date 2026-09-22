#!/usr/bin/env bash
# Card #MDL1 — the owner's review of the models pane (2026-09-21), driven live.
#
#   "tab 1: add horizontal line dividers between providers."
#   "check the advanced provider settings. not sure whats helpful or needed."
#   "for available, remove the recent section. i would order the sections alphabetically."
#   "it seems like i cant disable gemini flash lite. just to say -- this tab is only for terminal
#    agents, so gemini flash lite should be optional. and relay lite shouldnt show up."
#   "tab 3: in a pane, i dont want separate tabs for the modes. they should just be in divided
#    sections. remove the lite section."
#
# Xvfb, an isolated HOME/XDG_*/TMPDIR under a short path (the 108-byte unix-socket limit), fake
# provider keys and no network turn: every step is a pane draw, a tab or a key. `isolation/enabled
# =false` because the worker exits under a fake XDG_RUNTIME_DIR otherwise. RELAY_KEYRING=off so the
# owner's real identity key is never touched (memory: a direct run overwrites it).
#
# The profile is seeded with a **lite list that names gemini-3.5-flash-lite**, which is the owner's
# report: membership of that list used to pin the model available, so its tick could not be cleared.
# `conf-after.txt` is the proof of both halves — the tick cleared (`models/available` has no gemini
# row) and the lite list untouched.
#
#   a  the providers tab: a rule between every two providers
#   b  the same, scrolled to the bottom: "+ add provider" (the custom endpoint) and "keys from warp"
#   c  the key box: the consent sentences, where a key is actually entered
#   d  Options › Models: no "Advanced provider settings" row any more
#   e  the available tab: favorites, then a section per provider alphabetically, no "recent",
#      no relay-lite, and gemini-3.5-flash-lite ticked
#   f  …its tick cleared: the lite list no longer pins it
#   g  the priorities tab: one page, a section per class, no class tabs and no lite
#   h  typing in it: each section offers what it does not list
#   i  ctrl+enter on the row under the **flash** header: it lands in flash
set -uo pipefail
root=/home/elliott/repos/relay-terminal
out=$root/docs/qa_evidence/2026-09-21-models-pane-review
build=${RELAY_BUILD:-$root/build}
width=1600 height=900

mkdir -p "$out"
display=
for n in $(seq 800 830); do [[ -e /tmp/.X11-unix/X$n ]] || { display=:$n; break; }; done
[[ -z $display ]] && { echo "no free display in 800..830"; exit 1; }
echo "display $display"

sandbox=/tmp/claude-1000/mpr          # short: XDG_RUNTIME_DIR holds a unix socket
xvfb_pid= relay_pid=
cleanup() { kill "${relay_pid:-0}" "${xvfb_pid:-0}" 2>/dev/null; sleep 1; }
trap cleanup EXIT

Xvfb "$display" -screen 0 $((width + 40))x$((height + 40))x24 >/dev/null 2>&1 &
xvfb_pid=$!
sleep 2
kill -0 "$xvfb_pid" 2>/dev/null || { echo "Xvfb died"; exit 1; }
export DISPLAY=$display
export RELAY_KEYRING=off
export RELAY_OPENROUTER_CATALOG=off
export RELAY_GLM_CODING_API_KEY=xvfb-not-a-real-key
export RELAY_KIMI_CODE_API_KEY=xvfb-not-a-real-key-either
export RELAY_GEMINI_API_KEY=xvfb-nor-is-this-one
export RELAY_ANTHROPIC_API_KEY=xvfb-still-not-a-key

rm -rf "$sandbox"
export HOME=$sandbox
export XDG_CONFIG_HOME=$HOME/.config XDG_DATA_HOME=$HOME/.local/share XDG_CACHE_HOME=$HOME/.cache
export XDG_RUNTIME_DIR=$sandbox/run TMPDIR=$sandbox/tmp
work=$HOME/project
mkdir -p "$XDG_CONFIG_HOME/RelayTerminal" "$XDG_DATA_HOME" "$XDG_CACHE_HOME/relay" "$XDG_RUNTIME_DIR" "$TMPDIR" "$work"
chmod 700 "$XDG_RUNTIME_DIR"
printf "PS1='\\\\w \\$ '\nunset PROMPT_COMMAND\n" >"$HOME/.bashrc"
printf '# project\n' >"$work/README.md"

conf=$XDG_CONFIG_HOME/RelayTerminal/relay.conf
cat >"$conf" <<CONF
[isolation]
enabled=false

[suggestions]
next_command=false
next_prompt=false

[security]
approvals_chosen=true
approvals_ask=@Invalid()

[url_handler]
announced=true

[instructions]
onboarded=true

[models]
tier\\lite=gemini|gemini-3.5-flash-lite|
tier\\main=glm-coding|glm-5.3|, kimi-code|k3|
tier\\high=anthropic|claude-opus-5|
tier\\flash=glm-coding|glm-5.3-flash|
CONF
cp "$conf" "$out/conf-before.txt"

win=
largest_window() {
    win= ; local best=0 w
    for w in $(xdotool search --pid "$relay_pid" 2>/dev/null); do
        eval "$(xdotool getwindowgeometry --shell "$w" 2>/dev/null)"
        (( WIDTH * HEIGHT > best )) && { best=$(( WIDTH * HEIGHT )); win=$w; }
    done
}
k() { xdotool key --delay 60 "$@"; }
t() { xdotool type --delay 35 "$1"; }
shot() { sleep "${2:-0.9}"; xdotool mousemove $((width + 20)) $((height + 20)); sleep 0.3; import -window root "$out/$1.png"; }

"$build/relay" --workspace "$work" >>"$out/relay-stderr.log" 2>&1 &
relay_pid=$!
sleep 12
largest_window
[[ -z $win ]] && { echo "no Relay window"; exit 1; }
xdotool windowmove "$win" 0 0 windowsize "$win" $width $height
xdotool windowfocus "$win"; sleep 6

# --- a/b. the providers tab ------------------------------------------------------------------------
k ctrl+shift+m; sleep 6
k alt+1; sleep 3
shot a-providers-dividers 2
# The bottom of the providers group: "+ add provider" (where a custom endpoint is added) and the
# Warp import that was a button in the retired dialog.
xdotool mousemove 1200 600; for _ in 1 2 3 4 5 6 7 8; do xdotool click 5; sleep 0.2; done
shot b-add-provider-and-warp-import 2

# --- c. the key box: the consent sentences --------------------------------------------------------
# Reached through the pane's own search so no click lands on a moving row.
k alt+1; sleep 1
t "openrouter"; sleep 2.5
# One row matches, so "add key…" is at a known place.
xdotool mousemove ${ADDKEY_XY:-1453 244} click 1; sleep 3
shot c-key-box-consent 2
k Escape; sleep 2
# A modal takes the keyboard from the pane and Escape gives it back to the window, not to the pane,
# so the next tab is reached with the mouse on the tab itself.
xdotool mousemove ${AVAILABLE_TAB_XY:-932 135} click 1; sleep 2
# …and alt+2 again now that the keyboard is back in the pane, because it is what puts the caret in
# the filter line. Without it the next `type` went to the tab bar and the list stayed unfiltered.
k alt+2; sleep 2

# --- e/f. the available tab ------------------------------------------------------------------------
sleep 1
shot e-available-alphabetical-no-recent 2
# gemini-3.5-flash-lite is in the lite list; its tick is the user's now. One row matches, so the
# tick is at a known place: AVAIL_XY is measured off e1 on the first run of this script.
t "flash-lite"; sleep 2.5
shot e1-gemini-flash-lite-found 1.5
xdotool mousemove ${AVAIL_XY:-869 233} click 1; sleep 2.5
shot f-gemini-flash-lite-unticked 2
k alt+2; sleep 1
k ctrl+a; sleep 0.4; k BackSpace; sleep 2
shot f1-available-after-the-untick 2

# --- g/h/i. the priorities tab ---------------------------------------------------------------------
k alt+3; sleep 3
shot g-priorities-sections 2
t "opus"; sleep 2.5
shot h-each-section-offers-what-it-does-not-list 2
# Down to the row under the **flash** header — the shot before the key says which row it is — then
# ctrl+enter. The lists in `conf-after.txt` are what it landed in.
for n in $(seq 1 ${DOWNS:-4}); do k Down; sleep 0.5; done
shot i0-highlight-in-the-flash-section 1.5
k ctrl+Return; sleep 3
shot i-ctrl-enter-added-into-flash 2

# --- d. Options › Models: the advanced row is gone ------------------------------------------------
# Last, and from the terminal pane, so the models pane above is never toggled shut by its own key.
xdotool mousemove 400 400 click 1; sleep 1.5
k ctrl+shift+o; sleep 8
t "advanced provider"; sleep 3
shot d-options-no-advanced-row 2
k ctrl+a; sleep 0.4; k BackSpace; sleep 0.6
t "output token"; sleep 3
shot d1-options-defaults-still-has-the-token-limit 2

sleep 6                              # the settings are written on a timer; let them land
kill -TERM "$relay_pid"; sleep 6
relay_pid=
cp "$conf" "$out/conf-after.txt"
{
  echo "--- the lite list, untouched by the un-tick ---"
  grep -n 'tier' "$out/conf-after.txt"
  echo
  echo "--- models/available: what the un-tick wrote (no gemini row = not available) ---"
  grep -n 'available' "$out/conf-after.txt"
} >"$out/lists-after.txt" 2>&1
cat "$out/lists-after.txt"
echo "done; shots in $out"
