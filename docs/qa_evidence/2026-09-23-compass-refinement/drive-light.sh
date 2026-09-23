#!/usr/bin/env bash
# Open Actions in an isolated Relay under Xvfb and capture the Compass and a group menu.
set -euo pipefail
cd "$(dirname "$0")"
evidence_dir=$PWD
repo_root=$(cd ../../.. && pwd)
display_number=
for number in $(seq 640 679); do
    if [[ ! -e /tmp/.X11-unix/X$number ]]; then display_number=:$number; break; fi
done
[[ -n $display_number ]]
profile_dir=$(mktemp -d /tmp/relay-compass.XXXXXX)
Xvfb "$display_number" -screen 0 1440x960x24 >"$profile_dir/xvfb.log" 2>&1 &
xvfb_pid=$!
relay_pid=
cleanup() {
    [[ -z $relay_pid ]] || kill "$relay_pid" 2>/dev/null || true
    kill "$xvfb_pid" 2>/dev/null || true
    rm -rf "$profile_dir"
}
trap cleanup EXIT
sleep 2
export DISPLAY=$display_number RELAY_KEYRING=off
unset RELAY_OPEN_SOCKET
export HOME=$profile_dir/home XDG_RUNTIME_DIR=$profile_dir/run TMPDIR=$profile_dir/tmp
export XDG_CONFIG_HOME=$HOME/.config XDG_DATA_HOME=$HOME/.local/share XDG_CACHE_HOME=$HOME/.cache
mkdir -p "$HOME" "$XDG_RUNTIME_DIR" "$TMPDIR" "$XDG_CONFIG_HOME/RelayTerminal" "$XDG_DATA_HOME" "$XDG_CACHE_HOME" "$HOME/project"
chmod 700 "$XDG_RUNTIME_DIR"
cat >"$XDG_CONFIG_HOME/RelayTerminal/relay.conf" <<'CONF'
[instructions]
onboarded=true
[theme]
name=relay-light
[security]
approvals_chosen=true
[url_handler]
announced=true
CONF
(cd "$HOME/project" && exec "$repo_root/build/relay" --workspace "$HOME/project" --fresh --engine-core libvterm) >"$profile_dir/relay.log" 2>&1 &
relay_pid=$!
sleep 8
window_id= best=0
for candidate in $(xdotool search --pid "$relay_pid" 2>/dev/null); do
    eval "$(xdotool getwindowgeometry --shell "$candidate" 2>/dev/null)"
    if (( WIDTH * HEIGHT > best )); then best=$(( WIDTH * HEIGHT )); window_id=$candidate; fi
done
[[ -n $window_id ]] || { cat "$profile_dir/relay.log"; exit 1; }
xdotool windowmove "$window_id" 0 0 windowsize "$window_id" 1400 900 windowfocus "$window_id"
sleep 1
xdotool key ctrl+question
sleep 1
import -window root -crop 1400x900+0+0 "$evidence_dir/04-light.png"
printf 'captured light Compass evidence in %s\n' "$evidence_dir"
