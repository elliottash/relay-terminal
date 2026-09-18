#!/usr/bin/env bash
# Print the SGRs under one theme, then switch themes with /dark and screenshot again: if the
# scrollback recolours, the indexed palette did its job.
set -uo pipefail
out_before=$1; out_after=$2
home=$(mktemp -d); mkdir -p "$home/.config/RelayTerminal"
printf '[theme]\nname=ibm-beige\n' > "$home/.config/RelayTerminal/relay.conf"
Xvfb :79 -screen 0 1400x900x24 >/dev/null 2>&1 & xvfb=$!; sleep 2
HOME="$home" RELAY_KEYRING=off DISPLAY=:79 ./build/relay --workspace /tmp --fresh >/dev/null 2>&1 & app=$!; sleep 7
win=$(DISPLAY=:79 xdotool search --name "^Relay" | tail -1)
DISPLAY=:79 xdotool windowactivate "$win" 2>/dev/null; sleep 1
DISPLAY=:79 xdotool type --delay 12 "printf '\\e[39mPROSE default fg\\e[0m\\n\\e[33mINLINE code\\e[0m\\n\\e[35mHEADING\\e[0m\\n'"
sleep 1; DISPLAY=:79 xdotool key Return; sleep 3
DISPLAY=:79 import -window "$win" "$out_before" 2>/dev/null
DISPLAY=:79 xdotool type --delay 12 "/dark"; sleep 1; DISPLAY=:79 xdotool key Return; sleep 4
DISPLAY=:79 import -window "$win" "$out_after" 2>/dev/null
kill "$app" 2>/dev/null; sleep 1; kill "$xvfb" 2>/dev/null; rm -rf "$home"
