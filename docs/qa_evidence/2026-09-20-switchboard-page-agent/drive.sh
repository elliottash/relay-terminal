#!/usr/bin/env bash
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# QA harness for the Switchboard page agent (#8YQ9, protocol 19.18).
#
#   docs/qa_evidence/2026-09-20-switchboard-page-agent/drive.sh [build-dir]
#
# Shaped after docs/qa_evidence/2026-09-19-helpful-line-breaks/drive.sh. It does the parts that
# can be scripted blind:
#
#   * builds the fixture board under /tmp/q8/home/proj/.switchboard (short path: the worker's
#     Unix socket must fit in 108 bytes, and XDG_RUNTIME_DIR/TMPDIR are isolated under it),
#     including two deliberately malformed cards — one with no `id` (which belongs to no section,
#     so only the unscoped Check finds it) and one in Executing with an unknown front matter
#     field (which is what the section's ⚠ triage finds);
#   * builds the fresh project /tmp/q8/home/newproj with a TODO.md of two items, for the survey;
#   * starts stub-provider.py on 127.0.0.1:8823 and points the profile's `local:stub` endpoint
#     at it — the `switchboard` role defaults to the main agent's model, so the board worker's
#     page agent answers on the stub with no key anywhere;
#   * boots Relay under Xvfb with an isolated HOME / XDG_CONFIG_HOME / XDG_DATA_HOME /
#     XDG_RUNTIME_DIR / TMPDIR and RELAY_KEYRING=off, opens the Switchboard and widens its pane;
#   * sends one prompt, queues two behind it, and shoots the streaming answer, the queue and the
#     Stop button.
#
# The rest of the run was driven by hand from the same shell (`xdotool mousemove … click 1`),
# because the buttons move as the panel grows and a blind click lands somewhere else — one did,
# in the first take: `n` reached the list and created a card. Every coordinate below was read off
# a screenshot first. The README says which shot came from where.
#
# Needs Xvfb, xdotool, ImageMagick, tesseract. It leaves Relay running; kill it with
# `kill $(sed -n 's/^RELAY_PID=//p' /tmp/q8/state)`.
set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"
out=$PWD
root=$(cd ../../.. && pwd)
build=${1:-$root/build}
sandbox=/tmp/q8
port=8823
width=1480 height=1000

# ---------------------------------------------------------------- the sandbox and the fixtures
rm -rf "$sandbox"
mkdir -p "$sandbox/home" "$sandbox/run" "$sandbox/tmp"
chmod 700 "$sandbox/run"
work=$sandbox/home/proj
mkdir -p "$work/.switchboard/features" "$work/.switchboard/changes" "$work/.switchboard/threads"
cat >"$work/.switchboard/board.yaml" <<'YAML'
# Switchboard configuration. Format: docs/SWITCHBOARD-FORMAT.md
version: 1
tabs: [{id: features, folder: features}, {id: bugs, folder: changes}, {id: done, filter: 'status:done,dropped'}]
columns: [inbox, discussing, planning, planned, executing, needs-verification, needs-qa, done]
agent: {autonomy: auto, max_creates_per_turn: 5}
memory: {autonomy: auto}
YAML
card() {   # id status rank title body file
    cat >"$work/.switchboard/features/$6" <<EOF
---
id: $1
type: work
status: $2
assignee: agent
rank: $3
created: '2026-09-20'
links: {commits: [], evidence: [], github: null, plans: [], related: []}
---
# $4

## Issue
$5
EOF
}
card A1B2 inbox              mmmm "Voice input in the composer"  "A microphone in the composer that dictates a prompt."      2026-09-20-voice-input-in-the-composer.md
card C3D4 inbox              mmnn "Dictate a prompt with the mic" "Duplicate of the voice card: dictation from the composer." 2026-09-20-dictate-a-prompt-with-the-mic.md
card E5F6 discussing         mmoo "Vertical tabs on the left"     "Put the tab strip down the left edge."                     2026-09-20-vertical-tabs-on-the-left.md
card G7H8 executing          mmpp "Fold the thinking trace"       "Collapse thinking by default; a click unfolds it."         2026-09-20-fold-the-thinking-trace.md
card J9K1 needs-verification mmqq "Equalize pane sizes"           "A menu item that makes every pane the same size."          2026-09-20-equalize-pane-sizes.md
# Deliberately malformed #1: no `id`. The board's own check reports `missing_id`; it belongs to
# no section (BoardCommands._problem_section skips a card with no id), so a scoped triage leaves
# it out and only the unscoped Check button and the banner over the list report it.
cat >"$work/.switchboard/features/2026-09-20-broken-card-with-no-id.md" <<'EOF'
---
type: work
status: inbox
assignee: agent
rank: mmrr
created: '2026-09-20'
---
# Broken card with no id

