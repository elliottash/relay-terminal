"""Drive 6: the unresolved note — the AGENT's own `board_move_card` does not go through
`gate_move`, which the GUI/protocol `board_move` does. Pre-existing: `_tests_gate` has lived in
`board_protocol` since #7BM4, before this card."""
import json, os, sys
from pathlib import Path
X = Path(os.environ["X"]); D = Path(os.environ.get("WORK", "/tmp/claude-1000/v-pr4q-orders"))
sys.path.insert(0, str(X / "backend"))
from relay_core import board as B, board_tools as BT, tests_protocol as TP
staged = json.loads((D / "STAGED.json").read_text()); DONE = staged["cards"]["done"]
board = B.Board(D / ".switchboard", D)
tc = TP.TestsCommands(D, emit=lambda e: None)
print("gate says:", (tc.gate_move(DONE, "done") or {}).get("message"))
tools = BT.BoardTools(board, emit=lambda e: None, state_path=D / ".relay" / "rate.json")
tools.context.actor = "agent"
try:
    out = tools.run("board_move_card", {"id": DONE, "status": "done",
                                        "reason": "an agent landing it with no override"})
    print("board_move_card (agent tool):", {k: out.get(k) for k in ("card", "status")} if isinstance(out, dict) else out)
except Exception as exc:
    print("board_move_card refused:", type(exc).__name__, exc)
print("card status now:", board.card_by_id(DONE).status)
print("thread has an override marker:", "relay:override" in (D/".switchboard/threads"/f"{DONE}.md").read_text())
