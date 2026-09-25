#!/usr/bin/env bash
set -euo pipefail
repo=/home/elliott/repos/relay-terminal
mkdir -p /tmp/claude-1000/tryit
scratch=$(mktemp -d /tmp/claude-1000/tryit/jx8z.XXXXXX)
mkdir -p "$scratch/data"
XDG_DATA_HOME="$scratch/data" RELAY_LOG_ORIGIN=qa PYTHONPATH="$repo/backend" python3 - <<'PY'
import json
from relay_core import logs
logs.configure('worker', level='info', pane='tryit')
for preset, used, reset in [('guest:claude:sample', 42.5, 1790400000),
                            ('guest:codex:sample', 71.0, 1790500000)]:
    logs.usage_state({'event': 'usage_limits', 'preset': preset,
        'source': 'subscription_poll', 'windows': [{'kind': 'weekly',
        'used_percent': used, 'resets_at': reset}], 'resets_available': 1,
        'resets_expire_at': 1792703299})
for line in (logs.log_dir() / logs.USAGE_STATES).read_text().splitlines():
    row=json.loads(line)
    print(json.dumps({key: row[key] for key in ('ts','preset','source','windows','resets_available','resets_expire_at')}, sort_keys=True))
PY
printf 'Sample file: %s\n' "$scratch/data/relay/logs/usage-states.jsonl"
