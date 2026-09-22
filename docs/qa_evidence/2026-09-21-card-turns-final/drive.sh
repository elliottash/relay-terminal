#!/usr/bin/env bash
# What the final pass of card #CTRN closed, driven live.
#
#   drive.sh [relay-binary] [out-dir] [phase ...]
#
# Both paths are **absolute**: the script runs from its own directory. With no phase named it
# runs them all.
#
#   queue  a queued card prompt has a **row** in the §12 queue strip of that card's console —
#          the owner's words, not the prompt the model is sent — and the row's affordances send
#          the queue ops with the card's `surface`: ↑ selects it, Shift+Delete removes it, and
#          the prompt that was withdrawn never runs
#   cards  one console, several cards: switching cards puts the card before's transcript away
#          and draws the card being opened's, so nothing bleeds between them, and coming back to
#          the first card draws what it said
#   back   a restart on the same profile, **restoring the saved layout** (the check the steps 4-5
#          drive could not make: it relaunched with `--workspace`, which skips the restore, so
#          the window came back with a new tab id and a new conversation was the right answer).
#          Same tab id, the card's thread complete, and the model handed the conversation it had.
#
# Isolation: Xvfb, a private HOME / XDG_* / TMPDIR under a short path (the 108-byte socket
# limit), RELAY_KEYRING=off and **no provider account** — the profile points a local model
# endpoint at stub-provider.py on loopback, so every agent in the run is that script.
# Needs Xvfb, xdotool, ImageMagick, tesseract.
#
# The checks are gated on the widget rectangles `RELAY_QA_RECTS` writes (a widget that is not in
# the dump is not on screen), the bytes of `issues/threads/<ID>.md`, the conversation files under
# the helper store, and `windows.json`'s saved tab id. The queue row's *text* is read off the
# rectangle rather than by OCR: `queueList` carries its rows' text in the dump.
set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"
out=${2:-$PWD}
root=$(cd ../../.. && pwd)
relay=${1:-$root/build/relay}
shift 2 2>/dev/null || true
phases=${*:-queue cards back}
width=1500 height=1100
port=${RELAY_QA_PORT:-8899}
mkdir -p "$out"
[[ -x $relay ]] || { echo "no relay binary at $relay"; exit 1; }

display=
for n in $(seq 160 199); do [[ -e /tmp/.X11-unix/X$n ]] || { display=:$n; break; }; done
[[ -z $display ]] && { echo "no free display"; exit 1; }

sandbox=$(mktemp -d /tmp/rl-ctrf.XXXX)
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
# Four cards, one per thing being checked, and **a card is used by one phase only**. The
# phases share a board, and a card turn's prompt is seeded with the card's own file and the
# tail of its thread — so a keyword an earlier phase typed on a card is in every later prompt
# on it, and the stub answers the *earliest* keyword in the message. The steps 4-5 drive
# recorded that as a failure of its own ("the stub's doing, not Relay's"); one card per phase
# is the way round it. The titles start with a word that appears nowhere else on screen, so
# the row to click cannot be confused with prose.
for title, rank in [("Kestrel: the console draws the card turn", "a"),
                    ("Marmot: the restart brings the conversation back", "b"),
                    ("Zebra keeps its own scrollback", "c"),
                    ("Walrus keeps its own scrollback", "d")]:
    card = B.new_card("work", title, "inbox", created="2026-09-21", rank=rank,
                      request="the console's brief has a board")
    B.write_new_card(board, card, "features")
    ids.append(card.id)
print(" ".join(ids))
FIX
)
[[ -n $cards ]] || { echo "board fixture failed"; exit 1; }
read -r cardA cardB cardC cardD <<<"$cards"
echo "cards: A=#$cardA (queue) B=#$cardB (restart) C=#$cardC D=#$cardD (transcripts)"

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
    # `--workspace` is **not** passed on a restart. `src/main.cpp` reads an explicit
    # `--workspace` as "start fresh here" exactly as `--fresh` does
    # (`startFresh = parser.isSet(fresh) || parser.isSet(workspace)`), so the steps 4-5 drive's
    # `launch --keep` skipped `restoreSavedLayout()` and came back with a brand-new tab — which
    # is why it could not decide whether the per-(tab, card) key survives a restart. The
    # subshell is already in `$work`, and the option defaults to the current directory.
    local args=(--workspace "$work" --fresh)
    [[ ${1:-} == --keep ]] && args=()
    (cd "$work" && exec "$relay" "${args[@]}") >>"$sandbox/relay.log" 2>&1 &
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
threadB=$work/issues/threads/$cardB.md


