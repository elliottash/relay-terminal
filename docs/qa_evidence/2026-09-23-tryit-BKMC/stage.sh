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

printf 'Opening isolated Relay with %s\n' "$repo/build-fast/relay"
unset RELAY_OPEN_SOCKET
export HOME="$fixture/home"
export XDG_RUNTIME_DIR="$fixture/run"
export TMPDIR="$fixture/tmp"
export XDG_CONFIG_HOME="$fixture/home/.config"
export XDG_DATA_HOME="$fixture/home/.local/share"
export XDG_CACHE_HOME="$fixture/home/.cache"
export RELAY_KEYRING=off
exec "$repo/build-fast/relay" --workspace "$fixture/project" --clean-shell --fresh
