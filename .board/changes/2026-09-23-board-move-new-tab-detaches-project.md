---
id: G4VB
type: work
status: needs-verification
labels: [bug, board, gui]
assignee: codex
rank: m
created: '2026-09-23'
source: 'user in Relay, 2026-09-23'
links: {plans: [], commits: [4b1df0327181], evidence: [docs/qa_evidence/2026-09-23-board-move-G4VB/04-moved.png], related: [JN7X, A0SF], github: null}
---
# Moving the Board into a new tab loses its project

## Issue
potential bug: moving baord to new tab detaches it from a project and then the helper agent mistakenly says the project has no board

## Done means
Moving a Board pane to a new tab by action or drag attaches that tab to the Board's project. The moved Board and its helper can still read that project's cards. A generic pane moved into a new tab keeps the existing unattached behavior.
Failure is a moved Board shown with no project chip, or its helper reporting that the project has no board.

## Plan
**Goal.** Keep the Board's own project attached when its pane becomes a new tab.

**Findings.** `RelayWindow::adoptLeafAsTab` creates an unattached tab for every pane. The Board view retains its workspace, while `boardSettingsFor` and the helper use the tab's attachment. `BoardView::setTabId` is called only when the pane is first created.

**Steps.**
1. In the common new-tab adoption path, attach a moved Board pane to its view's workspace and update the view's tab id. Leave generic panes unattached.
2. Add focused regression coverage for the new-tab path and existing generic-pane rule.
3. Build and run the targeted test; record evidence and land the change.

**Risks.** The Board view's helper conversation is keyed by tab. Updating the tab id must rebind it to the destination without changing the Board's project. Other sessions are editing `RelayWindow.h`; land only this change's hunk.

**Verify.** Build with `scripts/relay-build`, run the `boardworkspace` test, and inspect the resulting new-tab path.

## Execution Summary
`adoptLeafAsTab` now attaches a moved Board to the Board view's project and updates its tab id before the new helper is asked to work. Generic panes remain unattached. A live drag under Xvfb moved the Board into a new tab titled `relay-terminal`, with that project's cards visible:

![Board in a new project tab](docs/qa_evidence/2026-09-23-board-move-G4VB/04-moved.png)

## Tests
`scripts/relay-build --target relay-boardworkspace-tests` — passed.
`QT_QPA_PLATFORM=offscreen ./build/relay-boardworkspace-tests movedBoardKeepsItsProjectInANewTab` — passed.
`scripts/relay-build --target relay` — passed.
`ctest --test-dir build -R '^boardworkspace$' --output-on-failure` — 25 passed, one unrelated existing failure: `anOptionOrSessionLinkOpensWhereItNames` expects the old `Pane::openOutputTarget` signature (already tracked on the Sphinxpad build gate card).
Manual Xvfb drag: `docs/qa_evidence/2026-09-23-board-move-G4VB/04-moved.png`.