## Issue
This card's front matter has no `id:` line, which is what the board's check is meant to catch.
EOF
# Deliberately malformed #2: a valid id in Executing with an unknown front matter field, so the
# Executing header's ⚠ has exactly one thing to find.
cat >"$work/.switchboard/features/2026-09-20-stray-front-matter-field.md" <<'EOF'
---
id: N2M3
type: work
status: executing
assignee: agent
rank: mmss
created: '2026-09-20'
urgency: sometime
links: {commits: [], evidence: [], github: null, plans: [], related: []}
---
# Stray front matter field

## Issue
This card carries an `urgency:` key that the card format does not define.
EOF
python3 "$root/scripts/relay-board.py" --issues "$work/.switchboard" check --json

# The fresh project the survey runs on (item 10). No board: the picker's "Initialize new project
# here" makes one, and `board_init` leaves `survey-state.json` at `pending`.
mkdir -p "$sandbox/home/newproj"
cat >"$sandbox/home/newproj/TODO.md" <<'EOF'
# TODO

- [ ] Write the README
- [ ] Add a licence header to every source file
EOF
printf 'print("hello")\n' >"$sandbox/home/newproj/main.py"

# ---------------------------------------------------------------- the stub provider and Relay
pkill -f "stub-provider.py $port" 2>/dev/null
setsid nohup python3 "$out/stub-provider.py" "$port" --slow 4 >"$sandbox/stub.log" 2>&1 </dev/null &
sleep 2
curl -s -m 3 "http://127.0.0.1:$port/v1/models" || { echo "stub did not come up"; exit 1; }

display=
for n in $(seq 170 199); do [[ -e /tmp/.X11-unix/X$n ]] || { display=:$n; break; }; done
[[ -z $display ]] && { echo "no free display"; exit 1; }

export HOME=$sandbox/home XDG_RUNTIME_DIR=$sandbox/run TMPDIR=$sandbox/tmp
export XDG_CONFIG_HOME=$HOME/.config XDG_DATA_HOME=$HOME/.local/share XDG_CACHE_HOME=$HOME/.cache
mkdir -p "$XDG_CONFIG_HOME/RelayTerminal" "$XDG_CONFIG_HOME/relay" "$XDG_DATA_HOME" "$XDG_CACHE_HOME"
ln -sfn "/run/user/$(id -u)/bus" "$sandbox/run/bus"
printf "PS1='\\\\w \$ '\nunset PROMPT_COMMAND\n" >"$HOME/.bashrc"
# `approvals_chosen` answers the first-launch Approvals pane, which would otherwise be the first
# thing on screen and has nothing to do with this card.
cat >"$XDG_CONFIG_HOME/RelayTerminal/relay.conf" <<CONF
[instructions]
onboarded=true
[theme]
name=relay-dark
[appearance]
pane_colours=type
[provider]
preset=local:stub
[security]
approvals_chosen=true
approvals_ask=@Invalid()
CONF
printf '{"version": 1, "endpoints": [{"id": "local:stub", "label": "Stub", "base_url": "http://127.0.0.1:%s/v1", "model": "stub", "server": "openai-compatible", "context_window": 131072}]}\n' \
    "$port" >"$XDG_CONFIG_HOME/relay/local-models.json"

