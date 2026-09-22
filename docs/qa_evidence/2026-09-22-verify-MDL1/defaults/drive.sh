#!/usr/bin/env bash
# Card #MDL1, tasks t:a3 and t:a4 — "a pane runs on rank 1 of the main list until you pick
# something else in that pane", and /swap as a toggle with memory.
#
# Xvfb, an isolated HOME/XDG_*/TMPDIR under a short path (the 108-byte unix-socket limit), two fake
# provider keys and no network turn: every step here is a model *switch*, which the worker answers
# without calling anybody. `isolation/enabled=false` because the worker exits under a fake
# XDG_RUNTIME_DIR otherwise. RELAY_KEYRING=off so the owner's real identity key is never touched.
#
# The main list is seeded so that rank 1 is a model that is NOT its provider's default
# (glm-coding|glm-5.3-flash) — that is what tells "the pane followed the list" apart from "the pane
# followed the preset". Rank 2 is glm-5.3 and rank 3 kimi-k3, so /swap is exercised from a model
# that is neither of the two keys the old implementation knew.
#
#   run 1  1  a new pane opens on rank 1's model
#          2  pick another model in pane A, split → pane B is still on rank 1
#          3  /swap from rank 3, and back
#          4  the main list is reordered live (/profile) → the next new pane (a new tab) follows
#          5  pane C is put on a model of its own, the list is put back so nothing sits on rank 1,
#             and Relay is asked to quit (SIGTERM)
#   run 2  6  the panes come back on the models they had, none of which is rank 1
set -uo pipefail
root=/home/elliott/repos/relay-terminal
out=/tmp/mdl1verify-artifacts/defaults
build=$root/build
width=1600 height=900

mkdir -p "$out"
display=
for n in $(seq 380 410); do [[ -e /tmp/.X11-unix/X$n ]] || { display=:$n; break; }; done
[[ -z $display ]] && { echo "no free display in 380..410"; exit 1; }
echo "display $display"

sandbox=/tmp/mv-df          # short: XDG_RUNTIME_DIR holds a unix socket
xvfb_pid= relay_pid=
cleanup() { [[ -n ${relay_pid:-} ]] && kill "$relay_pid" 2>/dev/null; [[ -n ${xvfb_pid:-} ]] && kill "$xvfb_pid" 2>/dev/null; sleep 1; }
trap cleanup EXIT

Xvfb "$display" -screen 0 $((width + 40))x$((height + 40))x24 >/dev/null 2>&1 &
xvfb_pid=$!
sleep 2
kill -0 "$xvfb_pid" 2>/dev/null || { echo "Xvfb died"; exit 1; }
export DISPLAY=$display
export RELAY_KEYRING=off
export RELAY_GLM_CODING_API_KEY=xvfb-not-a-real-key
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
# `provider/preset` and `provider/model` are deliberately set to a *different* model of a
# *different* provider from rank 1: before #MDL1 that pair is what a new pane started on.
cat >"$conf" <<CONF
[instructions]
onboarded=true

[isolation]
enabled=false

[provider]
preset=kimi-code
model=k3

[agent]
effort=high

[models]
tier\\main=glm-coding|glm-5.3-flash|low, glm-coding|glm-5.3|max, kimi-code|k3|high
tier\\flash=glm-coding|glm-5.3-flash|low
tier\\high=glm-coding|glm-5.3|max
tier\\lite=glm-coding|glm-5.3-flash|low
tier\\local=@Invalid()
profile_order=work, admin
profile=work
profiles\\work\\tier\\main=glm-coding|glm-5.3-flash|low, glm-coding|glm-5.3|max, kimi-code|k3|high
profiles\\work\\tier\\flash=glm-coding|glm-5.3-flash|low
profiles\\work\\tier\\high=glm-coding|glm-5.3|max
profiles\\work\\tier\\lite=glm-coding|glm-5.3-flash|low
profiles\\admin\\tier\\main=kimi-code|k3|high, glm-coding|glm-5.3-flash|low, glm-coding|glm-5.3|max
profiles\\admin\\tier\\flash=glm-coding|glm-5.3-flash|low
profiles\\admin\\tier\\high=glm-coding|glm-5.3|max
profiles\\admin\\tier\\lite=glm-coding|glm-5.3-flash|low

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
shot() { sleep "${2:-0.8}"; xdotool mousemove $((width + 20)) $((height + 20)); sleep 0.3; import -window root "$out/$1.png"; }
# The composer strip of every pane: where the model chip is. Cropped so the text is readable.
strip() { convert "$out/$1.png" -crop "${width}x100+0+$((height - 100))" +repage "$out/$1-chips.png"; }

