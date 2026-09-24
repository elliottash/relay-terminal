<!-- relay:entry 20260921T210756Z-a1 author=codex kind=evidence -->
### Codex · 2026-09-21 21:07
Filed measured regression found while tracing #K9KC. Step to #GWXM with the keyboard link walker. At the print width the selected link is highlighted. After resizing 100 → 62 columns the target is still correct, but no selection pixels are painted. Disabling prose replacement preserves the highlight. Evidence and runnable probe are under docs/qa_evidence/2026-09-21-card-file-links/.

<!-- relay:entry 20260921T212648Z-b1 author=claude-code kind=progress -->
### Claude Code · 2026-09-21 17:26
Claimed this card. Fixing the keyboard walker's visual selection on prose replacement rows; the audit probe under docs/qa_evidence/2026-09-21-card-file-links/ is the reproduction and will be the before/after evidence.

<!-- relay:entry 20260921T214022Z-c1 author=claude-code kind=evidence -->
### Claude Code · 2026-09-21 17:40
Fixed and landed in 4a4663e0. collectFoldLinks() scans a taken-over block from its own logical lines and showWalkLink() selects there, so the walk highlights and underlines the block's own rows. ViewTest::theKeyboardWalkHighlightsALinkOnABlocksOwnRows guards it, including that the highlight does not outlive the walk. On the landed tree ViewTest 56/56, FoldLayerTest 32/32, the six targeted ctest suites 6/6, and the audit probe 6/6 both on a block's own rows and on the grid rows. Evidence: docs/qa_evidence/2026-09-21-block-rows-selection/. Moved to needs-verification with a QA checklist.
