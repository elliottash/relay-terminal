#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Relay under Xvfb against fake-provider.py, isolated (HOME, all XDG dirs, TMPDIR), left running
# for xdotool to drive. Prints the jail and display; stop it by killing this script.
# Usage: launch.sh <jail dir> [repo root] [build dir]
set -euo pipefail

JAIL=${1:?jail dir}
ROOT=${2:-$(cd "$(dirname "$0")/../../.." && pwd)}
BUILD=${3:-$ROOT/build}
OUT=$(cd "$(dirname "$0")" && pwd)
PORT=${RELAY_QA_PORT:-18437}
export RELAY_KEYRING=off
export HOME="$JAIL/home" XDG_CONFIG_HOME="$JAIL/config" XDG_DATA_HOME="$JAIL/data"
export XDG_CACHE_HOME="$JAIL/cache" XDG_RUNTIME_DIR="$JAIL/run" TMPDIR="$JAIL/tmp"
mkdir -p "$HOME" "$XDG_CONFIG_HOME/RelayTerminal" "$XDG_CONFIG_HOME/relay" "$XDG_DATA_HOME" "$XDG_CACHE_HOME" \
         "$JAIL/work" "$TMPDIR"
mkdir -m 700 -p "$XDG_RUNTIME_DIR"
export DISPLAY=:${RELAY_QA_DISPLAY:-93}

cat >"$XDG_CONFIG_HOME/RelayTerminal/relay.conf" <<'CONF'
[provider]
preset=local:fake

[instructions]
onboarded=true
CONF
# COPY_ON_SELECT=1: Settings › Terminal › Copy on select, which the bubble now honours too.
if [ "${COPY_ON_SELECT:-0}" = 1 ]; then printf '\n[terminal]\ncopy_on_select=true\n' >>"$XDG_CONFIG_HOME/RelayTerminal/relay.conf"; fi
printf '{"version": 1, "endpoints": [{"id": "local:fake", "label": "Fake", "base_url": "http://127.0.0.1:%s/v1", "model": "fake", "server": "openai-compatible", "context_window": 131072}]}\n' \
  "$PORT" >"$XDG_CONFIG_HOME/relay/local-models.json"

: >"$JAIL/requests.jsonl"
python3 "$OUT/fake-provider.py" "$PORT" "$JAIL/requests.jsonl" &
FAKE=$!
Xvfb "$DISPLAY" -screen 0 1400x900x24 >/dev/null 2>&1 &
XVFB=$!
trap 'kill "${APP:-0}" "$XVFB" "$FAKE" 2>/dev/null || true' EXIT
sleep 2
cd "$JAIL/work"
"$BUILD/relay" --clean-shell --fresh >"$JAIL/relay-stdout.txt" 2>&1 &
APP=$!
echo "jail=$JAIL display=$DISPLAY relay=$APP"
wait "$APP"
