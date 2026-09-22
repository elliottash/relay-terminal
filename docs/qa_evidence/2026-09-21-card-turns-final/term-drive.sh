#!/usr/bin/env bash
# The terminal pane, before and after — the ten-shot pixel gate (card #AGNT step 1, reused by
# #CTRN's final pass because `src/Pane.h` was opened again).
#
# **Copied from `../2026-09-20-agent-console-extraction/drive.sh`** (which is the record of that
# day and is not edited) with one change, and the change is the whole reason for the copy: the
# profile now pins the **Main tier list** to the stub as well as `provider/preset`. Since card
# #MDL1 a pane starts on rank 1 of the Main list, and a fresh profile takes the worker's tier
# defaults — which on a machine with Claude Code installed is that guest harness. Run without the
# pin, the "terminal pane" of this drive was running `claude-fable-5.1`, answered "Not logged in ·
# Please run /login" and re-opened the approvals pane mid-run, so five of its six checks failed
# and not one pixel of it was about Relay.
#
#   drive.sh <relay-binary> <out-dir>
#
# Run it twice -- once on a build of the tip the extraction started from, once on a build of the
# landed tree -- and compare the two shot sets with `compare -metric AE`. The terminal pane must
# behave byte-for-byte as it did; that is the one real risk in the step (#AGNT, Risks 1), and this
# is what says so.
#
# Xvfb with an isolated HOME, XDG_CONFIG_HOME, XDG_DATA_HOME, XDG_RUNTIME_DIR and TMPDIR under a
# short path (the 108-byte socket limit), RELAY_KEYRING=off, and **no provider account**: the
# profile points a local model endpoint at stub-provider.py on 127.0.0.1, so the pane's agent is
# that script and answers the same words in the same chunks every time. Needs Xvfb, xdotool,
# ImageMagick, tesseract.
#
# The shots, in order, are the scenes the step must not change:
#
#   01 a fresh pane           04 the tool-call row        07 a queued second prompt, its strip
#   02 a turn with thinking   05 the model box popup      08 Esc during a turn
#   03 the fold unfolded      06 the model box closed     09 the pane after it all
#
# Determinism: one launch, one pane, a fixed window size, a fixed font, the same keystrokes with
# the same delays, and a stub whose sleeps are constant. What still moves between two runs is the
# clock Relay prints itself -- the turn timer in the prompt-box strip, and "thought for N s" on a
# settled reasoning anchor -- so a non-zero difference in shots 02, 03 and 08 is read, not waved
# away: NOTES.md names the pixels.
set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"
relay=${1:?say which relay binary}
out=${2:?say where the shots go}
width=1400 height=900
port=${RELAY_QA_PORT:-8837}
mkdir -p "$out"
[[ -x $relay ]] || { echo "no relay binary at $relay"; exit 1; }

display=
for n in $(seq 170 199); do [[ -e /tmp/.X11-unix/X$n ]] || { display=:$n; break; }; done
[[ -z $display ]] && { echo "no free display"; exit 1; }

sandbox=$(mktemp -d /tmp/rl-agnt.XXXX)
stub_pid= xvfb_pid= relay_pid=
cleanup() {
    local pid
    for pid in $relay_pid $stub_pid $xvfb_pid; do kill "$pid" 2>/dev/null; done
    rm -rf "$sandbox"
}
trap cleanup EXIT

Xvfb "$display" -screen 0 $((width + 40))x$((height + 40))x24 >/dev/null 2>&1 &
xvfb_pid=$!
sleep 2
kill -0 "$xvfb_pid" 2>/dev/null || { echo "Xvfb died on $display"; exit 1; }
export DISPLAY=$display RELAY_KEYRING=off

mkdir -p "$sandbox/home" "$sandbox/run" "$sandbox/tmp"; chmod 700 "$sandbox/run"
ln -sfn "/run/user/$(id -u)/bus" "$sandbox/run/bus"
export HOME=$sandbox/home XDG_RUNTIME_DIR=$sandbox/run TMPDIR=$sandbox/tmp
export XDG_CONFIG_HOME=$HOME/.config XDG_DATA_HOME=$HOME/.local/share XDG_CACHE_HOME=$HOME/.cache
work=$HOME/project
mkdir -p "$XDG_CONFIG_HOME/RelayTerminal" "$XDG_CONFIG_HOME/relay" "$XDG_DATA_HOME" "$work"
# A prompt that never moves: no git branch, no host name, no time.
printf "PS1='\\\\w \\\$ '\nunset PROMPT_COMMAND\n" >"$HOME/.bashrc"
printf 'the fixture file the read_file call reads\n' >"$work/fixture.txt"

python3 "$PWD/stub-provider.py" "$port" >/dev/null 2>&1 &
stub_pid=$!
sleep 1

