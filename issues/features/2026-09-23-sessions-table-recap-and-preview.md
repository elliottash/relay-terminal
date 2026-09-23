---
id: PV7W
type: work
status: needs-verification
labels: [feature, gui, sessions]
assignee: codex
implemented_by: openai/gpt-6-sol via codex
rank: m
created: '2026-09-23'
source: Codex in a Relay pane, 2026-09-23
links: {plans: [], commits: [a90f5e423aef80a8a6a4e0f3ddf01b8193d1c2ae], evidence: [docs/qa_evidence/2026-09-23-sessions-recap-preview/], related: [Y4MT, RCP9], github: null}
---
# Show recap in the Sessions table and open preview on demand

## Issue
can we change it where the preview box is not open by default. instead, the last column is actually the final recap summary from the convo (we might need to discuss, that those should always be generated for closed sessions so they get saved for here).

then you can double click to preview, and there is a preview (p) button to the left of the resume button, and that will replace the sessions table with a preview of the selected convo.

## Done means
- The Sessions list uses its full width at first, with a last Recap column populated from a saved conversation summary when present; its header sorts A→Z / Z→A like the other columns.
- Selecting a row alone keeps the table visible and does not fetch a full preview.
- Double clicking a session or clicking Preview (P), beside Resume, replaces the table with that conversation's preview; a clear Back action returns to the table without resuming it.
- Enter and Resume still resume, and the preview is usable by keyboard.
- The closing-session summary-generation policy is recorded separately from the display change if the owner has not decided it.

## Plan
**Goal.** Make Sessions a recap-first table with an explicit full-preview view.

**Findings.** `src/Conversations.cpp` currently places `sessionsTree` and `conversationPreview` side by side in a splitter and requests a preview on selection. `backend/relay_core/conv_index.py` already sends a persisted `summary` on every list row; automatic summaries exist but are cadence based.

**Steps.** Replace the split view with a stack for table and preview; put the saved summary in the last column; wire double-click, Preview (P), Back, and keyboard handling; retain the existing worker preview and Resume path. Add focused widget coverage for state transitions and summary text.

**Risks.** Missing summaries need an honest empty state. Automatic generation on close requires a separate decision because it spends model calls and changes session persistence.

**Verify.** Build the conversations test target, run the new interaction test, and capture table/preview screenshots with representative rows.

## Execution Summary
The Sessions table now occupies the full list area and has a Recap column showing the saved summary or “No recap saved.” Recap can be sorted A→Z / Z→A from its header or the Sort menu. Selection remains in the table without fetching a transcript. Double click, Preview (P), or P on the selected row opens the full-width preview; Back or Escape returns, while Enter and Resume retain their resume behavior. The mouse actions trigger a P shortcut hint. Automatic final recap generation on close awaits the decision on #RCP9.

![Full-width Sessions table with Recap column](docs/qa_evidence/2026-09-23-sessions-recap-preview/list.png)

![Full-width conversation preview replacing the table](docs/qa_evidence/2026-09-23-sessions-recap-preview/preview.png)

## Tests
- PASS: `scripts/relay-build --target relay` built the full app.
- PASS: `scripts/relay-build --target relay-conversations-tests` built the widget tests.
- PASS: six focused Sessions widget cases under Xvfb (8 passed, 0 failed): `sessionsTableShowsRecapAndPreviewsOnDemand`, `headerClickSortsByThatColumn`, `rowsCarryTheirSummaryAndTags`, `unfoldAsksOnceAndFillsFromTheOverview`, `summariesFromTheButtonAndTheBatch`, `groupRowsSpanTheWidth`.
- PASS: `PYTHONPATH=backend python3 -m unittest tests.test_conv_index` (122 passed).
- PARTIAL: full `conversations` suite: 49 passed, 1 failed, 2 skipped. The one failure is the earlier dropdown audit test's mouse click on No grouping when run in the full suite; it passes in isolation. See evidence notes for commands and scope.
- PASS: `git diff --check` for the changed source and test paths.