# The **console's own rectangle**, cropped and read. The card page draws a thread view above the
# console and the same words can be in both, so "the transcript is this card's" is only a claim
# about the transcript if the transcript is what is read.
console_view() {   # shot -> the text under the thread view, which is the console
    local shot=$1 r; r=$(rect boardCardDocument) || return 1
    set -- $r
    local top=$(( $2 + $4 )) left=$1 wide=$3
    convert "$out/$shot.png" -crop "${wide}x$(( height - top ))+${left}+${top}" +repage -scale 200% png:- 2>/dev/null \
        | tesseract - stdout --psm 6 2>/dev/null
}
# The §12 queue strip's rows, read off the list widget's own rectangle. `queueList` carries no
# `text` property, so this is the one OCR read in the phase — and it is cropped to the widget
# under test before it is read.
# The card page's prompt box, cropped to its own rectangle: the lowest `composerEditor` on the
# right half of the window is the card's, and the terminal pane's is the other one on screen.
composer_box() {   # shot -> the text of the card page's composer in it
    local shot=$1 r
    r=$(python3 "$PWD/rect_composer.py" "$RELAY_QA_RECTS" "$((width / 2))") || return 1
    set -- $r
    convert "$out/$shot.png" -crop "$3x$4+$1+$2" +repage -scale 300% png:- 2>/dev/null \
        | tesseract - stdout --psm 6 2>/dev/null
}
queue_rows() {   # shot -> the text of queueList in it
    local shot=$1 r; r=$(rect queueList) || return 1
    set -- $r
    convert "$out/$shot.png" -crop "$3x$4+$1+$2" +repage -scale 200% png:- 2>/dev/null \
        | tesseract - stdout --psm 6 2>/dev/null
}
agent_comments() { grep -c "author=agent kind=comment" "$1" 2>/dev/null || echo 0; }

# ============================================ a queued card prompt has a row in the §12 strip
if want queue; then
note "===== a queued card prompt draws a row in the card console's §12 queue strip ====="
launch || exit 1
open_board q0
open_card q0 "Kestrel"
shot q01-card
has_rect "q1 the card page is open with its console" boardCardDocument

# A turn that streams for half a minute, so there is something to queue behind.
ask_card "count slowly for me"
sleep 8
shot q02-running
has_rect "q1 the first turn is running" boardBusyStrip

# The second Enter. It queues in the **worker's** queue — a card's prompts wait in that card's
# own supervisor — and until this pass nothing on screen said so.
ask_card "trace the card please"
sleep 4
shot q03-queued
if rect queueList >/dev/null; then
    ok "q2 the queued card prompt has a row in the §12 strip (queueList is on screen)"
else
    bad "q2 no queueList on screen: the prompt queued with nothing to say so (q03-queued.png)"
fi
note "  queueTitle: $(rect_text queueTitle)"
note "  queueHint:  $(rect_text queueHint)"
queue_rows q03-queued >"$out/q03-queuerows.txt"
note "  the strip's rows, read off queueList: $(tr '\n' ' ' <"$out/q03-queuerows.txt")"
# `preview` is the owner's own words, not the prompt the model is sent: `board_ask` builds that
# out of the card's seed block and the mode's brief, and the row would otherwise read
# "[Switchboard card #… ] You are Relay's Switchboard agent…".
if grep -qi "trace the card" "$out/q03-queuerows.txt"; then
    ok "q2 and the row is the owner's own words, not the prompt the model is sent"
elif grep -qi "Switchboard agent" "$out/q03-queuerows.txt"; then
    bad "q2 the row shows the model's prompt, not the owner's words (q03-queuerows.txt)"
else
    bad "q2 the row's text could not be read (q03-queuerows.txt)"
fi

