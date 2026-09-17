#!/usr/bin/env bash
# Live check that a tool's output collapses in the pane (SWITCHBOARD-DESIGN.md 4.3), under
# Xvfb + xdotool. The agent runs `seq 1 200`; the pane must show the ⚙ call line and a result
# line carrying the size, not 200 lines of output.
#
#   docs/qa_evidence/2026-09-17-collapsed-tool-output/drive.sh [build-dir]
#
# Writes implementer-NN-*.png next to this script plus relay-stderr.log. Needs Xvfb, xdotool and
# ImageMagick `import`. An isolated XDG_CONFIG_HOME/XDG_DATA_HOME keeps the run out of the real
# profile. The agent talks to stub-provider.py on 127.0.0.1 only: no network, no keys.
set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"
out=$PWD
root=$(cd ../../.. && pwd)
build=${1:-$root/build}
port=8732
# Pick a display nobody else holds. Relay's QA scripts are run side by side on this machine, and a
# taken display is the dangerous case: Xvfb exits, the app lands on someone else's X server and
# xdotool types into their window. Refuse to drive a display we did not create.
display=
for n in $(seq 120 160); do
    [[ -e /tmp/.X11-unix/X$n ]] && continue
    display=:$n
    break
done
[[ -z $display ]] && { echo "no free X display in 120..160"; exit 1; }

export XDG_CONFIG_HOME=$(mktemp -d) XDG_DATA_HOME=$(mktemp -d) XDG_CACHE_HOME=$(mktemp -d)
export RELAY_KEYRING=off
work=$(mktemp -d)
mkdir -p "$XDG_CONFIG_HOME/RelayTerminal"
cat >"$XDG_CONFIG_HOME/RelayTerminal/relay.conf" <<CONF
[instructions]
onboarded=true
[terminal]
shell_integration=true
[provider]
preset=custom
base=http://127.0.0.1:$port/v1
model=relay-qa-stub
extra={}
max_tokens=1024
CONF
trap 'kill "${relay_pid:-0}" "${stub_pid:-0}" "${xvfb_pid:-0}" 2>/dev/null; rm -rf "$XDG_CONFIG_HOME" "$XDG_DATA_HOME" "$XDG_CACHE_HOME" "$work"' EXIT

python3 "$out/stub-provider.py" "$port" &
stub_pid=$!
sleep 1

Xvfb "$display" -screen 0 1400x800x24 >/dev/null 2>&1 &
xvfb_pid=$!
sleep 2
kill -0 "$xvfb_pid" 2>/dev/null || { echo "Xvfb died on $display (taken?)"; exit 1; }
export DISPLAY=$display
# Nothing but this run may own the display, or xdotool could drive another session's window.
xdotool search --name . 2>/dev/null | grep -q . && { echo "$display already has windows"; exit 1; }

shot() {
    import -window root "$out/implementer-$1.png"
    kill -0 "${relay_pid:-0}" 2>/dev/null || echo "relay is gone before $1"
}
t() { xdotool type --delay 14 "$1"; }
k() { xdotool key --delay 45 "$@"; }

"$build/relay" --workspace "$work" >"$out/relay-stderr.log" 2>&1 &
relay_pid=$!
sleep 6
win=$(xdotool search --pid "$relay_pid" --name Relay | tail -1)
[[ -z $win ]] && { echo "no Relay window"; exit 1; }
xdotool windowmove "$win" 0 0 windowsize "$win" 1400 800
xdotool windowfocus "$win"
xdotool mousemove 700 400
sleep 2

# ---- point the pane at the loopback stub (palette -> Provider / BYOK…) --------------------
# The settings file holds the base URL and model; the dialog still needs a non-empty key (an empty
# one means "use the keyring", and this run has none) and the consent box. The placeholder is not a
# credential: the stub ignores the Authorization header.
k ctrl+shift+a; sleep 1
t 'Provider'; sleep 1
k Return; sleep 2
k Tab Tab Tab; sleep 0.3
t 'loopback-stub-not-a-key'; sleep 0.3
k shift+Tab shift+Tab shift+Tab shift+Tab shift+Tab shift+Tab; sleep 0.5
k space; sleep 0.5
k Return; sleep 4
shot 01-agent-ready

# ---- one agent turn whose tool prints 200 lines ------------------------------------------
xdotool windowfocus "$win"
t 'count to two hundred'; sleep 0.5
k ctrl+Return; sleep 10
shot 02-collapsed-tool-output

echo "wrote $out/implementer-*.png"
