#!/usr/bin/env bash
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# Live drive for card #2FQ9: an artifact's agent popped out into a linked shell pane and docked
# back. Shaped after docs/qa_evidence/2026-09-20-switchboard-page-agent/drive.sh.
#
#   docs/qa_evidence/2026-09-26-2fq9-linked-agent-shell/drive.sh <relay binary>
#
# It builds a fixture project under /tmp/f9 (short: the worker's socket must fit in 108 bytes)
# with a one-card board and a Markdown file, starts stub-provider.py on 127.0.0.1:8841 as the
# `local:stub` endpoint, and boots Relay under Xvfb with an isolated HOME, XDG_* dirs, runtime dir
# and TMPDIR, RELAY_KEYRING=off. It leaves Relay running and writes /tmp/f9/state; the steps after
# that are driven by hand with xdotool and relay-drive, and README.md says which shot came from
# which step. Needs Xvfb, xdotool and ImageMagick.
set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"
out=$PWD
root=$(cd ../../.. && pwd)
binary=${1:?relay binary}
sandbox=/tmp/f9
port=8841
width=1480 height=980

pkill -f "stub-provider.py $port" 2>/dev/null
[[ -f $sandbox/state ]] && kill "$(sed -n 's/^RELAY_PID=//p' $sandbox/state)" "$(sed -n 's/^XVFB_PID=//p' $sandbox/state)" 2>/dev/null
sleep 1
rm -rf "$sandbox"
mkdir -p "$sandbox/home" "$sandbox/run" "$sandbox/tmp"
chmod 700 "$sandbox/run"
work=$sandbox/home/proj
mkdir -p "$work/.board/features" "$work/.board/changes" "$work/.board/threads"
cat >"$work/.board/board.yaml" <<'YAML'
# Switchboard configuration. Format: docs/SWITCHBOARD-FORMAT.md
version: 1
tabs: [{id: features, folder: features}, {id: bugs, folder: changes}, {id: done, filter: 'status:done,dropped'}]
columns: [inbox, discussing, planning, planned, executing, needs-verification, needs-qa, done]
agent: {autonomy: auto, max_creates_per_turn: 5}
memory: {autonomy: auto}
YAML
cat >"$work/.board/features/2026-09-26-linked-shell-fixture.md" <<'CARD'
---
id: F9A1
type: work
status: discussing
assignee: agent
rank: mmmm
created: '2026-09-26'
links: {commits: [], evidence: [], github: null, plans: [], related: []}
---
# Linked shell fixture card

## Issue
A card whose agent is popped out into a linked shell pane and docked back.
CARD
printf '# Notes\n\nA Markdown file whose agent is popped out too.\n' >"$work/notes.md"
(cd "$work" && git init -q && git add -A && git -c user.name=qa -c user.email=qa@example.invalid commit -qm fixture)

setsid nohup python3 "$out/stub-provider.py" "$port" "$sandbox/stub-requests.jsonl" >"$sandbox/stub.log" 2>&1 </dev/null &
sleep 1.5
curl -s -m 3 "http://127.0.0.1:$port/v1/models" >/dev/null || { echo "stub did not come up"; exit 1; }

display=
for n in $(seq 170 199); do [[ -e /tmp/.X11-unix/X$n ]] || { display=:$n; break; }; done
[[ -z $display ]] && { echo "no free display"; exit 1; }

# A shell inside Relay carries RELAY_OPEN_SOCKET, the *running* Relay's socket, and relay-drive and
# relay-open read it before XDG_RUNTIME_DIR: left set, every driver call reaches the person's own
# Relay instead of this sandbox (it did, on the first take of this run). Point it at the sandbox
# once it has written its socket: RELAY_OPEN_SOCKET=$(cat /tmp/f9/run/relay/open-socket).
unset RELAY_OPEN_SOCKET
export HOME=$sandbox/home XDG_RUNTIME_DIR=$sandbox/run TMPDIR=$sandbox/tmp
export XDG_CONFIG_HOME=$HOME/.config XDG_DATA_HOME=$HOME/.local/share XDG_CACHE_HOME=$HOME/.cache
mkdir -p "$XDG_CONFIG_HOME/RelayTerminal" "$XDG_CONFIG_HOME/relay" "$XDG_DATA_HOME" "$XDG_CACHE_HOME"
ln -sfn "/run/user/$(id -u)/bus" "$sandbox/run/bus"
printf "PS1='\\\\w \$ '\nunset PROMPT_COMMAND\n" >"$HOME/.bashrc"
cat >"$XDG_CONFIG_HOME/RelayTerminal/relay.conf" <<'CONF'
[instructions]
onboarded=true
[theme]
name=relay-dark
[provider]
preset=local:stub
[security]
approvals_chosen=true
[terminal]
persistLocal=false
CONF
printf '{"version": 1, "endpoints": [{"id": "local:stub", "label": "Stub", "base_url": "http://127.0.0.1:%s/v1", "model": "stub", "server": "openai-compatible", "context_window": 131072}]}\n' \
    "$port" >"$XDG_CONFIG_HOME/relay/local-models.json"

nohup Xvfb "$display" -screen 0 $((width + 120))x$((height + 80))x24 >"$sandbox/xvfb.log" 2>&1 &
xvfb_pid=$!
sleep 2
export DISPLAY=$display RELAY_KEYRING=off RELAY_SESSION=qa2fq9
cd "$work" || exit 1
nohup "$binary" --workspace "$work" >"$sandbox/relay.log" 2>&1 &
relay_pid=$!
sleep 9
win= ; best=0
for w in $(xdotool search --pid "$relay_pid" 2>/dev/null); do
    eval "$(xdotool getwindowgeometry --shell "$w" 2>/dev/null)"
    (( WIDTH * HEIGHT > best )) && { best=$(( WIDTH * HEIGHT )); win=$w; }
done
[[ -z $win ]] && { echo "no Relay window"; tail -40 "$sandbox/relay.log"; exit 1; }
xdotool windowmove "$win" 0 0 windowsize "$win" $width $height windowfocus "$win"
sleep 1.5
cat >"$sandbox/state" <<STATE
DISPLAY=$display
WIN=$win
RELAY_PID=$relay_pid
XVFB_PID=$xvfb_pid
WORK=$work
STATE
echo "Relay is up on $display (window $win); state in $sandbox/state."
