"""Drive 3: the real board_protocol move path on the staged project — refusal, override
recorded once, not asked again, and not holding for a different revision."""
import json, os, subprocess, sys
from pathlib import Path
X = Path(os.environ["X"]); D = Path(os.environ.get("WORK", "/tmp/claude-1000/v-pr4q-orders"))
sys.path.insert(0, str(X / "backend"))
from relay_core import board as B, board_protocol as BP, board_tools as BT, tests_protocol as TP
staged = json.loads((D / "STAGED.json").read_text()); DONE = staged["cards"]["done"]
board = B.Board(D / ".switchboard", D)
events = []
def commands():
    c = BP.BoardCommands(None, events.append)
    c.tools = BT.BoardTools(board, emit=events.append, state_path=D / ".relay" / "rate.json")
    return c
def move(card, status="done", **extra):
    del events[:]
    commands().dispatch({"type": "board_move", "id": "m1", "card": card, "status": status,
                         "reason": "landing", **extra})
    errs = [e for e in events if e.get("event") == "error"]
    return errs[0] if errs else None
def status_of(c): return board.card_by_id(c).status

print("== A: move refused (statuses shown)")
e = move(DONE)
print("   code:", e["code"], "| tests:", e["tests"], "| rev:", e["revision"])
print("   text:", e["text"])
print("   statuses:", [(r["test"], r["status"]) for r in e["statuses"]])
print("   card still:", status_of(DONE))

print("\n== B: the same move with an override lands it")
e = move(DONE, override="totals_large_order is the bug under repair; test_invoice has never run here")
print("   error:", e, "| card now:", status_of(DONE))
th = (D / ".switchboard/threads" / f"{DONE}.md").read_text()
print("   override markers on thread:")
for l in th.splitlines():
    if "relay:override" in l: print("     ", l.strip())

print("\n== C: put it back and move again — asked again?")
commands().dispatch({"type": "board_move", "id": "m2", "card": DONE,
                     "status": "needs-verification", "reason": "back for the second attempt"})
print("   card back at:", status_of(DONE))
e = move(DONE)
print("   second attempt error:", e if e is None else (e["code"], e["tests"]))
print("   card now:", status_of(DONE))

print("\n== D: a different revision — the override must not hold")
tc = TP.TestsCommands(D, emit=lambda ev: None)
card = board.card_by_id(DONE)
print("   live_overrides at this revision:", sorted(tc.live_overrides(DONE, tc.card_revision(DONE, card)[2])))
print("   live_overrides at another revision:", sorted(tc.live_overrides(DONE, "deadbeefcafe")))
