#!/bin/bash
# #ZPWT Try it — a small-scale rehearsal of the landed pane memory behaviour, run through the
# landed worker code (backend/relay_core/tools.py + jobs.py) of this checkout. Rerunnable; it
# uses only a throwaway scope inside app-relay.slice and files under the scratch dir.
set -u
ROOT="${RELAY_ROOT:-$(cd "$(dirname "$0")/../../.." && pwd)}"
DIR="${RELAY_TRYIT_DIR:-/home/elliott/.cache/relay/scratch/tryit}"
EVIDENCE="$(cd "$(dirname "$0")" && pwd)"
mkdir -p "$DIR"
cat > "$DIR/hog.py" <<'PY'
import sys, time
mb = int(sys.argv[1])
buf = bytearray(mb * 1000 * 1000)
for i in range(0, len(buf), 4096): buf[i] = 1
print(f"hog: {mb}MB resident", flush=True)
time.sleep(2)
PY
cat > "$DIR/pane.py" <<'PY'
# A stand-in for an agent pane: it runs run_command jobs through the real ToolExecutor, exactly
# as the shipped worker does, and stays alive to report what happened when the kernel kills one.
import json, sys, threading, tempfile
sys.path.insert(0, sys.argv[1])
from relay_core.tools import ToolExecutor

events, cancel = [], threading.Event()
tools = ToolExecutor(tempfile.mkdtemp(prefix="tryit-"), events.append, cancel)

def run(label, **args):
    print(f"\n=== {label}")
    result = tools.execute(tools.prepare("run_command", dict(args, timeout_seconds=90)))
    shown = {k: v for k, v in result.items() if k not in ("output",)}
    print(json.dumps(shown, indent=1))
    print(f"output: {result.get('output', '')!r}")

run("A. plain run_command, hog past the pane cap (pane scope: 1500M)",
    command=f"{sys.executable} {sys.argv[2]}/hog.py 1800")
run("B. run_command memory_max=600M, hog 900M past its own bound",
    command=f"{sys.executable} {sys.argv[2]}/hog.py 900", memory_max="600M")
tools.shutdown()
print("\nPANE-ALIVE: this pane process survived both kills and is still reporting.")
PY
# The pane analog: a scope just like an agent pane's, small so the rehearsal is quick.
systemd-run --user --scope --quiet --collect --unit=relay-tryit-pane --slice=app-relay.slice \
  -p MemoryMax=1500M -p MemorySwapMax=0 -p OOMPolicy=continue \
  -- python3 "$DIR/pane.py" "$ROOT/backend" "$DIR" 2>&1 | tee "$EVIDENCE/01-tryit-run.txt"
echo
echo "open: less $EVIDENCE/01-tryit-run.txt   (staged by $0)"
