#!/bin/bash
# #83YV: Ctrl+Alt+E opens the palette on the "New pane" list; "py" + Enter makes a Python console.
# Isolated profile under Xvfb; RELAY_EVIDENCE_BIN is the binary under test.
set -uo pipefail
DIR="$(cd "$(dirname "$0")" && pwd)"
: "${DISPLAY_NUM:=134}"
PROFILE=$(mktemp -d); WS=$(mktemp -d)
cleanup() { [ -n "${APP:-}" ] && kill "$APP" 2>/dev/null; [ -n "${XV:-}" ] && kill "$XV" 2>/dev/null; sleep 1; rm -rf -- "$PROFILE" "$WS"; }
trap cleanup EXIT
export XDG_DATA_HOME="$PROFILE/share" XDG_CACHE_HOME="$PROFILE/cache" XDG_STATE_HOME="$PROFILE/state" XDG_CONFIG_HOME="$PROFILE/config"
export XDG_RUNTIME_DIR="$PROFILE/run"; mkdir -p "$XDG_RUNTIME_DIR" "$XDG_CONFIG_HOME/RelayTerminal"; chmod 700 "$XDG_RUNTIME_DIR"
export RELAY_KEYRING=off
printf '[instructions]\nonboarded=true\n[theme]\nname=relay-dark\n[terminal]\npersistLocal=false\n' > "$XDG_CONFIG_HOME/RelayTerminal/relay.conf"
Xvfb :$DISPLAY_NUM -screen 0 1600x1000x24 & XV=$!; sleep 2; export DISPLAY=:$DISPLAY_NUM
"$RELAY_EVIDENCE_BIN" --fresh --workspace "$WS" >/dev/null 2>&1 & APP=$!
for i in $(seq 1 40); do WIN=$(xdotool search --class relay 2>/dev/null | head -1); [ -n "$WIN" ] && break; sleep 1; done
xdotool windowactivate --sync "$WIN"; sleep 4
xdotool key ctrl+alt+e; sleep 1.5
import -window root "$DIR/01-chooser.png"
xdotool type --delay 80 'py'; sleep 1
import -window root "$DIR/02-filtered.png"
xdotool key Return; sleep 20
import -window root "$DIR/03-python-console.png"
xdotool key ctrl+alt+e; sleep 1.5; xdotool key Escape; sleep 1
import -window root "$DIR/04-esc-closed.png"
echo DONE
