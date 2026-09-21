#!/usr/bin/env bash
# Every agent in Relay is a console — card #AGNT, the integration drive (steps 1-9, Verify).
#
#   drive.sh [relay-binary] [out-dir] [phase ...]
#
# Both paths are **absolute**: the script runs from its own directory, so a relative out-dir lands
# under this folder rather than where it was typed. With no phase named it runs them all.
#
#   sb     (a) the Switchboard console: a thinking bubble that folds, a tool row, the §12 queue
#              strip with a second prompt that can be edited and removed, Esc, the action row
#              (Check k · Clean up u) left-aligned above the box, and Alt+M as the pane's picker
#   card   (b) the card console: Plan (p) / Execute (x) above the box, Enter discusses and the
#              turn reaches issues/threads/<ID>.md with its provenance, Ctrl+Shift+Enter comments
#   opt    (c) the Options helper: the collapsed "Helper Agent (Alt+Q)" row, an agent write shown
#              with Undo and the row marked "changed by the agent", an option: link revealed in
#              place, and the same console after a swap to Actions
#   sess   (d) the Sessions helper: "open the three sessions about <keyword> in new panes"
#   cross  (e) the owner's rule: an option changed from the Sessions helper, a card opened from
#              the Options helper, and a terminal pane's agent reading the board
#   info   (f) the ⓘ and Activity Ask rows still draft into the terminal pane's composer
#   tabs   (g) two tabs on one project are two conversations, and a restart brings each back
#
# Isolation: Xvfb, a private HOME / XDG_* / TMPDIR under a short path (the 108-byte socket
# limit), RELAY_KEYRING=off and **no provider account** — the profile points a local model
# endpoint at stub-provider.py on loopback, so every agent in the run is that script.
# Needs Xvfb, xdotool, ImageMagick, tesseract.
#
# Each check writes one PASS/FAIL line to notes.txt naming the screenshot it was read from; a
# check that cannot find what it wanted logs FAIL and carries on, so one miss does not hide the
# rest. Every check is also looked at by eye in the shots.
set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"
out=${2:-$PWD}
root=$(cd ../../.. && pwd)
relay=${1:-$root/build/relay}
shift 2 2>/dev/null || true
phases=${*:-sb card opt sess cross info tabs}
width=1500 height=1100
port=${RELAY_QA_PORT:-8891}
mkdir -p "$out"
[[ -x $relay ]] || { echo "no relay binary at $relay"; exit 1; }

display=
for n in $(seq 150 199); do [[ -e /tmp/.X11-unix/X$n ]] || { display=:$n; break; }; done
[[ -z $display ]] && { echo "no free display"; exit 1; }

sandbox=$(mktemp -d /tmp/rl-agnt.XXXX)
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
# Where the named widgets are. OCR finds words and cannot find an icon — the bell that opens the
# notification list, a switch, the ⧉ beside a card id — so the things a drive has to *click*
# rather than read come from here. Only set for a drive: see RelayWindow::startQaRects.
export RELAY_QA_RECTS=$sandbox/rects.json
export XDG_CONFIG_HOME=$HOME/.config XDG_DATA_HOME=$HOME/.local/share XDG_CACHE_HOME=$HOME/.cache
work=$HOME/project
mkdir -p "$XDG_CONFIG_HOME/RelayTerminal" "$XDG_CONFIG_HOME/relay" "$XDG_DATA_HOME" "$work"
printf "PS1='\\\\w \\\$ '\nunset PROMPT_COMMAND\n" >"$HOME/.bashrc"
echo "the drive wrote this" >"$work/fixture.txt"

# A board with a few cards, so the Switchboard's console is not the board-less case and there is
# a card to open. The first card's id is handed to the stub, which is what `open the card` opens.
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
first = None
rows = [("The console is the prompt box", "inbox", "a"),
        ("A second card, so the list is a list", "inbox", "b"),
        ("A third card for the cleanup to look at", "discussing", "c")]
for title, status, rank in rows:
    card = B.new_card("work", title, status, created="2026-09-21", rank=rank,
                      request="the console's brief has a board")
    B.write_new_card(board, card, "features")
    if first is None:
        first = card.id
