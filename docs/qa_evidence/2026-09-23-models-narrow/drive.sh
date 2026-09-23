#!/usr/bin/env bash
set -euo pipefail

root=/home/elliott/repos/relay-terminal
out=$root/docs/qa_evidence/2026-09-23-models-narrow
sandbox=$(mktemp -d /tmp/relay-n4pw-XXXXXX)
display=:892
cleanup() { kill "${relay_pid:-0}" "${xvfb_pid:-0}" 2>/dev/null || true; rm -rf "$sandbox"; }
trap cleanup EXIT
Xvfb "$display" -screen 0 1000x800x24 >/dev/null 2>&1 & xvfb_pid=$!
sleep 2
export DISPLAY=$display XDG_CONFIG_HOME=$sandbox/config XDG_DATA_HOME=$sandbox/data
export XDG_CACHE_HOME=$sandbox/cache XDG_RUNTIME_DIR=$sandbox/run TMPDIR=$sandbox/tmp
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
"$root/build/relay" --workspace "$sandbox/project" >"$sandbox/relay-stderr.log" 2>&1 & relay_pid=$!
sleep 10
win=$(xdotool search --pid "$relay_pid" | head -1)
xdotool windowmove "$win" 0 0 windowsize "$win" 840 760 windowfocus "$win"
sleep 3
xdotool key ctrl+shift+m
sleep 4
xdotool mousemove 460 135 click 1
sleep 2
import -window root "$out/06-live-providers.png"
xdotool mousemove 520 135 click 1
sleep 2
import -window root "$out/08-live-available.png"
xdotool mousemove 590 135 click 1
sleep 2
import -window root "$out/09-live-priorities.png"
xdotool mousemove 690 135 click 1
sleep 2
import -window root "$out/07-live-effort.png"
xdotool mousemove 714 135 click 1
sleep 2
xdotool mousemove 530 275 click 1
sleep 1
import -window root "$out/11-live-jobs-popup.png"
xdotool key Escape
sleep 1
import -window root "$out/10-live-jobs.png"
