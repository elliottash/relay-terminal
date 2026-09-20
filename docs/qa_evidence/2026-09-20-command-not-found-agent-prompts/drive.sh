#!/usr/bin/env bash
# Live check of card #EB4A under Xvfb + xdotool: a prose prompt typed for the agent must not
# print "command not found: …" under its ✦ echo; a real typo still must.
#
#   drive.sh [build-dir]
#
# Writes implementer-*.png next to this script plus relay-stderr.log and OCR text of each
# screenshot (tesseract). An isolated XDG_CONFIG_HOME/XDG_DATA_HOME keeps the run out of the
# real profile. The lines submitted are innocuous prose; no secrets are typed.
set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"
out=$PWD
root=$(cd ../../.. && pwd)
build=${1:-$root/build}
display=:94

export XDG_CONFIG_HOME=$(mktemp -d) XDG_DATA_HOME=$(mktemp -d) XDG_CACHE_HOME=$(mktemp -d)
work=$(mktemp -d)
mkdir -p "$XDG_CONFIG_HOME/RelayTerminal"
cat >"$XDG_CONFIG_HOME/RelayTerminal/relay.conf" <<'CONF'
[instructions]
onboarded=true
[terminal]
shell_integration=true
CONF
trap 'kill "${relay_pid:-0}" "${xvfb_pid:-0}" 2>/dev/null; rm -rf "$XDG_CONFIG_HOME" "$XDG_DATA_HOME" "$XDG_CACHE_HOME" "$work"' EXIT

Xvfb "$display" -screen 0 1400x900x24 >/dev/null 2>&1 &
xvfb_pid=$!
sleep 1
export DISPLAY=$display

shot() { import -window root "$out/implementer-$1.png"; sleep 0.5;
         tesseract "$out/implementer-$1.png" "$out/implementer-$1" >/dev/null 2>&1; }
t() { xdotool type --delay 15 "$1"; }
k() { xdotool key --delay 50 "$@"; }

"$build/relay" --workspace "$work" >"$out/relay-stderr.log" 2>&1 &
relay_pid=$!
sleep 7
win=$(xdotool search --pid "$relay_pid" --name Relay | tail -1)
[[ -z $win ]] && { echo "no Relay window"; exit 1; }
xdotool windowmove "$win" 0 0 windowsize "$win" 1400 900
xdotool windowfocus "$win"
sleep 2
shot 00-start

# 1. AUTO mode, the owner's line: the ✦ echo must have NO "command not found" note under it.
t 'another session like that, same issue?'
sleep 0.5
k Return
sleep 12
shot 01-auto-owner-line

# 2. AUTO mode, a real typo: "command not found: gti" must still appear.
t 'gti status'
sleep 0.5
k Return
sleep 12
shot 02-auto-typo-keeps-note

# 3. AGENT mode (Ctrl+I cycles the composer chip auto → terminal → agent): prose prints no note.
k ctrl+i
sleep 1
k ctrl+i
sleep 1
shot 03-mode-chip
t 'same issue as before?'
sleep 0.5
k Return
sleep 12
shot 04-agent-mode-prose

echo "screenshots and OCR in $out"
