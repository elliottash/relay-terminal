#!/usr/bin/env bash
# Card #MDL1 — the reasoning levels a pane offers are the MODEL's own (owner, 2026-09-21: "i want
# the effort options in relay to be determined by the model … so xhigh shows up for codex for
# example"; "for no knob models, the effort box should be grayed out. same for relay free.").
#
# Xvfb, an isolated HOME/XDG_*/TMPDIR under a short path (the 108-byte unix-socket limit), fake
# provider keys and no network turn: every step is a model switch or a level pick, which the worker
# answers without calling anybody. `isolation/enabled=false` because the worker exits under a fake
# XDG_RUNTIME_DIR otherwise. RELAY_KEYRING=off so the owner's real identity key is never touched.
#
# TWO runs, because the two halves need opposite sandboxes:
#
#  run 1  "providers" — keys for openai, kimi and glm. The pane starts on kimi, which takes three
#         levels; the OpenAI API row takes four, `xhigh` among them, which Relay's retired four
#         could not hold. Then the snap (xhigh onto a model that stops at max), then Relay Free,
#         whose box is greyed.
#
#         The Alt+E lists are opened in the order kimi (3) then openai (4) on purpose: the box's
#         list is a *shrinking* one when a pane moves the other way, and relay::FilterPopup draws
#         a list that got shorter between two opens one row short, scrolled past its first row
#         (seen here: openai then kimi showed "high" and "max" with "low" off the top, while the
#         box itself held all three). That is the Alt+E list's own sizing, not the box's contents,
#         and it belongs to whoever owns src/FilterPopup.cpp — see NOTES.md.
#  run 2  "codex" — a fake `codex` on PATH answers `codex debug models` with six levels, low
#         medium high xhigh max ultra; the worker reads that JSON itself
#         (guest_harness_codex.catalog_rows) and sends the list. No provider keys at all and
#         `cryptography` shimmed out, so Relay Free cannot stand in: the pane has nothing to
#         configure on at the first `presets`, waits, and takes rank 1 of main — codex — when the
#         background catalogue scan lands. A harness ranked first is held until the first prompt,
#         so nothing is spawned and the box is drawn from the catalogue.
set -uo pipefail
root=/home/elliott/repos/relay-terminal
out=$root/docs/qa_evidence/2026-09-21-effort-by-model
build=$root/build
width=1600 height=900
# Read off a full-window shot: the level box in the composer strip, right of the model box.
effort_x=1490 effort_y=865

mkdir -p "$out"
display=
for n in $(seq 640 670); do [[ -e /tmp/.X11-unix/X$n ]] || { display=:$n; break; }; done
[[ -z $display ]] && { echo "no free display in 640..670"; exit 1; }
echo "display $display"

xvfb_pid= relay_pid=
cleanup() { kill "${relay_pid:-0}" "${xvfb_pid:-0}" 2>/dev/null; sleep 1; }
trap cleanup EXIT

Xvfb "$display" -screen 0 $((width + 40))x$((height + 40))x24 >/dev/null 2>&1 &
xvfb_pid=$!
sleep 2
kill -0 "$xvfb_pid" 2>/dev/null || { echo "Xvfb died"; exit 1; }
export DISPLAY=$display
export RELAY_KEYRING=off

k() { xdotool key --delay 70 "$@"; }
t() { xdotool type --delay 35 "$1"; }
shot() { sleep "${2:-0.9}"; xdotool mousemove $((width + 20)) $((height + 20)); sleep 0.35; import -window root "$out/$1.png"; }
# The bottom of the window: the composer strip with the model and level boxes, the status bar under
# it, and whatever list drops open above them.
crop() { convert "$out/$1.png" -crop "${width}x470+0+$((height - 450))" +repage "$out/$1-box.png"; }
both() { shot "$1" "${2:-0.9}"; crop "$1"; }
# A command typed into the composer. The status bar keeps a line for five seconds, so a step that
# is about what Relay *said* shoots at once.
say() { xdotool mousemove 400 $((height - 70)) click 1; sleep 0.4; t "$1"; sleep 0.5; k Return; sleep "${2:-3}"; }

