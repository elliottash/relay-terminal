"""Verifier drive 1 of #PR4Q: statuses, evidence from another host, retired, gate."""
import json, os, sys
from pathlib import Path
X = Path(os.environ["X"]); D = Path(os.environ.get("WORK", "/tmp/claude-1000/v-pr4q-orders"))
sys.path.insert(0, str(X / "backend"))
from relay_core import tests_protocol as TP
from relay_core import board as B

staged = json.loads((D / "STAGED.json").read_text())
DONE = staged["cards"]["done"]; FLAKY = staged["cards"]["flaky"]
events = []
tc = TP.TestsCommands(D, emit=events.append)
print("=== card", DONE, "revision:", tc.card_revision(DONE)[2])
r = tc.check_card(DONE)
print("--- statuses (Done means 1):")
for row in r["statuses"]:
    print("   %-18s %-28s %s" % (row.get("status"), row.get("test") or row.get("line"), (row.get("message") or "")[:90]))
print("--- actions:", r["actions"])
print("--- findings (advisory):")
for f in r["findings"]:
    print("   %-16s %-24s %s" % (f.get("severity"), f.get("verdict"), (f.get("message") or "")[:90]))
print("--- full statuses json:")
print(json.dumps(r["statuses"], indent=1)[:4000])
