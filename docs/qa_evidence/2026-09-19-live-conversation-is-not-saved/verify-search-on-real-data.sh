#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Reproduce the "sessions search does not full-text the conversation content" report.
#
# Runs the current build under Xvfb on an isolated HOME/XDG tree that holds a *copy* of the real
# sessions and index, with the pane's workspace set to this checkout (the workspace most of the
# index's conversations belong to). Drives the Sessions pane with xdotool, screenshots it, and
# OCRs the screenshots so the list and the search box can be read from the terminal.
set -uo pipefail
OUT=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
ROOT=/home/elliott/repos/relay-terminal
BUILD=${1:-$ROOT/build}
PORT=18571
JAIL=$(mktemp -d /tmp/relay-repro-XXXXXX)
chmod 700 "$JAIL"
export RELAY_KEYRING=off HOME="$JAIL/home" XDG_CONFIG_HOME="$JAIL/config" XDG_DATA_HOME="$JAIL/data"
export XDG_CACHE_HOME="$JAIL/cache" XDG_RUNTIME_DIR="$JAIL/run" TMPDIR="$JAIL/tmp" DISPLAY=:89
mkdir -p "$HOME" "$XDG_CONFIG_HOME/RelayTerminal" "$XDG_CONFIG_HOME/relay" "$XDG_DATA_HOME" "$XDG_CACHE_HOME" "$TMPDIR"
mkdir -m 700 -p "$XDG_RUNTIME_DIR"
mkdir -p "$XDG_DATA_HOME/relay"
# Only the sessions and the index: no window layout, no closed list, no remote state.
cp -a /home/elliott/.local/share/relay/sessions "$XDG_DATA_HOME/relay/sessions"
cp -a /home/elliott/.local/share/relay/index.db "$XDG_DATA_HOME/relay/index.db"
cat >"$XDG_CONFIG_HOME/RelayTerminal/relay.conf" <<CONF
[provider]
preset=local:fake

[instructions]
onboarded=true

[isolation]
enabled=false
CONF
printf '{"version": 1, "endpoints": [{"id": "local:fake", "label": "Fake local", "base_url": "http://127.0.0.1:%s/v1", "model": "fake-qa", "server": "openai-compatible", "context_window": 131072}]}\n' \
  "$PORT" >"$XDG_CONFIG_HOME/relay/local-models.json"
python3 "$ROOT/docs/qa_evidence/2026-09-18-session-info-and-manager/fake-provider.py" "$PORT" >"$JAIL/fake.log" 2>&1 &
FAKE=$!
Xvfb "$DISPLAY" -screen 0 1500x950x24 >/dev/null 2>&1 &
XVFB=$!
cleanup() { kill "${APP:-0}" "$XVFB" "$FAKE" 2>/dev/null; }
trap cleanup EXIT
sleep 2
cd "$ROOT"
rm -f /tmp/relay-shim/worker-stderr.log
RELAY_DATA_DIR=/tmp/relay-shim "$BUILD/relay" >"$JAIL/relay.log" 2>&1 &
APP=$!
sleep 10
win=$(xdotool search --pid "$APP" --name Relay | tail -1)
xdotool windowmove "$win" 0 0 windowfocus "$win"
sleep 1
shot() {
  import -window root "$OUT/repro-$1.png"
  tesseract "$OUT/repro-$1.png" "$OUT/repro-$1" >/dev/null 2>&1
  echo "--- $1"
  sed -n '1,60p' "$OUT/repro-$1.txt"
}
shot 00-startup
xdotool key ctrl+shift+y; sleep 4
shot 01-sessions-opened
xdotool type --delay 25 "pelican"; sleep 3
shot 02-typed-pelican
xdotool key ctrl+a; xdotool type --delay 25 "superseded"; sleep 3
shot 03-typed-superseded
xdotool key ctrl+a; xdotool type --delay 25 "qa_evidence"; sleep 3
shot 04-typed-qa_evidence
echo "=== relay.log (tail)"
tail -30 "$JAIL/relay.log"
echo "=== jail: $JAIL (kept for inspection)"
