#!/usr/bin/env bash
# #AQ6X phase 2, live: the signals the machine has open are two folded rows on the Switchboard, and
# one of them opens a page with four actions.
#
# Nothing is stubbed. The fixture writes the two files the **real** fold reads — the run history
# (`.private/tests/history.jsonl`, #7BM4) and the signals event log
# (`.private/signals/events.jsonl`) — through `relay_core.test_history` and `relay_core.signals`
# themselves, so what the pane draws is what the worker folded:
#
#   * `ctest:panelayout` and `ctest:themes` each failed in two consecutive runs -> two `broken`
#     signals, `open` (decision 3: open on the second consecutive failing execution).
#   * `ctest:voice` failed the same way and was then dismissed by the owner, `environmental`, for
#     seven days -> it is behind the second toggle, not in the count.
#   * `ctest:queuenav` passed in both runs -> no signal at all, which is the point of the design.
#
# Then, under Xvfb with an isolated HOME, XDG_CONFIG_HOME, XDG_RUNTIME_DIR and TMPDIR (short paths:
# the runtime dir holds sockets and the limit is 108 bytes) and RELAY_KEYRING=off:
#
#   1. The board opens and asks `signals_list`; the row reads "▸ 2 signals", folded, above the
#      first section header, with "▸ 1 dismissed" under it.
#   2. A click shows the two signal rows — kind, key, ×2, when they were last seen.
#   3. A click on the dismissed toggle shows `ctest:voice` with its expiry.
#   4. Enter on a signal row opens its page: the key, its fields, the excerpt, and Claim / Release /
#      Dismiss / Promote.
#   5. Dismiss opens the form in the page — reason, comment, and a date seven days out.
#   6. Claim sends `signals_claim`; the worker writes it and the row comes back wearing the chip.
#
#   docs/qa_evidence/2026-09-20-signals-gui/drive.sh [relay-binary] [out-dir]
set -uo pipefail
root=/home/elliott/repos/relay-terminal
relay=${1:-$root/build/relay}
out=${2:-$root/docs/qa_evidence/2026-09-20-signals-gui}
width=1500 height=1150
mkdir -p "$out"

display=
for n in $(seq 150 189); do [[ -e /tmp/.X11-unix/X$n ]] || { display=:$n; break; }; done
[[ -z $display ]] && { echo "no free display"; exit 1; }

sandbox=$(mktemp -d /tmp/rl-aq6x.XXXX)
xvfb_pid= relay_pid=
cleanup() {
    local pid
    for pid in $relay_pid $xvfb_pid; do kill "$pid" 2>/dev/null; done
    rm -rf "$sandbox"
}
trap cleanup EXIT

Xvfb "$display" -screen 0 $((width + 40))x$((height + 40))x24 >/dev/null 2>&1 &
xvfb_pid=$!
sleep 2
kill -0 "$xvfb_pid" 2>/dev/null || { echo "Xvfb died on $display"; exit 1; }
export DISPLAY=$display RELAY_KEYRING=off

mkdir -p "$sandbox/home/.config/RelayTerminal" "$sandbox/home/.local/share" "$sandbox/run" "$sandbox/tmp"
chmod 700 "$sandbox/run"
ln -sfn "/run/user/$(id -u)/bus" "$sandbox/run/bus"
export HOME=$sandbox/home XDG_RUNTIME_DIR=$sandbox/run TMPDIR=$sandbox/tmp
export XDG_CONFIG_HOME=$HOME/.config XDG_DATA_HOME=$HOME/.local/share XDG_CACHE_HOME=$HOME/.cache
cat >"$XDG_CONFIG_HOME/RelayTerminal/relay.conf" <<'CONF'
[instructions]
onboarded=true
CONF

work=$HOME/project
mkdir -p "$work"
PYTHONPATH="$root/backend" python3 - "$work" <<'FIX' | tee "$out/fixture.txt"
import json, sys
from datetime import datetime, timedelta, timezone
from pathlib import Path
from relay_core import board as B, test_history as H, signals as S

work = Path(sys.argv[1])
root = work / "issues"
root.mkdir(parents=True)
(root / B.BOARD_CONFIG).write_text(
    "tabs: [{id: features, folder: features}, {id: bugs, folder: changes}]\n"
    "columns: [inbox, discussing, ready, executing, needs-verification, needs-qa, done]\n",
    encoding="utf-8")
board = B.Board(root, work)
for title, status, rank in [("Alpha voice mode", "ready", "i1"),
                            ("Bravo release notes", "inbox", "i2"),
                            ("Charlie cache header", "executing", "i3")]:
    card = B.new_card("work", title, status, rank=rank, request=f"{title}, for the signals evidence")
    B.write_new_card(board, card, "features")
    print("card", card.id, status)

# A promoted signal's card: the machine owns the `## Signal` section and rewrites it on every
# state change, and the card page shows it as a strip.
promoted = B.new_card("work", "Delta promoted from a failing test", "inbox", rank="h9",
                      request="ctest:panelayout has failed in two consecutive runs.")
