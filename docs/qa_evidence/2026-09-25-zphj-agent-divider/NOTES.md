# #ZPHJ — moveable split between an artifact and its docked agent (implementer evidence)

Revision: the landing commit for #ZPHJ (tip + `zphj` hunks). Built in a land.py verify slot and
copied privately before driving, because the shared slot binary was rebuilt by another session
mid-run (the first attempt's later screenshots were discarded for that reason).

## Tests (verify slot, `ctest -R '^(filesync|boardsolo|filepanes|boardpane|windowstate)$'`)
All 5 passed. New cases:
- `filesync` `theAgentDividerDragsFoldsAndRestores` — file preview: folded row with a disabled
  handle; unfold at the 45 % default; drag up (>150 px) and down; only drags are remembered;
  fold/unfold returns to the chosen share; resize keeps the share; a second pane given the share
  opens at it; one pane's drag does not change another's; an out-of-range share is the default.
- `filesync` `thePlanEditorsAgentDividerDrags` — the plan editor's divider drags.
- `boardsolo` `theCardPanesAgentDividerDragsAndRestores` — standalone Card pane with a long
  transcript: handle live once the console exists, 40 % default, drag past the old 40 % cap and
  back down, resize keeps the share (floored at the console's minimum, kept for a taller pane),
  restored pane opens at the share, per-pane independence.

## Live (Xvfb 1600x1000, isolated XDG_* and XDG_RUNTIME_DIR, xdotool)
Layout: one tab, text preview | Card pane (#ZPHJ on a throwaway copy of the board).
`restore-input-windows.json` seeds preview `agent: 0.7`, card `agent: 0.65`.
- `01-restored.png` — Card pane opens at 65 % with the grip on the divider; file agent folded
  to its one row.
- `02-file-agent-unfolded-at-saved-70pct.png` — "✦ Agent" click: file agent opens at 70 %.
- `03-card-dragged-down.png` — Card divider dragged down: the card document gets the room.
- `04-file-agent-folded.png`, `05-file-agent-unfolded-again.png` — fold to one row, unfold back
  to 70 %; the outer pane edges do not move.
- `saved-after-drag.json` — what Relay wrote on quit: preview 0.7, card 0.2365.
- `06-restart-restores-card-24pct.png` — after restart the Card pane comes back at 24 %.
Earlier run (same build): dragging the file divider down wrote preview `agent: 0.428`.

Limits seen: the Card agent can grow until the card header (title, status, labels, links) and
two lines of the document remain — the header's natural height; it shrinks to the console's
minimum (about 8 lines plus the composer).
