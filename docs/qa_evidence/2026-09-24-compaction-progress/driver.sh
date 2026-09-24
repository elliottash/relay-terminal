#!/usr/bin/env bash
# SPDX-License-Identifier: AGPL-3.0-or-later
# Evidence driver for card 31BM (compaction % progress), offline: Relay under Xvfb with an
# isolated HOME/XDG/TMPDIR, against fake-provider.py as local model endpoints (no key, no credits).
#
# A fresh profile now opens its first pane as a guest Claude harness, so the run selects the
# local endpoint the way a person would: `/model local:big`, two "essay" turns to fill the
# conversation, then `/model local:small` — the switch compacts (12,000-token window), and the
# summary streams slowly (60 SSE deltas, 0.4 s apart), so the context chip is photographed
# mid-compaction showing "compacting… N%", then a later tick, then after it lands.
#
# Usage: driver.sh [repo root] [build dir]   (RELAY_QA_PORT, RELAY_QA_DISPLAY, KEEP_JAIL to keep)
set -euo pipefail

ROOT=${1:-$(cd "$(dirname "$0")/../../.." && pwd)}
BUILD=${2:-$ROOT/build}
OUT=$(cd "$(dirname "$0")" && pwd)
JAIL=$(mktemp -d)
PORT=${RELAY_QA_PORT:-18499}
export RELAY_KEYRING=off

export HOME="$JAIL/home"
export XDG_CONFIG_HOME="$JAIL/config"
export XDG_DATA_HOME="$JAIL/data"
export XDG_CACHE_HOME="$JAIL/cache"
export XDG_RUNTIME_DIR="$JAIL/run"
export TMPDIR="$JAIL/tmp"
mkdir -p "$HOME" "$XDG_CONFIG_HOME/RelayTerminal" "$XDG_CONFIG_HOME/relay" "$XDG_DATA_HOME" "$XDG_CACHE_HOME" \
         "$JAIL/work" "$TMPDIR"
mkdir -m 700 -p "$XDG_RUNTIME_DIR"
export DISPLAY=:${RELAY_QA_DISPLAY:-99}

cat >"$XDG_CONFIG_HOME/RelayTerminal/relay.conf" <<CONF
[instructions]
onboarded=true
CONF
endpoint() { # id label model window
  printf '{"id": "%s", "label": "%s", "base_url": "http://127.0.0.1:%s/v1", "model": "%s", "server": "openai-compatible", "context_window": %s}' \
    "$1" "$2" "$PORT" "$3" "$4"
}
printf '{"version": 1, "endpoints": [%s, %s]}\n' \
  "$(endpoint local:big 'Fake big' big 131072)" "$(endpoint local:small 'Fake small' small 12000)" \
  >"$XDG_CONFIG_HOME/relay/local-models.json"

: >"$OUT/requests.jsonl"
python3 "$OUT/fake-provider.py" "$PORT" "$OUT/requests.jsonl" &
FAKE=$!
sleep 1
kill -0 "$FAKE" 2>/dev/null || { echo "port $PORT busy"; exit 1; }
Xvfb "$DISPLAY" -screen 0 1400x900x24 >/dev/null 2>&1 &
XVFB=$!
cleanup() { kill "${APP:-0}" "$XVFB" "$FAKE" 2>/dev/null || true; [ -n "${KEEP_JAIL:-}" ] || rm -rf "$JAIL"; }
trap cleanup EXIT
sleep 2

cd "$JAIL/work"
"$BUILD/relay" >"$JAIL/relay-stdout.txt" 2>&1 &
APP=$!
sleep 8

shot() { import -window root "$OUT/$1.png"; }
type_() { xdotool type --delay 20 "$1"; sleep 0.5; }
ask() { type_ "$1"; xdotool key ctrl+Return; }
model_() { type_ "/model $1"; xdotool key Return; sleep 2; }
turns_ended() { grep -rhs ' turn_end ' "$XDG_DATA_HOME/relay/logs" 2>/dev/null | wc -l; }
wait_turns() { for _ in $(seq 1 120); do [ "$(turns_ended)" -ge "$1" ] && return 0; sleep 1; done; return 1; }
progress_lines() { grep -rhsE 'type=compaction_progress' "$XDG_DATA_HOME/relay/logs" 2>/dev/null | wc -l; }
wait_progress() { for _ in $(seq 1 120); do [ "$(progress_lines)" -ge "$1" ] && return 0; sleep 1; done; return 1; }
wait_compacted() { for _ in $(seq 1 120); do grep -rhsqE 'type=compacted' "$XDG_DATA_HOME/relay/logs" 2>/dev/null && return 0; sleep 1; done; return 1; }

shot 01-ready
model_ local:big
ask "Write an essay about progress indicators, part one."; wait_turns 1
ask "Write an essay about progress indicators, part two."; wait_turns 2
sleep 2; shot 02-long-on-big
model_ local:small                       # the switch compacts: big summarises into small's window
sleep 6                                  # switch decided; the slow summary is streaming by now
shot 03-compacting-percent               # ~8 s of the 24 s stream: the chip holds a low percent
sleep 7; shot 04-compacting-later        # ~15 s in: a visibly higher percent
sleep 16                                 # the stream finishes; compacted clears the chip
ask "Tell me a one-line joke."; wait_turns 3
sleep 2; shot 05-landed
grep -rhsE 'type=(compaction_started|compaction_progress|compacted)' "$XDG_DATA_HOME/relay/logs" 2>/dev/null \
  | sed -E 's/ (session|pane|prompt_chars|messages)=[^ ]*//g' >"$OUT/log-lines.txt" || true
grep -rhsE ' (model_selection|model_applied|compaction_started|compacted) ' \
    "$XDG_DATA_HOME/relay/logs" 2>/dev/null \
  | sed -E 's/ (session|pane|prompt_chars|messages)=[^ ]*//g' >>"$OUT/log-lines.txt" || true
echo "driver finished: $(ls "$OUT"/*.png | wc -l) shots, $(grep -c SUMMARY "$OUT/requests.jsonl" || true) summary requests"
