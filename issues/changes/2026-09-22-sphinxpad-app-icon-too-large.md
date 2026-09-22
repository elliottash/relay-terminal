---
id: ATP7
type: work
status: needs-verification
assignee: codex
labels: [bug, appearance]
rank: maip7
created: '2026-09-22'
source: 'User report in Relay; observed on sphinxpad, 2026-09-22'
links: {plans: [], commits: [dcf941a726cdb35669aac435e4c0aa698c41d198], evidence: [docs/qa_evidence/2026-09-22-ATP7/], related: [], github: null}
---
# Top-left app icon appears too large on sphinxpad

## Issue
top-left app icon looked too big on sphinxpad

## Done means
The top-left Relay icon stays smaller than its 26-logical-pixel chrome controls, with the original app artwork intact.
At 1× and 2× display scaling it has the same logical size, stays centered, and neither clips nor expands the tab row.

## Plan
**Goal:** constrain the app icon to a proportionate chrome size.
**Findings:** `src/RelayWindow.h::buildWindowChrome` gives an unconstrained QLabel a 22-pixel icon rasterized using application-wide DPR; neighboring controls paint 14-pixel glyphs in 26-pixel buttons.
**Steps:**
1. Add a bounded, paint-time app icon widget in `src/WindowChrome.h`, using Qt icon painting for the current screen.
2. Replace only the icon creation block in `buildWindowChrome`, preserving artwork, tooltip, and dragging.
3. Build and stage the real chrome widgets under Xvfb at 1× and 2×, recording screenshots and geometry checks.
**Risks:** sphinxpad's precise compositor/DPI setup is unavailable locally; staged scaling establishes proportionality but not a remote machine verdict. Keep shared header edits narrow.
**Verify:** targeted compiled widget assertions and staged screenshots, local Relay build, exact-tree landing build, board validation.

## Execution Summary
Replaced the unconstrained 22-pixel QLabel pixmap with an 18×18 logical-pixel `ChromeAppIcon`; QIcon paints against the current device scale. Preserved the complete app artwork, tooltip, and transparent mouse handling. Changes in `src/WindowChrome.h` and only the icon construction block in `src/RelayWindow.h`.
Evidence: `docs/qa_evidence/2026-09-22-ATP7/` contains the reproducible live-app driver, geometry, and inspected screenshots at 1× and 2×. No sphinxpad-specific compositor check was performed.

## Tests
- `scripts/relay-build --target relay` — PASS.
- `python3 docs/qa_evidence/2026-09-22-ATP7/stage.py` — PASS at 1× and 2×; icon 18×18, bell 26×26, bounded corner.
- `manual: docs/qa_evidence/2026-09-22-ATP7/scale-1.png` — inspected, proportionate and unclipped.
- `manual: docs/qa_evidence/2026-09-22-ATP7/scale-2.png` — inspected, proportionate and unclipped.
- `python3 scripts/relay-board.py check` — existing unrelated board diagnostics; none for ATP7. `tests_check` unavailable in exposed bridge.
- `scripts/land.py commit atp7-icon` — PASS: exact materialized tree compiled before landing `dcf941a7`; only the app-icon hunk selected in shared RelayWindow.h.
