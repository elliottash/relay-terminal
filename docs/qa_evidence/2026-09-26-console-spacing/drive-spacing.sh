#!/bin/bash
# #83YV: the same exchange — one line for the program, then one agent prompt — in a shell pane
# (left) and a Python console (right), to compare their line spacing. Isolated profile, Xvfb,
# the local stub model (stub-provider.py). RELAY_EVIDENCE_BIN is the binary under test.
set -uo pipefail
DIR="$(cd "$(dirname "$0")" && pwd)"
: "${DISPLAY_NUM:=135}" "${PORT:=8815}"
PROFILE=$(mktemp -d); WS=$(mktemp -d)
cleanup() { for p in ${APP:-} ${STUB:-} ${XV:-}; do kill "$p" 2>/dev/null; done; sleep 1; rm -rf -- "$PROFILE" "$WS"; }
trap cleanup EXIT
export XDG_DATA_HOME="$PROFILE/share" XDG_CACHE_HOME="$PROFILE/cache" XDG_STATE_HOME="$PROFILE/state" XDG_CONFIG_HOME="$PROFILE/config"
export XDG_RUNTIME_DIR="$PROFILE/run"; mkdir -p "$XDG_RUNTIME_DIR" "$XDG_CONFIG_HOME/RelayTerminal" "$XDG_CONFIG_HOME/relay"; chmod 700 "$XDG_RUNTIME_DIR"
export RELAY_KEYRING=off
cat > "$XDG_CONFIG_HOME/RelayTerminal/relay.conf" <<CONF
[instructions]
onboarded=true
[theme]
name=relay-dark
[provider]
preset=local:stub
[models]
tier\main="local:stub|stub-1||rank=1"
[approvals]
mode=allow
[terminal]
persistLocal=false
CONF
printf '{"version": 1, "endpoints": [{"id": "local:stub", "label": "Stub", "base_url": "http://127.0.0.1:%s/v1", "model": "stub-1", "server": "openai-compatible", "context_window": 131072}]}\n' "$PORT" >"$XDG_CONFIG_HOME/relay/local-models.json"
Xvfb :$DISPLAY_NUM -screen 0 1600x1000x24 & XV=$!; sleep 2; export DISPLAY=:$DISPLAY_NUM
python3 "$DIR/stub-provider.py" "$PORT" >/dev/null 2>&1 & STUB=$!; sleep 1
"$RELAY_EVIDENCE_BIN" --fresh --workspace "$WS" >/dev/null 2>&1 & APP=$!
for i in $(seq 1 40); do WIN=$(xdotool search --class relay 2>/dev/null | head -1); [ -n "$WIN" ] && break; sleep 1; done
xdotool windowactivate --sync "$WIN" 2>/dev/null; sleep 10
# Left: a shell pane.
xdotool type --delay 50 'echo hi'; xdotool key Return; sleep 2
xdotool type --delay 40 'add one to x and print it'; xdotool key Return; sleep 12
# Right: a Python console, the same two lines.
xdotool key ctrl+alt+e; sleep 1.5; xdotool type --delay 80 'py'; xdotool key Return; sleep 25
xdotool type --delay 50 'x = 41'; xdotool key Return; sleep 3
xdotool type --delay 40 'add one to x and print it'; xdotool key Return; sleep 15
import -window root "$DIR/01-shell-and-console.png"
echo DONE