start_relay() {           # $1 = the sandbox, already prepared
    "$build/relay" --workspace "$1/project" >>"$out/relay-stderr.log" 2>&1 &
    relay_pid=$!
    sleep 16
    local w best=0
    win=
    for w in $(xdotool search --pid "$relay_pid" 2>/dev/null); do
        eval "$(xdotool getwindowgeometry --shell "$w" 2>/dev/null)"
        (( WIDTH * HEIGHT > best )) && { best=$(( WIDTH * HEIGHT )); win=$w; }
    done
    [[ -z $win ]] && { echo "no Relay window"; exit 1; }
    xdotool windowmove "$win" 0 0 windowsize "$win" $width $height
    xdotool windowfocus "$win"; sleep 5
}

sandbox_common() {        # $1 = sandbox root
    rm -rf "$1"
    export HOME=$1
    export XDG_CONFIG_HOME=$HOME/.config XDG_DATA_HOME=$HOME/.local/share XDG_CACHE_HOME=$HOME/.cache
    export XDG_RUNTIME_DIR=$1/run TMPDIR=$1/tmp
    mkdir -p "$XDG_CONFIG_HOME/RelayTerminal" "$XDG_DATA_HOME" "$XDG_CACHE_HOME" \
             "$XDG_RUNTIME_DIR" "$TMPDIR" "$HOME/project" "$1/bin" "$1/pyshim"
    chmod 700 "$XDG_RUNTIME_DIR"
    printf "PS1='\\\\w \\$ '\nunset PROMPT_COMMAND\n" >"$HOME/.bashrc"
    printf '# project\n' >"$HOME/project/README.md"
}

# ==================================================================================================
# run 1 — the providers with keys
# ==================================================================================================
sandbox=/tmp/claude-1000/ef1
sandbox_common "$sandbox"
export PATH=$sandbox/bin:$PATH        # empty: no guest harness in this run
unset PYTHONPATH
export RELAY_OPENAI_API_KEY=xvfb-not-a-real-key
export RELAY_KIMI_CODE_API_KEY=xvfb-not-a-real-key-two
export RELAY_GLM_CODING_API_KEY=xvfb-not-a-real-key-either

conf=$XDG_CONFIG_HOME/RelayTerminal/relay.conf
cat >"$conf" <<CONF
[instructions]
onboarded=true

[isolation]
enabled=false

[agent]
effort=high

[models]
tier\\main=kimi-code|k3|, openai|gpt-6-astra|, relay-free|relay-main|
tier\\high=openai|gpt-6-astra|xhigh
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
cp "$conf" "$out/run1-conf.txt"
start_relay "$sandbox"

# --- a. the pane on kimi, whose model takes three levels ------------------------------------------
both a-kimi-pane 2

# --- b. Alt+E on kimi: three levels, in kimi's order, and no medium -------------------------------
sleep 3
xdotool mousemove 400 $((height - 70)) click 1; sleep 0.5
k alt+e; sleep 1.8
both b-alt-e-kimi-three
k Escape; sleep 1

# --- c. the OpenAI API row: four levels, xhigh among them and no "max" ----------------------------
say "/model gpt-6-astra@openai" 4
both c-openai-pane 1
xdotool mousemove 400 $((height - 70)) click 1; sleep 0.5
k alt+e; sleep 1.8
both c2-alt-e-openai-xhigh
k Escape; sleep 1

# --- d. /effort xhigh: a word Relay's four could not hold, typed and taken ------------------------
say "/effort xhigh" 1.2
both d-effort-xhigh 0.4

# --- e. the same level onto kimi, which stops at max: it snaps, and says so on the model's line ---
say "/model kimi-k3" 1.2
both e-snap-to-max 0.4

# --- f. Relay Free: the box is greyed, and the tooltip says why ----------------------------------
say "/model relay-main" 4
both f1-relay-free-greyed 1.5
xdotool mousemove "$effort_x" "$effort_y"; sleep 2.5
import -window root "$out/f2-relay-free-tooltip.png"
crop f2-relay-free-tooltip

# --- g. Alt+E and /effort on a fixed model say why instead of changing anything -------------------
xdotool mousemove 400 $((height - 70)) click 1; sleep 0.5
k alt+e; sleep 1.2
both g1-alt-e-says-why 0.4
say "/effort low" 1.2
both g2-effort-says-why 0.4