B.write_new_card(board, promoted, "changes")
path = Path(board.card_path(promoted)) if hasattr(board, "card_path") else None
if path is None or not path.exists():
    path = next(p for p in (root / "changes").rglob("*.md") if promoted.id in p.read_text("utf-8"))
with path.open("a", encoding="utf-8") as fh:
    fh.write("\n## Signal\n\n"
             "- key: `ctest:panelayout`\n"
             "- kind: broken · state: open\n"
             "- failures: 2, first seen 40 min ago\n"
             "- machine-owned: this section is rewritten on every state change\n")
print("promoted card", promoted.id, path.relative_to(work))

now = datetime.now(timezone.utc)
def ex(key, result, run, minutes, message=""):
    return H.Execution(ts=(now - timedelta(minutes=minutes)).isoformat().replace("+00:00", "Z"),
                       id=key, result=result, runner="ctest", run_id=run, duration=0.4,
                       message=message.splitlines()[0] if message else "", excerpt=message)

FAILS = {
    "ctest:panelayout": "FAILED: panelayout\nAssertion `rows.size() == 4` failed\n"
                        "  actual 3, at tests/panelayout_test.cpp:214",
    "ctest:themes": "FAILED: themes\nAssertion `ink == gruvbox.fg` failed\n"
                    "  at tests/themes_test.cpp:88",
    "ctest:voice": "FAILED: voice\nALSA: no sound card on this host",
}
rows = []
for minutes, run in ((40, "run-a1b2"), (18, "run-c3d4")):
    for key, message in FAILS.items():
        rows.append(ex(key, "fail", run, minutes, message))
    rows.append(ex("ctest:queuenav", "pass", run, minutes))
history = H.default_path(work, root)
history.parent.mkdir(parents=True, exist_ok=True)
print("executions", H.append(rows, history), "->", history.relative_to(work))

events = S.default_path(work, root)
events.parent.mkdir(parents=True, exist_ok=True)
S.append_event({"action": "dismiss", "key": "ctest:voice", "by": "owner",
                "reason": "environmental", "comment": "this host has no sound card",
                "until": (now + timedelta(days=7)).date().isoformat()}, events)

folded = S.state(work, root)
summary = S.summary(folded.values())
print("open", [s["key"] for s in summary["open"]])
print("dismissed", [s["key"] for s in summary["dismissed"]])
print("pending_count", summary["pending_count"])
for signal in summary["open"]:
    print("  ", signal["key"], signal["kind"], signal["state"], "count", signal["count"])
FIX

: >"$out/ocr.txt"
echo "relay binary: $relay ($(stat -c %y "$relay"))" >>"$out/ocr.txt"

(cd "$work" && exec "$relay" --workspace "$work" --fresh) >"$sandbox/relay.log" 2>&1 &
relay_pid=$!
sleep 9
win= ; best=0
for candidate in $(xdotool search --pid "$relay_pid" 2>/dev/null); do
    eval "$(xdotool getwindowgeometry --shell "$candidate" 2>/dev/null)"
    (( WIDTH * HEIGHT > best )) && { best=$(( WIDTH * HEIGHT )); win=$candidate; }
done
[[ -z $win ]] && { echo "no Relay window"; cat "$sandbox/relay.log"; exit 1; }
xdotool windowmove "$win" 0 0 windowsize "$win" "$width" "$height"
xdotool windowfocus "$win"; sleep 6

