#!/bin/sh
# #DVV2 Try-it staging: one agent's scratch life, in a sandbox away from the real
# ledger and roots. Rerunnable: it wipes its own sandbox first. No network, no model.
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
SAND="$HERE/sandbox"
REPO="$(cd "$HERE/../../.." && pwd)"
rm -rf "$SAND"
mkdir -p "$SAND/home" "$SAND/project"

R="env -i HOME=$SAND/home PATH=/usr/bin:/bin"
R="$R RELAY_LEDGER=$SAND/home/ledger.jsonl"
R="$R RELAY_SCRATCH_HOME=$SAND/home/.cache/scratch"
R="$R RELAY_TOOLS_HOME=$SAND/home/.local/tools"
R="$R RELAY_STATE_HOME=$SAND/home/.local/state"
R="$R RELAY_SESSION_TOKEN=tok-tryit"
RS="$R python3 $REPO/scripts/relay-scratch"

step() { printf '\n\033[1m== %s\033[0m\n' "$1"; }

step "1. A pane session starts: the worker writes one TMPDIR row for the pane; mktemp then lands in it, no agent effort"
$RS new --class scratch --purpose "TMPDIR for pane tok-tryit" --session tok-tryit --lifetime days:7 >/dev/null
mkdir -p "$SAND/home/.cache/scratch/relay/scratch/tok-tryit/tmp"
STRAY=$($R TMPDIR="$SAND/home/.cache/scratch/relay/scratch/tok-tryit/tmp" mktemp)
echo "a plain mktemp landed in: $STRAY"

step "2. The agent asks for scratch for a build, and for a keep dir for its report draft"
BUILD=$($RS new --class scratch --purpose "export tree for card TY" --card DVV2 --session tok-tryit)
echo "build: $BUILD"; echo hello > "$BUILD/tree.txt"
KEEP=$($RS new --class keep --purpose "draft report" --project "$SAND/project" --card DVV2 --session tok-tryit)
echo "keep:  $KEEP"; echo "quarterly numbers" > "$KEEP/report.md"

step "3. The agent misbehaves: a rogue dir straight into /tmp, outside everything"
mkdir -p /tmp/dvv2-tryit-rogue && echo important > /tmp/dvv2-tryit-rogue/only-copy.txt

step "4. The ledger after the turn (scratch + keep rows, created_by with card and session)"
$RS ledger

step "5. The task ends: the agent releases scratch (reclaimed, gone from disk)"
$RS release "$BUILD"; ls "$BUILD" 2>&1 | head -1

step "6. The keep dir refuses to be deleted silently: promote it into the project"
$RS release "$KEEP" 2>&1 || true
$RS release "$KEEP" --promote-to "$SAND/project/docs/final-report" && \
  cat "$SAND/project/docs/final-report/report.md"

step "7. The pane closes: end_session reclaims this session's scratch, keeps keep/install rows"
$R python3 -c "
import sys, os; sys.path.insert(0, '$REPO/backend')
os.environ['RELAY_LEDGER'] = '$SAND/home/ledger.jsonl'
os.environ['RELAY_SCRATCH_HOME'] = '$SAND/home/.cache/scratch'
os.environ['RELAY_TOOLS_HOME'] = '$SAND/home/.local/tools'
os.environ['RELAY_STATE_HOME'] = '$SAND/home/.local/state'
from relay_core import scratch
print('reclaimed:', [r.id for r in scratch.end_session('tok-tryit') if r.state == 'reclaimed'])
print('still live:', [(r.id, r.cls) for r in scratch.ScratchLedger().live_rows()])"

step "8. The rogue dir is still there, unledgered - exactly what the post-turn sweep names to the agent"
$R python3 - <<'PY'
import sys, time
sys.path.insert(0, "backend")
import os
os.environ.setdefault("RELAY_LEDGER", os.path.join(os.environ["HOME"], "ledger.jsonl"))
os.environ["RELAY_SCRATCH_ROOTS"] = "/tmp"
from relay_core import scratch
strays = scratch.unledgered_created_since(time.time() - 600)
print("sweep found:", [p for p in strays if "dvv2-tryit" in p])
PY

step "9. The floor that failed this machine once: gc --apply below 6h idle on default roots refuses"
$RS gc --apply --idle-hours 0 --force-idle >/dev/null 2>&1 && echo "(sandbox narrowed: floor not in effect)"
$R python3 -c "
import sys, os; sys.path.insert(0, 'backend')
os.environ.pop('RELAY_SCRATCH_ROOTS', None)
from relay_core import scratch
try:
    scratch.gc(idle_hours=0, apply=True, log=lambda *a, **k: None)
    print('BAD: ran')
except ValueError as e:
    print('refused:', e)"

step "Done. Everything above ran against $SAND - the real ledger was never touched."
step "The one thing left for you: the rogue dir /tmp/dvv2-tryit-rogue is still on the real /tmp (that is the point)."
