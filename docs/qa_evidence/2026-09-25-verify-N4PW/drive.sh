#!/usr/bin/env bash
set -uo pipefail
EV=/home/elliott/repos/relay-terminal/docs/qa_evidence/2026-09-25-verify-N4PW
export XDG_CONFIG_HOME="$EV/xdg/config" XDG_DATA_HOME="$EV/xdg/data" XDG_CACHE_HOME="$EV/xdg/cache"
export RELAY_KEYRING=off RELAY_HOSTED=off RELAY_MEMORY_IMPORT=off RELAY_WORKSPACE="$EV/ws"
mkdir -p "$XDG_CONFIG_HOME" "$XDG_DATA_HOME" "$XDG_CACHE_HOME"
export DISPLAY=:97
Xvfb :97 -screen 0 1600x1000x24 & sleep 1.5
/home/elliott/repos/relay-terminal/build/relay & 
sleep 12
xdotool search --name "Relay — " | head -1
import -window root "$EV/00-main.png"
xdotool key ctrl+shift+m
sleep 4
import -window root "$EV/01-models-wide.png"
