#!/usr/bin/env bash
# Card #BXMS: the model box's "all models" and "model settings" rows, in a sandboxed Relay on Xvfb.
set -euo pipefail
root=/home/elliott/repos/relay-terminal
bin=${RELAY_BIN:-$root/build/relay}
out=$root/docs/qa_evidence/2026-09-23-box-all-models
sandbox=$(mktemp -d /tmp/relay-bxms-XXXXXX)
display=:893
cleanup() { kill "${relay_pid:-0}" "${xvfb_pid:-0}" 2>/dev/null || true; rm -rf "$sandbox"; }
trap cleanup EXIT
Xvfb "$display" -screen 0 1400x900x24 >/dev/null 2>&1 & xvfb_pid=$!
sleep 2
export DISPLAY=$display HOME=$sandbox XDG_CONFIG_HOME=$sandbox/.config XDG_DATA_HOME=$sandbox/.local/share
export XDG_CACHE_HOME=$sandbox/.cache XDG_RUNTIME_DIR=$sandbox/run TMPDIR=$sandbox/tmp
export RELAY_KEYRING=off RELAY_OPENROUTER_CATALOG=off
mkdir -p "$XDG_CONFIG_HOME/RelayTerminal" "$XDG_DATA_HOME" "$XDG_CACHE_HOME" "$XDG_RUNTIME_DIR" "$TMPDIR" "$sandbox/project"
chmod 700 "$XDG_RUNTIME_DIR"
cat > "$XDG_CONFIG_HOME/RelayTerminal/relay.conf" <<'CONF'
[isolation]
enabled=false
[instructions]
onboarded=true
[security]
approvals_chosen=true
approvals_ask=@Invalid()
CONF
"$bin" --workspace "$sandbox/project" >"$sandbox/relay-stderr.log" 2>&1 & relay_pid=$!
sleep 14
win=$(xdotool search --pid "$relay_pid" | head -1)
xdotool windowmove "$win" 0 0 windowsize "$win" 1360 860 windowfocus "$win"
sleep 3
xdotool key alt+m
sleep 2
import -window root "$out/01-box.png"
# The last row is "model settings", the one above it "all models".
xdotool key End Up Return
sleep 2
import -window root "$out/02-all-models.png"
# Type to narrow the whole list to one "other models" row and take it: an ordinary box pick.
xdotool type --delay 80 "gpt-6-astra"
sleep 1
xdotool key Return
sleep 2
import -window root "$out/04-picked-from-all.png"
xdotool key alt+m
sleep 1
xdotool key Escape
sleep 1
xdotool key alt+m
sleep 2
xdotool key End Return
sleep 5
import -window root "$out/03-model-settings.png"
cp "$sandbox/relay-stderr.log" "$out/relay-stderr.log" 2>/dev/null || true
