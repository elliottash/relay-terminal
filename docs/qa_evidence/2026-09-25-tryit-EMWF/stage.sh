#!/bin/sh
# #EMWF Try it — a disposable board with three cards: one filed the old way
# (bare quote as the Issue) and two the new way (summary, then the user's words
# as an attributed, session-linked quote). Rerunnable; no model, no network.
set -e
FIX=/home/elliott/.cache/relay/scratch/tryit/emwf-issue-quote
rm -rf "$FIX"
mkdir -p "$FIX/issues"
cd "$FIX"
git init -q .
cat > issues/board.yaml <<'YAML'
version: 1
tabs: [{id: features, folder: features}, {id: bugs, folder: changes},
  {id: planning, folder: planning},
  {id: done, filter: "status:done,dropped"}]
columns: [inbox, discussing, ready, in-progress, waiting, needs-qa, done]
agent: {autonomy: auto, max_creates_per_turn: 5}
YAML
PYTHONPATH=/home/elliott/repos/relay-terminal/backend python3 - <<'PY'
from pathlib import Path
from relay_core import board as B
from relay_core import board_tools as T

fix = Path("/home/elliott/.cache/relay/scratch/tryit/emwf-issue-quote")
board = B.Board(fix / "issues", fix)
ctx = T.ToolContext(actor="agent", pane="1",
                    session_id="0f3ac2de91b4e8a6c0d5f17392ab4e76")
tools = T.BoardTools(board, context=ctx)
tools.begin_turn("tryit-1")

# The old way: the user's words pasted in as the whole Issue (how #WZ3K was filed).
tools.run("board_create_card", {
    "tab": "features", "status": "inbox",
    "title": "OLD WAY: scratch watcher flags the guest harness home",
    "request": "delegate a subagent to file the scratch watcher issue"})

# The new way: summary first, the user's words quoted and attributed.
tools.run("board_create_card", {
    "tab": "features", "status": "inbox",
    "title": "NEW WAY: voice transcription mode",
    "summary": "Voice-to-text in the composer: a microphone icon that dictates into the input "
               "box, matching how Warp does it.",
    "request": "add voice transcribe mode (microphone icon). like warp"})

# The new way, quoting no one: a fault the agent noticed.
tools.run("board_create_card", {
    "tab": "features", "status": "inbox",
    "title": "NEW WAY, no quote: a fault the agent noticed",
    "summary": "The model dropdown forgets a renamed provider until restart; a fault noticed "
               "while working on something else, quoting nobody."})

print()
print("=== the three Issue sections ===")
for card in sorted(board.cards(), key=lambda c: c.title):
    if not card.title.startswith(("OLD WAY", "NEW WAY")):
        continue
    print()
    print(f"--- {card.title}  ({card.id}) ---")
    body = card.body.split("## Issue", 1)[1]
    print("## Issue" + body)
PY
echo
echo "Board fixture: $FIX/issues/board.yaml"
