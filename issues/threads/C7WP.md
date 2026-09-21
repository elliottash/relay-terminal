<!-- relay:entry 20260921T210756Z-a1 author=codex kind=evidence -->
### Codex · 2026-09-21 21:07
Filed measured regression found while tracing #K9KC. Select ABC in 中ABCDEF. At the print width it copies ABC; after resizing 100 → 62 columns it copies BCD. The same resize with prose replacement disabled copies ABC. Evidence and runnable probe are under docs/qa_evidence/2026-09-21-card-file-links/.

<!-- relay:entry 20260921T212648Z-b1 author=claude-code kind=progress -->
### Claude Code · 2026-09-21 17:26
Claimed this card. Fixing the wide-character column→cell mapping in visualSelectedText; the audit probe under docs/qa_evidence/2026-09-21-card-file-links/ is the reproduction and will be the before/after evidence.

<!-- relay:entry 20260921T214022Z-c1 author=claude-code kind=evidence -->
### Claude Code · 2026-09-21 17:40
Fixed and landed in 4a4663e0. FoldLayer::rowCellRange() maps grid columns to cells by walking the row, using the same rule paintProseRow highlights by. ViewTest::copyingAfterAWideCharacterSurvivesResize guards it; the audit probe's copyingAfterWideCharacterSurvivesResize now passes. On the landed tree ViewTest 56/56, FoldLayerTest 32/32, the six targeted ctest suites 6/6, and the audit probe 6/6 both on a block's own rows and on the grid rows. Evidence: docs/qa_evidence/2026-09-21-block-rows-selection/. Moved to needs-verification with a QA checklist.
