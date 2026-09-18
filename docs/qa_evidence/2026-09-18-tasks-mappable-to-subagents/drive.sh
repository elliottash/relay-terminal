#!/usr/bin/env bash
# Tasks mappable to subagents (card #QHR1; feature intake 2026-09-18: "the tasks list should be
# mappable to subagents.").
#
#   docs/qa_evidence/2026-09-18-tasks-mappable-to-subagents/drive.sh [build-dir]
#
# One fresh Relay under Xvfb with a fully isolated profile (HOME, XDG_*, TMPDIR, RELAY_KEYRING=off).
# The only model is fake-provider.py, registered as a local endpoint (see its docstring for the script).
# A RELAY_DATA_DIR in the environment is passed through (the backend Relay runs).
#
#   01  the turn: T1 went to background subagent a1 (✦ line and strip row say "T1 · …"), T2 done, T3 deferred
#   02  Ctrl+Shift+K: the task list, T1 "✦ a1" in progress, the detail says a1 is working on it
#   03  Enter on T1: the subagent pane opens on a1's tab, headed "general a1 · T1 · Write the changelog"
#   04  Esc, click T3, S: T3 goes to a new subagent a2, in progress; a1 finished meanwhile and T1 is completed
#   05  a2 finished: Tasks 3/3
#   06  right-click on T1: "Open subagent a1" and a disabled "Run as subagent  S"
#   07  "Open subagent a2" from T3's menu: a2's tab, its task (todo, note, the user's words), and the Enter hint
#
# Needs Xvfb, xdotool and ImageMagick.
set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"
out=$PWD
root=$(cd ../../.. && pwd)
build=${1:-$root/build}
width=1400 height=900 port=18957

display=
for n in $(seq 160 199); do [[ -e /tmp/.X11-unix/X$n ]] || { display=:$n; break; }; done
[[ -z $display ]] && { echo "no free display"; exit 1; }

sandbox=${TMPDIR:-/tmp}/relay-tasks-subagents-$$
xvfb_pid= relay_pid= fake_pid=
cleanup() {
    [[ -n $relay_pid ]] && kill "$relay_pid" 2>/dev/null
    [[ -n $fake_pid ]] && kill "$fake_pid" 2>/dev/null
    [[ -n $xvfb_pid ]] && kill "$xvfb_pid" 2>/dev/null
    sleep 1; rm -rf "$sandbox"
}
trap cleanup EXIT

rm -f "$out/requests.jsonl"
python3 "$out/fake-provider.py" $port "$out/requests.jsonl" &
fake_pid=$!

Xvfb "$display" -screen 0 $((width + 40))x$((height + 40))x24 >/dev/null 2>&1 &
xvfb_pid=$!
sleep 2
export DISPLAY=$display RELAY_KEYRING=off
unset HTTP_PROXY HTTPS_PROXY ALL_PROXY http_proxy https_proxy all_proxy

export HOME=$sandbox
export XDG_CONFIG_HOME=$HOME/.config XDG_DATA_HOME=$HOME/.local/share XDG_CACHE_HOME=$HOME/.cache XDG_STATE_HOME=$HOME/.state
export XDG_RUNTIME_DIR=$HOME/run TMPDIR=$HOME/tmp
work=$HOME/project
mkdir -p "$XDG_CONFIG_HOME/RelayTerminal" "$XDG_DATA_HOME" "$XDG_CACHE_HOME" "$XDG_STATE_HOME" "$work" \
         "$XDG_RUNTIME_DIR" "$TMPDIR"
chmod 700 "$XDG_RUNTIME_DIR"
printf 'a\nb\n' >"$work/one.txt"; printf 'c\n' >"$work/two.txt"; printf 'd\ne\nf\n' >"$work/three.txt"
printf "PS1='\\\\w \$ '\nunset PROMPT_COMMAND\n" >"$HOME/.bashrc"
printf '[instructions]\nonboarded=true\n' >"$XDG_CONFIG_HOME/RelayTerminal/relay.conf"
export RELAY_LOCAL_MODELS=$HOME/local-models.json
cat >"$RELAY_LOCAL_MODELS" <<JSON
{"version": 1, "endpoints": [{"id": "local:fake", "label": "Fake", "base_url": "http://127.0.0.1:$port/v1",
  "model": "fake", "server": "openai-compatible", "context_window": 65536, "tools": true, "thinking": false, "extra": {},
  "note": "", "first_token_timeout": 30.0, "parallel_tool_calls": false, "tool_text_recovery": false}]}
JSON

t() { xdotool type --delay 16 "$1"; }
k() { xdotool key --delay 60 "$@"; }
click() { xdotool mousemove "$1" "$2" click "${3:-1}"; }
shot() { sleep 0.8; import -window root -crop ${width}x${height}+0+0 "$out/implementer-$1.png"; }

"$build/relay" --clean-shell --fresh --workspace "$work" >"$sandbox-stderr.log" 2>&1 &
relay_pid=$!
sleep 9
win=
for id in $(xdotool search --name "Relay" 2>/dev/null); do win=$id; done
[[ -z $win ]] && { echo "no Relay window"; exit 1; }
xdotool windowmove "$win" 0 0 windowsize "$win" $width $height
xdotool windowfocus "$win"; sleep 2

click 600 825
t '*do these three tasks'; k Return; sleep 3
# The provider dialog on first use: tick the consent box, then Save.
click 384 615; sleep 0.3; click 862 647; sleep 7
shot 01-t1-went-to-subagent-a1
k ctrl+shift+k; sleep 1.5
shot 02-task-list-shows-a1-on-t1
k Return; sleep 2
shot 03-enter-opens-a1s-tab
k Escape; sleep 1
click 445 198; sleep 0.5
k s; sleep 2.5
shot 04-s-hands-t3-to-a2
sleep 14
shot 05-a2-finished-tasks-3-of-3
click 420 158 3; sleep 1
shot 06-row-menu
k Escape; sleep 0.5
click 445 198 3; sleep 0.8; click 520 218; sleep 1.5
shot 07-a2s-tab-and-the-enter-hint

python3 - "$out/requests.jsonl" <<'PY'
import json, sys
for n, line in enumerate(open(sys.argv[1]), 1):
    d = json.loads(line)
    who = "main" if d["tools"] else "sub "
    calls = [c["function"]["name"] for c in d["reply"].get("tool_calls") or []]
    print(f"{n:2} {who} last={d['last'][:70]!r} -> {calls or d['reply']['content'][:50]!r}")
PY