nohup Xvfb "$display" -screen 0 $((width + 120))x$((height + 80))x24 >"$sandbox/xvfb.log" 2>&1 &
xvfb_pid=$!
sleep 2
export DISPLAY=$display RELAY_KEYRING=off RELAY_SESSION=qa8yq9
cd "$work" || exit 1
nohup "$build/relay" --workspace "$work" >"$sandbox/relay.log" 2>&1 &
relay_pid=$!
sleep 8
win= ; best=0
for w in $(xdotool search --pid "$relay_pid" 2>/dev/null); do
    eval "$(xdotool getwindowgeometry --shell "$w" 2>/dev/null)"
    (( WIDTH * HEIGHT > best )) && { best=$(( WIDTH * HEIGHT )); win=$w; }
done
[[ -z $win ]] && { echo "no Relay window"; tail -40 "$sandbox/relay.log"; exit 1; }
xdotool windowmove "$win" 0 0 windowsize "$win" $width $height windowfocus "$win"
sleep 1.5
cat >"$sandbox/state" <<STATE
DISPLAY=$display
WIN=$win
RELAY_PID=$relay_pid
XVFB_PID=$xvfb_pid
WORK=$work
PORT=$port
WIDTH=$width
HEIGHT=$height
STATE

shot() {   # shot <name> [crop]
    xdotool mousemove $((width + 40)) $((height + 40)); sleep 0.7
    import -window "$win" "$sandbox/_full.png"
    convert "$sandbox/_full.png" -crop "${2:-1046x960+434+40}" +repage "$out/$1.png"
}

# ---------------------------------------------------------------- the board and its layout
xdotool key --delay 80 ctrl+shift+s; sleep 6
# Widen the Switchboard: the pane splitter is a few pixels wide, so four x positions are tried.
for x in 738 740 742 744; do
    xdotool mousemove $x 600; sleep 0.2; xdotool mousedown 1; sleep 0.3
    xdotool mousemove $((x - 5)) 600; sleep 0.2; xdotool mousemove 430 600; sleep 0.4
    xdotool mouseup 1; sleep 0.5
done
# Unfold the four sections that hold cards, bottom up (unfolding moves everything below it).
for y in 431 393 279 241; do xdotool mousemove 520 $y; sleep 0.2; xdotool click 1; sleep 0.5; done
sleep 1
shot 01-panel-buttons

# ---------------------------------------------------------------- one turn, two queued prompts
# The composer is clicked **once**: it keeps the keyboard across Enter, and re-clicking a box
# whose y has moved is how the first take typed into the card list instead.
xdotool mousemove 890 941 click 1; sleep 0.8
xdotool type --delay 25 "which cards are duplicates of each other?"; sleep 0.4; xdotool key Return
sleep 7;   shot 02-answer-streaming
xdotool type --delay 25 "then sort the inbox by rank";              sleep 0.4; xdotool key Return
sleep 2.5; shot 03-second-prompt-queued
xdotool type --delay 25 "and label the two voice cards";            sleep 0.4; xdotool key Return
sleep 2.5; shot 04-send-reads-stop

echo "Relay is up on $display (window $win); state in $sandbox/state."
echo "The rest of the run is by hand — see README.md:"
echo "  Stop           click the Send button, which now reads Stop"
echo "  Check          click Check in the panel's button row"
echo "  triage         hover the EXECUTING header, click the ⚠ at its right end"
echo "  finding        click a finding line; the fix request lands in the composer, unsent"
echo "  Ctrl+/         opens the Actions palette instead of the composer (bug, see README)"
echo "  the survey     new tab, cd /tmp/q8/home/newproj, Ctrl+Shift+A → \"Projects\" → Enter,"
echo "                 then \"Initialize new project here\""
