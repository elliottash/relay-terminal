---
id: N8F4
type: work
status: needs-verification
labels: [feature, sessions, gui]
assignee: codex
rank: m
created: '2026-09-23'
source: Codex in a Relay pane, 2026-09-23
links: {plans: [], commits: [56db2ed6e62570f7018d06ead0caf51182badfcf], evidence: [docs/qa_evidence/2026-09-23-newest-first-N8F4/], related: [EV45], github: null}
---
# Keep the session manager newest first by default

## Issue
in the session manager, always put newest first as the default sort

## Done means
- Opening Sessions and entering or clearing a search leaves the default sort at Newest first, with the Updated arrow pointing down.
- Other sorts, including Best match, remain available when selected explicitly and persist as the query changes.
- The worker receives no explicit sort for the default, so paging uses its newest-first order.

## Plan
**Goal.** Keep Newest first as the session manager's default through search changes.

**Findings.** `src/Conversations.cpp` switches from `recent` to `relevance` when text is entered. The worker already defaults to `recent`.

**Steps.** Remove the automatic search sort switch and its unused state. Update focused Qt assertions for search, header arrows, and an explicit Best match selection.

**Risks.** The source and test files have other sessions' edits. Land only this change's hunks.

**Verify.** Build and run the targeted conversations Qt tests; capture the Sessions widget with a search and Newest first selected.

## Execution Summary
Searching now leaves the session manager's selected sort alone. The default remains Newest first; Best match and the other sorts are available as explicit choices. The searched widget shows the selected sort, Updated arrow, and rows in newest-first order: ![Sessions search sorted newest first](docs/qa_evidence/2026-09-23-newest-first-N8F4/newest-first-search.png)

## Tests
`scripts/relay-build --target relay-conversations-tests` (pass)
`RELAY_SHOT_DIR=docs/qa_evidence/2026-09-23-newest-first-N8F4 QT_QPA_PLATFORM=offscreen build/relay-conversations-tests searchingKeepsNewestFirstUntilTheUserChoosesAnotherSort headerClickSortsByThatColumn` (pass: 4/4)
manual: `docs/qa_evidence/2026-09-23-newest-first-N8F4/newest-first-search.png`
