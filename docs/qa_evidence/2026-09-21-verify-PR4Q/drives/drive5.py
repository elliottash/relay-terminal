"""Drive 5: an ATTACHED result — a `relay-remote-tests` folder with a JUnit file, tied to this
card's revision — turns `missing-evidence` into `passed` and is offered as "Use this existing
result"."""
import json, os, sys
from pathlib import Path
X = Path(os.environ["X"]); D = Path(os.environ.get("WORK", "/tmp/claude-1000/v-pr4q-orders"))
sys.path.insert(0, str(X / "backend"))
from relay_core import tests_protocol as TP, test_history as H
staged = json.loads((D / "STAGED.json").read_text()); DONE = staged["cards"]["done"]
head = staged["head"]
tc = TP.TestsCommands(D, emit=lambda e: None)
before = {r["test"]: r["status"] for r in tc.check_card(DONE)["statuses"]}
print("before:", before["unittest:tests.test_invoice"])

folder = tc.incoming_dir() / "sphinxpad-2026-09-21-0900"
folder.mkdir(parents=True, exist_ok=True)
(folder / "meta.json").write_text(json.dumps(
    {"run_id": "sphinxpad-ci-41", "commit": head, "host": "sphinxpad",
     "finished": "2026-09-21T18:00:00Z"}) + "\n")
(folder / "unittest.xml").write_text("""<?xml version="1.0"?>
<testsuite name="tests.test_invoice" tests="3" failures="0">
  <testcase classname="tests.test_invoice.InvoiceTests" name="test_total_matches_the_order" time="0.01"/>
  <testcase classname="tests.test_invoice.InvoiceTests" name="test_large_order_has_one_line_per_item" time="0.01"/>
  <testcase classname="tests.test_invoice.InvoiceTests" name="test_currency_is_printed_with_two_decimals" time="0.01"/>
</testsuite>
""")
print("ingest:", tc.ingest_incoming())
after = {r["test"]: r for r in tc.check_card(DONE)["statuses"]}
row = after["unittest:tests.test_invoice"]
print("after:", row["status"], "|", row["message"])
print("evidence rows:", [(e["host"], e["run_id"], e["result"], e["applicable"]) for e in row["evidence"]])
print("use_existing offered:", row.get("use_existing"))
g = tc.gate_move(DONE, "done")
print("gate now blocks on:", (g or {}).get("tests", "nothing"))
