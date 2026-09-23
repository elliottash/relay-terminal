#!/usr/bin/env bash
set -euo pipefail

repo=$(cd "$(dirname "$0")/../../.." && pwd)
fixture=/tmp/claude-1000/tryit/BKMC
mkdir -p "$fixture/project" "$fixture/home/.config" "$fixture/home/.local/share" \
    "$fixture/home/.cache" "$fixture/home/.config/RelayTerminal" "$fixture/run" "$fixture/tmp"
chmod 700 "$fixture/run"
printf 'A disposable workspace for trying Ctrl+Shift+Z.\n' > "$fixture/project/README.txt"
printf '[instructions]\nonboarded=true\n[security]\napprovals_chosen=true\n' \
    > "$fixture/home/.config/RelayTerminal/relay.conf"

if [[ -s "$fixture/app.pid" ]]; then
    old_pid=$(cat "$fixture/app.pid")
    if [[ -r "/proc/$old_pid/cmdline" ]]; then
        old_command=$(tr '\0' ' ' < "/proc/$old_pid/cmdline")
        if [[ "$old_command" == *"$fixture/project"* ]]; then
            kill "$old_pid" 2>/dev/null || true
            sleep 0.25
        fi
    fi
fi
rm -f "$fixture/run/relay/open-socket"
unset RELAY_OPEN_SOCKET
export HOME="$fixture/home"
export XDG_RUNTIME_DIR="$fixture/run"
export TMPDIR="$fixture/tmp"
export XDG_CONFIG_HOME="$fixture/home/.config"
export XDG_DATA_HOME="$fixture/home/.local/share"
export XDG_CACHE_HOME="$fixture/home/.cache"
export RELAY_KEYRING=off
nohup "$repo/build-fast/relay" --workspace "$fixture/project" --clean-shell --fresh \
    > "$fixture/relay.log" 2>&1 < /dev/null &
app_pid=$!
printf '%s\n' "$app_pid" > "$fixture/app.pid"

socket_file="$XDG_RUNTIME_DIR/relay/open-socket"
for _ in $(seq 1 80); do
    if [[ -s "$socket_file" ]]; then
        socket=$(cat "$socket_file")
        if [[ -S "$socket" ]] && "$repo/scripts/relay-drive" --socket "$socket" panes >/dev/null 2>&1; then
            "$repo/scripts/relay-drive" --socket "$socket" action pane.splitRight >/dev/null
            printf 'Opened isolated Relay with a spare pane: %s\n' "$repo/build-fast/relay"
            exit 0
        fi
    fi
    kill -0 "$app_pid" 2>/dev/null || { cat "$fixture/relay.log" >&2; exit 1; }
    sleep 0.25
done
printf 'Relay did not open its local socket.\n' >&2
exit 1