shot() { import -window root "$out/$1.png"; }
text_of() { tesseract "$1" stdout --psm 11 2>/dev/null; }
# The rows are small type, and tesseract runs the count and the word together ("2signals"), so a
# row is found by a token that *contains* the word rather than by an exact match.
token_center_first() {
    tesseract "$1" stdout --psm 11 tsv 2>/dev/null |
        awk -v want="$2" 'tolower($12) ~ tolower(want) {print int($7+$9/2), int($8+$10/2); exit}'
}
click_token_first() {
    local box; box=$(token_center_first "$1" "$2")
    [[ -z ${box// /} ]] && { echo "MISSING TOKEN: $2" >>"$out/ocr.txt"; return 1; }
    xdotool mousemove $(( ${box% *} + ${3:-0} )) $(( ${box#* } + ${4:-0} )) click 1
    sleep 2
}
# The section names appear twice: once as a checkbox at the top of the list page and once as the
# section header. The header is the last of the two, and it is the one that unfolds.
token_center_last() {
    tesseract "$1" stdout --psm 11 tsv 2>/dev/null |
        awk -v want="$2" 'tolower($12) ~ tolower(want) {last=int($7+$9/2) " " int($8+$10/2)} END {print last}'
}
click_token_last() {
    local box; box=$(token_center_last "$1" "$2")
    [[ -z ${box// /} ]] && { echo "MISSING TOKEN (last): $2" >>"$out/ocr.txt"; return 1; }
    xdotool mousemove $(( ${box% *} + ${3:-0} )) $(( ${box#* } + ${4:-0} )) click 1
    sleep 2
}
says() {  # image needle label
    if grep -qi -- "$2" <<<"$(text_of "$1")"; then echo "$3: yes" >>"$out/ocr.txt"
    else echo "$3: NO" >>"$out/ocr.txt"; fi
}
says_not() {
    if grep -qi -- "$2" <<<"$(text_of "$1")"; then echo "$3: NO" >>"$out/ocr.txt"
    else echo "$3: yes" >>"$out/ocr.txt"; fi
}

# The first run opens an Approvals pane; close it so the board has the window.
shot 00-first-run
click_token_first "$out/00-first-run.png" "approvals" 0 0 || true; sleep 1
xdotool key --window "$win" ctrl+w; sleep 2

# 1. The Switchboard: the block above the first section header, both rows folded.
xdotool key --window "$win" ctrl+shift+s; sleep 9
shot 01-board-folded
# tesseract runs the count and the word together at this size ("2signals"), so the row is
# matched by a token that contains the word.
says "$out/01-board-folded.png" "2 *signals" "01 the fold row says '2 signals'"
says "$out/01-board-folded.png" "1 dismissed" "01 and the dismissed toggle says '1 dismissed'"
says_not "$out/01-board-folded.png" "panelayout" "01 the signals themselves are away"
says_not "$out/01-board-folded.png" "queuenav"   "01 a test that passes has no signal at all"

# 2. A click on the row shows the two signals.
click_token_first "$out/01-board-folded.png" "signals" 0 0 || true
shot 02-signals-open
says "$out/02-signals-open.png" "panelayout" "02 a click shows ctest:panelayout"
says "$out/02-signals-open.png" "themes"     "02 and ctest:themes"
says "$out/02-signals-open.png" "broken"     "02 with its kind as a word"
says_not "$out/02-signals-open.png" "voice"  "02 the dismissed one is still behind its toggle"

# 3. The dismissed toggle.
click_token_first "$out/02-signals-open.png" "dismissed" 0 0 || true
shot 03-dismissed-open
says "$out/03-dismissed-open.png" "voice"   "03 the dismissed toggle shows ctest:voice"
says "$out/03-dismissed-open.png" "expires" "03 with when the dismissal expires"

# 4. The page: a click on a signal row opens it.
click_token_first "$out/03-dismissed-open.png" "panelayout" 0 0 || true; sleep 1
shot 04-signal-page
says "$out/04-signal-page.png" "panelayout" "04 the page names the signal"
says "$out/04-signal-page.png" "Claim"      "04 Claim"
says "$out/04-signal-page.png" "Release"    "04 Release"
says "$out/04-signal-page.png" "Dismiss"    "04 Dismiss"
says "$out/04-signal-page.png" "Promote"    "04 Promote"
says "$out/04-signal-page.png" "Assertion"  "04 and the excerpt is the failure's own words"

# 5. Dismiss opens the form in the page, never over it.
click_token_first "$out/04-signal-page.png" "Dismiss" 0 0 || true
shot 05-dismiss-form
says "$out/05-dismiss-form.png" "expires"       "05 the form says every dismissal expires"
says "$out/05-dismiss-form.png" "Environmental" "05 with the reason it defaults to"
# The comment field is empty with a placeholder, which OCR does not read at this size; the shot
# holds it, and Cancel is the form's other control.
says "$out/05-dismiss-form.png" "Cancel"        "05 and the form can be cancelled"

# 6. Claim: the worker writes it and the row comes back wearing the chip.
xdotool key --window "$win" Escape; sleep 1          # the form, not the page
shot 06-form-closed
click_token_first "$out/06-form-closed.png" "Claim" 0 0 || true; sleep 3
shot 07-claimed
says "$out/07-claimed.png" "Claimed" "07 the notice says the claim was written"
xdotool key --window "$win" Escape; sleep 2
shot 08-row-claimed
says "$out/08-row-claimed.png" "panelayout" "08 back on the list, the row is still there"

# 9. A promoted signal's card wears the `## Signal` section as a strip on its page.
click_token_last "$out/08-row-claimed.png" "INBOX" 40 0 || true; sleep 2
shot 09-inbox-open
click_token_first "$out/09-inbox-open.png" "Delta" 0 0 || true; sleep 3
shot 10-card-signal-strip
says "$out/10-card-signal-strip.png" "machine" "10 the strip says whose words they are"
says "$out/10-card-signal-strip.png" "ctest:panelayout" "10 and names the signal's key"
says "$out/10-card-signal-strip.png" "rewritten" "10 and that they are rewritten on every change"

if kill -0 "$relay_pid" 2>/dev/null; then echo "relay still alive at the end: yes" >>"$out/ocr.txt"
else echo "relay still alive at the end: NO" >>"$out/ocr.txt"; fi
[[ -s "$sandbox/relay.log" ]] && cp "$sandbox/relay.log" "$out/relay.log"
grep -i "gui_crash" "$sandbox/relay.log" >>"$out/ocr.txt" 2>/dev/null ||
    echo "no gui_crash in relay.log" >>"$out/ocr.txt"
# What the worker wrote: the claim is an event in the signals log, and the fold reads it back.
echo "--- signals events after the run" >>"$out/ocr.txt"
cat "$work/issues/.private/signals/events.jsonl" >>"$out/ocr.txt" 2>/dev/null
echo "shots, fixture.txt and ocr.txt in $out"
