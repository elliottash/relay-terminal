#!/usr/bin/env bash
# A corrupt or foreign-version windows.json must not stop Relay from starting.
#   docs/qa_evidence/2026-09-17-restore-windows/broken-file.sh '{"version": 99, "windows": []}'
set -u
root=$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)
D=$(mktemp -d); C=$(mktemp -d); R=$(mktemp -d); chmod 700 "$R"
mkdir -p "$D/relay/state"
printf '%s' "$1" > "$D/relay/state/windows.json"
chmod 600 "$D/relay/state/windows.json"
export XDG_DATA_HOME=$D XDG_CONFIG_HOME=$C XDG_RUNTIME_DIR=$R XDG_CACHE_HOME=$(mktemp -d)
export QT_QPA_PLATFORM=offscreen RELAY_KEYRING=off RELAY_NO_URL_HANDLER=1
timeout 12 "$root/build/relay" 2>&1 
echo "--- state after:"
head -c 200 "$D/relay/state/windows.json"; echo
rm -rf "$D" "$C" "$R"
