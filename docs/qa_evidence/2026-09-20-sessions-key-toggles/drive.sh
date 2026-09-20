#!/usr/bin/env bash
# Ctrl+Shift+Y toggles the session manager: open, close, open again.
set -euo pipefail

root=$(cd "$(dirname "$0")/../../.." && pwd)
build=${1:-"$root/build"}
out=$(cd "$(dirname "$0")" && pwd)
jail=$(mktemp -d /tmp/systog.XXXXXX)
display=:207
export DISPLAY=$display HOME="$jail/home" XDG_CONFIG_HOME="$jail/config" XDG_DATA_HOME="$jail/data"
export XDG_CACHE_HOME="$jail/cache" XDG_RUNTIME_DIR="$jail/run" TMPDIR="$jail/tmp" RELAY_KEYRING=off
mkdir -p "$HOME" "$XDG_CONFIG_HOME/RelayTerminal" "$XDG_DATA_HOME" "$XDG_CACHE_HOME" "$XDG_RUNTIME_DIR" "$TMPDIR"
chmod 700 "$XDG_RUNTIME_DIR"
cat >"$XDG_CONFIG_HOME/RelayTerminal/relay.conf" <<'CONF'
[instructions]
onboarded=true
CONF

cleanup() {
    kill "${relay_pid:-}" "${xvfb_pid:-}" 2>/dev/null || true
    rm -rf "$jail"
}
trap cleanup EXIT

Xvfb "$display" -screen 0 1600x900x24 >"$out/xvfb.log" 2>&1 & xvfb_pid=$!
sleep 1
"$build/relay" --clean-shell --fresh >"$out/relay.log" 2>&1 & relay_pid=$!
sleep 6
win=$(xdotool search --onlyvisible --name Relay | head -n 1)
xdotool windowsize "$win" 1500 850; sleep 1
import -window "$win" "$out/implementer-0-before.png"

xdotool key --window "$win" ctrl+shift+y; sleep 2
import -window "$win" "$out/implementer-1-open.png"

# Second press, with the manager focused: it closes and the focus goes back to the pane.
xdotool key --window "$win" ctrl+shift+y; sleep 2
import -window "$win" "$out/implementer-2-closed.png"

# Third press: open again, to show the toggle is not a one-shot.
xdotool key --window "$win" ctrl+shift+y; sleep 2
import -window "$win" "$out/implementer-3-open-again.png"

# Focus back in the terminal pane: the key must bring the open manager forward, not close a pane
# the user is not looking at. Click the terminal's output, then press it once.
xdotool mousemove --window "$win" 300 200 click 1; sleep 1
import -window "$win" "$out/implementer-4-focus-in-pane.png"
xdotool key --window "$win" ctrl+shift+y; sleep 2
import -window "$win" "$out/implementer-5-brought-forward.png"
# And now, with it focused again, the next press closes it.
xdotool key --window "$win" ctrl+shift+y; sleep 2
import -window "$win" "$out/implementer-6-closed-again.png"
