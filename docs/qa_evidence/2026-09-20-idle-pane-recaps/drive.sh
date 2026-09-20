#!/usr/bin/env bash
# Live check of #D54R (the idle-pane away recap), under Xvfb + xdotool, OCR'd with tesseract.
# Sandbox and stub pattern reused from #MVGR (docs/qa_evidence/2026-09-20-recap-finished-at/).
#
#   01  the key-dialog walk configures the pane against the loopback stub (no network, no key)
#   02  three turns run and finish while the pane is WATCHED; past the (2 s) threshold no recap
#       is written — nothing over the shoulder of someone reading the pane
#   03  Ctrl+T: the pane's tab stops being current (window stays active the whole time) -> the
#       recap is written into the hidden pane without any refocusing; switching back shows
#       [end of message] / finished at HH:MM / Recap · ...
#   04  back on the watched pane, past the threshold again: no second recap without new work
#   05  a fourth turn finishes, Ctrl+E splits and the click moves the focus to the new pane
#       (relayActive flips, nothing hides): the left pane gets its second recap while both panes
#       stay on screen
#
#   docs/qa_evidence/2026-09-20-idle-pane-recaps/drive.sh <build-dir> [tag]
#
# Writes implementer-NN-*.png and run-<tag>.log next to this script. Needs Xvfb, xdotool,
# ImageMagick `import`/`convert` and tesseract. Every XDG/HOME/TMPDIR path is isolated, so a
# Relay already running on this machine is untouched.
set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"
out=$PWD
build=$1
tag=${2:-run}
port=8768
log=run-$tag.log
: >"$log"
fails=0
say() { echo "$*" | tee -a "$log"; }
ocr() { tesseract "$1" stdout 2>/dev/null; }
ocr_count() { ocr "$1" | grep -cF "$2" || true; }
ocr_has() { ocr "$1" | grep -qF "$2"; }
need() { if ocr_has "$1" "$2"; then say "PASS: $3"; else say "FAIL: $3 (wanted \"$2\" in $1)"; fails=$((fails+1)); fi; }
need_not() { if ocr_has "$1" "$2"; then say "FAIL: $3 (did not want \"$2\" in $1)"; fails=$((fails+1)); else say "PASS: $3"; fi; }
need_count() { local got; got=$(ocr_count "$1" "$2"); if [[ "$got" == "$3" ]]; then say "PASS: $4"; else say "FAIL: $4 (wanted $3x \"$2\", OCR found ${got}x in $1)"; fails=$((fails+1)); fi; }

display=
for n in $(seq 171 199); do
    [[ -e /tmp/.X11-unix/X$n ]] && continue
    display=:$n
    break
done
[[ -z $display ]] && { say "no free X display in 171..199"; exit 1; }
say "display $display"

sandbox=$(mktemp -d)
export HOME=$sandbox/home
export XDG_CONFIG_HOME=$sandbox/config XDG_DATA_HOME=$sandbox/data XDG_CACHE_HOME=$sandbox/cache
export XDG_RUNTIME_DIR=$sandbox/run TMPDIR=$sandbox/tmp
mkdir -p "$HOME" "$XDG_CONFIG_HOME/RelayTerminal" "$XDG_DATA_HOME" "$XDG_CACHE_HOME" "$XDG_RUNTIME_DIR" "$TMPDIR"
chmod 700 "$XDG_RUNTIME_DIR"
# Relay Free must stay unusable here (see the #WXT6 script): the pane must bind to the stub.
mkdir -p "$sandbox/fakepy/cryptography"
echo 'raise ImportError("qa sandbox: Relay Free off for this run")' >"$sandbox/fakepy/cryptography/__init__.py"
export PYTHONPATH=$sandbox/fakepy
export RELAY_KEYRING=off RELAY_NO_URL_HANDLER=1
work=$sandbox/work
mkdir -p "$work"
cat >"$XDG_CONFIG_HOME/RelayTerminal/relay.conf" <<CONF
[instructions]
onboarded=true
[terminal]
shell_integration=true
[agent]
show_tool_output=false
[provider]
preset=custom
base=http://127.0.0.1:$port/v1
model=relay-qa-stub
extra={}
max_tokens=1024
CONF

trap 'kill "${relay_pid:-0}" "${stub_pid:-0}" "${xvfb_pid:-0}" 2>/dev/null; sleep 1; rm -rf "$sandbox"' EXIT

python3 "$out/recap-stub-provider.py" "$port" &
stub_pid=$!
sleep 1

Xvfb "$display" -screen 0 1500x900x24 >/dev/null 2>&1 &
xvfb_pid=$!
sleep 2
kill -0 "$xvfb_pid" 2>/dev/null || { say "Xvfb died on $display (taken?)"; exit 1; }
export DISPLAY=$display
xdotool search --name . 2>/dev/null | grep -q . && { say "$display already has windows"; exit 1; }

shot() {
    import -window root "$out/implementer-$1.png"
    ocr "$out/implementer-$1.png" >>"$log"
    say "--- shot $1 above ---"
    kill -0 "${relay_pid:-0}" 2>/dev/null || say "relay is gone before $1"
}
t() { xdotool type --delay 12 "$1"; }
k() { xdotool key --delay 45 "$@"; }

