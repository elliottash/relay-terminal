#!/usr/bin/env bash
# Card #MDL1, task t:a6 — the model box is modes, then the models of the mode you are in
# (docs/MODEL-PICKING-DESIGN.md 5.1; owner, 2026-09-21: "first it just says high, main, flash, with
# the first model in parens … then it lists the models for the mode you are in … great, left/right
# changes mode. add /high").
#
# Xvfb, an isolated HOME/XDG_*/TMPDIR under a short path (the 108-byte unix-socket limit), three
# fake provider keys and no network turn: every step here is a model or mode *switch*, which the
# worker answers without calling anybody. `isolation/enabled=false` because the worker exits under a
# fake XDG_RUNTIME_DIR otherwise. RELAY_KEYRING=off so the owner's real identity key is never
# touched.
#
# The lists are seeded so that each thing the card asks for is visible:
#   main   kimi-code|k3, glm-coding|glm-5.3, glm|glm-5.3   — the last two are ONE model from two
#                                                            providers, so the row says "+1"
#   high   glm-coding|glm-5.3, openai|gpt-5.6-sol          — no OpenAI key here, so that row is
#                                                            greyed IN PLACE rather than dropped
#   flash  glm-coding|glm-5.3-flash, kimi-code|k3
#
#   a  the pane, collapsed box on main
#   b  Alt+M: the modes with the marker on main, and this pane's model highlighted below
#   c  Right: the flash list, popup still open, marker still on main (nothing is sent until Enter)
#   d  Left Left: the high list, with the keyless row greyed in place
#   e  a filter typed, then Right: the filter is kept and re-applied to the new mode
#   f  Enter on a model of the flash list: the collapsed box reads "<model> · flash"
#   g  /high typed: the box follows
#   h  /main: the model alone, no "(main)"
#   i  a Switchboard console's box, beside j, a terminal pane's: the same rows
#   k  /flash comes back on the model this pane picked for flash, not rank 1 of the list
#   l  after a quit and a relaunch with no arguments ("reopen where I left off"): the pane is back
#      on "kimi-k3 · flash", and the saved layout node says why
set -uo pipefail
root=/home/elliott/repos/relay-terminal
out=$root/docs/qa_evidence/2026-09-21-model-box-modes
build=$root/build
width=1600 height=900

mkdir -p "$out"
display=
for n in $(seq 460 490); do [[ -e /tmp/.X11-unix/X$n ]] || { display=:$n; break; }; done
[[ -z $display ]] && { echo "no free display in 460..490"; exit 1; }
echo "display $display"

sandbox=/tmp/claude-1000/bx          # short: XDG_RUNTIME_DIR holds a unix socket
xvfb_pid= relay_pid=
cleanup() { kill "${relay_pid:-0}" "${xvfb_pid:-0}" 2>/dev/null; sleep 1; }
trap cleanup EXIT

Xvfb "$display" -screen 0 $((width + 40))x$((height + 40))x24 >/dev/null 2>&1 &
xvfb_pid=$!
sleep 2
kill -0 "$xvfb_pid" 2>/dev/null || { echo "Xvfb died"; exit 1; }
export DISPLAY=$display
export RELAY_KEYRING=off
export RELAY_GLM_CODING_API_KEY=xvfb-not-a-real-key
export RELAY_GLM_API_KEY=xvfb-not-a-real-key-two
export RELAY_KIMI_CODE_API_KEY=xvfb-not-a-real-key-either

k() { xdotool key --delay 60 "$@"; }
t() { xdotool type --delay 35 "$1"; }

rm -rf "$sandbox"
export HOME=$sandbox
export XDG_CONFIG_HOME=$HOME/.config XDG_DATA_HOME=$HOME/.local/share XDG_CACHE_HOME=$HOME/.cache
export XDG_RUNTIME_DIR=$sandbox/run TMPDIR=$sandbox/tmp
work=$HOME/project
mkdir -p "$XDG_CONFIG_HOME/RelayTerminal" "$XDG_DATA_HOME" "$XDG_CACHE_HOME" "$XDG_RUNTIME_DIR" "$TMPDIR" "$work"
chmod 700 "$XDG_RUNTIME_DIR"
printf "PS1='\\\\w \\$ '\nunset PROMPT_COMMAND\n" >"$HOME/.bashrc"
printf '# project\n' >"$work/README.md"

conf=$XDG_CONFIG_HOME/RelayTerminal/relay.conf
cat >"$conf" <<CONF
[instructions]
onboarded=true

[isolation]
enabled=false

[provider]
preset=kimi-code

[agent]
effort=high

[models]
tier\\main=kimi-code|k3|high, glm-coding|glm-5.3|max, glm|glm-5.3|max
tier\\high=glm-coding|glm-5.3|max, openai|gpt-5.6-sol|max
tier\\flash=glm-coding|glm-5.3-flash|low, kimi-code|k3|high
tier\\lite=glm-coding|glm-5.3-flash|low
tier\\local=@Invalid()

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
# The bottom of the window: the composer strip with the model chip, and the list that drops open
# above it. Cropped so the text is readable at a glance.
crop() { convert "$out/$1.png" -crop "${width}x420+0+$((height - 420))" +repage "$out/$1-box.png"; }

