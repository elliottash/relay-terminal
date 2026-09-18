#!/usr/bin/env bash
# Prove the markdown palette resolves from the theme: print the exact SGRs the renderer emits
# into a pane, in each theme, and sample the pixels.
set -uo pipefail
theme=$1; disp=$2; out=$3
home=$(mktemp -d); mkdir -p "$home/.config/RelayTerminal"
printf '[theme]\nname=%s\n' "$theme" > "$home/.config/RelayTerminal/relay.conf"
Xvfb "$disp" -screen 0 1400x900x24 >/dev/null 2>&1 &
xvfb=$!; sleep 2
HOME="$home" RELAY_KEYRING=off DISPLAY="$disp" ./build/relay --workspace /tmp --fresh >/dev/null 2>&1 &
app=$!; sleep 7
win=$(DISPLAY="$disp" xdotool search --name "^Relay" | tail -1)
DISPLAY="$disp" xdotool windowactivate "$win" 2>/dev/null; sleep 1
DISPLAY="$disp" xdotool type --delay 12 "printf '\\e[39mPROSE default fg\\e[0m\\n\\e[33mINLINE code\\e[0m\\n\\e[35mHEADING marker\\e[0m\\n\\e[2;39mDIM faint\\e[0m\\n\\e[4;34mLINK blue\\e[0m\\n'"
sleep 1; DISPLAY="$disp" xdotool key Return; sleep 3
DISPLAY="$disp" import -window "$win" "$out" 2>/dev/null
kill "$app" 2>/dev/null; sleep 1; kill "$xvfb" 2>/dev/null; rm -rf "$home"
