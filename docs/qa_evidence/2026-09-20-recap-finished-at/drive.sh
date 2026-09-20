#!/usr/bin/env bash
# Live check of #MVGR (a recap printed after the agent finished opens with
# "[end of message]" / "finished at HH:MM"), under Xvfb + xdotool, OCR'd with tesseract.
# Pattern and sandbox reused from #TK9C / #WXT6 (docs/qa_evidence/2026-09-19-edit-diffs-fold-collapsed/).
#
#   01  the key-dialog walk configures the pane against the loopback stub (no network, no key)
#   02  three turns run, a fourth is submitted and the window deactivated: it finishes away
#   03  refocus after RELAY_RECAP_AWAY_SECONDS=2 -> the away recap block reads
#       [end of message] / finished at HH:MM / Recap · ... under one blank line
#   04  /recap by hand -> the same preamble (every recap reason shares the printing path)
#   05  a "slow" turn is submitted and /recap asked while it still runs -> the block shows
#       [end of message] with NO "finished at" line (a running turn has no honest finish time)
#
#   docs/qa_evidence/2026-09-20-recap-finished-at/drive.sh <build-dir> [tag]
#
# Writes implementer-NN-*.png and run-<tag>.log next to this script. Needs Xvfb, xdotool,
# ImageMagick `import`/`convert` and tesseract. Every XDG/HOME/TMPDIR path is isolated, so a
# Relay already running on this machine is untouched.
set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"
out=$PWD
build=$1
tag=${2:-run}
port=8767
log=run-$tag.log
: >"$log"
fails=0
say() { echo "$*" | tee -a "$log"; }
ocr() { tesseract "$1" stdout 2>/dev/null; }
ocr_has() { ocr "$1" | grep -qF "$2"; }
need() { if ocr_has "$1" "$2"; then say "PASS: $3"; else say "FAIL: $3 (wanted \"$2\" in $1)"; fails=$((fails+1)); fi; }
need_not() { if ocr_has "$1" "$2"; then say "FAIL: $3 (did not want \"$2\" in $1)"; fails=$((fails+1)); else say "PASS: $3"; fi; }

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

trap 'kill "${relay_pid:-0}" "${stub_pid:-0}" "${xvfb_pid:-0}" "${msg_pid:-0}" 2>/dev/null; sleep 1; rm -rf "$sandbox"' EXIT

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
ocr "$out/implementer-00-dialog.png" >>"$log"
savexy=$(tesseract "$out/implementer-00-dialog.png" stdout tsv 2>/dev/null | awk -F'\t' '$12 == "Cancel" && $11 > 40 { x = $7 + $9 / 2; y = $8 + $10 / 2 } END { if (x) printf "%d %d", x - 60, y }')
[[ -n $savexy ]] && savexy="$((X + ${savexy%% *})) $((Y + ${savexy##* }))"
[[ -z $savexy ]] && savexy="$((X + WIDTH - 153)) $((Y + HEIGHT - 19))"
say "Save at $savexy (dialog ${WIDTH}x${HEIGHT} at $X,$Y)"
xdotool windowactivate --sync "$dlg"; sleep 0.5
xdotool mousemove --sync $savexy; sleep 0.3; xdotool click --delay 120 1; sleep 14
shot 01-agent-ready

# ---- turns 1-3 --------------------------------------------------------------------------------
xdotool windowactivate --sync "$win"
for n in 1 2 3; do
    t "turn $n"; sleep 0.4
    k ctrl+Return; sleep 4
done
shot 02-three-turns

# ---- turn 4 is slow, so it is still running when focus leaves; it finishes while away --------
xmessage -geometry 200x60+1100+700 "away" &
msg_pid=$!
sleep 1
away=$(xdotool search --name away | tail -1)
t 'slow turn four, finishing while away'; sleep 0.4
k ctrl+Return
sleep 1.5
xdotool windowfocus --sync "$away"       # deactivate Relay while the turn still runs
xdotool windowactivate --sync "$away"
sleep 12                                 # the stub holds the turn 9 s: it ends while away
sleep 3                                  # and the away stretch passes the 2 s threshold
xdotool windowactivate --sync "$win"     # refocus -> recap_request {reason: "away"}
xdotool windowfocus --sync "$win"
sleep 10                                 # recap sidecall + print; let the toast fade
shot 03-away-recap
need  "$out/implementer-03-away-recap.png" "end of message" "03 the away recap opens with the marker"
need  "$out/implementer-03-away-recap.png" "finished at" "03 the away recap states the finish time"
need  "$out/implementer-03-away-recap.png" "Recap" "03 the away recap header follows"

# ---- /recap by hand ---------------------------------------------------------------------------
xdotool windowactivate --sync "$win"
t '/recap'; sleep 0.3
k ctrl+Return; sleep 7
shot 04-manual-recap
need  "$out/implementer-04-manual-recap.png" "end of message" "04 the manual recap opens with the marker"
need  "$out/implementer-04-manual-recap.png" "finished at" "04 the manual recap states the finish time"

# ---- a recap asked mid-run has no finish time to state ----------------------------------------
# While the agent is busy the prompt box is a queue: Ctrl+Return escalates a steer. A slash
# command is run by the slash list's own Enter, so plain Return is the key that can reach
# requestRecap() mid-run. If it still steers, this step fails and the queue behaviour is the
# finding (the worker's mid-run field set is covered by tests/test_sessions.py).
t 'slow turn please'; sleep 0.4
k ctrl+Return
sleep 2                                    # the turn is running (the stub holds it 9 s)
t '/recap'; sleep 0.6
k Return; sleep 6
import -window root "$out/implementer-05-midrun-full.png"
convert "$out/implementer-05-midrun-full.png" -crop 1500x300+0+600 +repage "$out/implementer-05-midrun-recap.png"
ocr "$out/implementer-05-midrun-recap.png" >>"$log"
say "--- shot 05 (bottom crop) above ---"
need     "$out/implementer-05-midrun-recap.png" "Recap" "05 a recap block printed while the turn runs"
need_not "$out/implementer-05-midrun-recap.png" "finished at" "05 the mid-run recap prints no finish time"
sleep 6                                    # let the slow turn end before the sandbox goes away

say "fails=$fails"
[[ $fails -eq 0 ]] || exit 1
say "PASS"