# ↑ on the empty prompt box takes the head of the queue back as an unsent draft (#QRC1, landed
# while this pass was driving). A worker row goes back the only way one can — `queue_remove`
# naming *this card's* queue and not the tab's — and the **whole** prompt comes back, not the
# 120-character preview the row was drawn from.
click_rect_in composerEditor $((width / 2)) 0
k Up; sleep 3
shot q04-taken-back
queue_rows q04-taken-back >"$out/q04-queuerows.txt" 2>/dev/null
if grep -qi "trace the card" "$out/q04-queuerows.txt" 2>/dev/null; then
    bad "q3 ↑ left the row in the strip (q04-queuerows.txt)"
else
    ok "q3 ↑ took the row out of the strip"
fi
composer_box q04-taken-back >"$out/q04-draft.txt"
note "  the prompt box after ↑: $(tr '\n' ' ' <"$out/q04-draft.txt")"
if grep -qi "trace the card" "$out/q04-draft.txt"; then
    ok "q3 and it came back into the prompt box as an unsent draft"
else bad "q3 the row's words did not come back into the prompt box (q04-draft.txt)"; fi
# Leave the draft unsent, so the check below is about the prompt that was withdrawn.
k ctrl+a; k Delete; sleep 1
shot q05-removed

# And the prompt that was withdrawn never ran: one answer on the thread, not two. The owner's
# *question* stays — a withdrawn queued prompt leaves a question with no answer, which is owner
# decision 5 and what a failed turn already leaves.
sleep 45
cp "$threadA" "$out/thread-$cardA-queue.md" 2>/dev/null
answers=$(agent_comments "$threadA")
note "  agent comments on the thread after the phase: $answers"
if [[ ${answers:-0} == 1 ]]; then
    ok "q4 the withdrawn prompt never ran: one answer on the thread, and the first turn's"
else
    bad "q4 $answers agent answers on the thread — the withdrawn prompt ran (thread-$cardA-queue.md)"
fi
if grep -qi "trace the card please" "$threadA"; then
    ok "q4 and its question is still on the thread, unanswered (owner decision 5)"
else
    bad "q4 the withdrawn prompt's question is not on the thread"
fi
fi

# ================================================ one console, several cards: the transcript
if want cards; then
note "===== one console, several cards: the transcript changes hands with the card ====="
launch || exit 1
open_board s0
open_card s0 "Zebra"
ask_card "trace the card please"
sleep 40
shot s01-cardC
console_view s01-cardC >"$out/s01-console.txt"
if grep -qi "TRACEDONE\|thought for\|fixture" "$out/s01-console.txt"; then
    ok "s1 card C's turn is in the card console's transcript"
else bad "s1 card C's turn is not in the console transcript (s01-console.txt)"; fi

# The other card. Its console is the **same widget**; what must change is what is drawn in it.
back_to_list
open_card s0b "Walrus"
sleep 4
shot s02-cardD
console_view s02-cardD >"$out/s02-console.txt"
if grep -qi "TRACEDONE\|read fixture" "$out/s02-console.txt"; then
    bad "s2 card D's console still shows card C's turn (s02-console.txt)"
else ok "s2 card D's console draws none of card C's turn"; fi

ask_card "explain the fold please"
sleep 40
shot s03-cardD-answered
console_view s03-cardD-answered >"$out/s03-console.txt"
if grep -qi "anchor\|reasoning block" "$out/s03-console.txt"; then
    ok "s2 and card D's own turn is drawn in it"
else bad "s2 card D's turn is not in the console transcript (s03-console.txt)"; fi

# Back to card C: its transcript comes back, under the mark the replay prints.
back_to_list
open_card s0c "Zebra"
sleep 5
shot s04-cardC-again
console_view s04-cardC-again >"$out/s04-console.txt"
if grep -qi "TRACEDONE\|fixture" "$out/s04-console.txt"; then
    ok "s3 card C's transcript is drawn again when its card is reopened"
else bad "s3 card C's transcript did not come back (s04-console.txt)"; fi
# The rule the replay prints under a surface's own text (`Pane::surfaceTextCloseMark`). It is
# not the conversation's — "this shell is new" is a sentence about something a console does not
# have — and it is not this pane's previous shell's either.
if grep -qi "what was said here before" "$out/s04-console.txt"; then
    ok "s3 and it is the replay that drew it (the surface's own rule, under the rows)"