print(first)
FIX
)
[[ -n $card ]] || { echo "board fixture failed"; exit 1; }

# Three saved conversations, in the folder the pane's store writes to — the shape conv_index walks.
ids=$(PYTHONPATH="$root/backend" python3 - "$work" <<'FIX'
import json, sys, time
from pathlib import Path
from relay_core import sessions as S

work = Path(sys.argv[1])
rows = [("11111111111111111111111111111111", "Splitting the pane layout"),
        ("22222222222222222222222222222222", "The pane header and its labels"),
        ("33333333333333333333333333333333", "Pane drag and the equalize key")]
directory = S.default_session_dir(work)
directory.mkdir(parents=True, exist_ok=True)
for index, (session_id, title) in enumerate(rows):
    data = {"version": 1, "kind": "relay_session", "id": session_id, "title": title,
            "created": 1000.0, "updated": time.time() - 60 + index, "workspace": str(work),
            "model": "stub", "preset": "local:stub", "effort": "high", "mode": "build",
            "turns": 1, "epoch": 0,
            "messages": [{"role": "user", "content": f"about the pane: {title}"},
                         {"role": "assistant", "content": "answered"}],
            "snapshots": {}, "checkpoints": {"items": [
                {"turn": 1, "prompt": f"about the pane: {title}", "prompt_preview": "about",
                 "time": 1000.0, "locations": {"0": 1}, "files": {}}]},
            "requests": {"items": []}, "todos": {"items": []}, "plan_path": None,
            "open_requests": 0}
    (directory / f"{session_id}.json").write_text(json.dumps(data), encoding="utf-8")
print(" ".join(r[0] for r in rows))
FIX
)
[[ -n $ids ]] || { echo "sessions fixture failed"; exit 1; }
echo "board card #$card, sessions $ids"