# The away-recap threshold (180 s in the code) is an env var; 2 s is enough to test the path.
export RELAY_RECAP_AWAY_SECONDS=2
"$build/relay" --engine-core libvterm --workspace "$work" >"$out/relay-$tag-stderr.log" 2>&1 &
relay_pid=$!
sleep 7
win=$(xdotool search --pid "$relay_pid" --name Relay | tail -1)
[[ -z $win ]] && { say "no Relay window"; exit 1; }
xdotool windowmove "$win" 0 0 windowsize "$win" 1500 900
xdotool windowactivate --sync "$win"
xdotool mousemove 750 450
sleep 2

# ---- configure against the loopback stub (the dialog walk is the #WXT6 one) ------------------
xdotool windowactivate --sync "$win"
t 'run the qa turns'; sleep 0.5
k ctrl+Return; sleep 5
dlg=$(xdotool search --name 'Bring your own key' | tail -1)
[[ -z $dlg ]] && { say "the key dialog did not open"; exit 1; }
eval "$(xdotool getwindowgeometry --shell "$dlg")"
import -window "$dlg" "$out/implementer-00-dialog.png"
savexy=$(tesseract "$out/implementer-00-dialog.png" stdout tsv 2>/dev/null | awk -F'\t' '$12 == "Cancel" && $11 > 40 { x = $7 + $9 / 2; y = $8 + $10 / 2 } END { if (x) printf "%d %d", x - 60, y }')
[[ -n $savexy ]] && savexy="$((X + ${savexy%% *})) $((Y + ${savexy##* }))"
[[ -z $savexy ]] && savexy="$((X + WIDTH - 153)) $((Y + HEIGHT - 19))"
say "Save at $savexy (dialog ${WIDTH}x${HEIGHT} at $X,$Y)"
xdotool windowactivate --sync "$dlg" 2>/dev/null; sleep 0.5
xdotool mousemove --sync $savexy; sleep 0.3; xdotool click --delay 120 1; sleep 8
# The approvals chooser that follows Save holds the first prompt until it is answered, so click
# "Allow everything — recommended" and count the turns from the loop below, not that prompt.
import -window root "$out/implementer-00a-approvals.png"
allowxy=$(tesseract "$out/implementer-00a-approvals.png" stdout tsv 2>/dev/null | awk -F'\t' '$12 == "Allow" { x = $7 + $9 / 2; y = $8 + $10 / 2 } END { if (x) printf "%d %d", x, y }')
[[ -z $allowxy ]] && allowxy="400 620"
xdotool mousemove --sync $allowxy; sleep 0.3; xdotool click --delay 120 1; sleep 8
shot 01-agent-ready

# ---- three turns (a fourth may have been held by the chooser; three is what the recap needs) --
for n in 2 3 4; do
    t "turn $n"; sleep 0.4
    k ctrl+Return; sleep 4
done
shot 02-three-turns
need_count "$out/implementer-02-three-turns.png" "Turn done." 3 "02 three stub turns completed"

# ---- 03 idle and WATCHED: past the threshold, no recap ---------------------------------------
sleep 6
import -window root "$out/implementer-03-watched-idle.png"
ocr "$out/implementer-03-watched-idle.png" >>"$log"
say "--- shot 03 above ---"
need_not "$out/implementer-03-watched-idle.png" "Recap" "03 no recap while the pane is watched"

# ---- 04 Ctrl+T: recap written into the hidden idle pane, no refocusing ----------------------
k ctrl+t; sleep 9
k ctrl+shift+Tab; sleep 3
shot 04-tab-away-recap
need     "$out/implementer-04-tab-away-recap.png" "end of message" "04 the idle recap opens with the marker"
need     "$out/implementer-04-tab-away-recap.png" "finished at"    "04 the idle recap states the finish time"
need     "$out/implementer-04-tab-away-recap.png" "Recap"          "04 the idle recap header is in the pane"

# ---- 05 watched again, no new work: no second recap ------------------------------------------
sleep 6
import -window root "$out/implementer-05-no-duplicate.png"
ocr "$out/implementer-05-no-duplicate.png" >>"$log"
say "--- shot 05 above ---"
need_count "$out/implementer-05-no-duplicate.png" "Recap" 1 "05 still exactly one recap without new work"

# ---- 06 split: focus moves to the right pane, the left gets its second recap -----------------
# The fourth turn runs in the LEFT pane (the one with the history), so its composer is clicked
# first; only then does the right pane take the focus, leaving the left one unwatched on screen.
k ctrl+e; sleep 1
xdotool mousemove 300 800; sleep 0.3; xdotool click --delay 120 1; sleep 1
t 'turn four'; sleep 0.4
k ctrl+Return; sleep 4
xdotool mousemove 1100 800; sleep 0.3; xdotool click --delay 120 1; sleep 9
import -window root "$out/implementer-06-split-full.png"
convert "$out/implementer-06-split-full.png" -crop 700x900+0+0 +repage "$out/implementer-06-split-left.png"
ocr "$out/implementer-06-split-left.png" >>"$log"
say "--- shot 06 (left pane crop) above ---"
need_count "$out/implementer-06-split-left.png" "Recap" 2 "06 the unwatched split pane got its second recap"

say "fails=$fails"
[[ $fails -eq 0 ]] || exit 1
say "PASS"