# `--workspace` means "open this folder in a new window", and main.cpp reads that as a reason not
# to reopen the saved layout — so run 2 is launched with no arguments at all, which is what
# "reopen where I left off" needs.
start_relay() {
    if [[ ${1:-} == restore ]]; then "$build/relay" >>"$out/relay-stderr.log" 2>&1 &
    else "$build/relay" --workspace "$work" >>"$out/relay-stderr.log" 2>&1 &
    fi
    relay_pid=$!
    sleep 10
    largest_window
    [[ -z $win ]] && { echo "no Relay window"; exit 1; }
    xdotool windowmove "$win" 0 0 windowsize "$win" $width $height
    xdotool windowfocus "$win"; sleep 4
}

start_relay

# --- a. the collapsed chip on main: the model alone, no "(main)" ---------------------------------
shot a-strip 2
crop a-strip

# --- b. Alt+M: the modes, the marker on main, this pane's model highlighted ----------------------
k alt+m; sleep 1.8
shot b-altm-main
crop b-altm-main

# --- c. Right: the flash list in place. The popup is still open and the marker has not moved: -----
#        nothing is sent to the worker until Enter.
k Right; sleep 1.2
shot c-right-flash
crop c-right-flash

# --- d. Left Left: the high list, with the keyless row greyed in place ---------------------------
k Left; sleep 0.8; k Left; sleep 1.2
shot d-left-left-high
crop d-left-left-high
k Escape; sleep 1.0

# --- e. a filter, then Right: the filter is kept and re-applied to the new mode ------------------
k alt+m; sleep 1.6
t "glm"; sleep 1.2
shot e1-filtered-main
crop e1-filtered-main
k Right; sleep 1.2
shot e2-filtered-then-right
crop e2-filtered-then-right
k Escape; sleep 1.0

# --- f. Enter on a model of the flash list: the pane goes to flash AND to that model -------------
k alt+m; sleep 1.6
k Right; sleep 1.0          # the flash page
k Down; sleep 0.5           # into the model list below the modes
shot f1-flash-highlighted
crop f1-flash-highlighted
k Return; sleep 3
shot f2-picked-flash 2
crop f2-picked-flash

# --- g. /high: the box follows -------------------------------------------------------------------
xdotool mousemove 300 $((height - 88)) click 1; sleep 0.6
t "/high"; k Return; sleep 3
shot g-slash-high 2
crop g-slash-high
k alt+m; sleep 1.6
shot g2-high-open
crop g2-high-open
k Escape; sleep 1.0

# --- h. /main: the model alone again --------------------------------------------------------------
t "/main"; k Return; sleep 3
shot h-slash-main 2
crop h-slash-main

# --- i / j. a console's box and a terminal pane's box are the same list --------------------------
# The Switchboard pane is a console (card #AGNT): a Pane with no pty, whose worker role is
# "switchboard" — a main-tier role. Its box used to read "kimi-k3 (switchboard)".
k ctrl+shift+s; sleep 12
shot i0-switchboard 2
# It opens on the Projects picker, because a tab is unattached until an action attaches it. Esc
# there closes the whole pane, so the button is the way in: "Initialize here" makes a .switchboard/
# in the sandbox project and leaves the board, with its console, in the right-hand pane.
xdotool mousemove 869 869 click 1; sleep 12
shot i1-switchboard-board 2
# The console is the right-hand pane: click ITS composer strip, then open its box.
xdotool mousemove $((width * 3 / 4)) $((height - 88)) click 1; sleep 1.0
k alt+m; sleep 1.8
shot i-console-box
crop i-console-box
k Escape; sleep 1.0
# And the terminal pane's box beside it, for the same shot: the same rows.
xdotool mousemove 300 $((height - 88)) click 1; sleep 1.0
k alt+m; sleep 1.8
shot j-pane-box
crop j-pane-box
k Escape; sleep 1.0

# --- k. /flash comes back on the model this pane picked for flash ---------------------------------
# Not rank 1 of the flash list (glm-5.3-flash): the pick made in step f is this pane's own, and
# /flash, Alt+F and the flash mode row all take it.
xdotool mousemove 300 $((height - 88)) click 1; sleep 0.8
t "/flash"; k Return; sleep 3
shot k-flash-remembers-the-pick 2
crop k-flash-remembers-the-pick

# --- l. quit, relaunch with no arguments, and the pane is still on it ------------------------------
cp "$conf" "$out/conf-after.txt"
sleep 8                            # the saved layout is written on a timer; let it land
python3 -m json.tool "$XDG_DATA_HOME/relay/state/windows.json" >"$out/saved-layout.json" 2>/dev/null
grep -o '"mode_picks":[^}]*}' "$out/saved-layout.json" >"$out/saved-mode-picks.txt" 2>/dev/null \
    || python3 - "$out/saved-layout.json" >"$out/saved-mode-picks.txt" <<'PYEOF'
import json, sys
def walk(node):
    if isinstance(node, dict):
        if "mode_picks" in node: print(json.dumps(node["mode_picks"], indent=2))
        for v in node.values(): walk(v)
    elif isinstance(node, list):
        for v in node: walk(v)
walk(json.load(open(sys.argv[1])))
PYEOF
kill -TERM "$relay_pid"; sleep 8
relay_pid=

start_relay restore
shot l-restored-on-its-pick 2
crop l-restored-on-its-pick
k alt+m; sleep 1.8
shot l2-restored-box
crop l2-restored-box
k Escape; sleep 1.0

echo "done; shots in $out"