else note "  (the replay's rule was not read by OCR in s04-console.txt; the words above are the check)"; fi
if grep -qi "anchor\|reasoning block" "$out/s04-console.txt"; then
    bad "s3 card D's turn came back with card C's (s04-console.txt)"
else ok "s3 and card D's turn did not come back with it"; fi
fi

# ======================================= a restart that actually restores the layout (#FEJQ)
if want back; then
note "===== a restart on the same profile: same tab, same conversation ====="
launch || exit 1
open_board r0
open_card r0 "Marmot"
# **A prompt with no scene keyword in it.** The stub answers "Nothing to do here.", which is a
# real turn on a real conversation and leaves nothing on the thread that could take the scene
# away from the question after the restart — the card's file and thread are seeded in front of
# every later prompt on it.
ask_card "note this one for later please"
sleep 30
shot r01-first
if grep -rl "note this one for later" "$XDG_DATA_HOME"/relay/helper-sessions/ >"$out/r02-card-session.txt" 2>/dev/null \
   && [[ -s $out/r02-card-session.txt ]]; then
    ok "r1 the card's turn is in a helper conversation file of its own"
else bad "r1 no helper conversation file holds the card's turn"; fi

# Quit and come back. `launch --keep` passes **no** `--workspace` and no `--fresh`, so
# `restoreSavedLayout()` runs — which is the whole difference from the steps 4-5 drive.
launch --keep || exit 1
grep -o '"tab_id": *"[^"]*"' "$XDG_DATA_HOME"/relay/state/windows.json 2>/dev/null | sort -u >"$out/r03-tab-before.txt"
note "  tab ids in the saved state the restart read: $(tr '\n' ' ' <"$out/r03-tab-before.txt")"
open_board r1
open_card r1 "Marmot"
shot r04-restarted
if [[ -f $threadB ]] && grep -qi "note this one for later" "$threadB"; then
    ok "r2 the thread is complete after the restart"
else bad "r2 the thread lost the turn across the restart"; fi

ask_card "how much do you remember about this card"
sleep 40
shot r05-remembers
console_view r05-remembers >"$out/r05-console.txt"
grep -o '"tab_id": *"[^"]*"' "$XDG_DATA_HOME"/relay/state/windows.json 2>/dev/null | sort -u >"$out/r06-tab-after.txt"
note "  tab ids after the restart: $(tr '\n' ' ' <"$out/r06-tab-after.txt")"
if [[ -s $out/r03-tab-before.txt ]] && diff -q "$out/r03-tab-before.txt" "$out/r06-tab-after.txt" >/dev/null; then
    ok "r3 the tab id survived the restart (#FEJQ's tab_id, saved and restored)"
else
    bad "r3 the tab id changed across the restart (r03/r06-tab-*.txt)"
fi
after=$(grep -rl "how much do you remember" "$XDG_DATA_HOME"/relay/helper-sessions/ 2>/dev/null | sort -u)
before=$(cat "$out/r02-card-session.txt" 2>/dev/null)
note "  the card's conversation before: ${before:-(none)}"
note "  and the file the turn after the restart went into: ${after:-(none)}"
if [[ -n $after && -n $before && $after == *"$before"* ]]; then
    ok "r4 the turn after the restart went into the card's own conversation, the one from before it"
else
    bad "r4 the restarted card console started a fresh conversation (r02-card-session.txt)"
fi
cp "$threadB" "$out/thread-$cardB.md" 2>/dev/null
remembered=$(cat "$out/r05-console.txt" "$out/thread-$cardB.md" 2>/dev/null | sed -n 's/.*HISTORY turns=\([0-9]*\).*/\1/p' | tail -1)
note "  HISTORY turns=${remembered:-?} (what the model was actually handed)"
if [[ ${remembered:-0} -ge 2 ]]; then
    ok "r4 and the model was handed the conversation it had ($remembered user turns)"
else
    bad "r4 the model was handed ${remembered:-?} user turn(s): the conversation did not come back"
fi
cp "$XDG_DATA_HOME/relay/logs/relay.log" "$out/relay.log" 2>/dev/null
fi

note ""
note "$pass passed, $fail failed"
echo "$pass passed, $fail failed — $out/notes.txt"
