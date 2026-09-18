#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Reproduce the Switchboard's HTTP 401 and show the fix, with no key and no network.
#
#   ./run.sh before   the configure the board sent until 2026-09-18 (preset + a stale base URL)
#   ./run.sh after    the configure it sends now (the preset alone)
#
# Both run the real backend/worker.py against a copy of relay_core whose `glm-coding` and `kimi`
# presets point at the stand-in on 127.0.0.1 (standin.py), with dummy keys in the environment and
# RELAY_KEYRING=off, so the desktop keyring is never touched. Everything else — the protocol, the
# preset table, the role resolution, board_open and board_ask — is the shipped code.
set -euo pipefail
mode="${1:-after}"
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo="$(cd "$here/../../.." && pwd)"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

# --- the stand-in provider ---------------------------------------------------------------------
coproc STANDIN { python3 -u "$here/standin.py"; }
read -r port <&"${STANDIN[0]}"
zai="http://127.0.0.1:$port/zai"
moonshot="http://127.0.0.1:$port/moonshot"

# --- a copy of the backend whose two presets point at it ---------------------------------------
cp -r "$repo/backend" "$work/backend"
find "$work/backend" -name __pycache__ -prune -exec rm -rf {} + 2>/dev/null || true
python3 - "$work/backend/relay_core/presets.py" "$zai" "$moonshot" <<'PY'
import sys
path, zai, moonshot = sys.argv[1:4]
text = open(path, encoding="utf-8").read()
text = text.replace('"https://api.z.ai/api/coding/paas/v4"', repr(zai))
text = text.replace('"https://api.moonshot.ai/v1"', repr(moonshot))
open(path, "w", encoding="utf-8").write(text)
PY

# --- a workspace with a board and one card -----------------------------------------------------
mkdir -p "$work/ws/issues/features"
cp "$repo/issues/board.yaml" "$work/ws/issues/board.yaml"
cat > "$work/ws/issues/features/2026-09-18-demo.md" <<'CARD'
---
id: A1B2
title: A demo card
type: work
status: inbox
created: 2026-09-18
---

## Request

A card to ask the agent about.
CARD

# --- the configure the GUI sends ----------------------------------------------------------------
# The owner's settings after switching model with the chip: provider/preset was rewritten to
# glm-coding, provider/base and provider/model still say Kimi.
python3 - "$mode" "$work/ws" "$moonshot" > "$work/configure.json" <<'PY'
import json, sys
mode, ws, moonshot = sys.argv[1:4]
msg = {"type": "configure", "workspace": ws, "agent_role": "switchboard",
       "use_stored_key": True, "api_key": "", "preset": "glm-coding", "max_tokens": 32768}
if mode == "before":                      # startBoardWorker before the fix
    msg["base_url"] = moonshot
    msg["model"] = "kimi-k3"
    msg["extra"] = {"reasoning_effort": "high", "thinking": {"type": "enabled"}}
print(json.dumps(msg))
PY

echo "--- configure sent by startBoardWorker ($mode) ---"
python3 -m json.tool < "$work/configure.json"

echo "--- worker events ---"
{ cat "$work/configure.json"
  echo '{"type":"board_open"}'
  sleep 1
  echo '{"type":"board_ask","card":"A1B2","text":"say ok","id":"ask1"}'
  sleep 6
} | RELAY_KEYRING=off \
    RELAY_GLM_CODING_API_KEY=zai-key \
    RELAY_KIMI_API_KEY=moonshot-key \
    RELAY_PANE_ID=switchboard \
    python3 -S -u "$work/backend/worker.py" 2>/dev/null |
  python3 -c '
import json, sys
for line in sys.stdin:
    try:
        event = json.loads(line)
    except ValueError:
        continue
    name = event.get("event")
    if name == "configured":
        print("configured: model=%s base=%s" % (event["model"], event["roles"]["main"]["base_url"]))
    elif name in ("error", "done", "board_thread_appended"):
        print("%s: %s" % (name, json.dumps({k: v for k, v in event.items()
                                            if k in ("text", "author", "card_id", "turn_id")})))
'

echo "--- what the stand-in saw ---"
echo dump >&"${STANDIN[1]}"
read -r seen <&"${STANDIN[0]}"
python3 -c 'import json,sys; [print(row) for row in json.loads(sys.argv[1])]' "$seen"
kill "$STANDIN_PID" 2>/dev/null || true
