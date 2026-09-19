#!/usr/bin/env bash
# SPDX-License-Identifier: AGPL-3.0-or-later
# Evidence driver for card 3ES1's three gaps, offline: Relay under Xvfb with an isolated HOME,
# XDG_CONFIG_HOME, XDG_DATA_HOME, XDG_CACHE_HOME, XDG_RUNTIME_DIR and TMPDIR, against
# fake-provider.py registered as three local model endpoints (no key, no credits):
#
#   local:big    131,072-token window (the model the pane starts on)
#   local:small   12,000-token window
#   local:micro    2,048-token window (smaller than the system prompt and tools)
#
#   case "compact": two long turns, then a turn whose first command sleeps 20 s; `/model small`
#                   while it sleeps. The bar must move to small's window at once (gap 1), the
#                   switch must compact with big summarising, then land (gap 3).
#   case "refuse":  idle `/model micro` (refused when asked), then a turn with one long answer and a
#                   sleeping command; `/model small` while it sleeps is refused at the next step
#                   because that turn alone does not fit, and the turn finishes on big (gap 3).
#
# Usage: gaps-driver.sh <compact|refuse> [repo root] [build dir]
set -euo pipefail

CASE=${1:?compact or refuse}
ROOT=${2:-$(cd "$(dirname "$0")/../../.." && pwd)}
BUILD=${3:-$ROOT/build}
OUT=$(cd "$(dirname "$0")" && pwd)
JAIL=$(mktemp -d)
PORT=${RELAY_QA_PORT:-18431}
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
export DISPLAY=:${RELAY_QA_DISPLAY:-97}

cat >"$XDG_CONFIG_HOME/RelayTerminal/relay.conf" <<'CONF'
[provider]
preset=local:big

[instructions]
onboarded=true
CONF
endpoint() { # id label model window
  printf '{"id": "%s", "label": "%s", "base_url": "http://127.0.0.1:%s/v1", "model": "%s", "server": "openai-compatible", "context_window": %s}' \
    "$1" "$2" "$PORT" "$3" "$4"
}
printf '{"version": 1, "endpoints": [%s, %s, %s]}\n' \
  "$(endpoint local:big 'Fake big' big 131072)" "$(endpoint local:small 'Fake small' small 12000)" \
  "$(endpoint local:micro 'Fake micro' micro 2048)" >"$XDG_CONFIG_HOME/relay/local-models.json"

: >"$OUT/gaps-$CASE-requests.jsonl"
python3 "$OUT/fake-provider.py" "$PORT" "$OUT/gaps-$CASE-requests.jsonl" &
FAKE=$!
Xvfb "$DISPLAY" -screen 0 1400x900x24 >/dev/null 2>&1 &
XVFB=$!
cleanup() { kill "${APP:-0}" "$XVFB" "$FAKE" 2>/dev/null || true; rm -rf "$JAIL"; }
trap cleanup EXIT
sleep 2

cd "$JAIL/work"
"$BUILD/relay" >"$JAIL/relay-stdout.txt" 2>&1 &
APP=$!
sleep 8

shot() { import -window root "$OUT/gaps-$CASE-$1.png"; }
type_() { xdotool type --delay 20 "$1"; sleep 0.5; }
turns_ended() { grep -rhs ' turn_end ' "$XDG_DATA_HOME/relay/logs" 2>/dev/null | wc -l; }
wait_turns() { for _ in $(seq 1 90); do [ "$(turns_ended)" -ge "$1" ] && return 0; sleep 1; done; return 1; }
ask() { type_ "$1"; xdotool key ctrl+Return; }
# The context bar's tooltip: hover the "% left" label (bottom right of the prompt row).
hover_bar() { xdotool mousemove "${BAR_X:-952}" "${BAR_Y:-775}"; sleep 2.5; }

shot 01-ready
if [ "$CASE" = compact ]; then
  ask "Write an essay about switching models, part one."; wait_turns 1
  ask "Write an essay about switching models, part two."; wait_turns 2
  sleep 2; shot 02-long-conversation-on-big
  ask "Please sleep: run the command and tell me what it printed."
  sleep 5; shot 03-command-running-on-big
  type_ "/model small"; xdotool key Return
  sleep 1.5; shot 04-switched-bar-on-small
  hover_bar; shot 05-bar-tooltip
  xdotool mousemove 700 300
  wait_turns 3; sleep 2; shot 06-landed-after-compaction
else
  type_ "/model micro"; xdotool key Return
  sleep 2; shot 02-micro-refused-when-asked
  ask "Write something bulky, then sleep: run the command and tell me what it printed."
  sleep 5; shot 03-command-running-on-big
  type_ "/model small"; xdotool key Return
  sleep 1.5; shot 04-switched-bar-on-small
  wait_turns 1; sleep 2; shot 05-refused-at-the-step-turn-finished
fi
# Event names, models and outcomes only.
grep -rhE ' (turn_start|model_applied|model_switch_refused|turn_end|protocol_error) |type=(model_changed|model_applied|model_switch_refused|compaction_started|compacted|agent_finished) ' \
    "$XDG_DATA_HOME/relay/logs" \
  | sed -E 's/ (session|pane|prompt_chars|messages|stall_s|max_steps|thinking_ms|open_items|retries|leaked_socket|effort|mode)=[^ ]*//g' \
  >"$OUT/gaps-$CASE-log-lines.txt" || true
