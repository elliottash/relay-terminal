#!/usr/bin/env bash
# Colour themes (issue 0JA7), third live check: the chosen theme survives a restart.
#
#   docs/qa_evidence/2026-09-17-color-themes/drive-persists.sh [build-dir]
#
# Starts Relay with `theme/name=solarized-dark` already in relay.conf — which is what the picker
# writes — and checks that the chrome, the terminal and the generated relayrc all come up on it.
set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"
out=$PWD
root=$(cd ../../.. && pwd)
build=${1:-$root/build}
display=:98

export XDG_CONFIG_HOME=$(mktemp -d) XDG_DATA_HOME=$(mktemp -d) XDG_CACHE_HOME=$(mktemp -d)
work=$(mktemp -d)
mkdir -p "$XDG_CONFIG_HOME/RelayTerminal"
cat >"$XDG_CONFIG_HOME/RelayTerminal/relay.conf" <<'CONF'
[instructions]
onboarded=true

[theme]
name=solarized-dark
CONF
trap 'kill "${relay_pid:-0}" "${xvfb_pid:-0}" 2>/dev/null; rm -rf "$XDG_CONFIG_HOME" "$XDG_DATA_HOME" "$XDG_CACHE_HOME" "$work"' EXIT
echo "hello from the colour-theme QA run" >"$work/README.md"

Xvfb "$display" -screen 0 1600x1000x24 >/dev/null 2>&1 &
xvfb_pid=$!
sleep 1
export DISPLAY=$display

"$build/relay" --workspace "$work" >"$out/implementer-persist-stderr.log" 2>&1 &
relay_pid=$!
sleep 14
main=""
for id in $(xdotool search --pid "$relay_pid"); do
  w=$(xdotool getwindowgeometry --shell "$id" | sed -n 's/^WIDTH=//p')
  [[ ${w:-0} -gt 400 ]] && main=$id
done
[[ -z $main ]] && { echo "no Relay window"; exit 1; }
xdotool windowmove "$main" 0 0 windowsize "$main" 1600 1000 windowfocus "$main"
sleep 3
xdotool mousemove 700 930 click 1; sleep 1
xdotool type --delay 12 '!ls --color=always -la /usr/share/zoneinfo | head -10'
sleep 0.5; xdotool key Return; sleep 3
import -window root "$out/implementer-p01-solarized-on-start.png"
echo "generated relayrc: $(tail -1 "$XDG_CACHE_HOME/relay/theme/relayrc")"
echo "screenshots in $out"
