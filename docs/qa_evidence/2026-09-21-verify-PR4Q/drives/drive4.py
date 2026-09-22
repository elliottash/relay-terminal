"""Drive 4: `tests_accept` ("Use this existing result"), the one `### Check` status replaced in
place ending in `history: thread`, and `refresh_checks` after a run."""
import json, os, sys, re
from pathlib import Path
X = Path(os.environ["X"]); D = Path(os.environ.get("WORK", "/tmp/claude-1000/v-pr4q-orders"))
sys.path.insert(0, str(X / "backend"))
from relay_core import tests_protocol as TP, board as B, test_history as H
staged = json.loads((D / "STAGED.json").read_text()); DONE = staged["cards"]["done"]
cardfile = next(p for p in (D/".switchboard").rglob("*.md") if p.read_text().startswith("---") and f"id: {DONE}" in p.read_text())
events = []
tc = TP.TestsCommands(D, emit=events.append)

print("== E: the offer — which listed check carries `use_existing`")
r = tc.check_card(DONE)
for row in r["statuses"]:
    print("   %-28s %-17s use_existing=%s accepted=%s" % (row["test"], row["status"], row.get("use_existing"), row.get("accepted")))
off = [row for row in r["statuses"] if row.get("use_existing")]
print("   offered on:", [(o['test'], o['evidence'][0]['host'], o['evidence'][0]['run_id']) for o in off])

print("\n== F: first Check writes ONE `### Check` status, replaced in place")
tc.emit_check(DONE)
body1 = cardfile.read_text()
tc.emit_check(DONE)                                     # a second check
tc.emit_check(DONE)                                     # and a third
body = cardfile.read_text()
print("   `### Check` headings in the card after three checks:", body.count("### Check"))
sec = body[body.index("## Tests"):]
print("   --- the `## Tests` section as it now stands ---")
for line in sec.strip().splitlines(): print("   |", line)
print("   ends with `history: thread`:", sec.strip().splitlines()[-1].strip().startswith("history: thread"))
print("   the four `## Tests` lines are untouched:",
      all(l in sec for l in ("- `ctest -R totals`", "- `ctest -R totals_large_order`",
                             "- `ctest -R rounding`", "- `tests/test_invoice.py`")))

print("\n== G: tests_accept — the laptop's run accepted as this card's evidence")
o = off[0]; run = o["evidence"][0]["run_id"]
del events[:]
tc.accept_result(DONE, o["test"], run)
body = cardfile.read_text(); sec = body[body.index("## Tests"):]
print("   accept marker on the card:", [l.strip() for l in sec.splitlines() if "relay:accept" in l])
print("   front matter untouched (no new field):", "accept" not in body.split("---")[1])
th = (D/".switchboard/threads"/f"{DONE}.md").read_text()
print("   thread evidence entry:", [l for l in th.splitlines() if "Accepted run" in l])
r2 = tc.check_card(DONE)
print("   after accepting, use_existing on that row:",
      [row.get("use_existing") for row in r2["statuses"] if row["test"] == o["test"]],
      "accepted:", [row.get("accepted") for row in r2["statuses"] if row["test"] == o["test"]])
print("   event kinds emitted by accept:", [e.get("event") for e in events])

print("\n== H: refresh_checks — a finished run re-checks the cards whose tests it ran")
del events[:]
touched = tc.refresh_checks(["ctest:totals_large_order"])
print("   cards re-checked for a run of ctest:totals_large_order:", touched)
print("   events:", [(e.get("event"), e.get("card")) for e in events])
print("   a run no card names:", tc.refresh_checks(["ctest:nothing_names_this"]))
