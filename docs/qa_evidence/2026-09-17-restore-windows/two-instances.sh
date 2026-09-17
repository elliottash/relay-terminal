#!/usr/bin/env bash
# Two Relays at once: only the lock owner writes the saved layout. Run from anywhere.
#   docs/qa_evidence/2026-09-17-restore-windows/two-instances.sh
set -u
root=$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)
D=$(mktemp -d); C=$(mktemp -d); R=$(mktemp -d); K=$(mktemp -d); chmod 700 "$R"
export XDG_DATA_HOME=$D XDG_CONFIG_HOME=$C XDG_RUNTIME_DIR=$R XDG_CACHE_HOME=$K
export QT_QPA_PLATFORM=offscreen RELAY_KEYRING=off RELAY_NO_URL_HANDLER=1
state=$D/relay/state/windows.json

"$root/build/relay" >/dev/null 2>&1 &
a=$!
sleep 8
echo "A saved: $(python3 -c "import json,sys;print(len(json.load(open(sys.argv[1]))['windows']))" "$state") window(s)"
ls -l "$R/relay/windows.lock" >/dev/null 2>&1 && echo "lock file exists"

# Mark the file so a write by B is visible: duplicate A's window record.
python3 - "$state" <<'PY'
import json, sys
s = json.load(open(sys.argv[1]))
s["windows"].append(json.loads(json.dumps(s["windows"][0])))
json.dump(s, open(sys.argv[1], "w"))
PY
echo "marked: $(python3 -c "import json,sys;print(len(json.load(open(sys.argv[1]))['windows']))" "$state") window(s)"

"$root/build/relay" >/dev/null 2>&1 &
b=$!
sleep 9
kill -TERM "$b"; wait "$b" 2>/dev/null
sleep 1
echo "after B ran and quit: $(python3 -c "import json,sys;print(len(json.load(open(sys.argv[1]))['windows']))" "$state") window(s)  (2 = B left it alone)"

kill -TERM "$a"; wait "$a" 2>/dev/null
rm -rf "$D" "$C" "$R" "$K"
