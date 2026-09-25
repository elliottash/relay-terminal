#!/usr/bin/env bash
# Stage the Try it situation for card #4DS8 (Options › Voice: Microphone is a dropdown of this
# machine's capture sources). Runs a disposable Relay on a private Xvfb display, whose only wires
# to this machine are the session bus and the PipeWire socket — the socket is what lets the
# dropdown enumerate the real sources — then walks the row with the keyboard alone:
#
#   app.settings -> type "microphone" -> Return (opens the dropdown) -> Down+Return (chooses it)
#
# Settings tabs and rows have no named drive control (only the Board surface does), so the walk
# uses the Options pane's own keyboard model (search opens focused; Return activates a row; a
# Choice row's activate opens its popup). No coordinate input, no terminal-pane input.
#
# One screenshot per step lands in this directory, plus settings-after.txt, which shows what the
# dropdown wrote. Rerunnable: every run rebuilds the sandbox from scratch and kills it afterwards.
# No model, no network.
#
#   ./stage.sh [RELAY_BIN=<relay>] [out dir]
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
repo=$(cd "$here/../../.." && pwd)
driver=$repo/scripts/relay-drive
bin=${RELAY_BIN:-$repo/build/relay}
out=${1:-$here}
mkdir -p "$out"; width=1600 height=1000

display=; for n in $(seq 170 199); do [[ -e /tmp/.X11-unix/X$n ]] || { display=:$n; break; }; done
[[ -n $display ]] || { echo "no free display" >&2; exit 1; }
sandbox=$(mktemp -d /tmp/rl-4ds8.XXXX)
cleanup() {
    for pid in ${relay_pid:-} ${xvfb_pid:-}; do kill -TERM "$pid" 2>/dev/null; done
    sleep 2; rm -rf "$sandbox"
}
trap cleanup EXIT
Xvfb "$display" -screen 0 $((width+40))x$((height+40))x24 >/dev/null 2>&1 & xvfb_pid=$!; sleep 2
export DISPLAY=$display RELAY_KEYRING=off
unset RELAY_OPEN_SOCKET   # discover only this disposable instance, never the caller's app
mkdir -p "$sandbox/home/.config/RelayTerminal" "$sandbox/run" "$sandbox/tmp" "$sandbox/project"
chmod 700 "$sandbox/run"
ln -sfn "/run/user/$(id -u)/bus" "$sandbox/run/bus"
# The Microphone row enumerates through the live PipeWire socket (this machine has pw-record but
# no pactl, so captureDevices goes via pw-dump, which speaks to this socket).
ln -sfn "/run/user/$(id -u)/pipewire-0" "$sandbox/run/pipewire-0"
export HOME=$sandbox/home XDG_RUNTIME_DIR=$sandbox/run TMPDIR=$sandbox/tmp
export XDG_CONFIG_HOME=$sandbox/home/.config XDG_DATA_HOME=$sandbox/home/.local/share
export XDG_CACHE_HOME=$sandbox/home/.cache
printf '[instructions]\nonboarded=true\n[security]\napprovals_chosen=true\n' \
    > "$XDG_CONFIG_HOME/RelayTerminal/relay.conf"

(cd "$sandbox/project" && exec "$bin" --workspace "$sandbox/project" --clean-shell --fresh) \
    > "$out/relay.log" 2>&1 & relay_pid=$!
sleep 12

shot() { import -window root "$out/$1.png"; }

# 1. Options open with the search focused.
"$driver" action app.settings; sleep 2
# 2. The search finds the row: label "Microphone".
xdotool type --delay 45 'microphone'; sleep 1
shot 10-options-search
# 3. Return activates the first matching row; a Choice row's activate opens its dropdown.
xdotool key Return; sleep 1
shot 11-dropdown-open
# 4. Down + Return chooses the first real source: the row writes voice/device and redraws.
xdotool key Down; sleep 1
xdotool key Return; sleep 2
shot 12-chosen
# 5. What the dropdown wrote (the sandbox settings file, not the caller's).
{
    echo '[voice] section of the sandbox relay.conf:'
    awk '/^\[voice\]$/{s=1; print; next} s && /^\[/{exit} s{print}' \
        "$XDG_CONFIG_HOME/RelayTerminal/relay.conf"
} > "$out/settings-after.txt"
cp "$XDG_CONFIG_HOME/RelayTerminal/relay.conf" "$out/sandbox-relay.conf.txt"
echo "staged in $out (relay pid $relay_pid leaves with this script)"
