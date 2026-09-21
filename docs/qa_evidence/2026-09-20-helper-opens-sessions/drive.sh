#!/usr/bin/env bash
# The Sessions helper opens conversations, says what it is doing, and can be interrupted
# (card #H6VQ, protocol §30.3/§30.4/§30.7), live.
#
#   docs/qa_evidence/2026-09-20-helper-opens-sessions/drive.sh [relay-binary] [out-dir]
#
# Xvfb with an isolated HOME, XDG_CONFIG_HOME, XDG_DATA_HOME, XDG_RUNTIME_DIR and TMPDIR under a
# short path (the 108-byte socket limit), RELAY_KEYRING=off, and **no provider account**: the
# profile points a local model endpoint at stub-provider.py on 127.0.0.1, so every agent in the
# run is that script. Needs Xvfb, xdotool, ImageMagick, tesseract.
#
# Three saved conversations are seeded into the isolated data root before the first launch, under
# the workspace-digest folder `SessionStore` writes to — which is the shape `conv_index` walks.
#
# Each check writes one PASS/FAIL line to notes.txt naming the screenshot it was read from; a step
# that cannot find what it wanted logs FAIL and carries on, so one miss does not hide the rest.
set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"
out=${2:-$PWD}
root=$(cd ../../.. && pwd)
relay=${1:-$root/build/relay}
width=1600 height=1000
port=${RELAY_QA_PORT:-8841}
mkdir -p "$out"
[[ -x $relay ]] || { echo "no relay binary at $relay"; exit 1; }

display=
for n in $(seq 150 199); do [[ -e /tmp/.X11-unix/X$n ]] || { display=:$n; break; }; done
[[ -z $display ]] && { echo "no free display"; exit 1; }

sandbox=$(mktemp -d /tmp/rl-h6vq.XXXX)
stub_pid= xvfb_pid= relay_pid=
cleanup() {
    local pid
    for pid in $relay_pid $stub_pid $xvfb_pid; do kill "$pid" 2>/dev/null; done
    rm -rf "$sandbox"
}
trap cleanup EXIT

Xvfb "$display" -screen 0 $((width + 40))x$((height + 40))x24 >/dev/null 2>&1 &
xvfb_pid=$!
sleep 2
kill -0 "$xvfb_pid" 2>/dev/null || { echo "Xvfb died on $display"; exit 1; }
export DISPLAY=$display RELAY_KEYRING=off

mkdir -p "$sandbox/home" "$sandbox/run" "$sandbox/tmp"; chmod 700 "$sandbox/run"
ln -sfn "/run/user/$(id -u)/bus" "$sandbox/run/bus"
export HOME=$sandbox/home XDG_RUNTIME_DIR=$sandbox/run TMPDIR=$sandbox/tmp
export XDG_CONFIG_HOME=$HOME/.config XDG_DATA_HOME=$HOME/.local/share XDG_CACHE_HOME=$HOME/.cache
work=$HOME/project
mkdir -p "$XDG_CONFIG_HOME/RelayTerminal" "$XDG_CONFIG_HOME/relay" "$XDG_DATA_HOME" "$work"
printf "PS1='\\\\w \\\$ '\nunset PROMPT_COMMAND\n" >"$HOME/.bashrc"

# Three saved conversations, in the folder the pane's store writes to.
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
[[ -n $ids ]] || { echo "seeding failed"; exit 1; }
echo "seeded: $ids"

python3 "$PWD/stub-provider.py" "$port" $ids >/dev/null 2>&1 &
stub_pid=$!
sleep 1

cat >"$XDG_CONFIG_HOME/RelayTerminal/relay.conf" <<CONF
[instructions]
onboarded=true
[theme]
name=relay-dark
[provider]
preset=local:stub
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
# Integers: the coordinates go straight into bash arithmetic, which has no floats.
word_xy() { words "$1" | awk -v want="$2" 'tolower($1) ~ tolower(want) {x=$2+$4/2; y=$3+$5/2} END {if (x) printf "%d %d\n", x, y}'; }
click_at() { [[ -z ${1:-} || -z ${2:-} ]] && return 1; xdotool mousemove "$1" "$2" click 1; sleep 1.5; }
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
    [[ -n $relay_pid ]] && { kill "$relay_pid" 2>/dev/null; wait "$relay_pid" 2>/dev/null; sleep 2; }
    (cd "$work" && exec "$relay" --workspace "$work" --fresh) >>"$sandbox/relay.log" 2>&1 &
    relay_pid=$!
    sleep 10
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