# --- h. the `/` popup's own row for /effort, on a pane whose level is fixed ------------------------
xdotool mousemove 400 $((height - 70)) click 1; sleep 0.4
t "/eff"; sleep 1.5
both h-slash-popup-fixed
k Escape; sleep 0.5; k ctrl+a; k BackSpace; sleep 0.5

cp "$conf" "$out/run1-conf-after.txt"
cp "$XDG_DATA_HOME/relay/logs/worker.log" "$out/run1-worker.log" 2>/dev/null
kill "$relay_pid" 2>/dev/null; sleep 3; relay_pid=

# ==================================================================================================
# run 2 — codex, through a fake `codex debug models`
# ==================================================================================================
sandbox=/tmp/claude-1000/ef2
sandbox_common "$sandbox"
unset RELAY_OPENAI_API_KEY RELAY_KIMI_CODE_API_KEY RELAY_GLM_CODING_API_KEY
echo 'raise ImportError("relay free switched off for the #MDL1 evidence run")' >"$sandbox/pyshim/cryptography.py"
export PYTHONPATH=$sandbox/pyshim
cat >"$sandbox/bin/codex" <<'FAKE'
#!/bin/sh
case "$1 $2" in
  "debug models") cat <<'JSON'
{"models":[
 {"slug":"gpt-6-astra","display_name":"GPT-6-Astra",
  "supported_reasoning_levels":[{"effort":"low"},{"effort":"medium"},{"effort":"high"},{"effort":"xhigh"},{"effort":"max"},{"effort":"ultra"}],
  "default_reasoning_level":"medium"},
 {"slug":"gpt-5.6-sol","display_name":"GPT-5.6-Sol",
  "supported_reasoning_levels":[{"effort":"low"},{"effort":"medium"},{"effort":"high"},{"effort":"xhigh"},{"effort":"max"},{"effort":"ultra"}],
  "default_reasoning_level":"low"}]}
JSON
  exit 0;;
  "login status") echo "Logged in using ChatGPT"; exit 0;;
  *) echo "fake codex: $*" >&2; exit 1;;
esac
FAKE
chmod +x "$sandbox/bin/codex"
# A `claude` that answers "not logged in" at once, so this machine's real Claude Code CLI is not
# asked and the background scan is not held up by it.
printf '#!/bin/sh\necho "not logged in" >&2\nexit 1\n' >"$sandbox/bin/claude"
chmod +x "$sandbox/bin/claude"
export PATH=$sandbox/bin:$PATH

conf=$XDG_CONFIG_HOME/RelayTerminal/relay.conf
cat >"$conf" <<CONF
[instructions]
onboarded=true

[isolation]
enabled=false

[agent]
effort=high

[models]
tier\\main=guest:codex|gpt-6-astra|xhigh, guest:codex|gpt-5.6-sol|
tier\\high=guest:codex|gpt-6-astra|max
tier\\flash=guest:codex|gpt-5.6-sol|low
tier\\lite=guest:codex|gpt-5.6-sol|low
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
cp "$conf" "$out/run2-conf.txt"
start_relay "$sandbox"

# --- i. a codex pane, on xhigh ------------------------------------------------------------------
both i-codex-pane 2

# --- j. Alt+E: codex's own SIX, xhigh and ultra included -----------------------------------------
sleep 3
xdotool mousemove 400 $((height - 70)) click 1; sleep 0.5
k alt+e; sleep 1.8
both j-alt-e-codex-six
k Escape; sleep 1

# --- k. /effort ultra: a level no Relay vocabulary ever had ---------------------------------------
say "/effort ultra" 1.2
both k-effort-ultra 0.4

# --- l. the `/` popup's own row for /effort: this model's six words --------------------------------
xdotool mousemove 400 $((height - 70)) click 1; sleep 0.4
t "/eff"; sleep 1.5
both l-slash-popup-six
k Escape; sleep 0.5; k ctrl+a; k BackSpace; sleep 0.5

# --- m. the Ctrl+Alt+M dialog: the codex row's level list is the same six --------------------------
xdotool mousemove 400 $((height - 70)) click 1; sleep 0.5
k ctrl+alt+m; sleep 3
shot m-dialog-codex-levels 2

cp "$conf" "$out/run2-conf-after.txt"
cp "$XDG_DATA_HOME/relay/logs/worker.log" "$out/run2-worker.log" 2>/dev/null
echo "done; shots in $out"
