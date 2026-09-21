#!/usr/bin/env bash
# A link in a console's transcript, and the card page's frame — card #AGNT, the three finishing
# items the integration drive left.
#
#   drive.sh [relay-binary] [out-dir] [phase ...]
#
# Both paths are **absolute**: the script runs from its own directory, so a relative out-dir
# lands under this folder rather than where it was typed. With no phase named it runs them all.
#
#   opt    (a) an `option:` link in the **Options** helper's own transcript reveals the row in
#              this same pane: the page goes General → Terminal and the row is on it
#   cross  (b) the same link clicked in a console that is **not** Options — the Switchboard's —
#              opens an Options pane at the row, through the window
#   sess   (c) a `session:` link in the Sessions helper's transcript selects the row here
#   board  (d) a `#ID` card link in the Switchboard console's transcript opens the card page
#   frame  (e) the card page: one frame around the reply box, and the action row's first button
#              is the plain card-shaped one rather than a bright outline
#
# **What the gates read.** "The row was revealed" was read twice off the words in the console's
# own answer — which say "Copy on select" whether the pane moved or not — and both readings were
# wrong (docs/qa_evidence/2026-09-21-console-write-undo/NOTES.md). So the page is read from
# `RELAY_QA_RECTS`: every `settingsRowLabel` on screen is a row of the page that is *showing*,
# and its `text` is the row's title. A page is proved by the rows drawn on it and by the rows of
# the other page being gone, never by a word that also occurs in a transcript. A frame and a
# button's outline are not words at all, so (e) reads them off the pixels (`reply-frames.py`).
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
phases=${*:-opt cross sess board frame}
width=1500 height=1100
port=${RELAY_QA_PORT:-8894}
mkdir -p "$out"
[[ -x $relay ]] || { echo "no relay binary at $relay"; exit 1; }

display=
for n in $(seq 150 199); do [[ -e /tmp/.X11-unix/X$n ]] || { display=:$n; break; }; done
[[ -z $display ]] && { echo "no free display"; exit 1; }

sandbox=$(mktemp -d /tmp/rl-lnk.XXXX)
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
    [[ -n $relay_pid ]] && { kill "$relay_pid" 2>/dev/null; wait "$relay_pid" 2>/dev/null; sleep 3; }
    (cd "$work" && exec "$relay" --workspace "$work" --fresh) >>"$sandbox/relay.log" 2>&1 &
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

