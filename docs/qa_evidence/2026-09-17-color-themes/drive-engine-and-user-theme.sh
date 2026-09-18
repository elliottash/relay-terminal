#!/usr/bin/env bash
# Colour themes (issue 0JA7), second live check: Relay's own terminal engine and a user theme.
#
#   docs/qa_evidence/2026-09-17-color-themes/drive-engine-and-user-theme.sh [build-dir]
#
# 1. Relay-engine panes (--engine=relay) switch colour in place, with no new pane.
# 2. A theme file dropped in ~/.config/relay/themes shows up in the picker and can be chosen.
#
# Writes implementer-e*.png next to this script. Same isolation as drive.sh.
set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"
out=$PWD
root=$(cd ../../.. && pwd)
build=${1:-$root/build}
display=:96

export XDG_CONFIG_HOME=$(mktemp -d) XDG_DATA_HOME=$(mktemp -d) XDG_CACHE_HOME=$(mktemp -d)
work=$(mktemp -d)
mkdir -p "$XDG_CONFIG_HOME/RelayTerminal" "$XDG_CONFIG_HOME/relay/themes"
printf '[instructions]\nonboarded=true\n' >"$XDG_CONFIG_HOME/RelayTerminal/relay.conf"
trap 'kill "${relay_pid:-0}" "${xvfb_pid:-0}" 2>/dev/null; rm -rf "$XDG_CONFIG_HOME" "$XDG_DATA_HOME" "$XDG_CACHE_HOME" "$work"' EXIT

echo "hello from the colour-theme QA run" >"$work/README.md"

# A user theme: only the keys it wants to change, the rest falls back to Relay Dark.
cat >"$XDG_CONFIG_HOME/relay/themes/qa-hot-pink.toml" <<'THEME'
[theme]
name = "QA Hot Pink"
variant = "dark"
description = "A user theme written by the QA run; only a few tokens are set."

[ui]
background = "#1a0714"
surface = "#260d1e"
surface_raised = "#341229"
border = "#572143"
border_strong = "#8c3a6d"
text = "#ffe6f4"
text_muted = "#c58fb0"
accent = "#ff4fa3"
accent_text = "#25030f"

[syntax]
command = "#ff4fa3"

[terminal]
background = "#1a0714"
foreground = "#ffe6f4"
THEME

Xvfb "$display" -screen 0 1600x1000x24 >/dev/null 2>&1 &
xvfb_pid=$!
sleep 1
export DISPLAY=$display

shot() { import -window root "$out/implementer-$1.png"; }
t() { xdotool type --delay 12 "$1"; }
k() { xdotool key --delay 40 "$@"; }
mainwin() {
  local m=""
  for id in $(xdotool search --pid "$relay_pid"); do
    local w
    w=$(xdotool getwindowgeometry --shell "$id" | sed -n 's/^WIDTH=//p')
    [[ ${w:-0} -gt 400 ]] && m=$id
  done
  echo "$m"
}

RELAY_ENGINE=relay "$build/relay" --workspace "$work" >"$out/implementer-engine-stderr.log" 2>&1 &
relay_pid=$!
sleep 14
win=$(mainwin)
[[ -z $win ]] && { echo "no Relay window"; exit 1; }
xdotool windowmove "$win" 0 0 windowsize "$win" 1600 1000 windowfocus "$win"
sleep 3
xdotool mousemove 700 930 click 1; sleep 1
t '!ls --color=always -la /usr/share/zoneinfo | head -10'
sleep 0.5; k Return; sleep 3
shot e01-engine-relay-dark

k ctrl+comma; sleep 4
settings=$(xdotool search --name "Settings" | tail -1)
xdotool windowmove "$settings" 700 60; sleep 1.5
xdotool mousemove 740 106 click 1; sleep 1.5
shot e02-engine-picker-with-user-theme

choose() {   # choose <index from the top> <shot name>
  xdotool mousemove 1388 183 click 1
  sleep 1.2
  k Home; sleep 0.4
  local i
  for ((i = 0; i < $1; i++)); do k Down; sleep 0.25; done
  k Return
  sleep 3
  shot "$2"
}

# Ids sorted: gruvbox-dark, qa-hot-pink, relay-dark, relay-light, solarized-dark.
choose 3 e03-engine-relay-light
choose 1 e04-engine-user-theme
k Escape; sleep 1.5
xdotool windowfocus "$win"; sleep 1
shot e05-engine-user-theme-main

kill -0 "$relay_pid" 2>/dev/null && echo "relay pid $relay_pid never restarted" || echo "WARNING: relay exited"
echo "screenshots in $out"
