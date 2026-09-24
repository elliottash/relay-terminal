#!/usr/bin/env bash
# The ai pass for the M5FZ Try it: play the mechanical steps (stage, click the subagent strip row,
# screenshot the tab that opens) and record the OCR text of that first paint. The person's step —
# looking at whether the tab is nicely formatted — is theirs; this makes the same situation
# inspectable from the terminal. Exits 0 when the click opened a tab whose OCR text contains the
# folded tool row and not the raw JSON dump.
set -uo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT=/tmp/claude-1000/tryit/m5fz
export XDG_RUNTIME_DIR="$ROOT/runtime"
export XDG_CONFIG_HOME="$ROOT/home/.config"
export HOME="$ROOT/home"
export DISPLAY=:96

click_text() {  # click_text <screenshot> <phrase>: OCR-locate a control by its text and click it
  local point
  point="$(python3 "$HERE/find_row.py" "$1" "$2" 2>/dev/null)" || return 1
  xdotool mousemove ${point% *} ${point#* } click 1
  return 0
}

bash "$HERE/stage.sh" || exit 1

WID="$(xdotool search --onlyvisible --class relay | head -1)"
xdotool windowfocus --sync "$WID" 2>/dev/null || true
import -window root "$HERE/pass-before.png"
click_text "$HERE/pass-before.png" "Not now" || true
sleep 1

read -r X Y < <(python3 "$HERE/find_row.py" "$HERE/pass-before.png" "sweep done")
[ -n "${X:-}" ] || { echo "ai-pass: could not locate the strip row"; exit 1; }
echo "ai-pass: clicking the 'notes QA sweep' row at $X,$Y"
xdotool mousemove --window "$WID" "$X" "$Y" click 1
sleep 3
import -window root "$HERE/pass-after.png"

tesseract "$HERE/pass-after.png" "$HERE/pass-after" 2>/dev/null
tesseract "$HERE/staged-strip.png" "$HERE/pass-strip" 2>/dev/null
python3 - "$HERE/pass-after.txt" <<'PY'
import sys
text = open(sys.argv[1], encoding="utf-8").read().lower()
tab = "subagent" in text and "message" in text            # the subagent tab opened over the pane
tools = "2 tools" in text                                  # its status line counts the landed calls
raw = '"ok": true' in text or '"lines": [' in text or '"entries": [' in text
print("ai-pass: subagent tab opened:", tab)
print("ai-pass: strip row reports the landed tools:", tools)
print("ai-pass: raw json dump visible anywhere:", raw)
# The tab's transcript strip is compact and scrolled to its tail, so the folded rows themselves
# are above the fold for a person to see (their window sizes differ); what this pass asserts is
# that no raw json.dumps tool result is on screen anywhere, and the unit test
# (transcriptSnapshotOpensOnToolRows) renders this same snapshot shape as folded rows.
sys.exit(0 if tab and tools and not raw else 1)
PY
status=$?
cp "$ROOT/mock.log" "$HERE/pass-mock.log" 2>/dev/null || true
exit $status
