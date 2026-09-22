#!/usr/bin/env bash
# Stop pauses the queue, Enter resumes it — driven live (card #7JD1).
#
#   drive.sh <relay-binary> <out-dir> [phase ...]
#
# Both paths are absolute; the script runs from its own directory. With no phase named it runs
# them all.
#
#   empty  a terminal pane: a turn runs, a second prompt queues behind it, Esc stops the turn and
#          pauses the queue — the strip says PAUSED and its hint says "Enter resumes" — and Enter
#          on the **empty** prompt box runs the queued prompt.
#   typed  the same, but after the Stop a *new* prompt is typed and sent: it runs at once, and the
#          prompt that was queued behind the stopped turn runs after it. Nothing was resumed by
#          hand, and the pane sent no `resume_queue`: the worker cleared the pause as the `ask`
#          went past it (backend/relay_core/queue.py, TurnSupervisor.submit).
#   card   a card's console, which is the same `Pane` with a context: the same Stop, the same
#          Enter, on that card's own queue.
#
# The evidence is three kinds and they have to agree:
#   * the shots, read with OCR and with the widget rectangles Relay dumps for a driver
#     (RELAY_QA_RECTS — a name that is not in the dump is not on screen; `queueTitle` and
#     `queueHint` carry their text there, so "QUEUE · PAUSED" and "Enter resumes" are read off the
#     widget rather than guessed from pixels);
#   * `requests.jsonl`, written by the stub beside this file as each request arrives: the
#     **order** and the **timing** of what reached the model. A queued prompt that appears there
#     only after the Enter is the whole claim of this card.
#
# Isolation: Xvfb, a private HOME / XDG_* / TMPDIR under a short path (the 108-byte socket limit),
# RELAY_KEYRING=off and no provider account — the profile points a local model endpoint at
# stub-provider.py on loopback, and the Main tier list is pinned to it so a fresh profile cannot
# take the worker's own default (a guest harness on a machine that has one).
# Needs Xvfb, xdotool, ImageMagick, tesseract.
set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"
relay=${1:?say which relay binary}
out=${2:?say where the shots go}
shift 2 2>/dev/null || true
phases=${*:-empty typed card}
width=1500 height=1000
port=${RELAY_QA_PORT:-8871}
root=$(cd ../../.. && pwd)
mkdir -p "$out"
[[ -x $relay ]] || { echo "no relay binary at $relay"; exit 1; }

display=
for n in $(seq 150 199); do [[ -e /tmp/.X11-unix/X$n ]] || { display=:$n; break; }; done
[[ -z $display ]] && { echo "no free display"; exit 1; }

sandbox=$(mktemp -d /tmp/rl-7jd1.XXXX)
stub_pid= xvfb_pid= relay_pid=
cleanup() {
    local pid
    for pid in $relay_pid $stub_pid $xvfb_pid; do [[ -n $pid ]] && kill "$pid" 2>/dev/null; done
    sleep 0.5
    for pid in $relay_pid $stub_pid $xvfb_pid; do [[ -n $pid ]] && kill -9 "$pid" 2>/dev/null; done
    rm -rf "$sandbox"
}
trap cleanup EXIT INT TERM

Xvfb "$display" -screen 0 $((width + 40))x$((height + 40))x24 >/dev/null 2>&1 &
xvfb_pid=$!
sleep 2
kill -0 "$xvfb_pid" 2>/dev/null || { echo "Xvfb died on $display"; exit 1; }
export DISPLAY=$display RELAY_KEYRING=off

mkdir -p "$sandbox/home" "$sandbox/run" "$sandbox/tmp"; chmod 700 "$sandbox/run"
ln -sfn "/run/user/$(id -u)/bus" "$sandbox/run/bus"
export HOME=$sandbox/home XDG_RUNTIME_DIR=$sandbox/run TMPDIR=$sandbox/tmp
export RELAY_QA_RECTS=$sandbox/rects.json
export XDG_CONFIG_HOME=$HOME/.config XDG_DATA_HOME=$HOME/.local/share XDG_CACHE_HOME=$HOME/.cache
work=$HOME/project
mkdir -p "$XDG_CONFIG_HOME/RelayTerminal" "$XDG_CONFIG_HOME/relay" "$XDG_DATA_HOME" "$work"
printf "PS1='\\\\w \\\$ '\nunset PROMPT_COMMAND\n" >"$HOME/.bashrc"

