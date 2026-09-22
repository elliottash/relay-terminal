"""Independent drive of #WC3E's Done-means bullets on a throwaway board."""
import sys, tempfile, pathlib
X = pathlib.Path(sys.argv[1])
sys.path[:0] = [str(X / "backend"), str(X / "tests")]
from relay_core import board as B, board_tools as T
from test_board_tools import CONFIG

tmp = tempfile.TemporaryDirectory()
repo = pathlib.Path(tmp.name).resolve()
root = repo / "issues"; root.mkdir()
(root / B.BOARD_CONFIG).write_text(CONFIG, encoding="utf-8")
board = B.Board(root, repo)
tools = T.BoardTools(board, emit=lambda e: None, autonomy=None,
                     context=T.ToolContext(actor="agent", model="anthropic/claude-opus-5", pane="2"),
                     state_path=repo / ".relay" / "board-rate.json",
                     pane_token="3f2504e0-4f89-11d3-9a0c-0305e82c3301")
tools.begin_turn("t-1")

def mk(title, request):
    r = tools.run("board_create_card", {"tab": "features", "status": "inbox",
                                        "title": title, "request": request})
    assert "error" not in r, r
    return r["id"]

print("=== D: agent move to done over an UNANSWERED numbered Human QA question ===")
cid = mk("Gate check", "check the human qa gate")
h = tools.run("board_read", {"id": cid})["hash"]
h = tools.run("board_update_card", {"id": cid, "base_hash": h, "append_section": {
    "heading": "Human QA", "text": "1. Is the beige icon acceptable on the dark header?\n2. Does the meter read right at 90%?"}})["hash"]
r = tools.run("board_move_card", {"id": cid, "status": "done", "reason": "looks fine to me"})
print("  result:", {k: r[k] for k in ("error", "code", "requires") if k in r})
print("  questions:", r.get("questions"))
print("  status now:", board.card_by_id(cid).status)

print("=== D2: same card, now with an indented Answer: under question 1 only ===")
h = tools.run("board_read", {"id": cid})["hash"]
h = tools.run("board_update_card", {"id": cid, "base_hash": h, "replace_section": {
    "heading": "Human QA", "text": "1. Is the beige icon acceptable on the dark header?\n    Answer: yes (owner)\n2. Does the meter read right at 90%?"}})["hash"]
r = tools.run("board_move_card", {"id": cid, "status": "done", "reason": "one left"})
print("  result:", {k: r[k] for k in ("error", "code") if k in r})
print("  remaining questions:", r.get("questions"))

print("=== D3: both answered -> the agent's move goes through ===")
h = tools.run("board_read", {"id": cid})["hash"]
h = tools.run("board_update_card", {"id": cid, "base_hash": h, "replace_section": {
    "heading": "Human QA", "text": "1. Is the beige icon acceptable?\n    Answer: yes (owner)\n2. Does the meter read right at 90%?\n    Answer: yes (owner)"}})["hash"]
r = tools.run("board_move_card", {"id": cid, "status": "done", "reason": "both answered"})
print("  error:", r.get("error"), " status now:", board.card_by_id(cid).status)

print("=== D4: the OWNER's own close over an unanswered question is untouched ===")
cid2 = mk("Owner close", "owner closes it")
h = tools.run("board_read", {"id": cid2})["hash"]
tools.run("board_update_card", {"id": cid2, "base_hash": h, "append_section": {
    "heading": "Human QA", "text": "1. Still unanswered?"}})
tools.context.actor = "owner"
r = tools.run("board_move_card", {"id": cid2, "status": "done", "reason": "owner says done"})
print("  error:", r.get("error"), " status now:", board.card_by_id(cid2).status)
tools.context.actor = "agent"

print("=== D5: prose Human QA with no numbered question gates nothing ===")
cid3 = mk("Prose QA", "prose only")
h = tools.run("board_read", {"id": cid3})["hash"]
tools.run("board_update_card", {"id": cid3, "base_hash": h, "append_section": {
    "heading": "Human QA", "text": "Have a look at the colours when you get a moment."}})
r = tools.run("board_move_card", {"id": cid3, "status": "done", "reason": "no numbered question"})
print("  error:", r.get("error"), " status now:", board.card_by_id(cid3).status)

print("=== P: a Plan turn may write Done means and Plan, and nothing else ===")
cid4 = mk("Plan scope", "plan scope check")
tools.begin_card_turn("plan", cid4)
def hh(): return tools.run("board_read", {"id": cid4})["hash"]
for heading, text in (("Done means", "- it records the outcome\nFailure: silence"),
                      ("Plan", "1. do the thing"),
                      ("Execution Summary", "built it"),
                      ("QA checklist", "all passed")):
    r = tools.run("board_update_card", {"id": cid4, "base_hash": hh(),
                                        "replace_section": {"heading": heading, "text": text}})
    print(f"  {heading:18} -> {'OK' if 'error' not in r else r.get('code') + ': ' + r['error'][:110]}")
tools.end_card_turn()
body = board.card_by_id(cid4).body
print("  sections on card:", [s for s in ("## Done means", "## Plan", "## Execution Summary", "## QA checklist") if s in body])
