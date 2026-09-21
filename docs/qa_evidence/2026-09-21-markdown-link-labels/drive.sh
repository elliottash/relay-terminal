#!/usr/bin/env bash
# A markdown link's **label** opens what it names — card #MDKN, the open item on #AGNT's QA list.
#
#   drive.sh [relay-binary] [out-dir] [phase ...]
#
# Both paths are **absolute**: the script runs from its own directory, so a relative out-dir
# lands under this folder rather than where it was typed. With no phase named it runs them all.
#
#   opt      the `option:` label clicked in a **terminal pane** opens Options at the row
#   sess     the `session:` label selects that conversation
#   sessctl  the same, clicking the printed `(session:…)` **target** instead of the label, so the
#            two can be compared on the one surface where a session link selects in place
#   card     the `#ID` label opens the card page
#   file     the path label opens the file in a preview pane
#   console  the same `option:` label clicked in the **Options helper's own console** reveals the
#            row in that same pane, through the context's first refusal
#   rewrap   the pane made narrower so the paragraph re-wraps: the label still opens the row,
#            which is the FoldSpan road rather than the grid one
#   think    the label inside a **thinking bubble** — a fold's own rows, drawn by the host
#   restart  the tab restarted on the same profile: the restored transcript's links still open.
#            Saved terminal bytes keep SGR and drop OSC 8, so the *label* there is plain text
#            again and the printed `(target)` beside it is what opens — degraded, never broken
#   pixels   ten shots of a transcript with no links, and ten of one that is all links, this
#            tree against **the same tree with the card switched off**: identical but the caret
#
# **What the gates read.** Never a word the transcript also says. An Options page is read out of
# `RELAY_QA_RECTS` — the `settingsRowLabel`s that are actually drawn — and a page is proved by
# its own rows being up and the other page's being gone. Every link's label is a word
# (`OPENROW`, `SESSIONROW`, `CARDROW`, `FILEROW`, `SITEROW`) that appears nowhere else on
# screen, so a click on the label cannot land on the same word in the prose, and the printed
# `(target)` beside it is a different token again — which is what tells the two apart.
#
# Isolation: Xvfb, a private HOME / XDG_* / TMPDIR under a short path (the 108-byte socket
# limit), RELAY_KEYRING=off and **no provider account** — the profile points a local model
# endpoint at stub-provider.py on loopback, so every agent in the run is that script.
# Needs Xvfb, xdotool, ImageMagick, tesseract.
set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"
out=${2:-$PWD}
root=$(cd ../../.. && pwd)
relay=${1:-$root/build/relay}
shift 2 2>/dev/null || true
phases=${*:-opt sess sessctl card file console rewrap think restart pixels}
width=1500 height=1100
port=${RELAY_QA_PORT:-8896}
mkdir -p "$out"
[[ -x $relay ]] || { echo "no relay binary at $relay"; exit 1; }

display=
for n in $(seq 150 199); do [[ -e /tmp/.X11-unix/X$n ]] || { display=:$n; break; }; done
[[ -z $display ]] && { echo "no free display"; exit 1; }

sandbox=$(mktemp -d /tmp/rl-lbl.XXXX)
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
    c = B.new_card("work", title, status, created="2026-09-21", rank=rank,
                   request="the console's brief has a board")
    B.write_new_card(board, c, "features")
    if first is None:
        first = c.id
