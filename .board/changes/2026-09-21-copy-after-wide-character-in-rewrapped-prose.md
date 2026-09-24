---
id: C7WP
type: work
status: needs-verification
assignee: claude-code
labels: [bug, terminal]
rank: m
created: '2026-09-21'
source: 'Codex regression audit requested by owner, 2026-09-21'
links: {plans: [], commits: [], evidence: [docs/qa_evidence/2026-09-21-card-file-links/commit-audit.md, docs/qa_evidence/2026-09-21-block-rows-selection/README.md], related: [K9KC, GWXM], github: null}
---
# Copying after a wide character returns the wrong text after resize

## Issue
when done, trace the commit that led to that and check for other introduced regressions

## Planning notes
Measured finding from the requested audit: Select ABC in 中ABCDEF. At the print width it copies ABC; after resizing 100 → 62 columns it copies BCD. The same resize with prose replacement disabled copies ABC.

Cause: visualSelectedText converts grid columns to grapheme indices by addition, ignoring each FoldLayer::Cell width. The older insertion-fold selection path had this assumption; 8147cc55 extended it to ordinary prose.

## Tests
- `python3 docs/qa_evidence/2026-09-21-card-file-links/run-audit-probe.py copyingAfterWideCharacterSurvivesResize` — reproduces the failure.
- `AUDIT_NATIVE_ROWS=1 python3 docs/qa_evidence/2026-09-21-card-file-links/run-audit-probe.py copyingAfterWideCharacterSurvivesResize` — control passes.

## Execution Summary
`TerminalView::visualSelectedText()` turned grid columns into cell indices by adding one to the
other, which is only right while every cell is one column wide. `FoldLayer::rowCellRange()` now
walks the row's own cells and answers the cells a column range covers; a cell counts as covered
when the column it *starts* at is in range, which is the rule `paintProseRow` highlights by — so
the text that copies is the text that looked selected. Files: `engine/view/FoldLayer.{h,cpp}`,
`engine/view/TerminalView.cpp`.

## Tests
- `ViewTest::copyingAfterAWideCharacterSurvivesResize` — the card's own case, in the green suite:
  `RELAY_ENGINE_TEST=ViewTest build/engine/relay-engine-tests copyingAfterAWideCharacterSurvivesResize`.
- The audit probe's `copyingAfterWideCharacterSurvivesResize` now passes (it failed when the card
  was filed): `python3 docs/qa_evidence/2026-09-21-card-file-links/run-audit-probe.py`.
- Whole `ViewTest` 55/55, `FoldLayerTest` 32/32, and the six targeted ctest suites 6/6. Evidence:
  `docs/qa_evidence/2026-09-21-block-rows-selection/`.

## QA checklist
- [ ] In a pane wide enough to print, have an agent reply containing a wide character with text
      after it (for example `中ABCDEF`), narrow the pane, then drag-select the three letters after
      the wide character: the clipboard holds those three letters, not the ones to their right.
- [ ] The highlight and the copied text agree at both widths — what looked selected is what pasted.
