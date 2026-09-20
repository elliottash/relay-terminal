#!/usr/bin/env bash
# One helper system, live (card #FEJQ, protocol §30): an agent drives the app from a terminal
# pane, the helper panel in Options and in Sessions asks the tab's worker and its answers link
# into the app, Info and Activity draft a question into the owning pane's composer, and two tabs
# on one project keep two conversations.
#
#   docs/qa_evidence/2026-09-20-agent-app-control/drive.sh [relay-binary] [out-dir]
#
# Xvfb with an isolated HOME, XDG_CONFIG_HOME, XDG_DATA_HOME, XDG_RUNTIME_DIR and TMPDIR under a
# short path (the 108-byte socket limit), RELAY_KEYRING=off, and **no provider account**: the
# profile points a local model endpoint at stub-provider.py on 127.0.0.1, so every agent in the
# run — the terminal pane's and the tab's helper — is that script. Needs Xvfb, xdotool,
# ImageMagick, tesseract.
#
# **Four launches, one profile.** Every phase starts Relay again on the same HOME instead of
# closing panes: the first version of this script ran one window through all of it and by the
# fourth phase five panes were sharing 1500 px, the board had a card open in its editor, and a
# keystroke meant for a helper's composer went into a card title. A relaunch costs ten seconds
# and is the difference between evidence and a guess. The settings the earlier phases wrote stay,
# which is the point of the one profile.
#
# Each check writes one PASS/FAIL line to notes.txt naming the screenshot it was read from; a step
# that cannot find what it wanted logs FAIL and carries on, so one miss does not hide the rest.
set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"
out=${2:-$PWD}
root=$(cd ../../.. && pwd)
relay=${1:-$root/build/relay}
width=1600 height=1000
port=${RELAY_QA_PORT:-8831}
mkdir -p "$out"
[[ -x $relay ]] || { echo "no relay binary at $relay"; exit 1; }

# The row and the action the agent is asked to reach for. Both are what the *catalog* calls them:
# a toggle row's id is "option:" + its QSettings key (RelayWindow::toggleRow), and the link scheme
# is `option:<section>/<row>`, whose row id has a slash of its own — the panel splits at the first.
ROW=option:terminal/copy_on_select
SECTION=terminal

display=
for n in $(seq 150 199); do [[ -e /tmp/.X11-unix/X$n ]] || { display=:$n; break; }; done
[[ -z $display ]] && { echo "no free display"; exit 1; }

sandbox=$(mktemp -d /tmp/rl-fejq.XXXX)
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
printf "PS1='\\\\w \\\$ '\nunset PROMPT_COMMAND\n" >"$HOME/.bashrc"

# A board with two cards, so "open the fixture card" has something to open.
card=$(PYTHONPATH="$root/backend" python3 - "$work" <<'FIX'
import sys
from pathlib import Path
from relay_core import board as B

work = Path(sys.argv[1])
root = work / "issues"
root.mkdir(parents=True)
(root / B.BOARD_CONFIG).write_text(
    "tabs: [{id: features, folder: features}]\n"
    "columns: [inbox, discussing, ready, in-progress, needs-qa, done]\n", encoding="utf-8")
board = B.Board(root, work)
one = B.new_card("work", "Fixture card the agent opens", "inbox", created="2026-09-18",
                 rank="a", request="the agent is asked to open this one by id")
B.write_new_card(board, one, "features")
two = B.new_card("work", "Second fixture card", "inbox", created="2026-09-19", rank="b",
                 request="a second card so the list is a list")
B.write_new_card(board, two, "features")
print(one.id.upper())
FIX
)
[[ -n $card ]] || { echo "fixture failed"; exit 1; }
echo "fixture card: #$card"

python3 "$PWD/stub-provider.py" "$port" "$card" >/dev/null 2>&1 &
stub_pid=$!
sleep 1

cat >"$XDG_CONFIG_HOME/RelayTerminal/relay.conf" <<CONF
[instructions]
onboarded=true
[theme]
name=relay-dark
[provider]
preset=local:stub
CONF
printf '{"version": 1, "endpoints": [{"id": "local:stub", "label": "Stub", "base_url": "http://127.0.0.1:%s/v1", "model": "stub", "server": "openai-compatible", "context_window": 131072}]}\n' \
    "$port" >"$XDG_CONFIG_HOME/relay/local-models.json"

