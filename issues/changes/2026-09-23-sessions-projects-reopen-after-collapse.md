---
id: P4C7
type: work
status: needs-verification
labels: [bug, gui, sessions]
assignee: codex
implemented_by: openai/gpt-6-sol via codex
rank: m
created: '2026-09-23'
source: Codex in a Relay pane, 2026-09-23
links: {plans: [], commits: [], evidence: [docs/qa_evidence/2026-09-23-sessions-project-collapse/], related: [], github: null}
---
# Keep Sessions project groups collapsed across refreshes

## Issue
also, the collapse button for projects in the sessions pane isnt working, it uncollapses a few seconds later.

## Done means
- Collapsing a project group keeps it collapsed when a new Sessions query result rebuilds the list.
- Expanding the group again keeps it expanded through later refreshes; other groups retain their own state.
- A focused widget test reproduces the delayed-refresh case and passes after the fix.

## Plan
**Goal.** Keep the reader's project group expansion choice through Sessions list refreshes.

**Findings.** `SessionManager::rebuildTree()` destroys every row, and `groupFor()` sets every new group expanded. It currently restores only session-row expansion by session id.

**Steps.** Record user group expand/collapse changes by grouping mode and group name. Apply those choices when `groupFor()` recreates a group. Add a widget test that collapses a project, feeds another result, and checks both collapse and re-expansion.

**Risks.** A group can disappear under a filter and later return, so the state must live beyond a single tree rebuild. Rebuild-time expansion changes must not overwrite the stored user choice.

**Verify.** Build and run the focused GUI test and conversations suite; capture the collapsed project in the pane.

## Execution Summary
`SessionManager` now records user-collapsed group names per grouping mode and restores them when refreshed results rebuild the tree. It reapplies the collapsed state after restoring the selected session, because Qt can expand that session's parent during selection. The widget test covers refresh, disappearance and return under filtering, and explicit re-expansion.

![Alpha project remains collapsed after a refreshed result](docs/qa_evidence/2026-09-23-sessions-project-collapse/sessions-project-collapsed.png)

## Tests
- `scripts/relay-build --target relay-conversations-tests` — passed.
- `QT_QPA_PLATFORM=offscreen RELAY_SHOT_DIR="$PWD/docs/qa_evidence/2026-09-23-sessions-project-collapse" build/relay-conversations-tests collapsedProjectStaysCollapsedAcrossResults` — passed; failed at the post-refresh assertion before selection restoration was fixed.
- `QT_QPA_PLATFORM=offscreen ctest --test-dir build -R '^conversations$' --output-on-failure` — passed (1/1).
- Manual evidence: `docs/qa_evidence/2026-09-23-sessions-project-collapse/notes.md`.
