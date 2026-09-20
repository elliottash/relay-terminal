#!/usr/bin/env bash
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# The whole of card #GH5T's evidence, in one run. No network, no keyring, no real provider and no
# real guest: `run-worker.py` replaces the guest-harness seam with the test fake (29.3), the only
# endpoint that ever answers is `stub-provider.py` on loopback, and every key is an environment
# variable with "stub" in it.
#
#   ./drive.sh            # writes logs/ beside this script
#
# Run it against a checkout that has the fix to see the "after" transcripts; run steps 1 only
# against the parent of the fix commit to see the failure the owner reported.
set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"
HERE="$PWD"
OUT="$HERE/logs"
rm -rf "$OUT"; mkdir -p "$OUT"
SCRATCH="$(mktemp -d)"
trap 'rm -rf "$SCRATCH"' EXIT

PROJ="$SCRATCH/proj"
mkdir -p "$PROJ/.switchboard/features" "$PROJ/.switchboard/threads"
cat > "$PROJ/.switchboard/board.yaml" <<'YAML'
version: 1
tabs: [{id: features, folder: features}, {id: done, filter: 'status:done,dropped'}]
columns: [inbox, executing, done]
agent: {autonomy: auto, max_creates_per_turn: 5}
memory: {autonomy: auto}
YAML
cat > "$PROJ/.switchboard/features/2026-09-20-a-fixture-card.md" <<'CARD'
---
id: FX01
type: work
status: inbox
labels: [feature]
rank: m
created: '2026-09-20'
source: 'fixture'
links: {plans: [], commits: [], evidence: [], related: [], github: null}
---
# A fixture card for the helper-on-guest repro

## Issue
a fixture
CARD

cat > "$SCRATCH/local-models.json" <<JSON
{"endpoints": [{"id": "local:stub", "label": "stub (local)",
                "base_url": "http://127.0.0.1:8842/v1", "model": "stub-model",
                "server": "openai-compatible", "context_window": 32768, "tools": true}]}
JSON

export HOME="$SCRATCH/home" XDG_DATA_HOME="$SCRATCH/home/data" XDG_CONFIG_HOME="$SCRATCH/home/config"
export RELAY_KEYRING=off RELAY_LOCAL_MODELS="$SCRATCH/local-models.json"
mkdir -p "$HOME"

# `configure` as the tab's helper: the window's guest preset and `agent_role: "switchboard"`.
helper() {   # helper <fallbacks json> [extra configure fields json]
  printf '{"type":"configure","preset":"guest:claude","workspace":"%s","agent_role":"switchboard",' "$PROJ"
  printf '"use_stored_key":true,"api_key":"","tab":"tab-1","fallbacks":%s%s}\n' "$1" "${2:-}"
}
run() {      # run <name> <stdin file> ; extra env comes from the caller
  python3 "$HERE/run-worker.py" < "$2" > "$OUT/$1.ndjson" 2> "$OUT/$1.stderr"
  echo "--- $1: $(tail -1 "$OUT/$1.stderr")"
  python3 - "$OUT/$1.ndjson" <<'PY'
import json, sys
for line in open(sys.argv[1]):
    e = json.loads(line)
    ev = e.get("event")
    if ev == "configured":
        print("    configured model=%r agent_role=%s guest=%s"
              % (e.get("model"), e.get("agent_role"), e.get("guest")))
        print("    roles.switchboard = %s" % json.dumps(e["roles"]["switchboard"]))
        print("    tiers.main        = %s" % json.dumps(e["tiers"]["main"]))
    elif ev in ("error", "delta", "board_chat_started"):
        print("    %-20s %s" % (ev, json.dumps({k: v for k, v in e.items()
                                                if k in ("text", "model")})[:300]))
PY
}

echo "=== 1. helper on a guest Main, priority list with a keyed provider ==="
# No ask here: the stub key would be posted to the real api.moonshot.ai. What this scene proves
# is the `configured` event; scene 5 is the one that answers, on loopback.
{ helper '[{"preset":"guest:codex","model":""},{"preset":"kimi","model":"kimi-k3"}]'
  echo '{"type":"shutdown"}'; } > "$SCRATCH/1.ndjson"
RELAY_KIMI_API_KEY=stub-kimi-key run 01-priority-list "$SCRATCH/1.ndjson"

echo "=== 2. helper on a guest Main, nothing on the list is usable ==="
{ helper '[{"preset":"guest:codex","model":""},{"preset":"kimi","model":"kimi-k3"}]'
  echo '{"type":"board_chat","id":"c1","text":"what is on the board?"}'
  echo '{"type":"shutdown"}'; } > "$SCRATCH/2.ndjson"
run 02-nothing-usable "$SCRATCH/2.ndjson"

echo "=== 3. a role pick in the helper's model box, under a guest window preset ==="
{ helper '[]' ',"roles":{"switchboard":{"preset":"glm-coding"}}'
  echo '{"type":"shutdown"}'; } > "$SCRATCH/3.ndjson"
RELAY_GLM_CODING_API_KEY=stub-glm-key run 03-role-pick "$SCRATCH/3.ndjson"

echo "=== 4. a PANE on the same guest preset is unchanged ==="
{ printf '{"type":"configure","preset":"guest:claude","workspace":"%s","use_stored_key":true,' "$PROJ"
  printf '"api_key":"","fallbacks":[{"preset":"kimi","model":"kimi-k3"}]}\n'
  echo '{"type":"shutdown"}'; } > "$SCRATCH/4.ndjson"
RELAY_KIMI_API_KEY=stub-kimi-key run 04-pane-unchanged "$SCRATCH/4.ndjson"

echo "=== 5. live: the helper answers a board question on the fallback endpoint ==="
python3 "$HERE/stub-provider.py" 8842 > "$OUT/05-stub-provider.log" 2>&1 &
STUB=$!
trap 'kill $STUB 2>/dev/null; rm -rf "$SCRATCH"' EXIT
sleep 1
{ helper '[{"preset":"guest:codex","model":""},{"preset":"local:stub","model":"stub-model"}]' ',"completion_check":false'
  echo '{"type":"board_chat","id":"c1","text":"which card is on this board?"}'; } > "$SCRATCH/5.ndjson"
{ cat "$SCRATCH/5.ndjson"; sleep 15; echo '{"type":"shutdown"}'; } \
  | python3 "$HERE/run-worker.py" > "$OUT/05-live.ndjson" 2> "$OUT/05-live.stderr"
echo "--- 05-live: $(tail -1 "$OUT/05-live.stderr")"
python3 - "$OUT/05-live.ndjson" <<'PY'
import json, sys
for line in open(sys.argv[1]):
    e = json.loads(line)
    if e.get("event") == "configured":
        print("    configured model=%r guest=%s" % (e.get("model"), e.get("guest")))
        print("    note: %s" % e["roles"]["switchboard"].get("note"))
    elif e.get("event") in ("delta", "error"):
        print("    %-6s %s" % (e["event"], e.get("text", "")[:300]))
PY
kill $STUB 2>/dev/null
echo
echo "transcripts in $OUT"
