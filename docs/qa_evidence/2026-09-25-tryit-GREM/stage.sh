#!/usr/bin/env bash
# #GREM Try it — stage the situation a terminal pane agent was in: it has just
# found that the card it is delivering duplicates another card, and wants to
# fold the duplicate away. Before #GREM the merge tool was refused outside a
# Board cleanup; this run shows a pane (no cleanup, no console) doing the fold.
#
# Rerunnable, offline, no model. Fixture under a short scratch path.
set -euo pipefail
REPO=$(cd "$(dirname "$0")/../../.." && pwd)
FIX=/home/elliott/.cache/relay/scratch/tryit/grem-merge
rm -rf "$FIX"; mkdir -p "$FIX/issues" "$FIX/.relay"

cat > "$FIX/issues/board.yaml" <<'YAML'
version: 1
tabs: [{id: features, folder: features}, {id: bugs, folder: changes},
  {id: planning, folder: planning},
  {id: done, filter: "status:done,dropped"}]
columns: [inbox, discussing, ready, in-progress, waiting, needs-qa, done]
agent: {autonomy: auto, max_creates_per_turn: 5}
YAML

PYTHONPATH="$REPO/backend" python3 - "$FIX" <<'PY'
import sys, pathlib
from relay_core import board as B
from relay_core import board_tools as T

fix = pathlib.Path(sys.argv[1])
board = B.Board(fix / "issues", fix)
tools = T.BoardTools(
    board, emit=lambda e: None, autonomy="auto",
    context=T.ToolContext(actor="agent", model="anthropic/claude-opus-5-5", pane="2"),
    state_path=fix / ".relay" / "board-rate.json",
    pane_token="11111111-2222-3333-4444-555555555555")
tools.begin_turn("tryit")

# The #BCJF/#P4XN story, renamed: two cards that ask the same thing, the
# survivor already carrying the work.
keep = tools.run("board_create_card",
                 {"tab": "features", "status": "in-progress",
                  "title": "Filter the model dropdown",
                  "summary": "Typing in the model dropdown should filter the list."})["id"]
gone = tools.run("board_create_card",
                 {"tab": "features", "status": "planned",
                  "title": "Model dropdown search",
                  "summary": "Let me type in the model dropdown to narrow it."})["id"]

offered = {s["function"]["name"] for s in tools.tool_specs()}
print("A terminal pane agent is offered board_merge_cards:", "board_merge_cards" in offered)
print("... and still NOT offered board_split_card:", "board_split_card" not in offered)
print()
res = tools.run("board_merge_cards",
                {"into": keep, "cards": [gone],
                 "reason": "the same request: filtering the model dropdown"})
print("merge result:", {k: v for k, v in res.items() if k != "merged"}
      if isinstance(res, dict) else res)
print()
survivor, folded = board.card_by_id(keep), board.card_by_id(gone)
print(f"survivor {survivor.id}: status={survivor.status}, title={survivor.title!r}")
print(f"folded  {folded.id}: status={folded.status} (file kept: "
      f"{(fix / 'issues' / 'features').exists() and folded.id} nothing deleted)")
print()
print("---- what landed on the survivor ----")
for line in survivor.body.splitlines():
    if "Merged in" in line or "Model dropdown search" in line or "narrow it" in line:
        print(line)
print("---- what the folded card now says ----")
tail = folded.body.splitlines()[-3:]
print("\n".join(tail))
PY
echo
echo "open: bash $0   (fixture: $FIX)"
