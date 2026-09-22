---
id: SRA7
type: work
status: needs-verification
assignee: codex
labels: [bug, terminal]
rank: msra7
created: '2026-09-22'
source: 'User report in Relay; observed on sphinxpad, 2026-09-22'
links: {plans: [], commits: [], evidence: [docs/qa_evidence/2026-09-22-SRA7/], related: [], github: null}
---
# Pane resize should preserve the top visible scrollback location

## Issue
when pane sizes change and you are in scrollback, it moves you around. it shoudl maintain the locaion at the top of the pane (similar to browsers)

## Done means
- A pane scrolled into history keeps its top content through height and width changes, allowing the anchored text to rewrap onto the containing row.
- Expanded fold content retains the top logical text location when it rewraps.
- A pane following live output continues following the bottom; missing or trimmed anchors clamp to retained content.

## Plan
**Goal:** Preserve the top visible content through terminal geometry changes.
**Findings:** `engine/core/LibVtermCore.cpp` resets scroll offset on resize; `engine/view/TerminalView.cpp` retains a numeric visual row across fold reflow. Ghostty supports tracked grid references.
**Steps:**
1. Preserve a history content anchor through core reflow and row resizing.
2. Preserve the top fold logical line/cell, or mapped real row, across view layout changes.
3. Add focused core/view regressions, stage screenshots where feasible, and land with evidence.
**Risks:** Reflow merges rows and history trimming can remove the anchor; clamp to the nearest retained row. Keep keyboard dispatch outside this change.
**Verify:** Targeted CoreTest and ViewTest resize cases under offscreen/Xvfb, exact-tree commit build, board validation.

## Execution Summary
Preserved the top history content through core resizing/reflow, including a tracked grid anchor for Ghostty. The view restores the logical top line/cell of expanded folds and prose after layout, while live output still follows the bottom. Root cause was libvterm resetting scroll offset plus the view retaining a numeric visual row across reflow.

Evidence: `docs/qa_evidence/2026-09-22-SRA7/` (test output and staged Xvfb screenshots). Keyboard zoom dispatch is unchanged.

## Tests
- `RELAY_ENGINE_TEST=CoreTest build/engine/relay-engine-tests resizePreservesHistoryTop resizeClampsTrimmedHistoryTop reflowOnResize rowsOnlyResizeKeepsCursorBelowAFullRow`
- `RELAY_ENGINE_TEST=ViewTest QT_QPA_PLATFORM=xcb xvfb-run -a build/engine/relay-engine-tests resizeKeepsTopContent resizeKeepsFoldTopContent resizeKeepsProseTopContent proseReflowsOnResize aFoldStaysUnderItsLineAcrossAResize aResizeKeepsTheMatchesInsideTheFold compressedProseKeepsFollowingOutputVisible`
- manual: docs/qa_evidence/2026-09-22-SRA7/README.md
