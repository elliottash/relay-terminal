#!/usr/bin/env bash
# Card #MDL1 t:a11 — the models pane (docs/MODEL-PICKING-DESIGN.md 5.8; owner, 2026-09-21: "lets
# build the models pane … and just remove ctrl alt m, not worth the extra confusion … typing it
# again closes the pane (or esc as you mentioned)").
#
# Xvfb, an isolated HOME/XDG_*/TMPDIR under a short path (the 108-byte unix-socket limit), fake
# provider keys and no network turn: every step is a pane draw, a tab or a key. `isolation/enabled
# =false` because the worker exits under a fake XDG_RUNTIME_DIR otherwise. RELAY_KEYRING=off so the
# owner's real identity key is never touched.
#
# Two runs from one script, because the first is about a profile that has never been used:
#
#   a  FIRST RUN — no saved layout and `instructions/onboarded` unset: a terminal pane at the left
#      and the models pane at the right, serving it, on **providers** (nothing to rank yet)
#   b  Ctrl+Shift+M with the focus in the models pane: it closes
#   c  Ctrl+Shift+M from the terminal pane: it opens again, serving that pane, on priorities
#   d  the providers tab (Alt+1)
#   e  the available tab (Alt+2): the tick column
#   f  the priorities tab (Alt+3)
#
# The tabs are Alt+1/2/3, not Ctrl+Tab: the first run of this script pressed Ctrl+Tab and nothing
# moved, because Ctrl+Tab is the window's own **Next tab** (Keymap `tab.next`) and never reaches a
# pane. That is what the key is now, in the code and in the footer line.
#   g  Enter on it: the served pane's model chip has changed
#   h  Escape: the focus is back in the served pane and the models pane is still there
#   i  Options › Models, the "models and priorities…" row, reached by its own search
#   i2 that row pressed: the models pane goes to priorities, on **main** (a page is in no mode)
#   j  a provider's "models… (N of M available)" link: the available tab, filtered to it
#
# Then a **second Relay start against the same profile** (run 2), which is the only way to prove
# the layout node: the pane is left on the **available** tab, Relay is asked to quit (SIGTERM,
# which writes the layout), and it is started again with **no arguments at all** — `--workspace`
# is what suppresses "reopen where I left off", so run 1 has it and run 2 must not have it.
#
#   k  the two panes as they are at quit, the models pane on available
#   l  after the restart: the pane is back, on the same tab, beside the terminal it serves
#   m  a row used in the restored pane: the *terminal* pane's chip changes, so it is serving it
#
# `check-layout.py` makes the three assertions the shots cannot: one models node in the saved
# layout, carrying the tab it was left on, beside a terminal pane.
#
# The two Options steps are driven through the pane's search box rather than by clicking at fixed
# coordinates: the first run of this script clicked where the rows would have been on the Models
# tab and hit the General tab instead.
set -uo pipefail
root=/home/elliott/repos/relay-terminal
out=/tmp/mdl1verify-artifacts/pane
build=${RELAY_BUILD:-$root/build}
width=1600 height=900

mkdir -p "$out"
display=
for n in $(seq 720 750); do [[ -e /tmp/.X11-unix/X$n ]] || { display=:$n; break; }; done
[[ -z $display ]] && { echo "no free display in 720..750"; exit 1; }
echo "display $display"

sandbox=/tmp/mv-mp           # short: XDG_RUNTIME_DIR holds a unix socket
xvfb_pid= relay_pid=
cleanup() { [[ -n ${relay_pid:-} ]] && kill "$relay_pid" 2>/dev/null; [[ -n ${xvfb_pid:-} ]] && kill "$xvfb_pid" 2>/dev/null; sleep 1; }
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

k() { xdotool key --delay 60 "$@"; }
t() { xdotool type --delay 35 "$1"; }

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
# **No `instructions/onboarded`**: that absence is the first-run condition the window reads.
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
shot() { sleep "${2:-0.9}"; xdotool mousemove $((width + 20)) $((height + 20)); sleep 0.3; import -window root "$out/$1.png"; }

"$build/relay" --workspace "$work" >>"$out/relay-stderr.log" 2>&1 &
relay_pid=$!
sleep 12
largest_window
[[ -z $win ]] && { echo "no Relay window"; exit 1; }
xdotool windowmove "$win" 0 0 windowsize "$win" $width $height
xdotool windowfocus "$win"; sleep 6