: >"$out/notes.txt"
pass=0 fail=0
note() { echo "$*" >>"$out/notes.txt"; }
ok()   { note "PASS $*"; pass=$((pass+1)); }
bad()  { note "FAIL $*"; fail=$((fail+1)); }

win=
t() { xdotool type --delay 30 "$1"; }
k() { xdotool key --delay 60 "$@"; }
# There is no window manager under Xvfb, so the input focus follows the pointer: a screenshot
# that parks the pointer off the window (to keep a hover highlight out of the picture) also takes
# the keyboard away from it, and the next `xdotool type` goes nowhere. The first patched run lost
# three asks that way — the panel expanded, the prompt was typed into nothing, and the log stayed
# on its placeholder. So the pointer goes back where it was and the focus is set explicitly.
shot() {
    local X=0 Y=0
    eval "$(xdotool getmouselocation --shell 2>/dev/null)"
    xdotool mousemove $((width + 20)) $((height + 20)); sleep 0.6
    import -window "$win" "$out/$1.png"
    xdotool mousemove "$X" "$Y"
    xdotool windowfocus "$win" 2>/dev/null
    sleep 0.3
}
# The notifications list is a Qt::Popup: an X window of its own, which `import -window $win` does
# not see. The Relay window sits at 0,0, so the root's coordinates are the window's.
shot_root() { sleep 0.6; import -window root "$out/$1.png"; }
text() { tesseract "$out/$1.png" - --psm 6 2>/dev/null; }
words() {   # "word left top width height", read at 2x so the small type is legible
    convert "$out/$1.png" -scale 200% png:- 2>/dev/null \
        | tesseract - stdout --psm 11 tsv 2>/dev/null \
        | awk 'NF>=12 && $12 != "" {print $12, int($7/2), int($8/2), int($9/2), int($10/2)}'
}
word_xy() { words "$1" | awk -v want="$2" 'tolower($1) ~ tolower(want) {x=$2+$4/2; y=$3+$5/2} END {if (x) print x, y}'; }
# The same, restricted to a band down the screen: a link in a helper's log is one of several
# places the same word appears (the session preview quotes the whole transcript), and the panel is
# always the bottom of the pane.
word_xy_below() { words "$1" | awk -v want="$2" -v top="$3" 'tolower($1) ~ tolower(want) && $3 > top {x=$2+$4/2; y=$3+$5/2} END {if (x) print x, y}'; }
# A word the 2x pass can read even at the size of the collapsed row's label, which the whole-page
# `text` pass sometimes cannot.
hasword() { if [[ -n $(word_xy "$2" "$3") ]]; then ok "$1 (\"$3\" read in $2.png)"; else bad "$1: no \"$3\" in $2.png"; fi; }
click_at() { [[ -z ${1:-} || -z ${2:-} ]] && return 1; xdotool mousemove "$1" "$2" click 1; sleep 1.5; }
# Shoot until the screen says something, or give up: a helper's first ask starts its worker, and
# a fixed sleep is either a guess that is too short or a minute spent on every step.
await() {   # shot pattern [seconds]
    local waited=0 limit=${3:-45}
    while :; do
        shot "$1"
        text "$1" | grep -qi -- "$2" && return 0
        (( waited >= limit )) && return 1
        sleep 3; waited=$((waited + 3))
    done
}
has()   { if text "$2" | grep -qi -- "$3"; then ok "$1 (\"$3\" in $2.png)"; else bad "$1: no \"$3\" in $2.png"; fi; }
hasnt() { if text "$2" | grep -qi -- "$3"; then bad "$1: \"$3\" is in $2.png and it should not be"
          else ok "$1 (no \"$3\" in $2.png)"; fi; }
awaited() { if await "$2" "$3" "${4:-45}"; then ok "$1 (\"$3\" in $2.png)"; else bad "$1: no \"$3\" in $2.png after ${4:-45}s"; fi; }

