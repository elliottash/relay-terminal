#!/usr/bin/env bash
# Relay under Xvfb, ssh localhost, the prompt box and the agent at the remote prompt (#S5SH).
set -uo pipefail
D=$(cd "$(dirname "$0")" && pwd)
BUILD=${BUILD:?}
OUT=${OUT:-$D/out}; mkdir -p "$OUT"; rm -f "$OUT"/*.png
JAIL=$(mktemp -d); PORT=18477
export RELAY_KEYRING=off HOME="$JAIL/home" XDG_CONFIG_HOME="$JAIL/config" XDG_DATA_HOME="$JAIL/data" \
       XDG_CACHE_HOME="$JAIL/cache" XDG_RUNTIME_DIR="$JAIL/run" TMPDIR="$JAIL/tmp"
mkdir -p "$HOME" "$XDG_CONFIG_HOME/RelayTerminal" "$XDG_CONFIG_HOME/relay" "$XDG_DATA_HOME" "$XDG_CACHE_HOME" "$JAIL/work" "$TMPDIR"
mkdir -m 700 -p "$XDG_RUNTIME_DIR"
export DISPLAY=:${QA_DISPLAY:-93}
printf '[provider]\npreset=local:big\n\n[instructions]\nonboarded=true\n%s' "${EXTRA_CONF:-}" >"$XDG_CONFIG_HOME/RelayTerminal/relay.conf"
printf '{"version": 1, "endpoints": [{"id": "local:big", "label": "Fake", "base_url": "http://127.0.0.1:%s/v1", "model": "big", "server": "openai-compatible", "context_window": 131072}]}\n' "$PORT" >"$XDG_CONFIG_HOME/relay/local-models.json"
: >"$OUT/requests.jsonl"
python3 "$D/fake.py" "$PORT" "$OUT/requests.jsonl" & FAKE=$!
Xvfb "$DISPLAY" -screen 0 1300x850x24 >/dev/null 2>&1 & XVFB=$!
cleanup() { kill "${APP:-0}" "$XVFB" "$FAKE" 2>/dev/null; cp -r "$XDG_DATA_HOME/relay/logs" "$OUT/logs" 2>/dev/null; rm -rf "$JAIL"; }
trap cleanup EXIT
sleep 2
cd "$JAIL/work"; "$BUILD/relay" >"$OUT/stdout.txt" 2>&1 & APP=$!
sleep 8
shot() { import -window root "$OUT/$1.png"; }
type_() { xdotool type --delay 15 "$1"; sleep 0.4; }
shot 01-ready
type_ "ssh localhost"; xdotool key Return; sleep 6; shot 02-logged-in
type_ "echo typed-from-the-prompt-box; hostname"; xdotool key Return; sleep 2.5; shot 03-remote-command
type_ "say hello"; xdotool key ctrl+Return; sleep 6; shot 04-agent-reply
type_ "check this on the host please"; xdotool key ctrl+Return; sleep 8; shot 05-agent-on-host
type_ "exit"; xdotool key Return; sleep 2.5; shot 06-after-exit
