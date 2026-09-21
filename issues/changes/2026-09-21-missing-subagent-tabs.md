---
id: T8SB
type: work
status: needs-verification
labels: [bug, subagents, ui]
assignee: codex
implemented_by: openai/gpt-6-astra via codex
rank: m
created: '2026-09-21'
source: User request in Relay, 2026-09-21
links: {plans: [], commits: [05a6ac725852f130fa24233e0cae632366442c1f], evidence: [docs/qa_evidence/2026-09-21-subagent-tabs/], related: [WD83, S8FT], github: null}
---
# Show all subagents in the tabbed pane

## Issue
also the tabbed subagents arent working, its not showing both of them in separate tabs like i want

## Plan
`SubagentTabsView::syncRows` only updates tabs already opened individually, while opening the pane hides the full agent list. Populate missing tabs from the model and add later arrivals without changing the selected tab or focus. Preserve explicit tab closure and reopening. Verify with the subagents Qt suite and an isolated Xvfb two-tab check.

## Execution Summary
The subagent pane now creates a separate tab for every listed agent on opening and adds later arrivals without changing the selected transcript or input focus. Explicitly closed running tabs remain closed through progress updates and can be reopened. Evidence: `docs/qa_evidence/2026-09-21-subagent-tabs/`.

## Tests
`ctest -R ^subagents$`
manual: docs/qa_evidence/2026-09-21-subagent-tabs/

## QA checklist
- Start two subagents, open the pane once, and confirm two separate tabs are visible.
- Select each tab and confirm it shows that agent's transcript.
- Start another agent while typing in a tab; confirm its tab appears without stealing focus or losing the draft.
- Close a running tab; confirm it stays closed through updates and reopens when selected from the agent entry point.
