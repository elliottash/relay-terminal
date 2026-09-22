import sys, tempfile, pathlib, json
X = pathlib.Path(sys.argv[1])
sys.path[:0] = [str(X / "backend"), str(X / "tests")]
from relay_core import board as B, board_tools as T, board_protocol as P
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

print("=== C: `check` on the four new headings, and on an invented one ===")
r = tools.run("board_create_card", {"tab": "features", "status": "inbox",
                                    "title": "Headings", "request": "headings check"})
cid = r["id"]
h = tools.run("board_read", {"id": cid})["hash"]
for heading in ("Done means", "Human QA", "Profile", "Try it", "QA checklist", "Verdict"):
    out = tools.run("board_update_card", {"id": cid, "base_hash": h,
                                          "append_section": {"heading": heading, "text": "x"}})
    if "hash" not in out:
        print("   update refused:", out); break
    h = out["hash"]
b2 = B.Board(root, repo)
print("  unknown_section after the six:", [p.message for p in b2.check() if p.code == "unknown_section"])
card = b2.card_by_id(cid)
p = b2.card_path(card) if hasattr(b2, "card_path") else None
if p is None:
    import glob
    p = pathlib.Path(glob.glob(str(root / "features" / "*.md"))[0])
p.write_text(p.read_text(encoding="utf-8") + "\n## Invented Heading\nx\n", encoding="utf-8")
b3 = B.Board(root, repo)
print("  unknown_section after an invented one:",
      [x.message for x in b3.check() if x.code == "unknown_section"])
