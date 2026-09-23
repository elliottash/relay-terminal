---
id: C7NQ
type: work
status: needs-verification
labels: [feature, sessions, gui]
assignee: agent
implemented_by: openai/gpt-6-sol via codex
session: 49dbf51d-820d-4c71-8c2c-f411cacebb78
rank: m
created: '2026-09-23'
source: Codex in a Relay pane, 2026-09-23
links: {plans: [], commits: [], evidence: [docs/qa_evidence/2026-09-23-remove-continue-C7NQ/sessions-by-project.png], related: [], github: null}
---
# Remove the Continue section from Sessions

## Issue
can you remove that section

## Done means
- Sessions shows no Continue heading when the search is empty.
- Unfinished, pinned, and recently closed sessions remain visible in their normal project, date, or ungrouped position.
- Search, selection, and grouping continue to work; the targeted Sessions test passes.

## Execution Summary
Removed the special Continue tree group. Pinned, unfinished, and recently closed sessions now appear once in their selected project, date, or ungrouped view. Screenshot: `docs/qa_evidence/2026-09-23-remove-continue-C7NQ/sessions-by-project.png`.

## Tests
`scripts/relay-build` (pass)
`QT_QPA_PLATFORM=offscreen build/relay-conversations-tests managerGroupsByProjectAndSearches rowsCarryTheirSummaryAndTags groupingKeepsUnfinishedSessionsInTheirRegularPlace collapsedProjectStaysCollapsedAcrossResults` (pass)
`ctest --test-dir build -R '^conversations$' --output-on-failure` (fails at `sessionsDropdownsRespondToMouseChoices`: choosing the "none" grouping via a simulated mouse click returned false; 48 passed, 1 failed, 2 skipped)
manual: `docs/qa_evidence/2026-09-23-remove-continue-C7NQ/sessions-by-project.png`
- `scripts/relay-build --target relay-conversations-tests` — passed on the current checkout.
- `QT_QPA_PLATFORM=offscreen build/relay-conversations-tests managerGroupsByProjectAndSearches rowsCarryTheirSummaryAndTags groupingKeepsUnfinishedSessionsInTheirRegularPlace collapsedProjectStaysCollapsedAcrossResults -silent` — 6 passed.
- `ctest --test-dir build -R '^conversations$' --output-on-failure` — 51 passed, 1 failed, 2 skipped; unrelated offscreen mouse popup test `sessionsDropdownsRespondToMouseChoices` failed. `QT_QPA_PLATFORM=xcb xvfb-run -a build/relay-conversations-tests sessionsDropdownsRespondToMouseChoices -silent` — 3 passed.
- Full suite with `QT_QPA_PLATFORM=xcb xvfb-run -a build/relay-conversations-tests -silent` — 51 passed, 1 failed, 2 skipped; unrelated `paneInfoPopoverCopiesAndKeepsTheInfoClick` visibility assertion failed under Xvfb.