# One card for the console phase. A card turn's prompt carries the card's own file and its
# thread, so the keyword a phase types is in every later prompt on that card — one card, one
# phase, as the #CTRN drive had to learn.
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
card = B.new_card("work", "Kestrel: the console's queue stops and goes", "inbox",
                  created="2026-09-21", rank="a", request="stop and go on a card's own queue")
B.write_new_card(board, card, "features")
print(card.id)
FIX
)
[[ -n $card ]] || { echo "board fixture failed"; exit 1; }
echo "card: #$card"

requests=$out/requests.jsonl
: >"$requests"
python3 "$PWD/stub-provider.py" "$port" "$requests" >/dev/null 2>&1 &
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
; Rank 1 of the Main list is what a new pane starts on (#MDL1): pin it, or a fresh profile takes
; the worker's tier defaults, which on a machine with Claude Code installed is that harness.
tier/main=local:stub|stub|
[roles]
; Every helper on the stub too — a card's console is a helper (13.1).
switchboard/preset=local:stub
switchboard/model=stub
CONF
printf '{"version": 1, "endpoints": [{"id": "local:stub", "label": "Stub", "base_url": "http://127.0.0.1:%s/v1", "model": "stub", "server": "openai-compatible", "context_window": 131072}]}\n' \
    "$port" >"$XDG_CONFIG_HOME/relay/local-models.json"

: >"$out/notes.txt"
pass=0 fail=0
note() { echo "$*" >>"$out/notes.txt"; }
ok()   { note "PASS $*"; pass=$((pass+1)); }
bad()  { note "FAIL $*"; fail=$((fail+1)); }

win=
t() { xdotool type --delay 30 "$1"; xdotool keyup ctrl shift alt super 2>/dev/null; }
k() { xdotool key --delay 60 "$@"; xdotool keyup ctrl shift alt super 2>/dev/null; }
shot() {
    local X=0 Y=0
    eval "$(xdotool getmouselocation --shell 2>/dev/null)"
    xdotool mousemove $((width + 20)) $((height + 20)); sleep 0.6
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
word_xy() { words "$1" | awk -v want="$2" 'tolower($1) ~ tolower(want) {x=$2+$4/2; y=$3+$5/2} END {if (x) printf "%d %d\n", x, y}'; }
word_xy_first() { words "$1" | awk -v want="$2" 'tolower($1) ~ tolower(want) && !seen {seen=1; printf "%d %d\n", $2+$4/2, $3+$5/2}'; }
click_at() { [[ -z ${1:-} || -z ${2:-} ]] && return 1; xdotool mousemove "$1" "$2" click --clearmodifiers 1; sleep 1.5; }
rect() {
    python3 - "$RELAY_QA_RECTS" "$1" <<'PY' 2>/dev/null
import json, sys
try:
    row = json.load(open(sys.argv[1])).get(sys.argv[2])
except Exception:
    sys.exit(1)
if not row:
    sys.exit(1)
print(row["x"], row["y"], row["w"], row["h"], row.get("text", ""))
PY
}
rect_text() { rect "$1" | cut -d' ' -f5-; }
has_rect() { if rect "$2" >/dev/null; then ok "$1 ($2 is on screen)"; else bad "$1: no widget named $2 on screen"; fi; }
no_rect()  { if rect "$2" >/dev/null; then bad "$1: $2 is still on screen"; else ok "$1 (no $2 on screen)"; fi; }
rect_says() {   # what  objectName  substring
    local got; got=$(rect_text "$2")
    if [[ $got == *"$3"* ]]; then ok "$1 ($2 says \"$got\")"; else bad "$1: $2 says \"$got\", wanted \"$3\""; fi
}
has() { if text "$2" | grep -qi -- "$3"; then ok "$1 (\"$3\" in $2.png)"; else bad "$1: no \"$3\" in $2.png"; fi; }
# What reached the model, in order: one line per request, the newest user message of each.
asked() { python3 - "$requests" <<'PY'
import json, sys
for line in open(sys.argv[1]):
    try:
        said = json.loads(line).get("user_messages") or []
    except Exception:
        continue
    print((said[-1] if said else "").replace("\n", " ")[:80])
PY
}
asked_count() { asked | grep -ci -- "$1"; }
reached() {   # what  keyword  wanted-count
    local got; got=$(asked_count "$2")
    if [[ $got -eq $3 ]]; then ok "$1 (\"$2\" reached the model $got time(s))"
    else bad "$1: \"$2\" reached the model $got time(s), wanted $3"; fi
}

(cd "$work" && exec "$relay" --workspace "$work") >>"$sandbox/relay.log" 2>&1 &
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
shot _approvals
yes=$(word_xy _approvals "recommend")
[[ -n $yes ]] && click_at ${yes% *} ${yes#* }
sleep 3

focus_prompt() {
    local r; r=$(rect composerEditor) || { xdotool mousemove 300 $((height - 70)) click 1; sleep 1; return; }
    set -- $r
    xdotool mousemove $(( $1 + $3 / 2 )) $(( $2 + $4 / 2 )) click --clearmodifiers 1
    sleep 1
}
ask() { focus_prompt; t "$1"; k Return; }

run_empty() {
    note "--- empty: Esc pauses the queue, Enter on the empty box resumes it ---"
    ask "count slowly for me"
    sleep 5
    focus_prompt; t "count slowly again please"; k Return
    sleep 3
    shot a01-queued
    rect_says "the second prompt is queued and the strip is not paused" queueTitle "QUEUE"
    reached "only the first prompt has reached the model" "count slowly again" 0

    k Escape
    sleep 5
    shot a02-paused
    rect_says "Esc paused the queue" queueTitle "PAUSED"
    rect_says "the hint names the key that resumes" queueHint "Enter resumes"
    reached "the queued prompt is still waiting, not sent" "count slowly again" 0

    focus_prompt
    k Return                       # the empty box: resume
    sleep 6
    shot a03-resumed
    reached "Enter on the empty box ran the queued prompt" "count slowly again" 1
    no_rect "the strip is gone: nothing is queued or paused any more" queueTitle
    k Escape; sleep 3
}

run_typed() {
    note "--- typed: Enter with words sends them and the queue goes on behind them ---"
    : >"$requests"
    ask "count slowly for me a second time"
    sleep 5
    focus_prompt; t "count slowly third and last"; k Return
    sleep 3
    shot b01-queued
    k Escape
    sleep 5
    shot b02-paused
    rect_says "Esc paused the queue again" queueTitle "PAUSED"
    reached "the queued prompt has not been sent" "count slowly third" 0

    focus_prompt; t "say hello instead"; k Return
    sleep 4
    shot b03-typed
    reached "the typed prompt ran at once" "say hello instead" 1
    sleep 12
    shot b04-queue-ran
    reached "and the prompt that was queued behind the Stop ran after it" "count slowly third" 1
    k Escape; sleep 3
}

run_card() {
    note "--- card: the same Stop and the same Enter, on that card's own queue ---"
    : >"$requests"
    k ctrl+shift+b                  # the Switchboard
    sleep 4
    shot c00-board
    local at; at=$(word_xy_first c00-board "kestrel")
    [[ -n $at ]] && click_at ${at% *} ${at#* }
    sleep 3
    shot c01-card
    has "the card page is open" c01-card "kestrel"
    # The card console's own prompt box is the last composerEditor on screen.
    local box; box=$(python3 - "$RELAY_QA_RECTS" <<'PY' 2>/dev/null
import json, sys
rects = json.load(open(sys.argv[1]))
boxes = [v for k, v in rects.items() if k.split("#")[0] == "composerEditor"]
if not boxes:
    sys.exit(1)
box = max(boxes, key=lambda r: r["y"])
print(box["x"] + box["w"] // 2, box["y"] + box["h"] // 2)
PY
)
    [[ -z $box ]] && { bad "card: no console prompt box on screen"; return; }
    xdotool mousemove ${box% *} ${box#* } click --clearmodifiers 1; sleep 1
    t "count slowly on this card"; k Return
    sleep 6
    xdotool mousemove ${box% *} ${box#* } click --clearmodifiers 1; sleep 1
    t "count slowly once more on this card"; k Return
    sleep 3
    shot c02-queued
    reached "the second card prompt is queued, not sent" "count slowly once more" 0

    k Escape
    sleep 5
    shot c03-paused
    reached "Stop did not send it either" "count slowly once more" 0

    xdotool mousemove ${box% *} ${box#* } click --clearmodifiers 1; sleep 1
    k Return                        # the empty console box: resume this card's queue
    sleep 8
    shot c04-resumed
    reached "Enter on the console's empty box ran the card's queued prompt" "count slowly once more" 1
    k Escape; sleep 3
}

for phase in $phases; do
    case $phase in
        empty) run_empty ;;
        typed) run_typed ;;
        card)  run_card ;;
        *) echo "unknown phase $phase" ;;
    esac
done

cp "$sandbox/relay.log" "$out/relay.log" 2>/dev/null
cp "$RELAY_QA_RECTS" "$out/rects-final.json" 2>/dev/null
note "--- $pass passed, $fail failed ---"
echo "PASS $pass  FAIL $fail"
[[ $fail -eq 0 ]]
