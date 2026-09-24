---
id: J4WK
type: work
status: needs-verification
assignee: claude-code
labels: [bug, terminal]
rank: m
created: '2026-09-21'
source: 'Codex regression audit requested by owner, 2026-09-21'
links: {plans: [], commits: [], evidence: [docs/qa_evidence/2026-09-21-card-file-links/commit-audit.md, docs/qa_evidence/2026-09-21-block-rows-selection/README.md], related: [K9KC, GWXM], github: null}
---
# Keyboard link navigation loses its visible selection after prose rewrap

## Issue
when done, trace the commit that led to that and check for other introduced regressions

## Planning notes
Measured finding from the requested audit: Step to #GWXM with the keyboard link walker. At the print width the selected link is highlighted. After resizing 100 → 62 columns the target is still correct, but no selection pixels are painted. Disabling prose replacement preserves the highlight.

Cause: showWalkLink selects emulator cells, while paintProseRow reads the separate visual selection. The replacement rows added by 8147cc55 hide the selected emulator rows.

## Tests
- `python3 docs/qa_evidence/2026-09-21-card-file-links/run-audit-probe.py keyboardLinkSelectionVisibleAfterResize` — reproduces the failure.
- `AUDIT_NATIVE_ROWS=1 python3 docs/qa_evidence/2026-09-21-card-file-links/run-audit-probe.py keyboardLinkSelectionVisibleAfterResize` — control passes.

## Execution Summary
The walk scanned the emulator grid and selected rows there. A block that has taken its rows over
hides exactly those rows and paints the view's own visual selection, so the walk's target stayed
right while nothing on screen said which link was current.

`TerminalView::collectFoldLinks()` now scans a taken-over block from the block's own logical lines
and records each link as the fold rows and columns it covers — the walk's half of what `linkAt()`
already does for the mouse, so keyboard and mouse now find the same links in the same places.
`showWalkLink()` sets the visual selection and the hover underline in those coordinates and scrolls
by visual row; `endLinkWalk()` clears it. Files: `engine/view/TerminalView.{h,cpp}`,
`engine/view/FoldLayer.{h,cpp}` (`foldHidingRow()`).

## Tests
- `ViewTest::theKeyboardWalkHighlightsALinkOnABlocksOwnRows` — highlighted at the print width, gone
  when the walk ends, highlighted again after a resize, and the copied text is the link's text.
- The audit probe's `keyboardLinkSelectionVisibleAfterResize` now passes.
- Whole `ViewTest` 55/55. Evidence: `docs/qa_evidence/2026-09-21-block-rows-selection/`.

## QA checklist
- [ ] In a pane, have an agent reply that mentions a known card id, then narrow the pane so the
      reply re-wraps. Walk the links with the keyboard: the current link is visibly highlighted and
      underlined on the block's rows, and Enter opens it.
- [ ] Walk forwards and backwards through several links in one reply — the highlight moves with the
      walk, and no highlight is left behind when the walk ends (Esc).
- [ ] The keyboard walk finds the same links the mouse underlines on hover, in the same places.
