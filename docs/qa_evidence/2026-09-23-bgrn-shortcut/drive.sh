#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../../.."
evidence_dir="$PWD/docs/qa_evidence/2026-09-23-bgrn-shortcut"
qa_root=$(mktemp -d)
trap 'kill "${relay_pid:-}" 2>/dev/null || true; rm -rf "$qa_root"' EXIT
mkdir -p "$qa_root"/{config,data,cache,runtime,tmp,workspace}
chmod 700 "$qa_root/runtime"
export XDG_CONFIG_HOME="$qa_root/config"
export XDG_DATA_HOME="$qa_root/data"
export XDG_CACHE_HOME="$qa_root/cache"
export XDG_RUNTIME_DIR="$qa_root/runtime"
export TMPDIR="$qa_root/tmp"
export QT_QPA_PLATFORM=xcb
./build/relay --fresh --clean-shell --workspace "$qa_root/workspace" >"$qa_root/relay.log" 2>&1 &
relay_pid=$!
for _ in $(seq 1 40); do
    socket=$(find "$TMPDIR" -path '*/relay-open-*/open.sock' -type s -print -quit)
    if [[ -n "$socket" ]]; then break; fi
    kill -0 "$relay_pid"
    sleep 0.25
done
test -n "${socket:-}"
scripts/relay-drive --socket "$socket" panes >"$evidence_dir/panes.json"
sleep 2
# A disposable first-run profile may show the instruction import dialog and Models pane.
xdotool mousemove 825 623 click 1
sleep 1
xdotool mousemove 1122 59 click 1
sleep 1
import -window root "$evidence_dir/composer.png"
sleep 12
xdotool mousemove 140 785 click 1 type --clearmodifiers 'background shortcut check'
xdotool key ctrl+alt+Return
sleep 2
import -window root "$evidence_dir/shortcut.png"
