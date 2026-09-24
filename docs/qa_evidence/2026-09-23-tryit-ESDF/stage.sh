#!/usr/bin/env bash
# Stage the Try-it situation for #ESDF: a disposable project with a Switchboard holding a day of
# work over every stage, then a Relay window on it under Xvfb, board open, left where the owner
# lands: the flat Recent list. Rerunnable; no network; everything disposable.
#
#   ./stage.sh            # stage the project and leave Relay running (see staging-notes.md)
#   ./stage.sh --stop     # take the staged Relay down (leaves the project and the captures)
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
driver=$(cd "$here/../../.." && pwd)/scripts/relay-drive
work=${RELAY_QA_DIR:-$HOME/relay-qa/esdf-notes}
stop=
[[ "${1:-}" == "--stop" ]] && stop=1

if [[ -n $stop ]]; then
    for pidfile in esdf-relay.pid esdf-xvfb.pid; do
        if [[ -f /tmp/claude-1000/tryit/esdf/run/$pidfile ]]; then
            kill -TERM "$(cat /tmp/claude-1000/tryit/esdf/run/$pidfile)" 2>/dev/null || true
        fi
    done
    echo "staged Relay stopped (project left at $work)"
    exit 0
fi

python3 "$here/stage.py" --dir "$work"

bin=${RELAY_BIN:?set RELAY_BIN to a relay binary (the clean-export build of main)}
width=1600 height=1000
display=; for n in $(seq 200 299); do [[ -e /tmp/.X11-unix/X$n ]] || { display=:$n; break; }; done
[[ -n $display ]] || { echo "no free X display" >&2; exit 1; }
sandbox=/tmp/claude-1000/tryit/esdf
rm -rf "$sandbox"; mkdir -p "$sandbox/home/.config/RelayTerminal" "$sandbox/run" "$sandbox/tmp"; chmod 700 "$sandbox/run"
ln -sfn "/run/user/$(id -u)/bus" "$sandbox/run/bus"
Xvfb "$display" -screen 0 $((width+40))x$((height+40))x24 >/dev/null 2>&1 &
echo $! > "$sandbox/run/esdf-xvfb.pid"
echo "${display#:}" > "$sandbox/run/display"
sleep 2
export DISPLAY=$display RELAY_KEYRING=off
unset RELAY_OPEN_SOCKET   # discover only this disposable instance, never the caller's app
export HOME=$sandbox/home XDG_RUNTIME_DIR=$sandbox/run TMPDIR=$sandbox/tmp XDG_CONFIG_HOME=$sandbox/home/.config XDG_DATA_HOME=$sandbox/home/.local/share XDG_CACHE_HOME=$sandbox/home/.cache
printf '[instructions]\nonboarded=true\n[security]\napprovals_chosen=true\n' > "$XDG_CONFIG_HOME/RelayTerminal/relay.conf"

(cd "$work" && exec "$bin" --workspace "$work" --clean-shell --fresh) > "$sandbox/relay.log" 2>&1 &
echo $! > "$sandbox/run/esdf-relay.pid"
sleep 12
# Board open, left in the owner's hands: the flat Recent list of the staged day.
"$driver" action board.open
sleep 3
echo "Relay is up on $display (pid $(cat "$sandbox/run/esdf-relay.pid")); board open on $work"
echo "stop it with: $0 --stop"
