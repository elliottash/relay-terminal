#!/usr/bin/env bash
# Live implementer evidence for card #62M4. Runs Relay under Xvfb with an isolated profile,
# starts sharing the only pane, closes the pairing dialog, and captures the pane header.
set -euo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")"
out=$PWD
root=$(cd ../../.. && pwd)
width=1200
height=760

display=
for n in $(seq 240 270); do
    [[ -e /tmp/.X11-unix/X$n ]] || { display=:$n; break; }
done
[[ -n $display ]] || { echo "no free X display"; exit 1; }

sandbox=$(mktemp -d /tmp/relay-62m4-XXXXXX)
xvfb_pid=
relay_pid=
cleanup() {
    kill "${relay_pid:-0}" "${xvfb_pid:-0}" 2>/dev/null || true
    rm -rf "$sandbox"
}
trap cleanup EXIT

export HOME=$sandbox/home
export XDG_CONFIG_HOME=$HOME/.config
export XDG_DATA_HOME=$HOME/.local/share
export XDG_CACHE_HOME=$HOME/.cache
export XDG_RUNTIME_DIR=$HOME/run
export TMPDIR=$HOME/tmp
export RELAY_KEYRING=off
export RELAY_REMOTE_PAIR_FILE=$sandbox/pair-url.txt
mkdir -p "$XDG_CONFIG_HOME/RelayTerminal" "$XDG_DATA_HOME" "$XDG_CACHE_HOME" \
         "$XDG_RUNTIME_DIR" "$TMPDIR" "$sandbox/work"
chmod 700 "$XDG_RUNTIME_DIR"
cat >"$XDG_CONFIG_HOME/RelayTerminal/relay.conf" <<'CONF'
[instructions]
onboarded=true
[suggestions]
next_command=false
next_prompt=false
CONF

Xvfb "$display" -screen 0 $((width + 40))x$((height + 40))x24 >/dev/null 2>&1 &
xvfb_pid=$!
sleep 1
export DISPLAY=$display

"$root/build/relay" --workspace "$sandbox/work" --clean-shell --fresh >"$sandbox/relay.log" 2>&1 &
relay_pid=$!
sleep 7
win=$(for candidate in $(xdotool search --onlyvisible --pid "$relay_pid" --name '^Relay' 2>/dev/null); do
    eval "$(xdotool getwindowgeometry --shell "$candidate" 2>/dev/null)"
    echo "$(( ${WIDTH:-0} * ${HEIGHT:-0} )) $candidate"
done | sort -rn | head -1 | cut -d' ' -f2)
[[ -n $win ]] || { tail -30 "$sandbox/relay.log"; exit 1; }

xdotool windowmove "$win" 0 0 windowsize "$win" "$width" "$height" windowfocus "$win"
sleep 1
import -window "$win" "$out/icon-only.png"

# The share control is beside the workspace path at the lower left of the prompt strip.
xdotool mousemove --window "$win" 266 $((height - 34)) click 1
for _ in $(seq 1 20); do
    [[ -s $RELAY_REMOTE_PAIR_FILE ]] && break
    sleep 1
done
[[ -s $RELAY_REMOTE_PAIR_FILE ]] || { tail -30 "$sandbox/relay.log"; exit 1; }

dlg=$(xdotool search --onlyvisible --name 'Share this pane' 2>/dev/null | head -1 || true)
if [[ -n $dlg ]]; then
    xdotool windowfocus "$dlg" key Escape
fi
xdotool windowfocus "$win"
xdotool mousemove $((width + 20)) $((height + 20))
sleep 2
import -window "$win" "$out/icon-only.png"

echo "captured $out/icon-only.png"
