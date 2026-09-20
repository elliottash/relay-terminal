#!/bin/bash
# launch.sh <display> <workspace> <tag> [extra relay args...]
# Starts one detached Relay in an isolated environment and prints "<pid> <window-id>".
set -u
R=$(dirname "$(readlink -f "$0")")
# shellcheck disable=SC1091
source "$R/env.sh" "$1" "$2" "$3"
shift 3
cd "$WS" || exit 1
setsid nohup "$RELAY_BIN" --workspace "$WS" --fresh --clean-shell "$@" \
    >"$H/relay.out" 2>&1 </dev/null &
disown
for _ in $(seq 60); do
    sleep 0.5
    pid=$(pgrep -f "relay --workspace $WS " | head -1)
    [ -n "$pid" ] || continue
    win=$(xdotool search --name "^Relay — " 2>/dev/null | head -1)
    [ -n "$win" ] && { echo "$pid $win"; exit 0; }
done
echo "FAILED" >&2
exit 1
