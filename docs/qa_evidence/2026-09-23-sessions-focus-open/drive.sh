#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"
out=$PWD
root=$(cd ../../.. && pwd)
sandbox=$(mktemp -d /tmp/relay-sessions-focus.XXXXXX)
relay_pid= xvfb_pid= stub_pid=
cleanup() {
    for pid in $relay_pid $xvfb_pid $stub_pid; do
        [ -n "$pid" ] && kill "$pid" 2>/dev/null || true
    done
    rm -rf "$sandbox"
}
trap cleanup EXIT
display=
for n in $(seq 160 199); do
    [ -e "/tmp/.X11-unix/X$n" ] || { display=":$n"; break; }
done
[ -n "$display" ]
Xvfb "$display" -screen 0 1240x940x24 >"$sandbox/xvfb.log" 2>&1 &
xvfb_pid=$!
sleep 2
export DISPLAY=$display RELAY_KEYRING=off HOME=$sandbox/home
export XDG_RUNTIME_DIR=$sandbox/run TMPDIR=$sandbox/tmp
export XDG_CONFIG_HOME=$HOME/.config XDG_DATA_HOME=$HOME/.local/share
export XDG_CACHE_HOME=$HOME/.cache XDG_STATE_HOME=$HOME/.local/state
export RELAY_QA_RECTS=$out/rects.json
mkdir -p "$HOME" "$XDG_RUNTIME_DIR" "$TMPDIR" "$XDG_CONFIG_HOME/RelayTerminal" "$XDG_CONFIG_HOME/relay" "$XDG_DATA_HOME"
chmod 700 "$XDG_RUNTIME_DIR"
python3 "$root/docs/qa_evidence/2026-09-20-session-resume-scrollback/stub-provider.py" 8831 >"$sandbox/stub.log" 2>&1 &
stub_pid=$!
work=$HOME/project
mkdir -p "$work"
printf "PS1='\\\\w \\\$ '\nunset PROMPT_COMMAND\n" >"$HOME/.bashrc"
cat >"$XDG_CONFIG_HOME/RelayTerminal/relay.conf" <<'CONF'
[instructions]
onboarded=true
[security]
approvals_chosen=true
[provider]
preset=local:stub
CONF
printf '{"version":1,"endpoints":[{"id":"local:stub","label":"Stub","base_url":"http://127.0.0.1:8831/v1","model":"stub","server":"openai-compatible","context_window":131072}]}\n' >"$XDG_CONFIG_HOME/relay/local-models.json"
RELAY_TEST_ROOT=$root RELAY_TEST_WORK=$work python3 - <<'PY'
import os
import sys
import time
from pathlib import Path
root = Path(os.environ["RELAY_TEST_ROOT"])
work = Path(os.environ["RELAY_TEST_WORK"])
sys.path[:0] = [str(root / "backend"), str(root / "tests")]
from relay_core.sessions import SessionStore, default_session_dir
from test_conv_index import session
store = SessionStore(default_session_dir(work))
for letter, title in (("a", "alpha focus session"), ("b", "bravo focus session")):
    data = session(letter * 32, workspace=str(work), title=title, turns=1,
                   updated=time.time() - (1 if letter == "a" else 20), model="stub")
    data["preset"] = "local:stub"
    data["messages"][0]["content"] = f"{letter} question"
    data["checkpoints"]["items"][0]["prompt"] = f"{letter} question"
    store.save(data)
PY
"$root/build/relay" --workspace "$work" >"$out/relay.log" 2>&1 &
relay_pid=$!
sleep 10
win=
best=0
for w in $(xdotool search --pid "$relay_pid" 2>/dev/null); do
    eval "$(xdotool getwindowgeometry --shell "$w" 2>/dev/null)"
    if (( WIDTH * HEIGHT > best )); then best=$((WIDTH * HEIGHT)); win=$w; fi
done
[ -n "$win" ]
xdotool windowmove "$win" 0 0
xdotool windowsize "$win" 1200 900
xdotool windowfocus "$win"
sleep 1
xdotool key ctrl+shift+s
sleep 4
xdotool type --clearmodifiers alpha
sleep 2
xdotool key shift+Return
sleep 20
import -window "$win" "$out/01-after-alpha.png"
xdotool key ctrl+a
xdotool type --clearmodifiers bravo
sleep 2
xdotool key shift+Return
sleep 20
import -window "$win" "$out/02-after-bravo.png"
python3 - <<'PY' >"$out/result.txt"
import json
from pathlib import Path
rects = json.loads(Path("rects.json").read_text())
composers = [key for key in rects if key.startswith("composerEditor")]
print("visible terminal composers:", len(composers))
print("sessions search visible:", "sessionsSearch" in rects)
print("focus result:", "PASS" if len(composers) >= 3 and "sessionsSearch" in rects else "FAIL")
PY
cat "$out/result.txt"
cp "$XDG_DATA_HOME/relay/logs/worker.log" "$out/worker.log" 2>/dev/null || true
cp "$XDG_DATA_HOME/relay/logs/relay.log" "$out/gui.log" 2>/dev/null || true
