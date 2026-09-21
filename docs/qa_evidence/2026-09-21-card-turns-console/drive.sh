#!/usr/bin/env bash
# A card turn is an ordinary console turn — card #CTRN, the GUI half (steps 4 and 5).
#
#   drive.sh [relay-binary] [out-dir] [phase ...]
#
# Both paths are **absolute**: the script runs from its own directory. With no phase named it
# runs them all.
#
#   card   the card console draws the turn — reasoning, a tool row, the answer — while the thread
#          view above it shows nothing of it and settles on the entry the worker writes; the busy
#          strip is the label and the ✕ and has no progress line; the turn's events reach exactly
#          one console
#   two    two cards working at once in one tab, and ✕ Stop on one leaving the other running
#   queue  a second Enter on a busy card queues in the §12 strip of that card's console, and
#          both turns land on the thread in order (it was refused with `board_busy` before)
#   plan   Ctrl+Enter plans, and a Plan turn that calls write_file is refused in a sentence that
#          names Execute — and the turn carries on
#   back   quit and start again: the card's thread is complete and its conversation is still
#          there, so the model remembers the earlier turn (the per-(tab, card) persist key)
#
# Isolation: Xvfb, a private HOME / XDG_* / TMPDIR under a short path (the 108-byte socket
# limit), RELAY_KEYRING=off and **no provider account** — the profile points a local model
# endpoint at stub-provider.py on loopback, so every agent in the run is that script.
# Needs Xvfb, xdotool, ImageMagick, tesseract.
#
# The checks are gated on things that do not depend on reading a terminal: the widget rectangles
# `RELAY_QA_RECTS` writes, the bytes of `issues/threads/<ID>.md`, and `relay.log`'s per-pane event
# lines. The one thing read by OCR is the **thread view**, cropped to its own widget rectangle —
# which is the surface under test.
set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"
out=${2:-$PWD}
root=$(cd ../../.. && pwd)
relay=${1:-$root/build/relay}
shift 2 2>/dev/null || true
phases=${*:-card two queue plan back}
width=1500 height=1100
port=${RELAY_QA_PORT:-8893}
mkdir -p "$out"
[[ -x $relay ]] || { echo "no relay binary at $relay"; exit 1; }

display=
for n in $(seq 160 199); do [[ -e /tmp/.X11-unix/X$n ]] || { display=:$n; break; }; done
[[ -z $display ]] && { echo "no free display"; exit 1; }

sandbox=$(mktemp -d /tmp/rl-ctrn.XXXX)
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
echo "the drive wrote this" >"$work/fixture.txt"

cards=$(PYTHONPATH="$root/backend" python3 - "$work" <<'FIX'
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
ids = []
for title, rank in [("The console draws the card turn", "a"),
                    ("The second card runs at the same time", "b")]:
    card = B.new_card("work", title, "inbox", created="2026-09-21", rank=rank,
                      request="the console's brief has a board")
    B.write_new_card(board, card, "features")
    ids.append(card.id)
print(" ".join(ids))
FIX
)
[[ -n $cards ]] || { echo "board fixture failed"; exit 1; }
read -r cardA cardB <<<"$cards"
echo "cards: A=#$cardA B=#$cardB"

python3 "$PWD/stub-provider.py" "$port" "--card=$cardA" >/dev/null 2>&1 &
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
[logging]
level=debug
[roles]
; The helper role — the Switchboard's agent, a card's console and every other helper (13.1).
; Without it a fresh profile resolves Main to whichever guest harness is on the machine and the
; card turn is refused with "the helper agent cannot run on Claude Code" before it starts.
switchboard/preset=local:stub
switchboard/model=stub
[agent]
app_writes=true
CONF
printf '{"version": 1, "endpoints": [{"id": "local:stub", "label": "Stub", "base_url": "http://127.0.0.1:%s/v1", "model": "stub", "server": "openai-compatible", "context_window": 131072}]}\n' \
    "$port" >"$XDG_CONFIG_HOME/relay/local-models.json"

: >"$out/notes.txt"
pass=0 fail=0
note() { echo "$*" >>"$out/notes.txt"; }
ok()   { note "PASS $*"; pass=$((pass+1)); }
bad()  { note "FAIL $*"; fail=$((fail+1)); }

