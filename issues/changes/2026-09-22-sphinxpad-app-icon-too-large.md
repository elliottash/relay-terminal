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
At 1×, 1.5× and 2× display scaling it has the same logical size, stays centered, and neither clips nor expands the tab row.

## Plan
**Goal:** constrain the app icon to a proportionate chrome size.
**Findings:** the old `QIcon::pixmap(QSize(22,22) * appDpr)` double-scales on Qt 6: the direct Qt5/Qt6 probe shows old QLabel hints of 22/33/44 logical pixels at DPR 1/1.5/2 on Qt 6, versus 22 throughout on Qt 5. Preserve the intended 22 logical pixels and paint using the current device.
**Steps:**
1. Add a bounded, paint-time app icon widget in `src/WindowChrome.h`, using Qt icon painting for the current screen.
2. Preserve the intended 22-pixel box (the first 18-pixel implementation is superseded following the Qt-version probe). Replace only the icon creation block in `buildWindowChrome`, preserving artwork, tooltip, and dragging.
3. Build and stage the real chrome widgets under Xvfb at 1×, 1.5× and 2×, recording screenshots and geometry checks.
**Risks:** sphinxpad's precise compositor/DPI setup is unavailable locally; staged scaling establishes proportionality but not a remote machine verdict. Keep shared header edits narrow.
**Verify:** targeted compiled widget assertions and staged screenshots, local Relay build, exact-tree landing build, board validation.

## Execution Summary
Confirmed Qt6 double scaling in the old QIcon pixmap request: the 22px intent became 33 logical pixels at DPR 1.5 and 44 at DPR 2. Qt5 did not reproduce this. Replaced the cached app-wide-DPR pixmap with a fixed **22×22 logical-pixel** `ChromeAppIcon`, painting the original artwork against the current paint device. This preserves the intended design size; the first 18px implementation is superseded.
Only `src/WindowChrome.h` and the icon construction block in `src/RelayWindow.h` changed. Other agents' shared-header hunks were excluded. Evidence in `docs/qa_evidence/2026-09-22-ATP7/` includes direct Qt5/Qt6 measurements, @2x asset behavior, Qt6 before/after images at fractional/2× scaling, and live app screenshots. No sphinxpad-specific compositor check was performed.

## Tests
- `scripts/relay-build --target relay` — PASS after restoring the intended 22px size.
- `python3 docs/qa_evidence/2026-09-22-ATP7/stage.py` — PASS at 1×, 1.5×, 2×; icon 22×22 and bell 26×26, with icon contained in its corner.
- `manual: docs/qa_evidence/2026-09-22-ATP7/dpi-probe.log` — compiled probe passes on Qt5.15.13 and Qt6.4.2 at all three scales; old Qt6 QLabel grows to 33/44 logical pixels. @2x variant preserves DPR2 when selected.
- `manual: docs/qa_evidence/2026-09-22-ATP7/qt6-compare-1.5.png` — inspected old/new rendering comparison.
- `manual: docs/qa_evidence/2026-09-22-ATP7/qt6-compare-2.png` — inspected old/new rendering comparison.
- `python3 scripts/relay-board.py check` — existing unrelated board diagnostics; none for ATP7. `tests_check` unavailable in exposed bridge.