launch() {   # a fresh window on the same profile; the phases do not share a layout
    [[ -n $relay_pid ]] && { kill "$relay_pid" 2>/dev/null; wait "$relay_pid" 2>/dev/null; sleep 2; }
    (cd "$work" && exec "$relay" --workspace "$work" --fresh) >>"$sandbox/relay.log" 2>&1 &
    relay_pid=$!
    sleep 10
    win= ; local best=0 w
    for w in $(xdotool search --pid "$relay_pid" 2>/dev/null); do
        eval "$(xdotool getwindowgeometry --shell "$w" 2>/dev/null)"
        (( WIDTH * HEIGHT > best )) && { best=$(( WIDTH * HEIGHT )); win=$w; }
    done
    [[ -z $win ]] && { echo "no Relay window"; tail -40 "$sandbox/relay.log"; return 1; }
    xdotool windowmove "$win" 0 0 windowsize "$win" $width $height
    xdotool windowfocus "$win"; sleep 4
    # The first launch asks how tools are approved, in a pane of its own that would otherwise take
    # half the window for the whole run. Take the recommended answer.
    shot _approvals
    local yes; yes=$(word_xy _approvals "recommend")
    [[ -n $yes ]] && click_at ${yes% *} ${yes#* }
    sleep 2
}
# The terminal pane's prompt box. Its foot is not a fixed corner: `app_action_run` opens the
# Activity pane under the terminal, and a click at the bottom-left then lands in *that* pane —
# which is how the first run's fifth prompt was never typed. So it is found by its placeholder.
focus_prompt() {
    shot _prompt
    local at; at=$(word_xy _prompt "prompts")
    if [[ -n $at ]]; then xdotool mousemove ${at% *} ${at#* } click 1
    else xdotool mousemove 260 $((height - 75)) click 1; fi
    sleep 1
}
ask_pane() { focus_prompt; t "$1"; k Return; }
# Open a helper panel and put the cursor in its composer. The key expands it and focuses the box,
# but a screenshot in between hands the focus back to the window rather than to the widget, so the
# composer is clicked as well: it is found from the Send button at the end of its strip, which is
# the one word in the panel that appears exactly once.
focus_helper() {
    k alt+q; sleep 2
    shot "$1"
    local at; at=$(word_xy "$1" "^send$")
    [[ -n $at ]] && click_at $(( ${at% *} - 200 )) $(( ${at#* } - 45 ))
    sleep 1
}

# ============================================================ (a) the agent drives the app
launch || exit 1
shot 00-start
has "00 Relay is up on the stub provider" 00-start "stub"

ask_pane "open options at copy on select please"
awaited "a1 app_open opened the Options pane" 01-open-options "Search options"
hasnt "a1 and the tool did not come back refused" 01-open-options "has no option row"

ask_pane "turn copy on select on"
awaited "a2 the row is marked as changed by the agent" 02-option-set "changed by the agent"
awaited "a2 the agent's own answer names before and after" 02-option-set "from off to on" 20

# The announcement is a notification, and Undo is its action (§30.6). The bell is the first
# button of the window's right-hand row.
click_at $((width - 278)) 27
shot_root 03-notification
has "a3 the notification names the change" 03-notification "Copy on select"
has "a3 and offers Undo" 03-notification "Undo"
undo=$(word_xy 03-notification "^undo$")
if [[ -n $undo ]]; then
    click_at ${undo% *} ${undo#* }; sleep 3
    shot 04-undone
    hasnt "a4 Undo put the row back and took the mark off it" 04-undone "changed by the agent"
else
    bad "a4 no Undo button found in 03-notification.png"; shot 04-undone
fi
k Escape; sleep 1

ask_pane "run the activity action for me"
awaited "a5 an agent_safe action ran (Activity opened)" 05-action-run "Activity"
hasnt "a5 and the action was not refused" 05-action-run "has no action"

ask_pane "search my sessions for anything about relay"
awaited "a6 the sessions search was answered inside the worker" 06-sessions-search "answered inside"

ask_pane "open the fixture card on the board"
awaited "a7 app_open opened the Switchboard on the card" 07-open-card "$card" 90

# ================================================ (b) the helper inside Options, and its links
launch || exit 1
k ctrl+shift+o; sleep 4
shot 08-options
hasword "b1 Options carries the helper's collapsed row" 08-options "helper"
focus_helper 09-options-ask
has "b2 expanded, it says which helper it is" 09-options-ask "Options helper"
t "where is copy on select?"; k Return
# The **first** ask in a launch starts the tab's worker (owner decision 5), so this one waits
# for a python start, a configure and a board open as well as the turn; the second ask below
# lands on a warm worker and is quick. 60 s was not enough and read as "no answer".
awaited "b3 the helper answered in the Options panel" 10-options-answer "Clicking that opens" 180
# The answer streams, and `await` returns on the first frame that matches — which can be the
# sentence before the link. Let the turn finish before looking for something to click.
sleep 6; shot 10-options-answer

link=$(word_xy_below 10-options-answer "^copy$" 640)
[[ -z $link ]] && link=$(word_xy_below 10-options-answer "^select" 640)
if [[ -n $link ]]; then
    click_at ${link% *} ${link#* }; sleep 3
    shot 11-options-link
    has "b4 the option: link revealed the row in this pane" 11-options-link "Copy on select"
else
    bad "b4 no link text found in 10-options-answer.png"; shot 11-options-link
fi

# The helper's own write. It travels on the *helper worker's* pipe, which is the round trip that
# answered `no_reply` until c78c8004.
focus_helper _helper2
t "switch copy on select on for me"; k Return
awaited "b5 the helper's own app_option_set went through" 12-helper-write "turned" 150
has "b5 and it is announced with an Undo" 12-helper-write "Undo"

# ======================================== (c) the helper in Sessions, and (d) the Ask rows
launch || exit 1
k ctrl+shift+y; sleep 5
shot 13-sessions
hasword "c1 Sessions carries the helper's collapsed row" 13-sessions "helper"
focus_helper 14-sessions-ask
has "c2 expanded, it says which helper it is" 14-sessions-ask "Sessions helper"
t "which conversation changed that setting?"; k Return
awaited "c3 the helper answered in the Sessions panel" 15-sessions-answer "those turns were about" 180
sleep 6; shot 15-sessions-answer          # the link is the answer's last words
link=$(word_xy_below 15-sessions-answer "^copy$" 640)
[[ -z $link ]] && link=$(word_xy_below 15-sessions-answer "^select" 640)
if [[ -n $link ]]; then
    click_at ${link% *} ${link#* }; sleep 4
    shot 16-sessions-link
    has "c4 the option: link from Sessions opened Options at the row" 16-sessions-link "Search options"
else
    bad "c4 no link text found in 15-sessions-answer.png"; shot 16-sessions-link
fi

launch || exit 1
focus_prompt; k alt+i; sleep 5
shot 17-info
has "d1 the info pane has an Ask row" 17-info "Ask the agent about this session"
chip=$(word_xy 17-info "costliest")
if [[ -n $chip ]]; then
    click_at ${chip% *} ${chip#* }; sleep 2
    shot 18-info-draft
    has "d2 the Info Ask chip drafted a question into the composer" 18-info-draft "turn"
else
    bad "d2 no Ask chip found in 17-info.png"; shot 18-info-draft
fi

focus_prompt; k alt+shift+r; sleep 5
shot 19-activity
has "d3 the Activity pane has an Ask row" 19-activity "Ask the agent about this activity"
chip=$(word_xy 19-activity "slowest")
if [[ -n $chip ]]; then
    click_at ${chip% *} ${chip#* }; sleep 2
    shot 20-activity-draft
    has "d4 the Activity Ask chip drafted a question into the composer" 20-activity-draft "turn"
else
    bad "d4 no Ask chip found in 19-activity.png"; shot 20-activity-draft
fi

# ========================================= (e) two tabs on one project, two conversations
launch || exit 1
k ctrl+shift+s; sleep 7
shot 21-tab1-board
has "e0 tab 1 has a Switchboard" 21-tab1-board "Switchboard"
k ctrl+shift+t; sleep 5           # a second tab in the same folder
k ctrl+shift+s; sleep 8
shot 22-tab2-board
k a; sleep 1                      # `a` is the board's own ask key
t "is this board mine alone?"; k Return
awaited "e1 tab 2's Switchboard answered" 23-tab2-answer "belongs to the tab" 180
k ctrl+shift+Tab; sleep 4
shot 24-tab1-after
hasnt "e2 tab 1's Switchboard did not draw tab 2's answer" 24-tab1-after "belongs to the tab"

note "---- $pass passed, $fail failed ----"
tail -1 "$out/notes.txt"
cp "$sandbox/relay.log" "$out/relay.log" 2>/dev/null
mkdir -p "$out/logs" && cp -r "$sandbox/home/.local/share/relay/logs/." "$out/logs/" 2>/dev/null
rm -f "$out/_approvals.png" "$out/_prompt.png" "$out/_helper2.png"
[[ $fail -eq 0 ]]
