#!/usr/bin/env bash
# Card #MDL1, task t:a5 — "every remaining place that prints a model to a person prints its NAME
# through one function, and tier/role words are lower-case" (rule 1 of docs/MODEL-PICKING-DESIGN.md).
#
# Xvfb, an isolated HOME/XDG_*/TMPDIR under a short path (the 108-byte unix-socket limit), four
# fake provider keys and no network turn: every step here is a model *switch* or a panel opening,
# which the worker answers without calling anybody. `isolation/enabled=false` because the worker
# exits under a fake XDG_RUNTIME_DIR otherwise; RELAY_KEYRING=off so the owner's real identity key
# is never touched. Shaped after 2026-09-21-model-defaults-and-swap/drive.sh.
#
# Two keys per model family on purpose: glm-5.3 is served by `glm-coding` and by `glm`, and Kimi K3
# by `kimi-code` (whose id for it is "k3") and by `kimi` ("kimi-k3"). That is what makes
# `/model glm-5.3@glm-coding` mean something, and what puts two spellings of one model in history.
#
#   1  the status line after a model switch  (/model kimi-k3)
#   2  /flash, then /main
#   3  the model chip's tooltip (hover)
#   4  /glm
#   5  /model glm-5.3@glm-coding, then /model glm-5.3@glm — one name, two providers
#   6  the conversation info panel (Ctrl+I)
#   7  the Conversations list: the Model column and its filter menu
set -uo pipefail
root=/home/elliott/repos/relay-terminal
out=$root/docs/qa_evidence/2026-09-21-model-names-everywhere
build=$root/build
width=1600 height=900

mkdir -p "$out"
display=
for n in $(seq 500 530); do [[ -e /tmp/.X11-unix/X$n ]] || { display=:$n; break; }; done
[[ -z $display ]] && { echo "no free display in 500..530"; exit 1; }
echo "display $display"

sandbox=/tmp/claude-1000/mn          # short: XDG_RUNTIME_DIR holds a unix socket
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
export RELAY_GLM_API_KEY=xvfb-not-a-real-key-2
export RELAY_KIMI_CODE_API_KEY=xvfb-not-a-real-key-3
export RELAY_KIMI_API_KEY=xvfb-not-a-real-key-4

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
preset=glm-coding
model=glm-5.3

[agent]
effort=high

[models]
tier\\main=glm-coding|glm-5.3|max, kimi-code|k3|high
tier\\flash=glm-coding|glm-5.3-flash|low
tier\\high=glm-coding|glm-5.3|max
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

# Three saved conversations under three ids of two models, so the Sessions pane has something to
# name (see seed_sessions.py).
python3 "$out/seed_sessions.py" "$work" || exit 1

win=
largest_window() {
    win= ; local best=0 w
    for w in $(xdotool search --pid "$relay_pid" 2>/dev/null); do
        eval "$(xdotool getwindowgeometry --shell "$w" 2>/dev/null)"
        (( WIDTH * HEIGHT > best )) && { best=$(( WIDTH * HEIGHT )); win=$w; }
    done
}
shot() { sleep "${2:-0.8}"; xdotool mousemove $((width + 20)) $((height + 20)); sleep 0.3; import -window root "$out/$1.png"; }
# The composer strip of every pane: where the model chip is.
strip() { convert "$out/$1.png" -crop "${width}x110+0+$((height - 110))" +repage "$out/$1-chips.png"; }
# What the pane *said*. The line goes to the status line and to a toast at the bottom right of the
# terminal area — and the toast queues: a model switch also re-applies the reasoning effort, whose
# "Effort: high" toast is shown first for 1.6 s. So the model's line is up at about 2.2 s, not at
# once, and that is when this looks. Measured, not guessed: the frames are in the probe under
# /tmp/claude-1000 and the sentence is the same in every one from 2.0 s on.
say() {
    sleep 2.2
    import -window root "$out/$1.png"
    convert "$out/$1.png" -crop "700x150+$((width - 716))+$((height - 270))" +repage "$out/$1-said.png"
    convert "$out/$1.png" -crop "${width}x110+0+$((height - 110))" +repage "$out/$1-chips.png"
}
# Type into the focused pane's prompt box (the click puts the caret there first).
ask() { xdotool mousemove 300 $((height - 88)) click 1; sleep 0.6; t "$1"; k Return; }

"$build/relay" --workspace "$work" >>"$out/relay-stderr.log" 2>&1 &
relay_pid=$!
sleep 10
largest_window
[[ -z $win ]] && { echo "no Relay window"; exit 1; }
xdotool windowmove "$win" 0 0 windowsize "$win" $width $height
xdotool windowfocus "$win"; sleep 4
# --- 1. the status line after a model switch ----------------------------------------------------
# "model: kimi-k3 · conversation kept" — lower-case, and the *name*: the Kimi Coding Plan's id for
# this model is "k3", which only its catalog row can turn into a name.
ask "/model kimi-k3"
say 01-model-switch

# --- 2. /flash, then /main ----------------------------------------------------------------------
# "flash: glm-5.3-flash · conversation kept", then "model: … · conversation kept" — never
# "Flash agent".
ask "/flash"
say 02-flash
ask "/flash"                     # already there: "Already on flash · glm-5.3-flash."
say 02b-already-on-flash
ask "/main"
say 03-main

# --- 3. the model chip's tooltip ----------------------------------------------------------------
# Every role row in it is "<lower-case role>: <model name>".
# The chip sits at the right of the composer strip; the little step first is what makes Qt see an
# enter event and start the tooltip's timer.
xdotool mousemove 1200 $((height - 36)); sleep 0.5
xdotool mousemove 1405 $((height - 36)); sleep 3.0
import -window root "$out/04-model-tooltip.png"
convert "$out/04-model-tooltip.png" -crop "${width}x480+0+$((height - 500))" +repage "$out/04-model-tooltip-chips.png"
xdotool mousemove $((width + 20)) $((height + 20)); sleep 0.8

# --- 4. /glm ------------------------------------------------------------------------------------
# "model: glm-5.3 · z.ai (glm)." — the model's name and its provider, not the preset label.
ask "/glm"
say 05-glm

# --- 5. one name, two providers -----------------------------------------------------------------
ask "/model kimi-k3"
sleep 3
# Both name the same model; the info panel below each says which provider took it, which is the
# whole point of the "@" — "z.ai · coding plan (glm-coding)" here, "z.ai · standard api (glm)" next.
ask "/model glm-5.3@glm-coding"
say 06-at-glm-coding
ask "/status"
sleep 4
shot 06b-at-glm-coding-info 2
k Escape; sleep 1.5
ask "/model glm-5.3@glm"
say 07-at-glm

# --- 6. the conversation info panel -------------------------------------------------------------
# "Model: glm-5.3 · z.ai · standard api (glm) · effort max": the name once, then which key.
ask "/status"
sleep 4
shot 08-session-info 2
k Escape; sleep 1.5

# --- 7. the Conversations list: the Model column and its filter ---------------------------------
# Three saved conversations under three ids of two models; the column names both, and the filter
# menu has one entry per model — `k3` and `kimi-k3` are one line.
ask "/conversations"
sleep 7
shot 09-conversations 2
xdotool mousemove 877 215 click 1; sleep 1.8
import -window root "$out/10-model-filter.png"
convert "$out/10-model-filter.png" -crop "620x300+790+195" +repage "$out/10-model-filter-menu.png"
k Escape; sleep 1

echo "done; shots in $out"
