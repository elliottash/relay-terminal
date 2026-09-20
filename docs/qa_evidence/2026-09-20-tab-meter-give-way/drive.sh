#!/usr/bin/env bash
# #VWSD implementer capture: five long tab names across the suffix give-way threshold.
set -euo pipefail

root=$(cd "$(dirname "$0")/../../.." && pwd)
build=${1:-"$root/build"}
out=$(cd "$(dirname "$0")" && pwd)
jail=$(mktemp -d /tmp/vwsd.XXXXXX)
display=:204
export DISPLAY=$display HOME="$jail/home" XDG_CONFIG_HOME="$jail/config" XDG_DATA_HOME="$jail/data"
export XDG_CACHE_HOME="$jail/cache" XDG_RUNTIME_DIR="$jail/run" TMPDIR="$jail/tmp" RELAY_KEYRING=off
mkdir -p "$HOME" "$XDG_CONFIG_HOME/RelayTerminal" "$XDG_DATA_HOME" "$XDG_CACHE_HOME" "$XDG_RUNTIME_DIR" "$TMPDIR"
chmod 700 "$XDG_RUNTIME_DIR"
cat >"$XDG_CONFIG_HOME/RelayTerminal/relay.conf" <<'CONF'
[instructions]
onboarded=true
[appearance]
pane_usage=true
CONF

cleanup() {
    kill "${relay_pid:-}" "${xvfb_pid:-}" 2>/dev/null || true
    rm -rf "$jail"
}
trap cleanup EXIT

Xvfb "$display" -screen 0 2800x760x24 >"$out/xvfb.log" 2>&1 & xvfb_pid=$!
sleep 1
"$build/relay" --clean-shell --fresh >"$out/relay.log" 2>&1 & relay_pid=$!
sleep 3
win=$(xdotool search --onlyvisible --name Relay | head -n 1)

# Each new tab is renamed through the normal prompt command; the names are deliberately longer
# than a narrow strip can afford alongside the fixed-width readings.
for name in "Investigating terminal rendering regression" "Writing migration documentation notes" \
            "Reviewing network timeout behaviour" "Preparing release candidate checklist"; do
    xdotool key ctrl+t; sleep .5
    xdotool type --delay 2 "/rename-tab $name"; xdotool key Return; sleep .7
done
sleep 6

for width in 2600 1500 1200 900 760 640 520 420; do
    xdotool windowsize "$win" "$width" 700; sleep 1
    import -window "$win" "$out/implementer-${width}.png"
done

# At the narrow size the 5 s clock can update its cached reading without putting it back into a
# label; the second take makes that steady state inspectable.
sleep 6
import -window "$win" "$out/implementer-420-after-clock.png"
xdotool windowsize "$win" 2600 700; sleep 1
import -window "$win" "$out/implementer-2600-restored.png"
