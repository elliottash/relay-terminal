#!/usr/bin/env bash
# Card #MDL1 — the four steps of model availability, and the box's typed filter
# (docs/MODEL-PICKING-DESIGN.md 5.7; owner, 2026-09-21: "there need to be 4 steps of model
# availability … and its step 2 that determines the models available in the text filter").
#
# Xvfb, an isolated HOME/XDG_*/TMPDIR under a short path (the 108-byte unix-socket limit), fake
# provider keys and no network turn: every step here is a page draw, a dialog or a combo popup,
# which the worker answers without calling anybody. `isolation/enabled=false` because the worker
# exits under a fake XDG_RUNTIME_DIR otherwise. RELAY_KEYRING=off so the owner's real identity key
# is never touched.
#
# The OpenRouter long tail is real: the owner's cached listing is copied into the sandbox's
# XDG_CACHE_HOME and RELAY_OPENROUTER_CATALOG=off stops any fetch. That is what step 2 holds back
# by default, and what the dialog's "more from openrouter" has to reach.
#
# The lists are seeded so that some available models are in **no** list — kimi's k3-256k,
# kimi-for-coding and kimi-for-coding-highspeed, and OpenRouter's three recommended rows — which is
# what the box's `other models` section is for.
#
#   a  Options › Models, providers open: "models… (N of M available)" under every provider
#   b  that link pressed: the dialog's `all` tab, filtered to that provider
#   c  the `all` tab with nothing typed: the available column, grouped by provider
#   d  a tail slug typed: "more from openrouter", its row un-ticked
#   e  a branded model un-ticked: the row stays, greyed, with an empty box
#   f  Alt+M and a name that is available but in no list: it comes under `other models`
#   g  Enter on it: the pane is on that model
#
# The binary is a clean export of the landed tree (see NOTES.md): the shared build/ carries other
# sessions' uncommitted edits, and one of them does not compile.
set -uo pipefail
root=/home/elliott/repos/relay-terminal
out=$root/docs/qa_evidence/2026-09-21-model-availability
build=${RELAY_BUILD:-$root/build}
width=1600 height=900

mkdir -p "$out"
display=
for n in $(seq 680 710); do [[ -e /tmp/.X11-unix/X$n ]] || { display=:$n; break; }; done
[[ -z $display ]] && { echo "no free display in 680..710"; exit 1; }
echo "display $display"

sandbox=/tmp/claude-1000/av           # short: XDG_RUNTIME_DIR holds a unix socket
xvfb_pid= relay_pid=
cleanup() { kill "${relay_pid:-0}" "${xvfb_pid:-0}" 2>/dev/null; sleep 1; }
trap cleanup EXIT

Xvfb "$display" -screen 0 $((width + 40))x$((height + 40))x24 >/dev/null 2>&1 &
xvfb_pid=$!
sleep 2
kill -0 "$xvfb_pid" 2>/dev/null || { echo "Xvfb died"; exit 1; }
export DISPLAY=$display
export RELAY_KEYRING=off
export RELAY_OPENROUTER_CATALOG=off          # serve the cache, never fetch
export RELAY_GLM_CODING_API_KEY=xvfb-not-a-real-key
export RELAY_KIMI_CODE_API_KEY=xvfb-not-a-real-key-either
export RELAY_OPENROUTER_API_KEY=xvfb-not-a-real-key-three

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
cp /home/elliott/.cache/relay/openrouter-models.json "$XDG_CACHE_HOME/relay/openrouter-models.json"
python3 - "$XDG_CACHE_HOME/relay/openrouter-models.json" <<'PY'
import json, sys, time
p = sys.argv[1]
d = json.load(open(p))
d["fetched_at"] = time.time()          # fresh, so nothing even wants to refresh it
json.dump(d, open(p, "w"))
print("openrouter cache rows:", len(d["rows"]))
PY

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
tier\\main=kimi-code|k3|high, glm-coding|glm-5.3|max
tier\\high=glm-coding|glm-5.3|max
tier\\flash=glm-coding|glm-5.3-flash|low
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

"$build/relay" --workspace "$work" >>"$out/relay-stderr.log" 2>&1 &
relay_pid=$!
sleep 10
largest_window
[[ -z $win ]] && { echo "no Relay window"; exit 1; }
xdotool windowmove "$win" 0 0 windowsize "$win" $width $height
xdotool windowfocus "$win"; sleep 4

# --- a. Options › Models, providers open -----------------------------------------------------------
# The heading folds by default once a provider is set up, so it is clicked open: the link this card
# adds sits under each provider row, and that is the row it is about.
k ctrl+shift+m; sleep 7
xdotool mousemove ${PROVIDERS_XY:-878 450} click 1; sleep 2.5
shot a-options-provider-link 2

# --- b. the link opens the dialog on the `all` tab, filtered to that provider -----------------------
xdotool mousemove ${LINK_XY:-1444 776} click 1; sleep 5
shot b-link-lands-on-all-tab 2

# --- c. the whole `all` tab: the available column, grouped by provider -----------------------------
k ctrl+a; sleep 0.4; k BackSpace; sleep 2.5
shot c-all-tab-available-column 2

# --- d. the long tail, typed: un-ticked under "more from openrouter" -------------------------------
t "${TAIL_QUERY:-muse}"; sleep 3
shot d-openrouter-tail-unticked 2
k ctrl+a; sleep 0.4; k BackSpace; sleep 2

# --- e. un-tick a branded model: the row stays, greyed, with an empty box --------------------------
t "${UNTICK_QUERY:-kimi-for-coding-highspeed}"; sleep 2.5
shot e0-before-untick 1.5
xdotool mousemove ${UNTICK_XY:-366 227} click 1; sleep 2
shot e-unticked-branded-model 2
k Escape; sleep 2

# --- f. Alt+M, a name that is available but in no list ---------------------------------------------
# Options is a splitter pane, not an overlay: Escape closes it, and the click puts the keyboard
# back in the terminal pane, which is whose model box Alt+M opens.
k Escape; sleep 2
# The box hangs off the bottom of the window, and its list drops *down*: a full-height window puts
# the list off the screen, where the shot cannot show it. Shorter window, same box.
xdotool windowsize "$win" $width 700; sleep 2
xdotool mousemove 400 300 click 1; sleep 1.5
k alt+m; sleep 3
t "kimi"; sleep 2.5
shot f-box-filter-other-models 2

# --- g. Enter on it: the pane is on that model ------------------------------------------------------
k Down; sleep 0.8
shot g0-highlighted 1.2
k Return; sleep 4
shot g-enter-switches-the-pane 2

cp "$conf" "$out/conf-after.txt"
sleep 2
echo "done; shots in $out"