# The Sessions pane's helper panel: Ctrl+Shift+Y opens the manager, Alt+Q expands the panel, and
# the composer is clicked as well because a screenshot in between hands the focus back to the
# window rather than to the widget.
open_sessions_helper() {
    k ctrl+shift+y; sleep 5
    k alt+q; sleep 2
    shot "$1"
    local at; at=$(word_xy "$1" "helper")
    [[ -n $at ]] && click_at $(( ${at% *} )) $(( ${at#* } + 40 ))
    sleep 1
}
ask_helper() { t "$1"; k Return; }

# ======================================================= (a) open a group of them in new panes
launch || exit 1
shot 00-start
open_sessions_helper 01-sessions-helper
has "a1 the Sessions helper panel is open" 01-sessions-helper "Sessions helper"

ask_helper "open the three sessions about panes in new panes"
# The panel shows what the helper did, in text, although the model answered with nothing at all:
# the worker appends the tool's own sentence (owner, 2026-09-20).
awaited "a2 the helper says in text what it is doing" 02-answer "Opened 3 conversations in new panes" 180
sleep 4; shot 03-three-panes
has "a3 the first conversation is open in a pane" 03-three-panes "Splitting the pane layout"
has "a3 the second is too" 03-three-panes "labels"
has "a3 and the third" 03-three-panes "equalize"
loaded=$(text 03-three-panes | grep -o "Session loaded" | wc -l)
if [[ ${loaded:-0} -ge 3 ]]; then ok "a3 three panes say which conversation they loaded"
else bad "a3 only ${loaded:-0} pane(s) say \"Session loaded\" in 03-three-panes.png"; fi

# The tab's helper worker must still be running: the panes it opened are not its own panels, and
# nothing about opening one stops it (§30.7).
stops=$(grep -h "worker_stop" "$XDG_DATA_HOME/relay/logs/worker.log" 2>/dev/null | wc -l)
if [[ ${stops:-0} -eq 0 ]]; then ok "a4 the helper worker did not stop (no worker_stop in worker.log)"
else bad "a4 the helper worker stopped $stops time(s) — see worker.log"; fi
grep -h "tool=app_open\|tool=app_sessions_search" "$XDG_DATA_HOME/relay/logs/worker.log" \
    >"$out/worker-app-calls.log" 2>/dev/null
if grep -q "tool=app_open ok=True" "$out/worker-app-calls.log"; then
    ok "a4 app_open was answered by the GUI (worker-app-calls.log)"
else bad "a4 no answered app_open in worker-app-calls.log"; fi

# ======================================================= (b) the queue, and interrupting it
launch || exit 1
open_sessions_helper 04-sessions-helper
ask_helper "take your time and look at everything"
awaited "b1 a turn is running in the Sessions helper" 05-running "Sessions helper" 60
sleep 3
ask_helper "and the one about the equalize key?"
awaited "b2 the second prompt is queued, and the Sessions panel shows the row" 06-queued "prompt waiting" 40

# ✕ Stop is the panel's own, and this one is in the Sessions panel: it carries `pane` out and the
# worker's answer comes back addressed to it. Stopping ends the *turn*; the queue survives it
# (19.18), so what the panel shows afterwards is the interrupted turn marked (cancelled) and the
# prompt that was waiting now running.
stop=$(word_xy 06-queued "stop")
if [[ -n $stop ]]; then
    click_at ${stop% *} ${stop#* }
    sleep 4
    shot 07-stopped
    has "b3 the interrupted turn is marked cancelled in the Sessions panel" 07-stopped "cancelled"
    hasnt "b3 and the queued prompt has started, so no row is waiting" 07-stopped "prompt waiting"
else
    bad "b3 no ✕ Stop button found in 06-queued.png"; shot 07-stopped
fi

cp "$XDG_DATA_HOME/relay/logs/worker.log" "$out/worker.log" 2>/dev/null
cp "$sandbox/relay.log" "$out/relay-stderr.log" 2>/dev/null
grep -h "helper\|board_chat\|worker_stop" "$XDG_DATA_HOME/relay/logs/relay.log" >"$out/relay-helper.log" 2>/dev/null
note ""
note "$pass passed, $fail failed"
echo "$pass passed, $fail failed — $out/notes.txt"