python3 "$PWD/stub-provider.py" "$port" $ids "--card=$card" >/dev/null 2>&1 &
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
t() { xdotool type --delay 30 "$1"; }
k() { xdotool key --delay 60 "$@"; }
# There is no compositor under Xvfb, so a region the app has damaged is sometimes left unpainted
# — a blank block exactly where a console's vterm is, which an earlier run read as "the console
# never answered" while the header above it already wore the answer's title. A one-pixel resize
# and back forces a full expose, which is cheaper than reading a lie.
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
# A `Qt::Popup` — the notification list, the model box, a menu — is its own top-level window, so
# `import -window $win` captures the app window with a hole where the popup is. The whole screen
# is what has the popup in it.
shotroot() {
    xdotool mousemove $((width + 20)) $((height + 20)); sleep 0.5
    import -window root "$out/$1.png"
    sleep 0.3
}
text() { tesseract "$out/$1.png" - --psm 6 2>/dev/null; }
words() {
    convert "$out/$1.png" -scale 200% png:- 2>/dev/null \
        | tesseract - stdout --psm 11 tsv 2>/dev/null \
        | awk 'NF>=12 && $12 != "" {print $12, int($7/2), int($8/2), int($9/2), int($10/2)}'
}
word_xy() { words "$1" | awk -v want="$2" 'tolower($1) ~ tolower(want) {x=$2+$4/2; y=$3+$5/2} END {if (x) printf "%d %d\n", x, y}'; }
word_xy_below() { words "$1" | awk -v want="$2" -v top="$3" 'tolower($1) ~ tolower(want) && $3 > top {x=$2+$4/2; y=$3+$5/2} END {if (x) printf "%d %d\n", x, y}'; }
word_xy_first() { words "$1" | awk -v want="$2" 'tolower($1) ~ tolower(want) && !seen {seen=1; printf "%d %d\n", $2+$4/2, $3+$5/2}'; }
click_at() { [[ -z ${1:-} || -z ${2:-} ]] && return 1; xdotool mousemove "$1" "$2" click 1; sleep 1.5; }
click_word() { local at; at=$(word_xy "$1" "$2"); [[ -n $at ]] && click_at ${at% *} ${at#* }; }
# The widget by name, from RELAY_QA_RECTS. Screen coordinates, which is what xdotool takes, and
# the window sits at 0,0. A name that is not on screen fails loudly rather than clicking nothing.
rect() {   # objectName -> "x y w h text"
    python3 - "$RELAY_QA_RECTS" "$1" <<'PY' 2>/dev/null
import json, sys
try:
    rows = json.load(open(sys.argv[1]))
except Exception:
    sys.exit(1)
row = rows.get(sys.argv[2])
if not row:
    sys.exit(1)
print(row["x"], row["y"], row["w"], row["h"], row.get("text", ""))
PY
}
click_rect() {   # objectName
    local r; r=$(rect "$1") || { note "  (no widget named $1 on screen)"; return 1; }
    set -- $r
    xdotool mousemove $(( $1 + $3 / 2 )) $(( $2 + $4 / 2 )) click 1
    sleep 1.5
}
# The same name in a named part of the screen. Object names are not unique — every prompt box in
# Relay is a `composerEditor` — and their order in the dump is the object tree's, which is not
# the screen's: `composerEditor#2` turned out to be the *terminal pane's*, so the card's Enter
# went to the wrong agent and the drive read it as "the card wrote no thread".
click_rect_in() {   # objectName minx miny
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
    xdotool mousemove ${xy% *} ${xy#* } click 1
    sleep 1.5
}
count_rects() {  # name prefix -> how many are on screen
    python3 - "$RELAY_QA_RECTS" "$1" <<'PY' 2>/dev/null || echo 0
import json, re, sys
try:
    rows = json.load(open(sys.argv[1]))
except Exception:
    print(0); raise SystemExit
name = sys.argv[2]
print(sum(1 for k in rows if k == name or re.fullmatch(re.escape(name) + r"#\d+", k)))
PY
}
await() {   # shot pattern [seconds]
    local waited=0 limit=${3:-60}
    while :; do
        shot "$1"
        text "$1" | grep -qi -- "$2" && return 0
        (( waited >= limit )) && return 1
        sleep 3; waited=$((waited + 3))
    done
}
has()   { if text "$2" | grep -qi -- "$3"; then ok "$1 (\"$3\" in $2.png)"; else bad "$1: no \"$3\" in $2.png"; fi; }
# `has` reads the shot as prose (psm 6) and misses small chrome; `hasword` reads it as words at
# 200 % (psm 11), which is what finds a button label or a collapsed row.
hasword() { if [[ -n $(word_xy "$2" "$3") ]]; then ok "$1 (\"$3\" read in $2.png)"; else bad "$1: no \"$3\" in $2.png"; fi; }
hasnt() { if text "$2" | grep -qi -- "$3"; then bad "$1: \"$3\" is in $2.png and it should not be"
          else ok "$1 (no \"$3\" in $2.png)"; fi; }
awaited() { if await "$2" "$3" "${4:-60}"; then ok "$1 (\"$3\" in $2.png)"; else bad "$1: no \"$3\" in $2.png after ${4:-60}s"; fi; }

launch() {   # [--keep-profile]
    [[ -n $relay_pid ]] && { kill "$relay_pid" 2>/dev/null; wait "$relay_pid" 2>/dev/null; sleep 3; }
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

# Put the cursor in a console's composer, found by a word of the placeholder its context wrote.
focus_console() {   # shot-name placeholder-word
    shot "$1"
    click_word "$1" "$2"
    sleep 1
}
ask() { t "$1"; k Return; }
# Ctrl+Shift+Enter. **This chord does not reach Qt under Xvfb**, whichever way it is spelled:
# `key ctrl+shift+Return`, `--clearmodifiers`, and holding the two down around a plain Return
# were all tried, and holding them breaks the *next* plain Enter as well. The app's two halves
# are proved by test instead, through the real widgets:
#
#   consolemode  every chord reaches its context with its own route — auto, agent, **shell** —
#                on a shell-less console, and again after a finished turn
#   board        the card page's console turns `shell` into `board_comment` with the card and the
#                words, clears the box, and does not send them twice
#
# So what stays unverified here is the keystroke, not the route mapping and not a busy guard.
# The check is kept, and says so when it fails.
chord_comment() { k ctrl+shift+Return; }

want() { [[ " $phases " == *" $1 "* ]]; }

# ================================================================= (a) the Switchboard console
if want sb; then
note "===== (a) the Switchboard console ====="
launch || exit 1
k ctrl+shift+s; sleep 6
shot a01-switchboard
has "a1 the Switchboard's console is the prompt box, not a panel" a01-switchboard "Ask the Switchboard agent"
# The action row: left-aligned buttons above the box, each wearing its letter (#PBX1).
has "a1 the action row reads Check (k)" a01-switchboard "Check (k)"
# A console draws **no pane header**: the window has one terminal pane and one console on
# screen, so exactly one `paneHeader` is drawn. Counted rather than read, because what is being
# asserted is the absence of a row.
headers=$(count_rects paneHeader)
note "paneHeader widgets on screen with the Switchboard console up: $headers"
if [[ ${headers:-0} == 1 ]]; then ok "a1 the console draws no pane header (only the terminal pane's)"
else bad "a1 $headers pane headers are drawn: a console is wearing one"; fi
has "a1 and Clean up (u)" a01-switchboard "Clean up (u)"

# --- a thinking bubble that folds and unfolds, exactly as the pane's does
focus_console a02-focus "Ask"
ask "explain the fold please"
awaited "a2 the console streams a turn into its transcript" a03-thinking "thought for" 120
# The turn's own title is made from the prompt, and the "On screen now:" hint is not part of the
# prompt (protocol 33). It used to be composed into the string, so the console's header read
# "On screen now: Inbox 2, Discussing 1, …" — and the Sessions list and the ledger would have
# kept it. The header is gone either way; this says the text is too.
hasnt "a2 no On-screen hint reached a title" a03-thinking "On screen now"
shot a03-thinking
k alt+r; sleep 2; shot a04-fold-open
has "a2 Alt+R unfolds the thinking bubble" a04-fold-open "reasoning block\|Looking at what this console"
k alt+r; sleep 2; shot a05-fold-closed
hasnt "a2 and folds it again" a05-fold-closed "Looking at what this console"

# --- a tool row
focus_console a06-focus "Ask"
ask "read the fixture file"
awaited "a3 a tool call draws its own row in the console" a07-toolrow "read_file\|Read fixture" 120

# --- the §12 queue strip: a second prompt queues, can be opened in the box, and removed
focus_console a08-focus "Ask"
ask "count slowly to twenty"
sleep 10
ask "and then say hello"
# Read **while the first turn is still running**: the strip is up only until the queue drains,
# and the queued row is the one place those words are — the composer was cleared by the send and
# the transcript has not printed them. A `QueueRowDelegate` row is "✦ <the prompt>  ×".
sleep 4; shot a09-queued
has "a4 the second prompt is in the §12 queue strip" a09-queued "and then say hello"
# Up opens the queued item in the prompt box — the pane's own edit path (queue.remove.mouse hint).
k Up; sleep 2; shot a10-queue-edit
has "a4 Up opens the queued prompt in the box to edit" a10-queue-edit "and then say hello"
# Shift+Delete **while the row is still selected**: Esc is queuenav's Cancel and leaves the
# queue, after which Shift+Delete has no row to remove (relay::queuenav::decide).
k shift+Delete; sleep 1; k ctrl+a Delete; sleep 2; shot a11-queue-removed
hasnt "a4 Shift+Delete removes it from the strip" a11-queue-removed "and then say hello"
# Esc stops the running turn.
k Escape; sleep 5; shot a12-esc
has "a4 Esc stops the turn" a12-esc "stopped\|cancelled\|Ready"

# --- the model box is the pane's picker, opened for this console with Alt+M
focus_console a13-focus "Ask"
k alt+m; sleep 3; shot a14-modelbox
has "a5 Alt+M opens this console's model box" a14-modelbox "Follow Main\|Main ·\|Stub"
k Escape; sleep 1

# --- Check (k). The letters of the action row are the *board's* keys (BoardView::handleBoardKey):
# they work anywhere in the pane that is not a text field, exactly as `n`, `e`, `p` and `x` do. So
# the composer is emptied and the keyboard put back on the list before `k` is pressed.
focus_console a15-focus "Ask"
k ctrl+a Delete; sleep 1
shot a15b-empty
click_word a15b-empty "inbox"
k k; sleep 10; shot a16-check
if text a16-check | grep -qi "problem\|finding\|no problems\|checked\|nothing to fix"; then
    ok "a6 the k key ran Check and the board answered (a16-check.png)"
else
    bad "a6 the k key did not run Check — see a16-check.png"
fi
fi

# ======================================================================= (b) the card console
if want card; then
note "===== (b) the card console ====="
launch || exit 1
k ctrl+shift+s; sleep 6
shot b00-folded
# The board opens with its sections folded, so the section header is clicked first and the card
# afterwards — a click on a title that is not drawn opens nothing, which is what the first run of
# this drive read as "the card page has no reply box".
click_word b00-folded "^inbox$"
sleep 2; shot b01-list
click_word b01-list "console"
sleep 4; shot b02-card
has "b1 the card page carries the console's reply box" b02-card "Enter discusses"
hasword "b1 Plan (p) is on the action row above the box" b02-card "^plan"
hasword "b1 and Execute (x), in the accent outline that says it leaves the board" b02-card "^execute"

# **The comment first**, on a card nothing has run on: Ctrl+Shift+Enter is the chord that writes
# the thread with no model call, and doing it first says whether the chord works at all before
# a turn's own bookkeeping is in the way. (It did not land when it was sent straight after a
# Discuss turn, and this is what tells the two apart.)
shot b03-focus
click_rect_in composerEditor $((width / 2)) 0 || focus_console b03-focus "discusses"
t "a note with no model call"
# `--clearmodifiers`: xdotool holds whatever the last `type` left on the modifier state, and a
# chord sent on top of it is not the chord. `board`'s own case drives all three chords through
# the console's editor and they land, before and after a turn — so what this is aiming at is
# the keystroke, not the page.
chord_comment       # Ctrl+Shift+Enter, held rather than sent as one key
sleep 8
thread=$work/issues/threads/$card.md
if grep -qi "a note with no model call" "$thread" 2>/dev/null; then
    ok "b3 Ctrl+Shift+Enter wrote a comment to the thread with no model call"
else
    bad "b3 Ctrl+Shift+Enter wrote no comment (Xvfb does not deliver the chord; see chord_comment)"
fi
shot b03b-comment
cp "$thread" "$out/thread-$card-comment.md" 2>/dev/null

click_rect_in composerEditor $((width / 2)) 0 || focus_console b03-focus "discusses"
ask "say hello to the card"
sleep 25; shot b04-discussed
if [[ -f $thread ]]; then
    cp "$thread" "$out/thread-$card.md"
    if grep -qi "say hello to the card" "$thread"; then
        ok "b2 Enter discussed: the owner's words are in issues/threads/$card.md"
    else bad "b2 the owner's words are not in issues/threads/$card.md"; fi
    if grep -qi "model=" "$thread"; then
        ok "b2 the answer carries its model=/turn= provenance"
    else bad "b2 no model= provenance in issues/threads/$card.md"; fi
else
    bad "b2 no thread file at $thread"
fi

# And again, straight after the turn: the chord is the same, the card is busy with nothing, and
# a second note lands beside the first.
shot b05-focus
click_rect_in composerEditor $((width / 2)) 0 || focus_console b05-focus "discusses"
t "a second note, after the turn"
chord_comment
sleep 8; shot b06-comment
if grep -qi "a second note, after the turn" "$thread" 2>/dev/null; then
    ok "b3 and a comment straight after a Discuss turn lands too"
else bad "b3 a comment after a Discuss turn did not land either (the same undelivered chord)"; fi
cp "$thread" "$out/thread-$card-after.md" 2>/dev/null
fi

# ========================================================================= (c) Options helper
if want opt; then
note "===== (c) the Options helper ====="
launch || exit 1
k ctrl+shift+o; sleep 5
shot c01-options
has "c1 Options carries the collapsed Helper Agent row" c01-options "Helper Agent"
k alt+q; sleep 3
focus_console c02-console "Ask"
has "c1 Alt+Q expands it into a console with the Options placeholder" c02-console "Ask the Options helper"

# The notification list is newest-first and scrolls, and a turn posts "Agent finished" of its
# own — so the change's notice can be below the fold before it has been looked for. Cleared
# first, and then the only thing in the list is what this turn did.
click_rect windowBellButton && { sleep 1; shotroot c02b-notices; click_word c02b-notices "clear"; k Escape; sleep 1; }
focus_console c02c-focus "Ask"
ask "turn on copy on select for me"
awaited "c2 the helper says in text what it changed" c03-changed "turned Copy on select on" 240
sleep 3; shot c03-changed
# The write itself, on disk, before anything is read off a screenshot.
note "relay.conf: $(grep -i copy_on_select "$XDG_CONFIG_HOME/RelayTerminal/relay.conf" 2>/dev/null || echo '(absent)')"
if grep -qi "copy_on_select=true" "$XDG_CONFIG_HOME/RelayTerminal/relay.conf" 2>/dev/null; then
    ok "c2 the agent's write reached the setting"
else
    bad "c2 the agent's write did not reach the setting — see c03-changed.png"
fi
# The **row** wears it — "changed by the agent just now: off → on" under the switch
# (SettingsPane::agentNote) — and the row is in the Terminal section, which is not the one the
# pane opens on. The agent's own `option:` link is what reveals it, which is c3's subject and is
# used here for its side effect.
focus_console c03c-focus "Ask"
ask "where is copy on select?"
awaited "c3 an option: link is in the answer" c06-link "Clicking that opens" 240
sleep 4; shot c06-link
at=$(word_xy c06-link "openrow")
if [[ -n $at ]]; then
    click_at ${at% *} ${at#* }; sleep 2; shot c07-revealed
    has "c3 the link revealed the row in this same pane" c07-revealed "Copy on select"
    has "c2 and the row is marked changed by the agent" c07-revealed "changed by the agent"
    # …and again after the Undo, to say the marker goes with the change it was about.
    marked_before=1
else
    bad "c3 no link text to click in c06-link.png"; shot c07-revealed
fi
# The notice is in the bell's list, not on the screen: "Agent changed Copy on select … Undo"
# (#FEJQ, protocol 30.6). The bell is an icon, so it is clicked by name out of RELAY_QA_RECTS
# rather than hunted for in the pixels.
click_rect windowBellButton || click_at $((width - 281)) 27
sleep 2; shotroot c04-notice
has "c2 the change is offered with Undo where the owner will find it" c04-notice "Agent changed"
has "c2 and the notice offers Undo" c04-notice "Undo"
click_word c04-notice "undo"
sleep 3; shot c04-undone
note "relay.conf after Undo: $(grep -i copy_on_select "$XDG_CONFIG_HOME/RelayTerminal/relay.conf" 2>/dev/null || echo '(absent)')"
if grep -qi "copy_on_select=true" "$XDG_CONFIG_HOME/RelayTerminal/relay.conf" 2>/dev/null; then
    bad "c2 Undo did not put Copy on select back (still true in relay.conf)"
else
    ok "c2 Undo put Copy on select back"
fi
# The row again: the marker is the change's, so it goes when the change does (§30.6).
focus_console c10-focus "Ask"
ask "where is copy on select?"
awaited "c5 the link is offered again" c11-link "Clicking that opens" 240
sleep 3; shot c11-link
at=$(word_xy c11-link "openrow")
if [[ -n $at ]]; then
    click_at ${at% *} ${at#* }; sleep 2; shot c12-unmarked
    hasnt "c5 the marker went with the change it was about" c12-unmarked "changed by the agent"
else
    bad "c5 no link to click in c11-link.png"; shot c12-unmarked
fi

# Actions: `palette.open` gives it a pane of its own beside Options rather than swapping the
# mode of the one that is up, so what is asserted is that *it* has a console too, with its own
# brief — on the same tab, so on the same worker and the same conversation.
k ctrl+shift+a; sleep 5; shot c08-actions
hasword "c4 Actions carries its own collapsed helper row" c08-actions "helper"
k alt+q; sleep 4
focus_console c09-actions-console "Actions"
has "c4 and behind it is a console with the Actions brief" c09-actions-console "Ask the Actions helper"
fi

# ======================================================================== (d) Sessions helper
if want sess; then
note "===== (d) the Sessions helper ====="
launch || exit 1
k ctrl+shift+y; sleep 5
shot d01-sessions
hasword "d1 Sessions carries the collapsed Helper Agent row" d01-sessions "helper"
k alt+q; sleep 3
focus_console d02-console "Ask"
has "d1 and expands into a console of its own" d02-console "Ask the Sessions helper"
ask "open the sessions about panes in new panes"
awaited "d2 the helper says in text what it is doing" d03-opened "Opened 3 conversations\|Opened 3" 180
sleep 6; shot d04-panes
# The tab label counts the panes ("project (2)"), and each new pane prints "Session loaded: …"
# into its own transcript. Both are read: the count is the fact, the lines are the picture.
tabcount=$(text d04-panes | sed -n 's/.*project (\([0-9]*\)).*/\1/p' | head -1)
opened=$(text d04-panes | grep -c "Session loaded")
note "panes in the tab after the open: ${tabcount:-?} · transcripts saying \"Session loaded\": $opened"
if [[ ${tabcount:-0} -ge 4 ]]; then
    ok "d2 the three conversations opened in panes of their own (tab says $tabcount)"
else
    bad "d2 the tab has ${tabcount:-?} pane(s): the conversations did not open — see d04-panes.png"
fi
fi

# ===================================================================== (e) across the contexts
if want cross; then
note "===== (e) agents work across panes and contexts ====="
launch || exit 1
# e1 — from the Sessions helper, change an option.
k ctrl+shift+y; sleep 5; k alt+q; sleep 3
focus_console e01-sessions "Ask"
ask "turn on copy on select from here"
awaited "e1 the Sessions helper changed an option" e02-sessions-option "Copy on select" 150
has "e1 and the write is offered with Undo" e02-sessions-option "Undo"

# e2 — from the Options helper, open a card.
k ctrl+shift+o; sleep 4; k alt+q; sleep 3
focus_console e03-options "Ask"
ask "open the card please"
awaited "e2 the Options helper opened a card in the Switchboard" e04-card-opened "Opened card" 150
sleep 4; shot e05-card-open
has "e2 and the Switchboard is showing it" e05-card-open "#$card\|$card"

# e3 — a terminal pane's agent using the board tools, which is the rule read the other way:
# a context specialises an agent without fencing it, so the terminal agent has `board_*` too.
shot e06-terminal
click_word e06-terminal "prompts"
ask "read the board for me"
awaited "e3 a terminal pane's agent used the board tools" e07-boardlist "BOARDLIST" 150
sleep 3; shot e07-boardlist
line=$(text e07-boardlist | grep -i "BOARDLIST" | tail -1)
note "the board, as the terminal pane's agent was given it: $line"
if grep -qi "count=[1-9]" <<<"$line"; then
    ok "e3 and it came back with the board's cards"
else
    bad "e3 the board tool answered nothing: $line"
fi
fi

# ================================================================= (f) the Info/Activity rows
if want info; then
note "===== (f) the Info and Activity Ask rows ====="
launch || exit 1
shot f01-pane
# A turn in the terminal pane, so ⓘ and Activity have something to show.
click_word f01-pane "prompts"
ask "say hello"
awaited "f1 the terminal pane answered" f02-answered "Hello from the tab" 120
k alt+i; sleep 4; shot f03-info
has "f1 the ⓘ pane carries its Ask row" f03-info "Ask"
# The row is three buttons under a muted line ("Ask the agent about this session · drafts a
# question in the terminal's prompt box · nothing is sent"), so a button is what is clicked.
at=$(word_xy f03-info "costliest")
[[ -z $at ]] && at=$(word_xy f03-info "^context")
if [[ -n $at ]]; then
    click_at ${at% *} ${at#* }; sleep 1
    # The question is a **draft** in the terminal pane's own composer, never a send: the row
    # hands it to the owning pane, which puts it at the cursor and focuses it (src/AskRow.h).
    shot f04-drafted
    if text f04-drafted | grep -qi "Shell commands or agent prompts"; then
        bad "f1 the ⓘ Ask row left the pane's composer empty (f04-drafted.png)"
    else
        ok "f1 the ⓘ Ask row drafted its question into the terminal pane's composer"
    fi
    k ctrl+a Delete; sleep 1
else
    bad "f1 no Ask row to click in f03-info.png"; shot f04-drafted
fi
# Activity opened from a console shows that console's turns, not the terminal pane's.
k ctrl+shift+o; sleep 4; k alt+q; sleep 3
focus_console f05-options "Ask"
ask "explain the fold in options"
awaited "f2 the Options console answered" f06-console-turn "thought for" 150
k alt+shift+r; sleep 5; shot f07-activity
if text f07-activity | grep -qi "Looking at what this console\|thinking\|reasoning"; then
    ok "f2 Activity opened from the console shows that console's turn (f07-activity.png)"
else
    bad "f2 Activity from the console shows nothing of its turn — see f07-activity.png"
fi
fi

# ========================================================= (g) two tabs, and a restart
if want tabs; then
note "===== (g) two tabs on one project, and a restart ====="
launch || exit 1
k ctrl+shift+o; sleep 4; k alt+q; sleep 3
focus_console g01-tab1 "Ask"
ask "say hello"
awaited "g1 tab one's helper answered" g02-tab1-answer "Hello from the tab" 120
k ctrl+shift+t; sleep 6          # a second tab on the same project
k ctrl+shift+o; sleep 4; k alt+q; sleep 3
shot g03-tab2
hasnt "g1 the second tab's helper is a conversation of its own" g03-tab2 "Hello from the tab"
helperdir=$XDG_DATA_HOME/relay/helper-sessions
focus_console g04-tab2 "Ask"
ask "say hello"
awaited "g1 tab two's helper answered too" g05-tab2-answer "Hello from the tab" 120
files=$(find "$helperdir" -name '*.json' ! -name '*.meta.json' 2>/dev/null | wc -l)
find "$helperdir" -name '*.json' 2>/dev/null | sort >"$out/helper-sessions.txt"
note "helper conversation files: $files"
if [[ ${files:-0} -ge 2 ]]; then ok "g1 two tabs kept two conversations"
else bad "g1 two tabs wrote $files conversation file(s)"; fi

# The restart: the same profile, no --fresh, and the tab comes back to the **conversation** it
# had. What persists is the store, not the transcript — a console is not a window leaf and is
# never serialised (§33), so it redraws empty and the agent remembers. The check is therefore
# that asking again lands in the file that was already there rather than in a new one.
before=$(find "$helperdir" -name '*.json' ! -name '*.meta.json' 2>/dev/null | wc -l)
launch --keep || exit 1
sleep 4
k ctrl+shift+o; sleep 4; k alt+q; sleep 3
focus_console g06-restarted "Ask"
ask "say hello"
awaited "g2 the helper answers after a restart" g07-restarted-answer "Hello from the tab" 150
sleep 3
after=$(find "$helperdir" -name '*.json' ! -name '*.meta.json' 2>/dev/null | wc -l)
find "$helperdir" -name '*.json' 2>/dev/null | sort >"$out/helper-sessions-after.txt"
# A count is not the check: the *second* tab has no project and asks for the first time after
# the restart, which is a new conversation and not a move. What says the restored tab went back
# to the one it had is the history below, which is the owner's decision in one number.
note "helper conversation files: $before before the restart, $after after"
# And the history came with it: the model is asked how many user messages it was handed, which
# is more than one only if the earlier turn is still in the conversation.
focus_console g08-focus "Ask"
ask "how much do you remember?"
awaited "g2 the restored console's agent still has the earlier turn" g09-history "HISTORY turns=" 150
sleep 3; shot g09-history
line=$(text g09-history | grep -o "HISTORY turns=[0-9]*" | tail -1)
note "the restored console's conversation, as the worker handed it to the model: $line"
turns=$(sed -n 's/HISTORY turns=//p' <<<"$line")
if [[ ${turns:-0} -ge 2 ]]; then
    ok "g2 the conversation came back with its history ($line)"
else
    bad "g2 the restored console started from nothing ($line)"
fi
fi

cp "$sandbox/relay.log" "$out/relay.log" 2>/dev/null
cp "$XDG_DATA_HOME/relay/logs/worker.log" "$out/worker.log" 2>/dev/null
echo "PASS $pass  FAIL $fail"
note "----"
note "PASS $pass  FAIL $fail"
[[ $fail == 0 ]]
