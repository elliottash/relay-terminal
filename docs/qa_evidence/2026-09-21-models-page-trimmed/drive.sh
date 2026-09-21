#!/usr/bin/env bash
# Card #MDL1, task t:a10 — Options › Models keeps providers, keys and profiles
# (docs/MODEL-PICKING-DESIGN.md 5.5; owner, 2026-09-21: "yes to all your recs").
#
# Xvfb, an isolated HOME/XDG_*/TMPDIR under a short path (the 108-byte unix-socket limit), fake
# provider keys and no network turn: every step here is a page draw or a dialog, which the worker
# answers without calling anybody. `isolation/enabled=false` because the worker exits under a fake
# XDG_RUNTIME_DIR otherwise. RELAY_KEYRING=off so the owner's real identity key is never touched.
#
# The OpenRouter long tail is real: the owner's cached listing (446 rows, fetched 2026-09-20) is
# copied into the sandbox's XDG_CACHE_HOME and RELAY_OPENROUTER_CATALOG=off stops any fetch. That
# is what `shown()` holds back and what typing in the dialog has to reach.
#
#   a  Options › Models, top: "models and priorities… (Ctrl+Alt+M)" is the first row, then providers
#   d  the button pressed: the dialog opens on the MAIN tab (not the mode the pane is in)
#   e  the `all` tab, nothing typed: the long tail is not in it
#   f  "muse-spark" typed: the tail appears under "more from openrouter"
#   g  the end of the `all` tab: "+ add a model by id…", the home of the box that left the page
#   b  providers opened: the keys, the tests, the guest permission row — and no checklist under it
#   c  the same page from the bottom: profiles and defaults, with no tier lists anywhere
#
# d..g come before b..c because the button is at the TOP of the page: scrolling down to photograph
# the rest and scrolling back is how the first run clicked "Fall over to a fallback model" instead.
set -uo pipefail
root=/home/elliott/repos/relay-terminal
out=$root/docs/qa_evidence/2026-09-21-models-page-trimmed
build=$root/build
width=1600 height=900

mkdir -p "$out"
display=
for n in $(seq 600 630); do [[ -e /tmp/.X11-unix/X$n ]] || { display=:$n; break; }; done
[[ -z $display ]] && { echo "no free display in 600..630"; exit 1; }
echo "display $display"

sandbox=/tmp/claude-1000/ct          # short: XDG_RUNTIME_DIR holds a unix socket
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

# --- a. Options › Models, top ---------------------------------------------------------------------
k ctrl+shift+m; sleep 6
shot a-models-top 2

# --- d. the button opens the dialog, on main -------------------------------------------------------
# The button's "models and priorities… (Ctrl+Alt+M)" cell, read off a-models-top.png. The page is
# untouched and at the top, which is the only place this coordinate is right.
xdotool mousemove 1420 361 click 1; sleep 5
shot d-dialog-on-main 2

# --- e. the `all` tab with nothing typed ------------------------------------------------------------
# Ctrl+Shift+Tab rather than a coordinate: the tabs wrap, so main → high → all whether or not this
# machine serves a local model (which decides whether there is a `local` tab at all).
k ctrl+shift+Tab; sleep 1
k ctrl+shift+Tab; sleep 2
shot e-all-tab 2

# --- f. the long tail, behind typing ----------------------------------------------------------------
t "muse-spark"; sleep 2.5
shot f-tail-more-from-openrouter 2

# --- g. "+ add a model by id…" at the end of the `all` tab --------------------------------------------
k ctrl+a; sleep 0.4; k BackSpace; sleep 2
k Next; sleep 0.6; k Next; sleep 0.6; k Next; sleep 1.5
shot g-add-a-model-by-id 2
k Escape; sleep 2

# --- b. providers opened: keys, tests, the guest permission row ------------------------------------
# The "providers" heading, read off a-models-top.png. It folds by default once a provider is set up.
xdotool mousemove 878 450 click 1; sleep 2
shot b-models-providers 1.5

# --- c. the same page from the bottom ---------------------------------------------------------------
xdotool mousemove 1100 600
for i in $(seq 1 12); do xdotool click 5; sleep 0.2; done
shot c-models-bottom 1.5

cp "$conf" "$out/conf-after.txt"
sleep 2
echo "done; shots in $out"
