#!/usr/bin/env bash
# Card #MDL1, task t:a8 — the model box as CLASSES (docs/MODEL-PICKING-DESIGN.md 5.3; owner,
# 2026-09-21, confirmed with four rulings: "the class header rows are not selectable in the picker.
# thats redundant." / "exhausted models dont show up." / "no need to show the model class in the
# pane header" / the cutoff and the class switch live in the dialog).
#
# Xvfb, an isolated HOME/XDG_*/TMPDIR under a short path (the 108-byte unix-socket limit), three
# fake provider keys and no network turn: every step here is a model or mode *switch*, which the
# worker answers without calling anybody. `isolation/enabled=false` because the worker exits under a
# fake XDG_RUNTIME_DIR otherwise. RELAY_KEYRING=off so the owner's real identity key is never
# touched.
#
# The lists are seeded so every rule the card asks for is visible at once:
#   main   kimi-code|k3, glm-coding|glm-5.3, kimi-code|k3-256k, kimi-code|kimi-for-coding
#          — FOUR rows against a cutoff of two, so the cutoff is a thing you can see
#   high   glm-coding|glm-5.3, openai|gpt-6-astra
#          — there is no OpenAI key here, so that row is ABSENT from the box (the same
#            `Group::spent` path an exhausted subscription takes; exhaustion itself needs a
#            provider `limits` report, which no fake key can produce, so it is stated in
#            tests/modelrows_test.cpp `spentAndKeylessModelsAreNotInTheBox` instead)
#   flash  glm-coding|glm-5.3-flash, kimi-code|kimi-for-coding-highspeed
#
#   a  the pane, collapsed box: the model alone
#   b  Alt+M: three headers, two models under each, this pane's model highlighted
#   c  Down x3: the highlight steps over the "main" header rather than landing on it
#   d  Right on a main row: the main class opens to its whole list
#   e  Left: it closes again
#   f  "kimi" typed: the two classes that still have a match keep their headers, high drops
#   g  Enter on a flash row: the pane goes to flash AND to that model, and the collapsed box
#      reads the MODEL ALONE — no "· flash"
#   h  Ctrl+Alt+M: the "show in box" column, then rank 3 ticked → Alt+M shows three of main
#   i  the flash tab's "show this class in the box" switched off → the class is gone from the box
set -uo pipefail
root=/home/elliott/repos/relay-terminal
out=$root/docs/qa_evidence/2026-09-21-model-box-classes
build=$root/build
width=1600 height=900

mkdir -p "$out"
display=
for n in $(seq 560 590); do [[ -e /tmp/.X11-unix/X$n ]] || { display=:$n; break; }; done
[[ -z $display ]] && { echo "no free display in 560..590"; exit 1; }
echo "display $display"

sandbox=/tmp/claude-1000/cx          # short: XDG_RUNTIME_DIR holds a unix socket
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
tier\\main=kimi-code|k3|high, glm-coding|glm-5.3|max, kimi-code|k3-256k|high, kimi-code|kimi-for-coding|
tier\\high=glm-coding|glm-5.3|max, openai|gpt-6-astra|max
tier\\flash=glm-coding|glm-5.3-flash|low, kimi-code|kimi-for-coding-highspeed|
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
crop() { convert "$out/$1.png" -crop "${width}x460+0+$((height - 460))" +repage "$out/$1-box.png"; }

start_relay() {
    "$build/relay" --workspace "$work" >>"$out/relay-stderr.log" 2>&1 &
    relay_pid=$!
    sleep 10
    largest_window
    [[ -z $win ]] && { echo "no Relay window"; exit 1; }
    xdotool windowmove "$win" 0 0 windowsize "$win" $width $height
    xdotool windowfocus "$win"; sleep 4
}

start_relay

# --- a. the collapsed chip: the model alone ------------------------------------------------------
shot a-strip 2
crop a-strip

# --- b. Alt+M: the classes, two models under each, this pane's model highlighted -----------------
k alt+m; sleep 1.8
shot b-altm-classes
crop b-altm-classes

# --- c. Down x3: the highlight never lands on a header -------------------------------------------
# It opens on kimi-k3 (main rank 1). One Down is glm-5.3, the next STEPS OVER the "flash" header
# onto glm-5.3-flash, the next is the flash class's second model.
k Down; sleep 0.5
k Down; sleep 0.5
shot c1-down-over-the-header
crop c1-down-over-the-header
k Down; sleep 0.5
shot c2-down-again
crop c2-down-again

# --- d. Right on a main row: the class opens to its whole list ------------------------------------
k Up; sleep 0.4; k Up; sleep 0.4        # back onto a main row
k Right; sleep 1.2
shot d-right-expands-main
crop d-right-expands-main

# --- e. Left: it closes again ---------------------------------------------------------------------
k Left; sleep 1.2
shot e-left-collapses-main
crop e-left-collapses-main

# --- f. typing filters across every class, and the headers with a match stay ----------------------
t "kimi"; sleep 1.2
shot f-filter-across-classes
crop f-filter-across-classes
k Escape; sleep 1.0

# --- g. Enter on a flash row: the class AND the model, and the chip is the model alone -------------
k alt+m; sleep 1.6
k Down; sleep 0.4; k Down; sleep 0.4    # over the flash header, onto glm-5.3-flash
shot g1-flash-row-highlighted
crop g1-flash-row-highlighted
k Return; sleep 3
shot g2-picked-flash 2
crop g2-picked-flash

# --- h. the dialog: "show in box" is a cutoff ------------------------------------------------------
xdotool mousemove 300 $((height - 88)) click 1; sleep 0.6
k ctrl+alt+m; sleep 3
# It opens on the tab of the class the pane is in, which step g left on flash: click "main", whose
# list is the four-deep one. Every coordinate below is read off h1-dialog.png.
xdotool mousemove 385 129 click 1; sleep 1.5
shot h1-dialog 2
# The checkbox of rank 3, in the "in box" column: the rows start at y=227 and are 20px apart.
xdotool mousemove 366 267 click 1; sleep 1.2
shot h2-dialog-row3-ticked 2
k Escape; sleep 1.5
# Closing a dialog leaves the focus where it was, not in the pane, so every shortcut below is
# preceded by a click on the composer strip.
xdotool mousemove 300 $((height - 88)) click 1; sleep 0.8
k alt+m; sleep 1.8
shot h3-altm-three-in-main
crop h3-altm-three-in-main
k Escape; sleep 1.0

# --- i. a class switched off leaves the box --------------------------------------------------------
xdotool mousemove 300 $((height - 88)) click 1; sleep 0.8
k ctrl+alt+m; sleep 3
xdotool mousemove 435 129 click 1; sleep 1.5      # the flash tab
shot i1-flash-tab 2
xdotool mousemove 1085 168 click 1; sleep 1.2     # "show this class in the box"
shot i2-flash-switched-off 2
k Escape; sleep 1.5
xdotool mousemove 300 $((height - 88)) click 1; sleep 0.8
k alt+m; sleep 1.8
shot i3-altm-no-flash
crop i3-altm-no-flash
k Escape; sleep 1.0

cp "$conf" "$out/conf-after.txt"
sleep 3
sed -n '/^\[models\]/,/^$/p' "$conf" >"$out/stored-box-keys.txt" 2>/dev/null

echo "done; shots in $out"
