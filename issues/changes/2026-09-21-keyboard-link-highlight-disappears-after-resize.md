---
id: J4WK
type: work
status: inbox
labels: [bug, terminal]
rank: m
created: '2026-09-21'
source: 'Codex regression audit requested by owner, 2026-09-21'
links: {plans: [], commits: [], evidence: [docs/qa_evidence/2026-09-21-card-file-links/commit-audit.md], related: [K9KC, GWXM], github: null}
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