cat >"$XDG_CONFIG_HOME/RelayTerminal/relay.conf" <<CONF
[instructions]
onboarded=true
[theme]
name=relay-dark
[provider]
preset=local:stub
[approvals]
mode=allow
[models]
; Rank 1 of the Main list is what a new pane starts on (card #MDL1, rule 3). Setting any tier
; list makes `curation::tierListsSet()` true, so the worker's defaults are not applied over it.
tier/main=local:stub|stub|
[roles]
; Every helper on the stub too, so nothing in the window reaches for a guest harness.
switchboard/preset=local:stub
switchboard/model=stub
CONF
printf '{"version": 1, "endpoints": [{"id": "local:stub", "label": "Stub", "base_url": "http://127.0.0.1:%s/v1", "model": "stub", "server": "openai-compatible", "context_window": 131072}]}\n' \
    "$port" >"$XDG_CONFIG_HOME/relay/local-models.json"

t() { xdotool type --delay 25 "$1"; }
k() { xdotool key --delay 60 "$@"; }
win=
shot() {
    local X=0 Y=0
    eval "$(xdotool getmouselocation --shell 2>/dev/null)"
    xdotool mousemove $((width + 20)) $((height + 20)); sleep 0.8
    import -window "$win" "$out/$1.png"
    xdotool mousemove "$X" "$Y"
    xdotool windowfocus "$win" 2>/dev/null
    sleep 0.3
}
text() { tesseract "$out/$1.png" - --psm 6 2>/dev/null; }
words() {
    convert "$out/$1.png" -scale 200% png:- 2>/dev/null \
        | tesseract - stdout --psm 11 tsv 2>/dev/null \
        | awk 'NF>=12 && $12 != "" {print $12, int($7/2), int($8/2), int($9/2), int($10/2)}'
}
word_xy() { words "$1" | awk -v want="$2" 'tolower($1) ~ tolower(want) {x=$2+$4/2; y=$3+$5/2} END {if (x) print x, y}'; }
click_at() { [[ -z ${1:-} || -z ${2:-} ]] && return 1; xdotool mousemove "$1" "$2" click 1; sleep 1.2; }
# Shoot until the screen says something, or give up.
await() {   # shot pattern [seconds]
    local waited=0 limit=${3:-45}
    while :; do
        shot "$1"
        text "$1" | grep -qi -- "$2" && return 0
        (( waited >= limit )) && return 1
        sleep 3; waited=$((waited + 3))
    done
}
: >"$out/notes.txt"
note() { echo "$*" >>"$out/notes.txt"; }
ok()  { note "PASS $*"; }
bad() { note "FAIL $*"; }
has() { if text "$2" | grep -qi -- "$3"; then ok "$1 (\"$3\" in $2.png)"; else bad "$1: no \"$3\" in $2.png"; fi; }

(cd "$work" && exec "$relay" --workspace "$work" --fresh) >>"$sandbox/relay.log" 2>&1 &
relay_pid=$!
sleep 10
best=0
for w in $(xdotool search --pid "$relay_pid" 2>/dev/null); do
    eval "$(xdotool getwindowgeometry --shell "$w" 2>/dev/null)"
    (( WIDTH * HEIGHT > best )) && { best=$(( WIDTH * HEIGHT )); win=$w; }
done
[[ -z $win ]] && { echo "no Relay window"; tail -40 "$sandbox/relay.log"; exit 1; }
xdotool windowmove "$win" 0 0 windowsize "$win" $width $height
xdotool windowfocus "$win"; sleep 4
# The first launch asks how tools are approved, in a pane of its own. Take the recommended answer.
shot _approvals
yes=$(word_xy _approvals "recommend")
[[ -n $yes ]] && click_at ${yes% *} ${yes#* }
sleep 3

focus_prompt() {
    shot _prompt
    local at; at=$(word_xy _prompt "prompts")
    if [[ -n $at ]]; then xdotool mousemove ${at% *} ${at#* } click 1
    else xdotool mousemove 260 $((height - 75)) click 1; fi
    sleep 1
}
ask() { focus_prompt; t "$1"; k Return; }

# ----- 01 a fresh pane ---------------------------------------------------------------------------
shot 01-fresh
has "the pane is up with its prompt box" 01-fresh "prompts"

# ----- 02/03 a turn with thinking, folded and unfolded --------------------------------------------
ask "explain the fold please"
await 02-thinking "thought for" 60 || bad "the reasoning anchor never settled"
has "the reasoning fold settled under its anchor" 02-thinking "thought for"
k alt+r; sleep 2                       # agent.thinkingPanel: unfold this pane's latest reasoning
shot 03-fold-open
has "Alt+R unfolds the reasoning" 03-fold-open "OSC"
k alt+r; sleep 2                       # and folds it again, so 04 starts from the same screen
shot 03b-fold-closed

# ----- 04 a tool-call row -------------------------------------------------------------------------
ask "read the fixture file"
await 04-toolrow "fixture" 60 || bad "the tool-call row never printed"
has "the tool call drew its own row" 04-toolrow "read"

# ----- 05/06 the model box popup ------------------------------------------------------------------
focus_prompt
k alt+m; sleep 2                       # agent.modelBox: drop this prompt box's model box open
shot 05-modelbox
has "the model box dropped open" 05-modelbox "stub"
k Escape; sleep 1.5
shot 06-modelbox-closed

# ----- 07 a queued second prompt, with the queue strip --------------------------------------------
ask "count slowly for me"
sleep 3
focus_prompt; t "count slowly again"; k Return   # lands behind the running turn
sleep 2
shot 07-queued
has "the second prompt queued, with its strip" 07-queued "queue"

# ----- 08 Esc during a turn -----------------------------------------------------------------------
focus_prompt
k Escape; sleep 3
shot 08-esc

# ----- 09 the pane afterwards ---------------------------------------------------------------------
sleep 6
shot 09-after

cp "$sandbox/relay.log" "$out/relay.log" 2>/dev/null
grep -c '^PASS' "$out/notes.txt" | sed 's/^/PASS lines: /'
grep -c '^FAIL' "$out/notes.txt" | sed 's/^/FAIL lines: /'
