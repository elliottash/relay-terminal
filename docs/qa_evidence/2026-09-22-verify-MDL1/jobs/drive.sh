#!/usr/bin/env bash
# Card #MDL1 — the models pane's fourth tab, **jobs** (docs/MODEL-PICKING-DESIGN.md 5.9; owner,
# 2026-09-21: "for the per-job models, i think that should be reviewed and improved and made a 4th
# tab. take a careful look at it to see how to improve it for that").
#
# Xvfb, an isolated HOME/XDG_*/TMPDIR under a short path (the 108-byte unix-socket limit), fake
# provider keys and no network turn: the whole tab is drawn from `model_roles`, which the worker
# resolves out of stored keys alone and sends with `configured`. RELAY_KEYRING=off so the owner's
# real identity key is never touched; `isolation/enabled=false` because the worker exits under a
# fake XDG_RUNTIME_DIR otherwise.
#
#   a  the models pane on **jobs** (Alt+4): fifteen rows grouped by tier, "runs on" filled from a
#      live worker — this is the column the retired modal never had
#   b  the row highlighted, with the model list Enter drops
#   c  a model picked, then a level: the override cell
#   d  the worker's next report: the "runs on" column has followed the override
#   e  Delete: the job is back on its tier, in both columns
#   e1 the models pane moved to providers, so the next step is a change and not a coincidence
#   f  Options › Models, the "per-job models" row, reached by its own search
#   g  that row pressed: the models pane, on the jobs tab
#
# Two things the shots are asked to show that a unit test cannot: that a *real* worker's
# `model_roles` fills the column (the unit test hands it a JSON object), and that the override
# reaches that worker and comes back — steps d and e are the loop, not the write.
set -uo pipefail
root=/home/elliott/repos/relay-terminal
out=/tmp/mdl1verify-artifacts/jobs
build=${RELAY_BUILD:-$root/build}
width=1600 height=900

mkdir -p "$out"
display=
for n in $(seq 840 870); do [[ -e /tmp/.X11-unix/X$n ]] || { display=:$n; break; }; done
[[ -z $display ]] && { echo "no free display in 840..870"; exit 1; }
echo "display $display"

sandbox=/tmp/mv-mpj          # short: XDG_RUNTIME_DIR holds a unix socket
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
cat >"$conf" <<CONF
[isolation]
enabled=false

[instructions]
onboarded=true

[suggestions]
next_command=false
next_prompt=false

[security]
approvals_chosen=true
approvals_ask=@Invalid()

[url_handler]
announced=true

# The five lists, pre-seeded on a cloud provider. Without this, rank 1 of main on this machine is
# the guest harness Relay finds on PATH, a guest pane starts its process on the **first turn**, and
# nothing is ever configured — so no model_roles is ever sent and every "runs on" cell reads an em
# dash. That is the tab telling the truth, but it is not what the column is for. A stored key is
# all the worker needs to resolve a role: no turn, no network.
[models]
tier\\main=glm-coding|glm-5.3|high, kimi-code|kimi-for-coding|high
tier\\high=glm-coding|glm-5.3|max
tier\\flash=glm-coding|glm-5.3-flash|low
tier\\lite=relay-free|relay-lite|low
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

# --- a. the models pane, on jobs -----------------------------------------------------------------
k ctrl+shift+m; sleep 6
k alt+4; sleep 3
shot a-jobs-runs-on 2.5

# --- b. the row: down to "summaries", and the list Enter drops ------------------------------------
# Seven Downs from "agent turns": subagents, helper agent, plan mode, /high panes, terminal driving,
# /flash panes, summaries. The five group headings are `NoItemFlags`, so Down steps over them —
# which is the point of the flat rows, and this is where it is proved with a real key map.
for _ in $(seq 7); do k Down; done
sleep 1.5
shot b-summaries-highlighted 1.5
k Return; sleep 2.5
shot b1-model-list 1.5

# --- c. pick a model, then a level ----------------------------------------------------------------
# "kimi-k3" and not "kimi": the run before this one typed the shorter word, the first match was
# `kimi-for-coding`, that model has no reasoning knob at all — so no level list opened, and the
# Down and Return meant for it went into the tree and opened another row's list instead.
t "kimi-k3"; sleep 1.5
shot c0-filtered 1.2
k Return; sleep 2.5
shot c1-level-list 1.5
k Down; sleep 0.6
k Return; sleep 3
shot c-override-set 2.5

# --- d. the worker's report: the column has followed ----------------------------------------------
sleep 4
shot d-runs-on-followed 2.5

# --- e. Delete: back on its tier ------------------------------------------------------------------
k Delete; sleep 4
shot e-override-cleared 2.5

# --- f/g. Options › Models, the "per-job models" row ----------------------------------------------
# Driven through the pane's own search box rather than by clicking at fixed coordinates, for the
# reason the t:a11 run learned: a click where a row "should be" lands on another tab.
#
# The models pane is put on **providers** first, so that "the row lands on the jobs tab" is a
# change and not a coincidence — the first run pressed the row while jobs was already in front and
# the shot proved nothing.
k alt+1; sleep 2.5
shot e1-models-pane-on-providers 1.5
k ctrl+shift+o; sleep 7
t "per-job models"; sleep 3
shot f-options-per-job-row 2
k Return; sleep 6
shot g-row-lands-on-jobs 2.5
cp "$conf" "$out/conf-after.txt"

sleep 2
echo "done; shots in $out"