# The link **in the transcript**, clicked where it actually is. A markdown `[LABEL](option:…)`
# prints as `LABEL (option:…)`: the label is painted in the link colour and the engine's link
# scanner underlines the *target* beside it, which is the clickable run
# (`relay::links::candidates`, `option:` stage). The earlier drive clicked the label, and the
# label opens nothing — which is the whole of "the link did not reveal the row".
click_link() {   # shot-name token
    local at; at=$(word_xy "$1" "$2")
    [[ -z $at ]] && { note "  (no \"$2\" token in $1.png to click)"; return 1; }
    click_at ${at% *} ${at#* }
}

want() { [[ " $phases " == *" $1 "* ]]; }

# ================================================= (a) an option: link in the Options helper
if want opt; then
note "===== (a) an option: link in the Options helper's own transcript ====="
launch || exit 1
k ctrl+shift+o; sleep 5
k alt+q; sleep 3
focus_console a01-console
has "a0 the Options helper is a console" a01-console "Ask the Options helper"
note "the page before the link: $(page_rows | tr '\n' '|')"
page_has "a0 Options opens on General" "Thinking display"
page_hasnt "a0 and the Terminal page is not the one up" "Copy on select"
ask "where is copy on select?"
awaited "a1 the answer carries an option: link" a02-link "Clicking" 240
sleep 3; shot a02-link
if click_link a02-link "option:"; then
    sleep 3; shot a03-revealed
    note "the page after the link: $(page_rows | tr '\n' '|')"
    page_has "a1 the link revealed the row on the page it names" "Copy on select"
    page_hasnt "a1 and the page it was on is gone" "Thinking display"
else
    bad "a1 no option: target to click in a02-link.png"; shot a03-revealed
fi
fi

# ======================================= (b) the same link in a console that is not Options
if want cross; then
note "===== (b) an option: link clicked in the Switchboard console ====="
launch || exit 1
k ctrl+shift+s; sleep 6
focus_console b01-console
has "b0 the Switchboard's own console is the prompt box" b01-console "Ask the Switchboard agent"
ask "where is copy on select?"
awaited "b1 the Switchboard console answers with the same link" b02-link "Clicking" 240
sleep 3; shot b02-link
if click_link b02-link "option:"; then
    sleep 4; shot b03-options
    note "the page the window opened: $(page_rows | tr '\n' '|')"
    page_has "b1 the window opened Options at the row the link names" "Copy on select"
    page_hasnt "b1 on the Terminal page, not the one Options opens on" "Thinking display"
else
    bad "b1 no option: target to click in b02-link.png"; shot b03-options
fi
fi

# ============================================ (c) a session: link in the Sessions helper
if want sess; then
note "===== (c) a session: link in the Sessions helper's transcript ====="
launch || exit 1
k ctrl+shift+y; sleep 5
k alt+q; sleep 3
focus_console c01-console
has "c0 the Sessions helper is a console" c01-console "the Sessions helper"
ask "which session is about the pane header?"
awaited "c1 the answer carries a session: link" c02-link "Clicking" 240
sleep 3; shot c02-link
if click_link c02-link "session:"; then
    sleep 3; shot c03-revealed
    # The list's detail beside it names the conversation the row is. "pane header" on its own
    # is no good — the prompt is echoed in the transcript above and says it too — so the gate is
    # the detail's own wording, and the conversation that *was* selected being gone.
    has "c1 the link selected that conversation in this pane" c03-revealed "pane header and"
    hasnt "c1 and the one that was selected before it is not" c03-revealed "equalize key"
else
    bad "c1 no session: target to click in c02-link.png"; shot c03-revealed
fi
fi

# ================================================ (d) a #ID card link in the board console
if want board; then
note "===== (d) a #ID card link in the Switchboard console ====="
launch || exit 1
k ctrl+shift+s; sleep 6
focus_console d01-console
has "d0 the Switchboard's own console is the prompt box" d01-console "Ask the Switchboard agent"
ask "which card is the console one?"
awaited "d1 the answer names the card" d02-link "Clicking" 240
sleep 3; shot d02-link
# By the `#`, not by the id. A four-character id is exactly what OCR gets wrong — this run read
# `#YFPM` as `#VFPM` — while the glyph in front of it is unambiguous, and the transcript's card
# reference is the only `#` token in the shot (the board's own rows print the id without one).
# `word_xy` takes the last match, and the transcript is at the foot of the pane.
if click_link d02-link "^#"; then
    sleep 3; shot d03-card
    has "d1 the card link opened the card page on this board" d03-card "Back to board"
    has "d1 …on the card it named" d03-card "The console is the prompt box"
else
    bad "d1 no #$card token to click in d02-link.png"; shot d03-card
fi
fi

# ============================================ (e) the card page's frame and its action row
if want frame; then
note "===== (e) the card page: one frame, and a plain first action ====="
launch || exit 1
k ctrl+shift+s; sleep 6
shot e00-folded
click_word e00-folded "^inbox$"
sleep 2; shot e01-list
click_word e01-list "console"
sleep 5; shot e02-card
has "e0 the card page carries the console's reply box" e02-card "Enter discusses"

# **One frame**, and **a plain first action** — both read off the pixels (`reply-frames.py`):
# a frame is a one-pixel column lighter than the pixel either side of it, a frame's column runs
# the height of the reply block and a button's runs only the height of the button. Nothing here
# goes through RELAY_QA_RECTS: the rectangles are written on a debounce and the card page can
# settle without another layout event, so a dump taken after the page opened can still be the
# list page's — which is what the first pass of this drive read.
#
# The band is the reply block of a 1500x1100 window: below the thread view's own frame and above
# the board's key legend.
band="755 1495 905 1055"
read_frames=$(python3 "$PWD/reply-frames.py" "$out/e02-card.png" $band 2>/dev/null)
note "$read_frames"
frames=$(sed -n 's/^FRAMES \([0-9]*\).*/\1/p' <<<"$read_frames")
if [[ ${frames:-0} == 2 ]]; then
    ok "e1 the card page draws one frame around the reply box (two rules: its left and its right)"
else
    bad "e1 ${frames:-?} rules cross the reply box: a border inside a border is back (4 was the report)"
fi
# The first action's left edge. `@border` is #2a2e37 in relay-dark and `@text` is #e6e8ec — the
# ring that was there — so luminance tells them apart without pinning a hex.
first=$(sed -n 's/^BUTTONS row=[0-9]* \([0-9]*\):\([0-9a-f]*\).*/\2/p' <<<"$read_frames")
if [[ -n $first ]]; then
    note "  Plan (p) border #$first luma $(luma "$first")"
    if (( $(luma "$first") <= 110 )); then
        ok "e2 Plan (p) is the plain card-shaped button (border luma $(luma "$first"))"
    else
        bad "e2 Plan (p) still wears a bright outline (#$first, luma $(luma "$first"))"
    fi
else
    bad "e2 no action row found in the reply block of e02-card.png"
fi
# …and Execute keeps the outline that says it hands the card to a pane: among the row's rules
# there is still one that is bright. Which colour it is is `[leaves="true"]`'s business and the
# `board` suite's; what must not happen is the whole row going quiet with it.
bright=0
for cell in $(sed -n 's/^BUTTONS row=[0-9]* [0-9]*:[0-9a-f]* //p' <<<"$read_frames"); do
    (( $(luma "${cell#*:}") >= 120 )) && bright=$((bright + 1))
done
if (( bright > 0 )); then
    ok "e2 and $bright rule(s) past Plan's are still bright: Execute (x) keeps its outline"
else
    bad "e2 no outlined action left on the row — Execute lost its accent with Plan's ring"
fi
fi

note ""
note "$pass PASS · $fail FAIL"
cp "$sandbox/relay.log" "$out/relay.log" 2>/dev/null
cp "$RELAY_QA_RECTS" "$out/rects-last.json" 2>/dev/null
echo "$pass PASS · $fail FAIL — $out/notes.txt"
[[ $fail == 0 ]]
