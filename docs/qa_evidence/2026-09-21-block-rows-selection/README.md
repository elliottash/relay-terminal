# A block's own rows: selection and the keyboard walk — #C7WP, #8SBD (view half), #J4WK

Relay prints a block of its own prose at one width; at any other width FoldLayer hides the grid
rows and paints its own wrap of the same logical lines (#R2WQ). Three things that read the grid
were never taught to read the block, and the commit audit under
`docs/qa_evidence/2026-09-21-card-file-links/` measured all three (`commit-audit.md`). This is the
fix and its evidence.

## What was wrong, and what changed

| Card | Fault | Change |
| --- | --- | --- |
| #C7WP | Selecting `ABC` in `中ABCDEF` copied `BCD` after a resize: `visualSelectedText()` turned grid columns into cell indices by addition, and a wide character makes the two differ. | `FoldLayer::rowCellRange()` walks the row's own cells and answers the cells those columns cover. A cell counts as covered when the column it *starts* at is in range — the rule `paintProseRow` highlights by, so what is painted is what copies. |
| #8SBD (view half) | `alpha beta gamma xyz delta` wrapped at 20 columns copied as `…xyzdelta`: the space a line wraps at belongs to no row, and the join only concatenated the rows' displayed cells. | The join now carries the cells between the previous row's end and this row's first. A wrap inside a token leaves no such cell, so a wrapped URL still copies whole. The core half of #8SBD (`LibVtermCore::selectedText`) is a separate change on the same card. |
| #J4WK | Stepping to `#GWXM` with the keyboard walker kept the right target but painted no highlight after a resize: `showWalkLink` selected the emulator rows the block hides, while the block paints the view's own visual selection. | `collectFoldLinks()` scans a taken-over block from its own logical lines and records the link as fold rows and columns — the walk's half of what `linkAt()` already does for the mouse — and `showWalkLink` sets the visual selection and the hover underline there. `endLinkWalk()` clears it. |

## Evidence

- `audit-probe-after.txt` — the audit's own probe, all 6 cases passing. Before this change the same
  probe failed exactly three: `copyingAfterWideCharacterSurvivesResize`,
  `copyPreservesSpaceAtWrappedEdge` and `keyboardLinkSelectionVisibleAfterResize`
  (`../2026-09-21-card-file-links/audit-after-link-fix.txt` is that run).
  Re-run: `python3 docs/qa_evidence/2026-09-21-card-file-links/run-audit-probe.py`
- `viewtest-new-cases.txt` — the four cases added to the normal green suite, so the probe is
  evidence and the suite is the guard.
- `targeted-suites.txt` — `ctest --test-dir build -R '^(markdown|wordwrap|transcriptgaps|panestatus|calllines|board)$'`, 6/6.
- Whole suites, run after the change: `ViewTest` 55/55, `FoldLayerTest` 32/32
  (`RELAY_ENGINE_TEST=ViewTest build/engine/relay-engine-tests`).

## Limits

Driven on the libvterm core, the only one that builds on this machine; the GhosttyCore path is
untouched and untested here. The selection rule for a partly covered wide cell now matches the
paint exactly; it was not consistent before, in either direction.
