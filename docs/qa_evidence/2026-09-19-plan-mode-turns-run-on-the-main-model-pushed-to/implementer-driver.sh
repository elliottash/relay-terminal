#!/usr/bin/env bash
# SPDX-License-Identifier: AGPL-3.0-or-later
# Implementer evidence driver for the planning role (issue Z0VG). Runs Relay under Xvfb with an
# isolated HOME/XDG_CONFIG_HOME/XDG_DATA_HOME/XDG_CACHE_HOME, shows the Plan mode row in the model
# roles modal, then runs one real plan-mode turn on the stored glm-coding key: the turn swaps to
# the planning role (the pane's own glm-5.3 pushed to max reasoning), says so inline and in the
# model chip, and goes back when the turn ends. Screenshots land beside this script.
#
# The keyring is left ENABLED so the pane runs on the stored glm-coding key: the point of the run
# is that a plan turn swaps the provider to max reasoning and back. No key is printed or captured.
#
# Usage: docs/qa_evidence/2026-09-19-plan-mode-turns-run-on-the-main-model-pushed-to/implementer-driver.sh [repo root] [build dir]
set -euo pipefail

ROOT=${1:-$(cd "$(dirname "$0")/../../.." && pwd)}
BUILD=${2:-$ROOT/build}
OUT=$(cd "$(dirname "$0")" && pwd)
JAIL=$(mktemp -d)

export HOME="$JAIL/home"
export XDG_CONFIG_HOME="$JAIL/config"
export XDG_DATA_HOME="$JAIL/data"
export XDG_CACHE_HOME="$JAIL/cache"
mkdir -p "$HOME" "$XDG_CONFIG_HOME/RelayTerminal" "$XDG_DATA_HOME" "$XDG_CACHE_HOME" "$JAIL/work"
export DISPLAY=:97

# Start configured, so the run is about plan mode and not about onboarding.
cat >"$XDG_CONFIG_HOME/RelayTerminal/relay.conf" <<'CONF'
[provider]
preset=glm-coding

[instructions]
onboarded=true

[hints]
enabled=true

# Start with the Advanced list open (QSettings roles/advanced_open), so the Plan mode row is
# visible in the screenshot without driving the disclosure button.
[roles]
advanced_open=true
CONF

Xvfb "$DISPLAY" -screen 0 1400x900x24 >/dev/null 2>&1 &
XVFB=$!
cleanup() { kill "${APP:-0}" "$XVFB" 2>/dev/null || true; rm -rf "$JAIL"; }
trap cleanup EXIT
sleep 2

cd "$JAIL/work"
"$BUILD/relay" >"$JAIL/relay-stdout.txt" 2>&1 &
APP=$!
sleep 7

shot() { import -window root "$OUT/implementer-$1.png"; sleep 1; }
type_() { xdotool type --delay 30 "$1"; sleep 1; }

shot 01-startup

# 1. The roles modal's Advanced list: the Plan mode row, right under Agent turns.
xdotool key ctrl+shift+a; sleep 2; type_ "model roles"; sleep 1
xdotool key Return; sleep 4
shot 02-model-roles-plan-mode-row
xdotool key Escape; sleep 2

# 2. Plan mode on (Shift+Tab from the prompt box).
xdotool key shift+Tab; sleep 2
shot 03-plan-mode-on

# 3. One real plan-mode turn: the pane prints the ◆ routing line and the chip says which model.
type_ "Reply with the single word: ready."
shot 04-prompt-ready
xdotool key ctrl+Return; sleep 2.5
shot 05-plan-turn-running
sleep 9
shot 06-plan-turn-answer

cp "$XDG_DATA_HOME/relay/logs/relay.log" "$OUT/implementer-relay.log" 2>/dev/null || true
echo "screenshots in $OUT"
