#!/usr/bin/env bash
# "waiting for N subagents . . ." in the prompt box (card #V7QD; feature intake 2026-09-18:
# "if an orchestrator terminal is waiting on subagents, play a ... waiting for subagents . . .
# blinking text in the prompt.").
#
#   docs/qa_evidence/2026-09-18-waiting-for-subagents/drive.sh [build-dir]
#
# One fresh Relay under Xvfb with a fully isolated profile (HOME, XDG_*, XDG_RUNTIME_DIR, TMPDIR,
# RELAY_KEYRING=off). The only model is fake-provider.py, registered as a local endpoint.
# A RELAY_DATA_DIR in the environment is passed through (the backend Relay runs).
#
#   01  blocked on agent_wait: the prompt box says "waiting for 2 subagents", the strip clock too
#   02  ~2 s later: the dots have grown (the same line, animated)
#   03  typing a steer: the line is gone from under the caret, nothing obstructs the text
#   04  the steer deleted again: the line is back
#   05  both subagents finished and the turn answered: the ordinary placeholder is back
#   06  the other way in — the turn ended and one background subagent is still running
#   07  that subagent finished: the ordinary placeholder is back
#
# Needs Xvfb, xdotool and ImageMagick.
set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"
out=$PWD
root=$(cd ../../.. && pwd)
build=${1:-$root/build}
width=1400 height=900 port=$((18900 + $$ % 90))

# Started from this shell's own pid, so two sessions driving Relay at once do not both decide that
# the same display is free and then take each other's out from under them.
display=
for n in $(seq $((160 + $$ % 40)) 239); do [[ -e /tmp/.X11-unix/X$n ]] || { display=:$n; break; }; done
[[ -z $display ]] && { echo "no free display"; exit 1; }

sandbox=${TMPDIR:-/tmp}/relay-waiting-subagents-$$
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
printf 'a\nb\n' >"$work/one.txt"; printf 'c\n' >"$work/two.txt"
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

# ----- blocked on agent_wait ------------------------------------------------------------------
click 600 825
t '*hold for the helpers'; k Return; sleep 3
# The provider dialog on first use: tick the consent box, then Save.
click 384 615; sleep 0.3; click 862 647; sleep 12
shot 01-blocked-on-agent-wait
sleep 1.4
shot 02-the-dots-have-grown
t 'and when you are done, say so'; sleep 0.6
shot 03-a-steer-is-never-obstructed
for _ in $(seq 32); do k BackSpace; done; sleep 1.2
shot 04-the-steer-deleted-the-line-is-back
sleep 45
shot 05-both-back-the-ordinary-placeholder

# ----- the turn ends, a background subagent is still running ------------------------------------
t '*hand it over'; k Return; sleep 12
shot 06-the-turn-ended-one-subagent-left
sleep 40
shot 07-that-one-finished-too

python3 - "$out/requests.jsonl" <<'PY'
import json, sys
for n, line in enumerate(open(sys.argv[1]), 1):
    d = json.loads(line)
    who = "main" if d["tools"] else "sub "
    calls = [c["function"]["name"] for c in d["reply"].get("tool_calls") or []]
    print(f"{n:2} {who} last={d['last'][:70]!r} -> {calls or d['reply']['content'][:50]!r}")
PY