win=
# **Every click clears the modifiers first.** `xdotool type` leaves whatever it last pressed
# latched in the X server's state, and `BoardView`'s row handler ignores a click that arrives with
# any modifier held (`itemClicked`: "QApplication::keyboardModifiers() != Qt::NoModifier" — so a
# Ctrl+click on a row does not open the card). A run that had typed a prompt then clicked a card
# row and the card never opened: three tries, three selected rows, no card page, and every check
# after it failed on a window that was still showing the list.
t() { xdotool type --delay 30 "$1"; xdotool keyup ctrl shift alt super 2>/dev/null; }
k() { xdotool key --delay 60 "$@"; xdotool keyup ctrl shift alt super 2>/dev/null; }
shot() {
    local X=0 Y=0
    eval "$(xdotool getmouselocation --shell 2>/dev/null)"
    xdotool mousemove $((width + 20)) $((height + 20)); sleep 0.5
    xdotool windowsize "$win" $((width - 1)) "$height"; sleep 0.2
    xdotool windowsize "$win" "$width" "$height"; sleep 0.6
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
# The **first** match, reading down the window. `word_xy` takes the last, and the last "second"
# on a board page is the console's own placeholder — "Ask the Switchboard agent — Enter sends, a
# second prompt queues" — not the card row above it. The drive clicked the placeholder, the card
# never opened, and every check after it read a window that was still showing the list.
word_xy_first() { words "$1" | awk -v want="$2" 'tolower($1) ~ tolower(want) && !seen {seen=1; printf "%d %d\n", $2+$4/2, $3+$5/2}'; }
click_at() { [[ -z ${1:-} || -z ${2:-} ]] && return 1; xdotool mousemove "$1" "$2" click --clearmodifiers 1; sleep 1.5; }
click_word() { local at; at=$(word_xy "$1" "$2"); [[ -n $at ]] && click_at ${at% *} ${at#* }; }
# The widget rectangles Relay dumps for a drive (RELAY_QA_RECTS). A name that is not in the dump
# is not on screen — which is itself a check here: `boardBusyWhat` must never be.
rect() {   # objectName -> "x y w h text"
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
has_rect() { if rect "$2" >/dev/null; then ok "$1 ($2 is on screen)"; else bad "$1: no widget named $2 on screen"; fi; }
no_rect()  { if rect "$2" >/dev/null; then bad "$1: $2 is on screen and should not be"; else ok "$1 (no $2 on screen)"; fi; }
rect_text() { rect "$1" | cut -d' ' -f5-; }
click_rect() {
    local r; r=$(rect "$1") || { note "  (no widget named $1 on screen)"; return 1; }
    set -- $r
    xdotool mousemove $(( $1 + $3 / 2 )) $(( $2 + $4 / 2 )) click --clearmodifiers 1
    sleep 1.5
}
click_rect_in() {   # objectName minx miny — the lowest one past that corner
    local xy
    xy=$(python3 - "$RELAY_QA_RECTS" "$1" "$2" "$3" <<'PY' 2>/dev/null
import json, re, sys
try:
    rows = json.load(open(sys.argv[1]))
except Exception:
    sys.exit(1)
name, minx, miny = sys.argv[2], int(sys.argv[3]), int(sys.argv[4])
best = None
for key, row in rows.items():
    if key != name and not re.fullmatch(re.escape(name) + r"#\d+", key):
        continue
    cx, cy = row["x"] + row["w"] // 2, row["y"] + row["h"] // 2
    if cx >= minx and cy >= miny and (best is None or cy > best[1]):
        best = (cx, cy)
if best is None:
    sys.exit(1)
print(best[0], best[1])
PY
) || { note "  (no $1 past ${2},${3} on screen)"; return 1; }
    xdotool mousemove ${xy% *} ${xy#* } click --clearmodifiers 1
    sleep 1.5
}
# The **thread view** only, cropped to its own rectangle before it is read: the card page's
# document is the surface under test ("nothing of the turn streams here"), and reading the whole
# window would read the console's transcript with it — where the very same words are supposed to
# be.
thread_view() {   # shot -> the text of boardCardDocument in it
    local shot=$1 r; r=$(rect boardCardDocument) || return 1
    set -- $r
    convert "$out/$shot.png" -crop "$3x$4+$1+$2" +repage -scale 200% png:- 2>/dev/null \
        | tesseract - stdout --psm 6 2>/dev/null
}

# How many consoles of the tab were handed the running card turn's events, by pane. `relay.log`
# writes one line per event per pane (`Pane::logEvent`), and `tool_started` is one of the types it
# keeps — so counting the distinct `pane=` ids on the card turn's tool line *is* the routing
# check: two before this card (the board's console drew it as well, and Options' would have), one
# after it.
panes_that_saw() {   # event-type -> distinct pane ids, one per line
    # `tool_started` is a Debug line (`Pane::logEvent`), so the profile above asks for debug.
    grep -h "event type=$1 " "$XDG_DATA_HOME"/relay/logs/relay.log 2>/dev/null \
        | sed -n 's/.* pane=\([0-9a-f]*\).*/\1/p' | sort -u
}

launch() {
    [[ -n $relay_pid ]] && { kill "$relay_pid" 2>/dev/null; wait "$relay_pid" 2>/dev/null; sleep 3; }
    # The widget dump belongs to the window that wrote it. Left in place across a relaunch it
    # answers for the *previous* run — which is how the phase after the first one read "the card
    # page is already open", skipped Ctrl+Shift+S and then drove a window with no board in it.
    rm -f "$RELAY_QA_RECTS"
    local fresh=--fresh
    [[ ${1:-} == --keep ]] && fresh=
    (cd "$work" && exec "$relay" --workspace "$work" $fresh) >>"$sandbox/relay.log" 2>&1 &
    relay_pid=$!
    sleep 12
    win= ; local best=0 w
    for w in $(xdotool search --pid "$relay_pid" 2>/dev/null); do
        eval "$(xdotool getwindowgeometry --shell "$w" 2>/dev/null)"
        (( WIDTH * HEIGHT > best )) && { best=$(( WIDTH * HEIGHT )); win=$w; }
    done
    [[ -z $win ]] && { echo "no Relay window"; tail -40 "$sandbox/relay.log"; return 1; }
    xdotool windowmove "$win" 0 0 windowsize "$win" $width $height
    xdotool windowfocus "$win"; sleep 4
    shot _approvals
    local yes; yes=$(word_xy _approvals "recommend")
    [[ -n $yes ]] && click_at ${yes% *} ${yes#* }
    sleep 2
}

open_board() {   # shot-prefix
    # Ctrl+Shift+S *toggles* the Switchboard pane, and after a restart the saved layout may
    # already have it open — pressing it then would close the pane the phase is about.
    if ! rect boardListPane >/dev/null && ! rect boardCardDocument >/dev/null; then
        k ctrl+shift+s; sleep 6
    fi
    shot "$1-board"
}
# One card's row, in whatever section it is in. The board opens with its sections folded and the
# phases share one fixture board, so a card an earlier phase discussed is in **Discussing** by the
# time the next phase looks for it — a run that unfolded Inbox alone then clicked a title that was
# not drawn and drove a window with no card open, which is what the first full run of this drive
# recorded as "the first card is working: no boardBusyStrip".
open_card() {    # shot-prefix word-of-the-title
    local at section waited
    # A restarted window comes back to the layout it was saved with, card page and all. If the
    # card that is already open is the one being asked for, there is nothing to click.
    if rect boardCardDocument >/dev/null; then
        shot "$1-open"
        [[ -n $(word_xy_first "$1-open" "$2") ]] && return 0
        back_to_list
    fi
    # Every section a card of this fixture can be in: the phases share one board, so the card
    # the next phase wants has been moved by the one before it — Inbox, then Discussing after a
    # Discuss, then Planning after a Plan — and the board opens with its sections folded.
    for section in inbox discussing planning "in progress" "needs qa"; do
        shot "$1-list"
        at=$(word_xy_first "$1-list" "$2")
        [[ -n $at ]] && break
        click_word "$1-list" "^$section\$"
        sleep 2
    done
    # Up to three goes: a turn running on another card writes its thread, the worker pushes
    # `board_changed`, and the list is re-drawn — under the click, if it lands in that moment.
    local try
    for try in 1 2 3; do
        shot "$1-list"
        at=$(word_xy_first "$1-list" "$2")
        [[ -z $at ]] && continue
        click_at ${at% *} ${at#* }
        # The page opens on the worker's `board_card` answer, so how long it takes is the
        # worker's — and a turn running on another card is the case worth measuring.
        waited=0
        while (( waited < 45 )); do
            if rect boardCardDocument >/dev/null; then
                (( waited > 3 )) && note "  (the card page took ${waited}s to open)"
                return 0
            fi
            sleep 3; waited=$((waited + 3))
        done
    done
    bad "the card page never opened on \"$2\" ($1-list.png)"
    return 1
}
# Type into the **card page's** composer: the lowest `composerEditor` on the right half of the
# window. Object names are not unique — every prompt box in Relay is one — and the terminal
# pane's is the other one on screen.
ask_card() {   # text
    click_rect_in composerEditor $((width / 2)) 0 || return 1
    t "$1"; k Return; sleep 2
}
# Back to the list. **Not Esc**: the keyboard is in the console's composer after an ask, where
# Esc is the pane's own "stop the turn" key — the first run of this drive pressed it, stayed on
# the card and read the card it was still on as "the other card is working too".
back_to_list() { click_rect boardBack; sleep 2; }
want() { [[ " $phases " == *" $1 "* ]]; }

# Every phase but the two-card one works on card A, so its thread is the record they all read.
threadA=$work/issues/threads/$cardA.md
threadQueue=$threadA
threadPlan=$threadA
threadBack=$threadA

# =================================================== the card console draws the turn (step 4+5)
if want card; then
note "===== the card console draws the turn, the thread view settles ====="
launch || exit 1
open_board c0
open_card c0 "console"
shot c01-card
has_rect "c1 the card page is open with its console" boardCardDocument

ask_card "trace the card please"
# The first round trip is reasoning and then a tool call, and the transcript in a card page is a
# few lines tall, so the fold and the tool row are caught before the answer scrolls them off.
sleep 4
shot c02a-fold
note "  the console's first rows, as OCR reads them: $(text c02a-fold | grep -i 'thought\|thinking\|read\|fixture' | head -3 | tr '\n' ' ')"
sleep 4
# --- while it runs -------------------------------------------------------------------------
shot c02-running
# The strip: the board's word for the card's turn, and the ✕ (owner decision 4, #VZ69 wording).
has_rect "c2 the busy strip is up while the turn runs" boardBusyStrip
strip=$(rect_text boardBusyLabel)
note "  boardBusyLabel: $strip"
if [[ $strip == *"Switchboarding"* && $strip == *"discussing"* ]]; then
    ok "c2 the strip names the mode (\"$strip\")"
else bad "c2 the strip does not name the mode: \"$strip\""; fi
stopword=$(rect_text boardStop)
if [[ $stopword == *"Stop discussing"* ]]; then ok "c2 and carries the ✕ (\"$stopword\")"
else bad "c2 the ✕ does not say what it stops: \"$stopword\""; fi
# The progress line the tool rows replace: the widget itself is gone, not merely hidden.
no_rect "c2 the strip has no progress line any more" boardBusyWhat
# The thread view, read on its own rectangle: nothing of the turn is in it.
thread_view c02-running >"$out/c02-threadview.txt"
if grep -qi "TRACEDONE\|thinking\|thought for\|read_file\|fixture" "$out/c02-threadview.txt"; then
    bad "c3 the thread view is drawing the running turn (see c02-threadview.txt)"
else
    ok "c3 the thread view draws nothing of the running turn (c02-threadview.txt)"
fi
# And the transcript below it is where the turn is: looked at by eye in c02-running.png, and
# counted here by the events the console was actually handed.
sleep 25
shot c03-settled
thread_view c03-settled >"$out/c03-threadview.txt"

# --- the record ------------------------------------------------------------------------------
if [[ -f $threadA ]]; then
    cp "$threadA" "$out/thread-$cardA.md"
    if grep -qi "trace the card please" "$threadA"; then
        ok "c4 the owner's words are on the thread, written at submit (19.10)"
    else bad "c4 the owner's words are not in issues/threads/$cardA.md"; fi
    if grep -q "author=agent kind=comment mode=discuss model=" "$threadA"; then
        ok "c4 the answer carries its mode=/model= provenance (owner decision 2)"
    else bad "c4 no \"author=agent kind=comment mode=discuss model=\" in the thread"; fi
    if grep -q "turn=" "$threadA"; then ok "c4 and its turn=<session>/<turn>"
    else bad "c4 no turn= provenance in the thread"; fi
    if grep -qi "TRACEDONE" "$threadA"; then ok "c4 the answer itself is the entry's text"
    else bad "c4 the answer is not in the thread"; fi
else
    bad "c4 no thread file was written at all"
fi
# The settled entry is what the thread view shows once the turn is over.
if grep -qi "TRACEDONE" "$out/c03-threadview.txt"; then
    ok "c5 the settled entry appears in the thread view when the turn ends"
else bad "c5 the settled entry is not in the thread view (c03-threadview.txt)"; fi
# ...and the strip is down.
no_rect "c5 the busy strip goes when the turn ends" boardBusyStrip

# --- the routing (step 5) --------------------------------------------------------------------
cp "$XDG_DATA_HOME/relay/logs/relay.log" "$out/relay.log" 2>/dev/null
for kind in tool_started agent_started agent_finished; do
    panes_that_saw "$kind" >"$out/c06-panes-$kind.txt"
    note "  panes handed the card turn's $kind: $(tr '\n' ' ' <"$out/c06-panes-$kind.txt")"
done
seen=$(wc -l <"$out/c06-panes-tool_started.txt")
started=$(wc -l <"$out/c06-panes-agent_started.txt")
if [[ ${seen:-0} == 1 && ${started:-0} == 1 ]]; then
    ok "c6 the card turn's events reached exactly one console (deliverToConsoles routes by surface)"
else
    bad "c6 $seen panes saw tool_started and $started saw agent_started — the events are fanned out"
fi
fi

# ======================================================= two cards at once, and ✕ Stop on one
if want two; then
note "===== two cards at once in one tab, and ✕ Stop on one ====="
launch || exit 1
open_board d0
open_card d0 "console"
# Three minutes of streaming: long enough to still be running when this phase has opened the
# other card, started its turn, stopped that one and come back here. Its card has never been
# asked anything else, so the scene its words pick is the only one in the prompt.
ask_card "hold the line on this card"
sleep 4
shot d01-first-running
has_rect "d1 the first card is working" boardBusyStrip

# Back to the list and on to the other card: its own page, its own console, no strip.
back_to_list
open_card d1 "second"
shot d02-second-card
no_rect "d2 the other card is idle even while the first one works" boardBusyStrip
ask_card "count slowly here as well"
sleep 4
shot d03-both-running
has_rect "d2 and it starts its own turn — two cards work at once (#DR4K, #0Z13)" boardBusyStrip

# ✕ Stop on this card. It names the card (`board_cancel {card}`), so the other one is untouched.
click_rect boardStop
sleep 4
shot d04-stopped
no_rect "d3 ✕ Stop ends the turn on the card it is on" boardBusyStrip
back_to_list
open_card d3 "console"
shot d05-other-still-running
# **This one is the stub's to lose, not Relay's.** A card turn's prompt is seeded with the card's
# own file *and its thread*, so by this point card A's thread already holds the first phase's
# "trace the card please" and the stub answers that scene — in a second — rather than the long
# one these words ask for. Relay's half of the check (Stop names one card) is d3 above, and the
# protocol half (the other card runs on and ends `done`) is in the backend drive's evidence.
has_rect "d3 and the other card is still working" boardBusyStrip
fi

# ==================================================== a second Enter queues, in the card console
if want queue; then
note "===== a second Enter on a busy card queues in that card's console ====="
launch || exit 1
open_board q0
open_card q0 "console"
ask_card "count slowly on this card"
sleep 8
ask_card "and then say hello"
sleep 5
shot q01-queued
# The §12 strip is the console's own, and there is exactly one of it on screen: the card's.
# **This is the one check of this drive that fails**, and the reason is in NOTES.md: the strip is
# drawn from the pane's *own* client-side queue (`Pane::m_entries`, filled when a line is typed
# into its composer and held back because the agent is busy), and a card's prompt never goes
# through that path — `CardContext::submit` sends it as `board_ask`, so the queue it waits in is
# the worker's, which arrives as `queue_changed` and which `rebuildQueueStrip` does not read.
# Drawing it needs `src/Pane.h`, which no step of #CTRN may open (the plan's Risks 8).
has_rect "q1 the second prompt queues in the §12 strip" queueStrip
strips=$(python3 - "$RELAY_QA_RECTS" <<'PY' 2>/dev/null || echo 0
import json, re, sys
try:
    rows = json.load(open(sys.argv[1]))
except Exception:
    print(0); raise SystemExit
print(sum(1 for k in rows if re.fullmatch(r"queueStrip(#\d+)?", k)))
PY
)
if [[ ${strips:-0} == 1 ]]; then ok "q1 and only in the card's console (one queueStrip on screen)"
else bad "q1 $strips queue strips on screen: the worker's queue has no row in the pane's strip (NOTES.md)"; fi
# Where it is: inside the card page, under the thread view rather than beside the board list.
docy=$(rect boardCardDocument | cut -d' ' -f2)
stripy=$(rect queueStrip | cut -d' ' -f2)
if [[ -n $docy && -n $stripy ]] && (( stripy > docy )); then
    ok "q1 the strip is in the card's console, below the thread view (y=$stripy > $docy)"
else bad "q1 the strip is not under the card's thread view (y=$stripy, thread at $docy)"; fi
# And it was not refused: `board_busy` would have put the words back in the box with a line
# under the thread, which is what this page did before card #CTRN.
no_rect "q2 the second prompt was not refused with board_busy" boardCardError

# Both turns land, in order, on the thread the worker writes.
sleep 60
shot q02-both-done
if [[ -f $threadQueue ]]; then
    cp "$threadQueue" "$out/thread-$cardA-queue.md"
    if grep -qi "count slowly on this card" "$threadQueue" && grep -qi "and then say hello" "$threadQueue"; then
        ok "q3 both questions are on the thread"
    else bad "q3 one of the two questions never reached the thread"; fi
    answers=$(grep -c "author=agent kind=comment" "$threadQueue")
    if (( answers >= 2 )); then ok "q3 and both turns answered ($answers agent entries)"
    else bad "q3 only $answers agent entries: the queued turn did not run"; fi
fi
fi

# ================================================= Ctrl+Enter plans, and a write is refused
if want plan; then
note "===== a Plan turn is offered the writers and refused at call time (decision 3) ====="
launch || exit 1
open_board p0
open_card p0 "console"
click_rect_in composerEditor $((width / 2)) 0
t "plan the write please"
# **The button, not Ctrl+Enter.** `xdotool key ctrl+Return` does not reach Qt under Xvfb — the
# words stayed in the box and no turn started, which the first full run of this drive read as
# "the refusal ended the turn". Plan (p) is the same path: `CardDetail::plan()` takes what is in
# the box with it. (The chord itself is covered by `consolemode` and `board`, through the real
# widgets.)
click_rect boardReplyButton
sleep 30
shot p01-refused
# The refusal is `CardScope.refusal`, which names Execute. It is drawn as the tool row's own
# error in the console's transcript, so this one is read from the shot — the sentence is the
# check, and the row is what the card's Verify item 3 asks to see.
# The sentence, not the word: "Execute (x)" is a button on the card's action row, so grepping
# for `Execute` alone passes on a page where nothing was refused at all.
if text p01-refused | grep -qi "not available in a Plan turn\|Writing code is Execute"; then
    ok "p1 the refused write is refused in CardScope's sentence, which names Execute (p01-refused.png)"
else bad "p1 no refusal sentence in p01-refused.png"; fi
sleep 20
shot p02-plan-done
if grep -q "kind=comment mode=plan" "$threadPlan" 2>/dev/null; then
    ok "p2 the Plan turn carried on and its answer is on the thread with mode=plan"
else bad "p2 no mode=plan entry on the thread: the refusal ended the turn"; fi
cp "$threadPlan" "$out/thread-$cardA-plan.md" 2>/dev/null
fi

# =========================================== a restart: the thread is complete, the model remembers
if want back; then
note "===== a restart brings the card's own conversation back (the per-(tab, card) key) ====="
launch || exit 1
open_board r0
open_card r0 "console"
ask_card "say hello to the card"
sleep 30
shot r01-first
before=$(grep -c "author=agent" "$threadBack" 2>/dev/null || echo 0)
# The conversation file: one per (tab, card), in the helper store §30.7 keeps them in.
ls "$XDG_DATA_HOME"/relay/helper-sessions/*/*.json >"$out/r02-helper-files.txt" 2>/dev/null
if grep -rl "say hello to the card" "$XDG_DATA_HOME"/relay/helper-sessions/ >"$out/r03-card-session.txt" 2>/dev/null \
   && [[ -s $out/r03-card-session.txt ]]; then
    ok "r1 the card's turn is in a helper conversation file of its own"
else bad "r1 no helper conversation file holds the card's turn"; fi

# The tab id is what keys the card's conversation together with the card (`<tab>/card:<ID>`),
# so a restart that came back with a *new* tab is a different conversation for a reason that has
# nothing to do with this card. Both ids go in the evidence.
grep -o '"tab_id": *"[^"]*"' "$XDG_DATA_HOME"/relay/state/windows.json 2>/dev/null | sort -u >"$out/r06-tab-before.txt"
note "  tab ids before the restart: $(tr '\n' ' ' <"$out/r06-tab-before.txt")"
launch --keep || exit 1
open_board r1
open_card r1 "console"
shot r04-restarted
if [[ -f $threadBack ]] && grep -qi "say hello to the card" "$threadBack"; then
    ok "r2 the thread is complete after the restart"
else bad "r2 the thread lost the turn across the restart"; fi
ask_card "how much do you remember about this card"
sleep 30
shot r05-remembers
# The tab id is what keys the card's conversation with the card (`<tab>/card:<ID>`), so it is
# read at the *end* — right after a launch the new window has not saved a state with its tab in
# it yet, and an empty file would say "the id changed" when nothing had happened.
grep -o '"tab_id": *"[^"]*"' "$XDG_DATA_HOME"/relay/state/windows.json 2>/dev/null | sort -u >"$out/r07-tab-after.txt"
note "  tab ids before the restart: $(tr '\n' ' ' <"$out/r06-tab-before.txt")"
note "  tab ids after the restart:  $(tr '\n' ' ' <"$out/r07-tab-after.txt")"
# **The conversation file itself**, which is what the check is about and what no OCR can argue
# with: the turn before the restart and the turn after it are in the same file, or they are not.
after=$(grep -rl "how much do you remember" "$XDG_DATA_HOME"/relay/helper-sessions/ 2>/dev/null | sort -u)
before=$(cat "$out/r03-card-session.txt" 2>/dev/null)
note "  the card's conversation before: ${before:-(none)}"
note "  and the file the turn after the restart went into: ${after:-(none)}"
if [[ -n $after && -n $before && $after == *"$before"* ]]; then
    ok "r3 the turn after the restart went into the card's own conversation, the one from before it"
elif [[ -s $out/r06-tab-before.txt ]] && ! diff -q "$out/r06-tab-before.txt" "$out/r07-tab-after.txt" >/dev/null; then
    bad "r3 a fresh conversation — but the **tab id** did not survive the restart either, so this run cannot say whether the key is right (r06/r07-tab-*.txt)"
else
    bad "r3 the restarted card console started a fresh conversation, with the same tab id"
fi
remembered=$(text r05-remembers | sed -n 's/.*HISTORY turns=\([0-9]*\).*/\1/p' | tail -1)
note "  HISTORY turns=${remembered:-?} (what the model was actually handed)"
fi

note ""
note "$pass passed, $fail failed"
echo "$pass passed, $fail failed — $out/notes.txt"
