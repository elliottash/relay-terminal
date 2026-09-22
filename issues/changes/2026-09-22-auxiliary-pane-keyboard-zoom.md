---
id: AZP7
type: work
status: needs-verification
assignee: codex
labels: [bug, keyboard, panes]
rank: mazp7
created: '2026-09-22'
source: 'User report in Relay; observed on sphinxpad, 2026-09-22'
links: {plans: [], commits: [], evidence: [docs/qa_evidence/2026-09-22-AZP7/README.md], related: [Z00M], github: null}
---
# Ctrl plus/minus does not zoom auxiliary panes

## Issue
ctrl +/- wouldnt zoom auxiliary panes (like the activities pane), ctrl + mousewheel worked.

## Done means
Ctrl+plus/minus changes the focused auxiliary transcript's text size, including Activity.
Ctrl+0 restores its initial size, including after Ctrl+wheel; terminal zoom still targets the terminal.
Zoom from an auxiliary pane never changes its owner terminal.

## Plan
**Goal:** Route keyboard zoom to the active auxiliary text surface.
**Findings:** `src/RelayWindow.h` dispatches terminal.zoom exclusively to m_active; Activity uses a read-only QPlainTextEdit whose Qt Ctrl+wheel handler already zooms.
**Steps:**
1. Add shared auxiliary text target and zoom handling, retaining the terminal path.
2. Capture the initial font before keyboard or wheel zoom and teach the shortcut on wheel use.
3. Run focused Qt regression tests and stage the UI under isolated Xvfb; land evidence.
**Risks:** Avoid zooming editable ask/composer fields or an inactive terminal; shared RelayWindow.h edits need hunk review.
**Verify:** Focused auxiliary zoom Qt tests, build gate, and staged Activity keyboard/wheel evidence.

## Execution Summary
Fixed keyboard zoom dispatch for auxiliary read-only text panes, including Activity. It selects the active transcript instead of the owner terminal, supports reset after wheel or keyboard zoom, and teaches the configured shortcut on Ctrl+wheel. Unsupported auxiliary panes leave the terminal alone.
Evidence: docs/qa_evidence/2026-09-22-AZP7/README.md, including staged Activity screenshots.

## Tests
`ctest -R auxiliaryzoom`
manual: docs/qa_evidence/2026-09-22-AZP7/tests.txt
manual: docs/qa_evidence/2026-09-22-AZP7/README.md

### Check 2026-09-22 19:37
- passed · ctest:auxiliaryzoom — ctest -R auxiliaryzoom passed for this revision on spark-dcc9, 2026-09-22T23:37:05Z
- not-applicable · manual:docs/qa_evidence/2026-09-22-AZP7/tests.txt — manual evidence, recorded by hand: docs/qa_evidence/2026-09-22-AZP7/tests.txt
- not-applicable · manual:docs/qa_evidence/2026-09-22-AZP7/README.md — manual evidence, recorded by hand: docs/qa_evidence/2026-09-22-AZP7/README.md
- warning · card — none of the listed tests is named after anything this card changed (issues/changes/2026-09-22-auxiliary-pane-keyboard-zoom.md, issues/changes/2026-09-22-codex-missing-from-priorities.md, issues/changes/2026-09-22-openrouter-priorities-refresh.md…)
history: thread
