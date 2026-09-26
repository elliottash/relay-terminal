#!/usr/bin/env bash
set -uo pipefail
EV=/home/elliott/repos/relay-terminal/docs/qa_evidence/2026-09-25-verify-HJ1T
export XDG_CONFIG_HOME="$EV/xdg/config" XDG_DATA_HOME="$EV/xdg/data" XDG_CACHE_HOME="$EV/xdg/cache"
export RELAY_KEYRING=off RELAY_HOSTED=off RELAY_MEMORY_IMPORT=off
export RELAY_WORKSPACE="$EV/ws"
mkdir -p "$XDG_CONFIG_HOME" "$XDG_DATA_HOME" "$XDG_CACHE_HOME"
export DISPLAY=:97
Xvfb :97 -screen 0 1600x1000x24 & XVFB=$!
sleep 1.5
/home/elliott/repos/relay-terminal/build/relay & APP=$!
sleep 12
import -window root -display :97 "$EV/01-main-window.png"
# open Models pane
xdotool search --name "Relay" windowactivate --sync key ctrl+shift+m
sleep 4
import -window root -display :97 "$EV/02-models-pane.png"
echo "APP_PID=$APP XVFB_PID=$XVFB"
wait