# `--workspace` means "open this folder in a new window", and main.cpp reads that as a reason not
# to reopen the saved layout — so run 2 is launched with no arguments at all, which is what
# "reopen where I left off" needs. The panes' directories come back with the layout.
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

# ================================ run 1 =========================================================
start_relay

# --- 1. a new pane opens on rank 1 of the main list, model and all -------------------------------
shot 01-new-pane-rank1 2
strip 01-new-pane-rank1

# --- 2. a pick is that pane's: pane A moves, pane B still opens on rank 1 ------------------------
xdotool mousemove 300 $((height - 88)) click 1; sleep 0.6
t "/model k3"; k Return; sleep 3
shot 02-pane-a-picked-k3 2
strip 02-pane-a-picked-k3
k ctrl+shift+e; sleep 9          # a second pane, to the right
shot 03-pane-b-still-rank1 2
strip 03-pane-b-still-rank1

# --- 3. /swap from a third model, and back -------------------------------------------------------
# Pane B has focus. Put it on rank 3 — neither of the two keys the old /swap knew — and toggle.
xdotool mousemove $((width * 3 / 4)) $((height - 88)) click 1; sleep 0.6
t "/model k3"; k Return; sleep 3
shot 04-pane-b-on-k3 2
strip 04-pane-b-on-k3
t "/swap"; k Return
sleep 0.4; import -window root "$out/05a-swap-immediately.png"      # the sentence as it is printed
sleep 2.5; shot 05-swap-to-rank1 0.2                                 # …and still there 2.9 s later
strip 05-swap-to-rank1
convert "$out/05a-swap-immediately.png" -crop "${width}x100+0+$((height - 100))" +repage "$out/05a-swap-immediately-chips.png"
t "/swap"; k Return; sleep 3
shot 06-swap-back-to-k3 2
strip 06-swap-back-to-k3

# --- 4. the main list is reordered, live: the next new pane follows it ---------------------------
# `/profile` swaps all five lists at once, in every pane — an edit of the main list that needs no
# restart. The "admin" profile here is the same three models with kimi-k3 first.
t "/profile admin"; k Return
# The confirmation first — `modelsCurated` re-sends the turn options to every worker and their
# answer toasts over it a moment later, which is not this card's to fix.
sleep 0.5; import -window root "$out/07a-profile-said.png"
convert "$out/07a-profile-said.png" -crop "${width}x180+0+$((height - 200))" +repage "$out/07a-profile-said-chips.png"
sleep 2.5; shot 07-profile-admin 0.2
strip 07-profile-admin
k ctrl+shift+t; sleep 10          # a new tab, so the third pane is full width and readable
shot 08-pane-c-follows-the-list 2
strip 08-pane-c-follows-the-list

# --- 5. pane C is put on a model of its own, the list is put back, and Relay quits ---------------
# After `/profile work` rank 1 is glm-5.3-flash again, so none of the three panes is sitting on
# rank 1: whatever comes back in run 2 came back because the pane saved it, not because it is the
# default.
xdotool mousemove 300 $((height - 88)) click 1; sleep 0.6
t "/model glm-5.3"; k Return; sleep 3
shot 09-pane-c-picked 2
strip 09-pane-c-picked
t "/profile work"; k Return; sleep 3
shot 09b-list-put-back 2
strip 09b-list-put-back
cp "$conf" "$out/conf-after-run1.txt"
sleep 8                            # the saved layout is written on a timer; let it land
kill -TERM "$relay_pid"; sleep 8
relay_pid=

# ================================ run 2 =========================================================
# Every pane comes back on the model it had, not on rank 1 and not on its preset's default.
start_relay restore
shot 10-restored 2
strip 10-restored
k ctrl+Tab; sleep 4                # the other tab, whichever way round they came back
shot 11-restored-other-tab 2
strip 11-restored-other-tab
cp "$conf" "$out/conf-after-run2.txt"
python3 -m json.tool "$XDG_DATA_HOME/relay/state/windows.json" >"$out/saved-layout.json" 2>/dev/null

echo "done; shots in $out"
