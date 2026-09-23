#!/usr/bin/env bash
set -euo pipefail
root=/home/elliott/repos/relay-terminal
out=$root/docs/qa_evidence/2026-09-23-provider-groups
sandbox=$(mktemp -d /tmp/relay-p3kd-XXXXXX)
display=:887
cleanup() { kill "${relay_pid:-0}" "${xvfb_pid:-0}" 2>/dev/null || true; rm -rf "$sandbox"; }
trap cleanup EXIT
Xvfb "$display" -screen 0 1640x940x24 >/dev/null 2>&1 & xvfb_pid=$!
sleep 2
export DISPLAY=$display HOME=$sandbox XDG_CONFIG_HOME=$sandbox/.config XDG_DATA_HOME=$sandbox/.local/share
export XDG_CACHE_HOME=$sandbox/.cache XDG_RUNTIME_DIR=$sandbox/run TMPDIR=$sandbox/tmp
export RELAY_KEYRING=off RELAY_OPENROUTER_CATALOG=off
export RELAY_GLM_CODING_API_KEY=xvfb-fake-key RELAY_KIMI_CODE_API_KEY=xvfb-fake-key
mkdir -p "$XDG_CONFIG_HOME/RelayTerminal" "$XDG_DATA_HOME" "$XDG_CACHE_HOME" "$XDG_RUNTIME_DIR" "$TMPDIR" "$sandbox/project"
chmod 700 "$XDG_RUNTIME_DIR"
printf '# demo\n' > "$sandbox/project/README.md"
cat > "$XDG_CONFIG_HOME/RelayTerminal/relay.conf" <<'CONF'
[isolation]
enabled=false
[instructions]
onboarded=true
[security]
approvals_chosen=true
approvals_ask=@Invalid()
[models]
tier\main=glm-coding|glm-5.3|
CONF
"$root/build/relay" --workspace "$sandbox/project" >"$out/relay-stderr.log" 2>&1 & relay_pid=$!
sleep 12
win=$(xdotool search --pid "$relay_pid" | head -1)
xdotool windowmove "$win" 0 0 windowsize "$win" 1600 900 windowfocus "$win"
sleep 4
xdotool key ctrl+shift+m
sleep 8
xdotool mousemove 850 135 click 1
sleep 3
import -window root "$out/01-providers.png"
xdotool mousemove 1200 500
for n in 1 2 3 4 5 6 7 8 9 10 11 12 13 14; do xdotool click 5; sleep 0.1; done
sleep 2
import -window root "$out/01b-provider-keys.png"
xdotool mousemove 400 400 click 1
xdotool key ctrl+shift+o
sleep 7
xdotool type --delay 40 'Model priority lists'
sleep 3
import -window root "$out/02-options-defaults.png"