# --- a. first run: a terminal pane at the left, the models pane at the right -----------------------
shot a-first-run 3

# --- b. Ctrl+Shift+M with the focus in the models pane: it closes ---------------------------------
# The first run puts the focus in the terminal, so the key is pressed once to take the models pane
# (which re-targets and focuses it) and again to close it.
k ctrl+shift+m; sleep 4
shot b0-focused 1.5
k ctrl+shift+m; sleep 3
shot b-closed 2

# --- c. …and again from the terminal pane: it opens, serving that pane, on priorities --------------
k ctrl+shift+m; sleep 5
shot c-reopened-priorities 2

# --- d/e/f. the three tabs ------------------------------------------------------------------------
k alt+1; sleep 3
shot d-providers 2
k alt+2; sleep 3
shot e-available 2
k alt+3; sleep 3
shot f-priorities 2

# --- g. Enter on a row: the served pane switches ---------------------------------------------------
xdotool mousemove ${ROW_XY:-1100 300} click 1; sleep 1.5
shot g0-highlighted 1.5
k Return; sleep 5
shot g-served-pane-switched 2

# --- h. Escape: the focus goes back, the pane stays open -------------------------------------------
k Escape; sleep 2
shot h-escape-focus-back 2

# --- i/i2/j. Options › Models: the row, and a provider's models… link -----------------------------
k ctrl+shift+o; sleep 7
t "models and priorities"; sleep 3
shot i-options-models-row 2
k Return; sleep 5
shot i2-row-opens-priorities-on-main 2
# Back to Options for the per-provider link. Its own search word is the one the row's aliases
# carry ("available models … step 2"), so the link is reached the same way.
k ctrl+shift+o; sleep 4
k ctrl+a; sleep 0.4; k BackSpace; sleep 1
t "available models"; sleep 3
shot i1-provider-models-link 2
k Return; sleep 5
shot j-link-lands-on-available 2

# =================================== run 2 ======================================================
# --- k. the layout at quit: two panes, the models pane on available -------------------------------
# Options goes first, so the layout under test is the pair this card is about: clicking into it and
# pressing its own key is what closes it (`toggleSettingsPane` closes the pane that has the focus).
xdotool mousemove ${OPTIONS_XY:-600 400} click 1; sleep 1.5
k ctrl+shift+o; sleep 3
# Back into the models pane, on available — the tab the restart has to bring back.
k ctrl+shift+m; sleep 4
k alt+2; sleep 3
shot k-before-quit 2
cp "$conf" "$out/conf-before-quit.txt"

sleep 8                              # the layout is written on a timer; let it land
kill -TERM "$relay_pid"; sleep 8
relay_pid=
python3 -m json.tool "$XDG_DATA_HOME/relay/state/windows.json" >"$out/saved-layout.json" 2>/dev/null
# The assertions the shots cannot make on their own.
python3 "$out/check-layout.py" "$XDG_DATA_HOME/relay/state/windows.json" >"$out/saved-layout-check.txt" 2>&1
layout_rc=$?
cat "$out/saved-layout-check.txt"
[[ $layout_rc -eq 0 ]] || { echo "the saved layout is wrong; stopping"; exit 1; }

# --- l. the restart: no arguments, so "reopen where I left off" runs ------------------------------
"$build/relay" >>"$out/relay-stderr.log" 2>&1 &
relay_pid=$!
sleep 14
largest_window
[[ -z $win ]] && { echo "no Relay window after the restart"; exit 1; }
xdotool windowmove "$win" 0 0 windowsize "$win" $width $height
xdotool windowfocus "$win"; sleep 5
shot l-after-restart 3

# --- m. the restored pane is serving the terminal beside it ---------------------------------------
# It came back pointed at the first terminal pane of its tab, which nothing in the layout told it:
# Enter on a row is what proves it, because the chip that changes is that pane's.
xdotool mousemove ${RESTORED_ROW_XY:-1100 320} click 1; sleep 1.5
shot m0-restored-row-highlighted 1.5
k Return; sleep 5
shot m-restored-row-switches-the-pane 2
cp "$conf" "$out/conf-after-restart.txt"

sleep 2
echo "done; shots in $out"
