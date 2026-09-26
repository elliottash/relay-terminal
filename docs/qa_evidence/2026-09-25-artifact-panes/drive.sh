#!/usr/bin/env bash
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# #PBZ4 — the agent docked under a file editor. Replays the live pass whose shots are beside this
# file (README.md says what each shows):
#
#   docs/qa_evidence/2026-09-25-artifact-panes/drive.sh [relay-binary] [data-dir]
#
# Isolated HOME / XDG dirs / TMPDIR, RELAY_KEYRING=off, its own Xvfb display, and no provider
# account: the profile points a local endpoint at stub-provider.py, which answers "add a sentence"
# with an edit_file on README.md after DELAY seconds (the window in which this script types into
# the buffer). The scratch project is a fresh git repo with its own README.md, never the checkout's.
# The shots were taken with a binary built from a clean `git archive` of the tree being landed, and
# RELAY_DATA_DIR pointed at that export, so the backend matched the binary.
set -uo pipefail
here=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
root=$(cd "$here/../../.." && pwd)
relay=${1:-$root/build/relay}
data=${2:-$root}
port=${RELAY_QA_PORT:-8861}
delay=12
[[ -x $relay ]] || { echo "no relay binary at $relay"; exit 1; }

display=
for n in $(seq 160 199); do [[ -e /tmp/.X11-unix/X$n ]] || { display=:$n; break; }; done
[[ -z $display ]] && { echo "no free display"; exit 1; }
sandbox=$(mktemp -d /tmp/rl-pbz4.XXXX)
relay_pid= stub_pid= xvfb_pid=
cleanup() { kill $relay_pid $stub_pid $xvfb_pid 2>/dev/null; rm -rf "$sandbox"; }
trap cleanup EXIT

Xvfb "$display" -screen 0 1540x980x24 >/dev/null 2>&1 & xvfb_pid=$!
sleep 2
export DISPLAY=$display RELAY_KEYRING=off RELAY_DATA_DIR=$data
unset RELAY_OPEN_SOCKET
mkdir -p "$sandbox/home/.config/RelayTerminal" "$sandbox/home/.config/relay" "$sandbox/run" "$sandbox/tmp"
chmod 700 "$sandbox/run"
ln -sfn "/run/user/$(id -u)/bus" "$sandbox/run/bus"
export HOME=$sandbox/home XDG_RUNTIME_DIR=$sandbox/run TMPDIR=$sandbox/tmp
export XDG_CONFIG_HOME=$HOME/.config XDG_DATA_HOME=$HOME/.local/share XDG_CACHE_HOME=$HOME/.cache
printf "PS1='\\\\w \\\$ '\nunset PROMPT_COMMAND\n" >"$HOME/.bashrc"

project=$HOME/project
mkdir -p "$project"
git -C "$project" init -q
printf '# Scratch project\n\nA small project used to try the docked agent.\n\n## Usage\n\nRun it.\n\n## Notes\n\nNothing yet.\n' >"$project/README.md"
git -C "$project" add README.md
git -C "$project" -c user.name=qa -c user.email=qa@example.invalid commit -qm init

python3 "$here/stub-provider.py" "$port" "$project/README.md" "$delay" "$sandbox/stub.log" >/dev/null 2>&1 &
stub_pid=$!
cat >"$XDG_CONFIG_HOME/RelayTerminal/relay.conf" <<CONF
[instructions]
onboarded=true
[theme]
name=relay-dark
[provider]
preset=local:stub
[approvals]
mode=allow
[security]
approvals_chosen=true
[terminal]
persistLocal=false
[models]
tier/main=local:stub|stub|
[roles]
switchboard/preset=local:stub
switchboard/model=stub
CONF
printf '{"version": 1, "endpoints": [{"id": "local:stub", "label": "Stub", "base_url": "http://127.0.0.1:%s/v1", "model": "stub", "server": "openai-compatible", "context_window": 131072}]}\n' \
    "$port" >"$XDG_CONFIG_HOME/relay/local-models.json"

(cd "$project" && exec "$relay" --workspace "$project" --fresh) >"$sandbox/relay.log" 2>&1 &
relay_pid=$!
sleep 12
win=$(for w in $(xdotool search --pid "$relay_pid"); do
          eval "$(xdotool getwindowgeometry --shell "$w")"; echo "$((WIDTH * HEIGHT)) $w"
      done | sort -n | tail -1 | cut -d' ' -f2)
[[ -z $win ]] && { echo "no Relay window"; tail -30 "$sandbox/relay.log"; exit 1; }
xdotool windowmove "$win" 0 0 windowsize "$win" 1500 940
sleep 3

shot() { import -window root "$here/$1.png"; }
click() { xdotool mousemove "$1" "$2" click 1; sleep "${3:-1}"; }
t() { xdotool type --delay 30 "$1"; }

click 400 865 0.4; t 'relay open README.md'; xdotool key Return; sleep 5   # the preview beside the pane
click 1135 63 1.5                                        # ✎ Edit
click 1410 907 6                                         # ✦ Agent (Alt+Q): the console is built
shot 01-docked-agent-with-markdown-actions
click 1100 848 0.4; xdotool type --delay 60 '/'; sleep 1.5
shot 02-slash-popup-markdown-commands
xdotool key End; xdotool type --delay 80 'o'; sleep 1.5
shot 02b-slash-popup-filtered
xdotool key ctrl+a BackSpace Escape; sleep 0.5
click 1100 848 0.4; t 'Please add a sentence under Usage.'; xdotool key Return; sleep 2.5
click 940 274 0.3; xdotool key End; xdotool type --delay 40 ' Typed while the agent worked.'   # the Notes line
shot 03-typing-during-the-turn
timeout 60 bash -c "until grep -q 'tool_results.*\\[\"' '$sandbox/stub.log'; do sleep 1; done"; sleep 4
shot 04-sentence-landed-typing-kept
click 1185 682 1.5                                       # the change list
shot 05-change-list-names-the-turn
click 1100 450 0.3; xdotool key ctrl+z; sleep 1.2        # one undo
shot 06-one-undo-removes-only-the-agents-step
click 1185 629 0.5; click 898 815 5                      # fold the list; Outline (o)
shot 07-outline-action-runs
cp "$sandbox/stub.log" "$here/stub-requests.log"
echo "disk after the pass (the agent's change is unsaved, so the disk is the original):"
cat "$project/README.md"
