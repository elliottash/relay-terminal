#!/bin/sh
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# The #3B1B live pass: the docked agent on the Tests pane and on the Sharing pane, on
# Xvfb display :109 with an isolated profile. Adapted from
# docs/qa_evidence/2026-09-25-artifact-panes/drive.sh (card #PBZ4), with two differences:
# the workspace is this checkout — so the Tests pane lists the repo's own suites from its
# build/ — and every step is keyboard-driven (the Actions palette, then Alt+Q), because a
# headless run has no per-pane button to click:
#
#   1. Ctrl+/ opens the Actions palette; "Test suites" filters it to tests.open; Return.
#   2. Wait for the helper's tests_list (the pane fills with this build's suites).
#   3. Alt+Q expands the pane's collapsed "Agent (Alt+Q)" row: the console is built on
#      first expand, and the question "why did the last run fail?" is typed into it.
#   4. Ctrl+/, "your devices", Return: the Sharing pane. Alt+Q, "what is being shared?".
#
# The stub provider (stub-provider.py) answers each question by echoing the
# "On screen now:" line the context sent with the ask, so the visible answer names the
# row the pane had selected. stub-requests.log is that proof in text form.
#
# The relay the build stamps is a snapshot of this whole tree — other sessions' uncommitted
# work rides along, as for every live pass in this repo.
set -u
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
cd "$root"

display=:${RELAY_QA_DISPLAY:-109}
port=${RELAY_QA_PORT:-8869}
sandbox=$(mktemp -d /tmp/3b1b-live.XXXX)
relay_pid= stub_pid= xvfb_pid=
cleanup() { kill $relay_pid $stub_pid $xvfb_pid 2>/dev/null; sleep 2; }
trap 'cleanup; rm -rf "$sandbox"' EXIT

Xvfb "$display" -screen 0 1600x1000x24 -nolisten tcp >/dev/null 2>&1 & xvfb_pid=$!
sleep 1
export DISPLAY="$display" RELAY_KEYRING=off RELAY_DATA_DIR="$root"
mkdir -p "$sandbox/home/.config/RelayTerminal" "$sandbox/home/.config/relay" "$sandbox/run" "$sandbox/tmp"
chmod 700 "$sandbox/run"
ln -sfn "/run/user/$(id -u)/bus" "$sandbox/run/bus"
export HOME="$sandbox/home" XDG_RUNTIME_DIR="$sandbox/run" TMPDIR="$sandbox/tmp"
export XDG_CONFIG_HOME="$HOME/.config" XDG_DATA_HOME="$HOME/.local/share" XDG_CACHE_HOME="$HOME/.cache"

python3 "$here/stub-provider.py" "$port" "$here/stub-requests.log" >/dev/null 2>&1 & stub_pid=$!
cat >"$XDG_CONFIG_HOME/RelayTerminal/relay.conf" <<'CONF'
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

./build/relay --workspace "$root" --fresh >"$sandbox/relay.log" 2>&1 & relay_pid=$!
sleep 12
win=$(for w in $(xdotool search --pid "$relay_pid" 2>/dev/null); do
          eval "$(xdotool getwindowgeometry --shell "$w")"; echo "$((WIDTH * HEIGHT)) $w"
      done | sort -n | tail -1 | cut -d' ' -f2)
[ -n "$win" ] || { echo "no Relay window"; tail -30 "$sandbox/relay.log"; exit 1; }
xdotool windowmove "$win" 0 0 windowsize "$win" 1560 960 windowactivate "$win"
sleep 3

shot() { import -window root "$here/$1.png"; }

xdotool key ctrl+shift+a; sleep 5                      # the Switchboard attaches this tab to
xdotool key ctrl+shift+a; sleep 2                      # the workspace project (§30.7); closed again
xdotool key ctrl+slash; sleep 1                     # Actions palette
xdotool type --delay 60 'Test suites'; sleep 1
xdotool key Return; sleep 45                        # tests_list: the helper scans build/ and pytest
shot 01-tests-pane
xdotool key Down; sleep 1                          # select the first row: the screen names it
shot 01b-tests-row-selected
xdotool key alt+q; sleep 3                          # first expand: the console is built here, and only here
shot 02-tests-agent-row
xdotool type --delay 45 'why did the last run fail?'; sleep 1
xdotool key Return; sleep 20
shot 03-tests-answer-cites-the-row

xdotool key ctrl+slash; sleep 1
xdotool type --delay 60 'sharing'; sleep 1
xdotool key Return; sleep 4
shot 04-sharing-pane
xdotool key alt+q; sleep 3
shot 05-sharing-agent-row
xdotool type --delay 45 'what is being shared?'; sleep 1
xdotool key Return; sleep 20
shot 06-sharing-answer

cp "$sandbox/relay.log" "$here/relay.out"
find "$HOME/.local" -name '*.json*' 2>/dev/null | head -20
echo "shots:"
ls "$here"
