---
id: C7WP
type: work
status: inbox
labels: [bug, terminal]
rank: m
created: '2026-09-21'
source: 'Codex regression audit requested by owner, 2026-09-21'
links: {plans: [], commits: [], evidence: [docs/qa_evidence/2026-09-21-card-file-links/commit-audit.md], related: [K9KC, GWXM], github: null}
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
