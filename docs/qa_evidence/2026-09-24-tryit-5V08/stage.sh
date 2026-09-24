#!/usr/bin/env bash
set -euo pipefail
repo=$(cd "$(dirname "$0")/../../.." && pwd)
work=/tmp/claude-1000/tryit/5v08
mkdir -p "$work/project" "$work/home/.config/RelayTerminal" "$work/run" "$work/tmp" "$work/home/.local/share" "$work/home/.cache"
chmod 700 "$work/run"
if [[ ! -f "$work/project/sample.docx" ]]; then
    cp "$repo/tests/fixtures/relay_docx_edit.docx" "$work/project/sample.docx"
fi
printf '[instructions]\nonboarded=true\n[security]\napprovals_chosen=true\n' > "$work/home/.config/RelayTerminal/relay.conf"
ln -sfn "/run/user/$(id -u)/bus" "$work/run/bus"
export HOME="$work/home" XDG_RUNTIME_DIR="$work/run" TMPDIR="$work/tmp"
export XDG_CONFIG_HOME="$work/home/.config" XDG_DATA_HOME="$work/home/.local/share" XDG_CACHE_HOME="$work/home/.cache"
export RELAY_KEYRING=off
unset RELAY_OPEN_SOCKET
if [[ ! -f "$work/relay.pid" ]] || ! kill -0 "$(cat "$work/relay.pid")" 2>/dev/null; then
    "$repo/build-fast/relay" --workspace "$work/project" --clean-shell --fresh > "$work/relay.log" 2>&1 &
    echo $! > "$work/relay.pid"
fi
for _ in $(seq 1 40); do
    [[ -s "$work/run/relay/open-socket" ]] && break
    sleep 0.5
done
python3 - "$work/run/relay/open-socket" "$work/project/sample.docx" <<'PY'
import json, socket, sys
address = open(sys.argv[1], encoding="utf-8").read().strip()
with socket.socket(socket.AF_UNIX) as connection:
    connection.settimeout(5)
    connection.connect(address)
    connection.sendall((json.dumps({"path": sys.argv[2], "line": 0}) + "\n").encode())
    reply = connection.recv(256).strip()
if reply != b"ok":
    raise SystemExit(f"Relay did not open the DOCX: {reply!r}")
PY
printf 'Opened %s in the isolated Relay window.\n' "$work/project/sample.docx"
