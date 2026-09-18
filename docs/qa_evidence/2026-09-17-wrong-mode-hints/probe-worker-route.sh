#!/usr/bin/env bash
# Probe the live worker with the exact route request shape the GUI sends for the B1 scenario
# (docs/qa_evidence/2026-09-17-wrong-mode-hints). Feeds one NDJSON line to backend/worker.py
# exactly as Pane::startWorker launches it (python3 -S -u) and prints the route replies.
set -euo pipefail
root=$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)
known=$(python3 - "$1" <<'PY'
import json, sys
event = json.load(open(sys.argv[1]))
print(json.dumps(event.get("known_commands", [])))
PY
)
python3 -S -u - <<PY
import json, subprocess, sys
request = {"type": "route", "id": "probe-b1", "text": "find the largest files",
           "mode": "shell", "known_commands": $known,
           "path": "/home/elliott/.local/bin:/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin",
           "cwd": "/tmp/relay-qa-wrong-mode"}
proc = subprocess.run(["python3", "-S", "-u", "$root/backend/worker.py"],
                      input=json.dumps(request) + "\n", capture_output=True, text=True, timeout=30)
for line in proc.stdout.splitlines():
    obj = json.loads(line)
    if obj.get("event") == "route":
        keep = {k: obj[k] for k in ("route", "valid", "agent_signal", "reason") if k in obj}
        print(json.dumps(keep, indent=1))
PY