print(first)
FIX
)
[[ -n $card ]] || { echo "board fixture failed"; exit 1; }

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
click_at() { [[ -z ${1:-} || -z ${2:-} ]] && return 1; xdotool mousemove "$1" "$2" click 1; sleep 1.5; }
click_word() { local at; at=$(word_xy "$1" "$2"); [[ -n $at ]] && click_at ${at% *} ${at#* }; }

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

# ---- what page of Options is showing -------------------------------------------------------
#
# Every row drawn on the page that is *up* has a `settingsRowLabel` (or …Strong) whose `text` is
# the row's title, so the set of those titles names the page. Nothing here reads the transcript,
# which is the whole point: "Copy on select" is in the console's answer on every page.
page_rows() {
    python3 - "$RELAY_QA_RECTS" <<'PY' 2>/dev/null
import json, re, sys
try:
    rows = json.load(open(sys.argv[1]))
except Exception:
    raise SystemExit
for key, row in rows.items():
    if re.fullmatch(r"settingsRowLabel(Strong)?(#\d+)?", key):
        text = (row.get("text") or "").strip()
        if text:
            print(text)
PY
}
page_has()   { if page_rows | grep -qix -- "$2"; then ok "$1 (\"$2\" is a row of the page on screen)"
               else bad "$1: no row \"$2\" on the page that is showing"; fi; }
page_hasnt() { if page_rows | grep -qix -- "$2"; then bad "$1: row \"$2\" is still on the page — it did not move"
               else ok "$1 (no \"$2\" row: that page is gone)"; fi; }

# ---- pixels: how bright an outline is -------------------------------------------------------
#
# Luminance rather than a hex comparison: a theme may move `@border`, and what the owner was
# looking at is "bright ring" against "quiet frame" — #e6e8ec against #2a2e37 in relay-dark.
luma() {   # RRGGBB -> 0..255
    python3 -c "import sys; h=sys.argv[1]; print(int(0.2126*int(h[0:2],16)+0.7152*int(h[2:4],16)+0.0722*int(h[4:6],16)))" "$1" 2>/dev/null
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
hasnt() { if text "$2" | grep -qi -- "$3"; then bad "$1: \"$3\" is in $2.png and it should not be"
          else ok "$1 (no \"$3\" in $2.png)"; fi; }
awaited() { if await "$2" "$3" "${4:-60}"; then ok "$1 (\"$3\" in $2.png)"; else bad "$1: no \"$3\" in $2.png after ${4:-60}s"; fi; }

launch() {
    # `--keep` is what makes a relaunch a *restart*: it drops **both** --fresh and --workspace,
    # because `main()` reads either of them as "start fresh"
    # (`startFresh = parser.isSet(fresh) || parser.isSet(workspace)`), and with a fresh start
    # nothing is restored and the restart phase is measuring an empty pane. The drive already
    # runs Relay with its working directory in the project, which is what the restored window
    # comes back to.
    local args=(--workspace "$work" --fresh)
    [[ ${1:-} == --keep ]] && args=()
    [[ -n $relay_pid ]] && { kill "$relay_pid" 2>/dev/null; wait "$relay_pid" 2>/dev/null; sleep 6; }
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

# The cursor into a console's own prompt box. By rectangle, never by a word of the placeholder:
# object names are not unique, and `word_xy` takes the **last** OCR match, so "Ask" in the
# Switchboard console's placeholder is one of several and a miss puts the keystrokes into the
# board's list — where `w`, `h`, `i` … are the board's own keys and the drive silently types a
# new card instead of a prompt (that happened, twice, in this run's first pass). The console of a
# tool pane is the lowest `composerEditor` on the right-hand half of the window.
focus_console() {   # shot-name
    shot "$1"
    click_rect_in composerEditor $((width / 2)) 0
    sleep 1
}
ask() { t "$1"; k Return; }

# The **label**, clicked where it is. A markdown `[LABEL](target)` prints as `LABEL (target)`;
# until card #MDKN only the `(target)` was clickable, and the label — painted in the link ink —
# was paint. This aims at the label on purpose, and never at the target beside it: every label
# in the drive is a word that occurs nowhere else on the screen, and `word_xy` takes the last
# OCR match, so a hit is the label or nothing.
click_label() {   # shot-name LABEL
    local at; at=$(word_xy "$1" "$2")
    [[ -z $at ]] && { note "  (no \"$2\" label in $1.png to click)"; return 1; }
    note "  clicking the label \"$2\" at $at"
    click_at ${at% *} ${at#* }
}
# The lowest match at or below a line, for a surface that shows the same words twice. The
# Sessions helper lists the conversation whose latest message *is* this answer, so its preview
# holds `[SESSIONROW](session:…)` in raw markdown halfway up the pane while the console's own
# transcript is at the foot of it; `word_xy` takes the last OCR match, and the last one is not
# reliably the lowest. The pattern is loose on purpose: at the console's size tesseract reads
# `SESSIONROW` as `SESSTONROW`.
click_label_below() {   # shot-name PATTERN miny
    local at
    at=$(words "$1" | awk -v want="$2" -v top="$3" \
        'tolower($1) ~ tolower(want) && $3 + $5 / 2 >= top { y = $3 + $5 / 2; if (y > by) { by = y; bx = $2 + $4 / 2 } }
         END { if (bx) printf "%d %d\n", bx, by }')
    [[ -z $at ]] && { note "  (no \"$2\" label below $3 in $1.png to click)"; return 1; }
    note "  clicking the label \"$2\" at $at"
    click_at ${at% *} ${at#* }
}
# The `(target)` printed beside a label, for the restart phase, where the label is plain text
# again because saved terminal bytes carry no OSC 8.
click_target() {   # shot-name token
    local at; at=$(word_xy "$1" "$2")
    [[ -z $at ]] && { note "  (no \"$2\" target in $1.png to click)"; return 1; }
    click_at ${at% *} ${at#* }
}
# A shot that does not put the window back to $width: the re-wrap phase needs the narrow one.
shotkeep() {
    xdotool mousemove $((width + 20)) $((height + 20)); sleep 0.6
    import -window "$win" "$out/$1.png"
    xdotool windowfocus "$win" 2>/dev/null
    sleep 0.3
}
# The terminal pane's own prompt box: the lowest composerEditor on the LEFT of the window, which
# is the pane itself rather than a tool pane's console.
focus_pane() {   # shot-name
    shot "$1"
    click_rect_in composerEditor 0 0
    sleep 1
}

want() { [[ " $phases " == *" $1 "* ]]; }

# The shared opening of every terminal-pane phase: launch, ask for the paragraph of links, and
# leave a shot of it with the five labels on screen.
ask_for_links() {   # shot-name step
    launch || return 1
    focus_pane "$1-focus"
    ask "show me the links please"
    awaited "$2 the reply carries the paragraph of links" "$1" "SITEROW" 240 || return 1
    sleep 2; shot "$1"
}

# ============================================================ (a) option: — a terminal pane
if want opt; then
note "===== (a) the option: label, clicked in a terminal pane ====="
if ask_for_links a01-links a0; then
    has "a0 the label and the target are both printed" a01-links "OPENROW"
    note "the page before the click: $(page_rows | tr '\n' '|')"
    if click_label a01-links "OPENROW"; then
        sleep 4; shot a02-options
        note "the page after the click: $(page_rows | tr '\n' '|')"
        page_has "a1 the label opened Options at the row it names" "Copy on select"
        page_hasnt "a1 on the Terminal page, not the one Options opens on" "Thinking display"
    else
        bad "a1 no OPENROW label to click in a01-links.png"; shot a02-options
    fi
fi
fi

# =========================================================== (b) session: — two surfaces
if want sess; then
note "===== (b) the session: label ====="
# (b1) The Sessions helper's own console, where the context gets first refusal and selects the
# conversation *in that pane*. This is the surface — and the gate — the #AGNT drive used for the
# printed target, so the only difference here is that the click lands on the label.
launch || exit 1
k ctrl+shift+y; sleep 5
k alt+q; sleep 3
focus_console b01-console
has "b0 the Sessions helper is a console" b01-console "the Sessions helper"
ask "show me the links please"
awaited "b1 the console's answer carries the paragraph" b02-links "SESSIONROW" 240
sleep 3; shot b02-links
if click_label_below b02-links "SESS.*ROW" 700; then
    sleep 3; shot b03-selected
    has "b1 the label selected that conversation in this pane" b03-selected "pane header and"
    hasnt "b1 and the one that was selected before it is not" b03-selected "equalize key"
else
    bad "b1 no SESSIONROW label in the console's transcript in b02-links.png"; shot b03-selected
fi
# (b2) The same label in a terminal pane goes through the window instead. `openSessionsFor()`
# takes the id as a *search*, not as a selection, so what the label proves here is that it
# routed to `relay://session/<id>` at all: the Sessions pane is up and the id is in its box.
# (That routing is the pane's, not this card's — the printed target has always done the same.)
if ask_for_links b04-links b2; then
    if click_label b04-links "SESSIONROW"; then
        sleep 4; shot b05-sessions
        has "b2 the label opened the Sessions pane from a terminal pane" b05-sessions "Recently closed"
    else
        bad "b2 no SESSIONROW label to click in b04-links.png"; shot b05-sessions
    fi
fi
fi

# ========== (b-control) the printed target, in the same console, so label and target compare
if want sessctl; then
note "===== (b-control) the printed (session:…) target in the Sessions helper's console ====="
launch || exit 1
k ctrl+shift+y; sleep 5
k alt+q; sleep 3
focus_console bc1-console
ask "show me the links please"
awaited "bc1 the console's answer carries the paragraph" bc2-links "SESSIONROW" 240
sleep 3; shot bc2-links
if click_target bc2-links "session:"; then
    sleep 3; shot bc3-selected
    has "bc1 the printed target selects that conversation in this pane" bc3-selected "pane header and"
else
    bad "bc1 no session: target to click in bc2-links.png"; shot bc3-selected
fi
fi

# ============================================================== (c) #ID — a terminal pane
if want card; then
note "===== (c) the #ID label, clicked in a terminal pane ====="
if ask_for_links c01-links c0; then
    if click_label c01-links "CARDROW"; then
        sleep 5; shot c02-card
        has "c1 the label opened the card page" c02-card "Back to board"
        has "c1 …on the card it named" c02-card "The console is the prompt box"
    else
        bad "c1 no CARDROW label to click in c01-links.png"; shot c02-card
    fi
fi
fi

# ============================================================ (d) a path — a terminal pane
if want file; then
note "===== (d) the path label, clicked in a terminal pane ====="
if ask_for_links d01-links d0; then
    if click_label d01-links "FILEROW"; then
        sleep 4; shot d02-file
        # The pane's own title, not the file's body: a preview pane draws the text at the
        # terminal's size and OCR reads "the drive wrote this" as noise. The title says which
        # file was opened, which is the thing under test.
        has "d1 the label opened the file it names" d02-file "fixture"
    else
        bad "d1 no FILEROW label to click in d01-links.png"; shot d02-file
    fi
    # The web label: nothing in this sandbox can open a browser, so what is asserted is that the
    # label resolved as a URL and was handed to the desktop — not read as a path that is not
    # there, which is what the pane says when a link resolves to nothing.
    if click_label d02-file "SITEROW"; then
        sleep 2; shot d03-site
        hasnt "d2 the web label is a URL, not a path the pane could not find" d03-site "No such file"
    else
        bad "d2 no SITEROW label to click in d02-file.png"; shot d03-site
    fi
fi
fi

# ============================== (e) the same label in the Options helper's own console
if want console; then
note "===== (e) the option: label, clicked in the Options helper's own console ====="
launch || exit 1
k ctrl+shift+o; sleep 5
k alt+q; sleep 3
focus_console e01-console
has "e0 the Options helper is a console" e01-console "Ask the Options helper"
page_has "e0 Options opens on General" "Thinking display"
page_hasnt "e0 and the Terminal page is not the one up" "Copy on select"
ask "show me the links please"
awaited "e1 the console's answer carries the paragraph" e02-links "OPENROW" 240
sleep 3; shot e02-links
if click_label e02-links "OPENROW"; then
    sleep 3; shot e03-revealed
    note "the page after the label: $(page_rows | tr '\n' '|')"
    page_has "e1 the label revealed the row in this same pane" "Copy on select"
    page_hasnt "e1 and the page it was on is gone" "Thinking display"
else
    bad "e1 no OPENROW label to click in e02-links.png"; shot e03-revealed
fi
fi

# ================================================= (f) the paragraph re-wrapped at a new width
if want rewrap; then
note "===== (f) the label after the pane is made narrower and the paragraph re-wraps ====="
if ask_for_links f01-wide f0; then
    # Narrower: the prose block is away from the width it was printed at, so the view paints its
    # own wrap of the block's logical lines and the label's target travels in the span rather
    # than in a cell. The window stays narrow for the click.
    xdotool windowsize "$win" 880 "$height"; sleep 3
    shotkeep f02-narrow
    has "f1 the paragraph is still on screen after the re-wrap" f02-narrow "OPENROW"
    if click_label f02-narrow "OPENROW"; then
        sleep 4; shotkeep f03-options
        note "the page after the re-wrapped label: $(page_rows | tr '\n' '|')"
        page_has "f1 the re-wrapped label opens the row it names" "Copy on select"
        page_hasnt "f1 on the Terminal page" "Thinking display"
    else
        bad "f1 no OPENROW label to click in f02-narrow.png"; shotkeep f03-options
    fi
    xdotool windowsize "$win" "$width" "$height"; sleep 2
fi
fi

# ====================================================== (g) the label inside a thinking bubble
if want think; then
note "===== (g) the label inside a thinking bubble, which is a fold ====="
launch || exit 1
focus_pane g00-focus
ask "think about the links for me"
awaited "g0 the turn ran and the reasoning settled" g01-thought "thought for" 240
k alt+r; sleep 3; shot g02-unfolded
has "g0 the bubble is open and the label is in it" g02-unfolded "OPENROW"
if click_label g02-unfolded "OPENROW"; then
    sleep 4; shot g03-options
    note "the page after the bubble's label: $(page_rows | tr '\n' '|')"
    page_has "g1 a label in a fold's own rows opens the row it names" "Copy on select"
    page_hasnt "g1 on the Terminal page" "Thinking display"
else
    bad "g1 no OPENROW label to click in g02-unfolded.png"; shot g03-options
fi
fi

# =========================================== (h) a restart: the restored transcript still opens
if want restart; then
note "===== (h) the restored transcript after a restart on the same profile ====="
if ask_for_links h01-links h0; then
    launch --keep || exit 1   # no --fresh: the tab comes back to its saved terminal text
    sleep 10; shot h02-restored
    has "h1 the paragraph came back with the tab" h02-restored "OPENROW"
    # Saved terminal bytes keep SGR and drop every other escape (Pane::sanitizeSgrOnly), so the
    # label's OSC 8 run is gone and the label is plain text again. The `(target)` printed beside
    # it is text and always was: the restored transcript degrades to what it did before card
    # #MDKN, and never to a link that opens the wrong thing or nothing at all.
    if click_target h02-restored "option:"; then
        sleep 4; shot h03-options
        note "the page after the restored target: $(page_rows | tr '\n' '|')"
        page_has "h1 the printed target still opens the row after a restart" "Copy on select"
    else
        bad "h1 no option: target to click in h02-restored.png"; shot h03-options
    fi
fi
fi

# ================= (i) the same transcript, this binary against the same tree with it switched off
#
# The control is not an older tip — main moves under this checkout all day and its chrome moves
# with it, which is what the first pass measured. It is **this commit's tree with card #MDKN
# switched off**: `Pane::proseUriFor()`'s three call sites handing the renderer an empty anchor,
# so `MarkdownAnsi` emits the bytes it emitted before the card and nothing else in the program
# differs at all. Pass `RELAY_QA_OFF` (the control) and `RELAY_QA_ON` (the same tree unpatched);
# with neither the phase only takes its own shots.
if want pixels; then
note "===== (i) ten shots each, with the card switched on and off in the same tree ====="
on=${RELAY_QA_ON:-$relay}
off=${RELAY_QA_OFF:-}
[[ -z $off ]] && note "  (no RELAY_QA_OFF: set it to a build of this tree with the feature off)"
run_shots() {   # binary prefix prompt settle-word
    relay=$1
    launch || return 1
    focus_pane "$2-focus"
    ask "$3"
    awaited "i0 $2 the turn ran" "$2-00" "$4" 240 || return 1
    sleep 6      # the turn settles — the usage line, the anchor rewrite — before the ten shots
    local n
    for n in 01 02 03 04 05 06 07 08 09; do sleep 0.6; shot "$2-$n"; done
}
# The **transcript** is what must not move. The whole window is measured too and reported, but
# not gated on: the tab title carries a live `cpu00%-mem00%` readout that differs between any two
# runs of anything, which is most of what a whole-window count sees. The band below the tab strip
# and above the status bar is the terminal, and there the only thing that may differ is the
# blinking caret — one cell.
band="${width}x$((height - 190))+0+100"
compare_shots() {   # old-prefix new-prefix label
    local same=0 diff=0 n px bpx
    for n in 01 02 03 04 05 06 07 08 09; do
        [[ -f $out/$1-$n.png && -f $out/$2-$n.png ]] || continue
        px=$(compare -metric AE "$out/$1-$n.png" "$out/$2-$n.png" null: 2>&1 | tr -d '\n')
        convert "$out/$1-$n.png" -crop "$band" +repage "$sandbox/cmp-a.png" 2>/dev/null
        convert "$out/$2-$n.png" -crop "$band" +repage "$sandbox/cmp-b.png" 2>/dev/null
        bpx=$(compare -metric AE "$sandbox/cmp-a.png" "$sandbox/cmp-b.png" null: 2>&1 | tr -d '\n')
        note "  $3 shot $n: $bpx pixels differ in the transcript ($px over the whole window)"
        if [[ $bpx =~ ^[0-9]+$ ]] && (( bpx <= 400 )); then same=$((same+1)); else diff=$((diff+1)); fi
    done
    if (( diff == 0 && same > 0 )); then
        ok "i1 $3: all $same shots have the same transcript with the card on and off"
    else
        bad "i1 $3: the transcript changed in $diff of $((same+diff)) shots"
    fi
}
keep=$relay
# (i1) a transcript with **no** links at all — nothing this card touches should reach it.
run_shots "$on" i-on-plain "count slowly to twenty" "twenty"
if [[ -n $off && -x $off ]]; then
    run_shots "$off" i-off-plain "count slowly to twenty" "twenty"
    compare_shots i-off-plain i-on-plain "a transcript with no links"
fi
# (i2) and the paragraph that **is** all links: an OSC 8 run takes no cells, so this must not
# move either — what changed is the pointer, the underline on hover and the click.
run_shots "$on" i-on-links "show me the links please" "SITEROW"
if [[ -n $off && -x $off ]]; then
    run_shots "$off" i-off-links "show me the links please" "SITEROW"
    compare_shots i-off-links i-on-links "a transcript full of links"
fi
relay=$keep
fi

note ""
note "$pass PASS · $fail FAIL"
cp "$sandbox/relay.log" "$out/relay.log" 2>/dev/null
cp "$RELAY_QA_RECTS" "$out/rects-last.json" 2>/dev/null
echo "$pass PASS · $fail FAIL — $out/notes.txt"
[[ $fail == 0 ]]
