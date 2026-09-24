---
id: AH7K
type: work
status: needs-verification
labels: [feature, activity]
assignee: codex
implemented_by: openai/gpt-6-sol via codex
rank: mah7k
created: '2026-09-22'
source: User request via Codex in a Relay pane, 2026-09-22
links: {plans: [], commits: [d8e53e5088178b2520e3a9a68b79c2da0b5d097e], evidence: [docs/qa_evidence/2026-09-22-activity-history/], related: [QT8C, 4X53], github: null}
---
# Show historical activity when opening Activity

## Issue
add a card feature -- would be better if opening activity also showed historical activity

## Done means
Opening Activity after agent turns shows their recent tool calls and available reasoning in turn order, before new live events.
Closing and reopening Activity does not duplicate turns or print historical rows into the terminal.
The view clearly indicates the retained history limit; a live turn continues normally.

## Plan
Goal: Show retained turns when Activity opens.
Findings: `Pane` keeps up to 50 completed `turn_summary` events and reasoning buffers; `AgentInternalsView` already renders the corresponding turn, thinking, and tool rows.
Steps: Save each completed turn's request with its summary; seed a new Activity view from the retained records; keep historical rows out of the close and reprint ledger; test opening and reopening.
Risk: Activity after an app restart has no in-memory summaries to replay. Verify the current-session behavior and make the limit visible in the view.
Verify: focused console integration check and a live GUI capture.

## Execution Summary
Opening Activity now seeds the view with the owning pane's retained completed turn summaries (up to 50). It shows available reasoning and tool-call labels in order, then continues with live events. Historical rows are excluded from the close/reprint ledger, so reopening does not repeat them in the terminal.

![Activity showing a completed turn's reasoning and tool call](docs/qa_evidence/2026-09-22-activity-history/01-activity-history.png)

History is retained for the current app process; older persisted sessions do not yet provide these summaries.

## Tests
`scripts/relay-build --target relay-consolemode-tests`
`QT_QPA_PLATFORM=offscreen ./build/relay-consolemode-tests --activity-history-only`
manual: `docs/qa_evidence/2026-09-22-activity-history/01-activity-history.png`
