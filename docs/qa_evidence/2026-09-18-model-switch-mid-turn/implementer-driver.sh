#!/usr/bin/env bash
# SPDX-License-Identifier: AGPL-3.0-or-later
# Implementer evidence driver for card 3ES1: changing the model while the agent is working.
# Runs Relay under Xvfb with an isolated HOME, XDG_CONFIG_HOME, XDG_DATA_HOME, XDG_CACHE_HOME,
# XDG_RUNTIME_DIR and TMPDIR. One multi-step agent turn per case; the model is switched while the
# first tool call runs, and the next model request of the same turn must go to the new model.
#
#   case "flash": glm-5.3 -> glm-5.3-flash with Alt+F (same provider, the Flash agent)
#   case "kimi":  glm-5.3 -> kimi-k3 with /kimi   (cross-provider: Z.AI -> Moonshot)
#
# The keyring is left ENABLED (RELAY_KEYRING unset) so the pane uses the stored glm-coding and kimi
# keys. No key is printed or captured: the screenshots show the transcript and the worker log lines
# copied out are the `model_applied` / `turn_start` events, which carry model names only.
#
# Usage: implementer-driver.sh <flash|kimi> [repo root] [build dir]
set -euo pipefail

CASE=${1:?flash or kimi}
ROOT=${2:-$(cd "$(dirname "$0")/../../.." && pwd)}
BUILD=${3:-$ROOT/build}
OUT=$(cd "$(dirname "$0")" && pwd)
JAIL=$(mktemp -d)
unset RELAY_KEYRING

export HOME="$JAIL/home"
export XDG_CONFIG_HOME="$JAIL/config"
export XDG_DATA_HOME="$JAIL/data"
export XDG_CACHE_HOME="$JAIL/cache"
export XDG_RUNTIME_DIR="$JAIL/run"
export TMPDIR="$JAIL/tmp"
mkdir -p "$HOME" "$XDG_CONFIG_HOME/RelayTerminal" "$XDG_DATA_HOME" "$XDG_CACHE_HOME" "$JAIL/work" "$TMPDIR"
mkdir -m 700 -p "$XDG_RUNTIME_DIR"
export DISPLAY=:${RELAY_QA_DISPLAY:-96}

cat >"$XDG_CONFIG_HOME/RelayTerminal/relay.conf" <<'CONF'
[provider]
preset=glm-coding

[instructions]
onboarded=true
CONF

Xvfb "$DISPLAY" -screen 0 1400x900x24 >/dev/null 2>&1 &
XVFB=$!
cleanup() { kill "${APP:-0}" "$XVFB" 2>/dev/null || true; rm -rf "$JAIL"; }
trap cleanup EXIT
sleep 2

cd "$JAIL/work"
"$BUILD/relay" >"$JAIL/relay-stdout.txt" 2>&1 &
APP=$!
sleep 8

shot() { import -window root "$OUT/implementer-$CASE-$1.png"; }
type_() { xdotool type --delay 25 "$1"; sleep 0.5; }

shot 01-ready
# A turn of three model requests at least: two sequential commands, then the answer.
type_ "Run the shell command 'sleep 25; echo alpha' with run_command. After it finishes, run 'echo beta' as a separate run_command call. Then reply with one short line naming both outputs."
xdotool key ctrl+Return
# Wait until the turn has started, then for its first request to come back with the tool call:
# the first command sleeps 25 s, so a switch 12 s in lands while it runs (the screenshot shows it).
for _ in $(seq 1 60); do
  if grep -rqs ' turn_start ' "$XDG_DATA_HOME/relay/logs" 2>/dev/null; then break; fi
  sleep 0.5
done
sleep 12
shot 02-first-command-running
if [ "$CASE" = flash ]; then
  xdotool key alt+f
else
  type_ "/kimi"; xdotool key Return
fi
sleep 1.5
shot 03-switched-mid-turn
# Let the turn finish.
for _ in $(seq 1 120); do
  if grep -rqs 'turn_end' "$XDG_DATA_HOME/relay/logs" 2>/dev/null; then break; fi
  sleep 1
done
sleep 2
shot 04-turn-finished
# The pane's provider settings after the switch (card WFJM): endpoint, model and extra, no key.
sed -n '/^\[provider\]/,/^\[/p' "$XDG_CONFIG_HOME/RelayTerminal/relay.conf" | grep -vi 'key' \
  >"$OUT/implementer-$CASE-provider-settings.txt" || true
# Model names, event names and outcomes only: never prompts, output or keys.
grep -rhE ' (turn_start|model_applied|turn_end|provider_stall|protocol_error) |type=(model_changed|model_applied|tool_started|agent_finished) ' \
    "$XDG_DATA_HOME/relay/logs" \
  | sed -E 's/ (session|pane|prompt_chars|messages|stall_s|max_steps|thinking_ms|open_items|retries|leaked_socket|effort|mode)=[^ ]*//g' \
  >"$OUT/implementer-$CASE-log-lines.txt" || true
