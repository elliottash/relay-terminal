---
id: R0PE
type: work
status: needs-verification
labels: [feature, subagents, ui]
assignee: codex
implemented_by: openai/gpt-6-astra via codex
rank: mr0pe
created: '2026-09-22'
source: Codex in a Relay pane, 2026-09-22
links: {plans: [], commits: [7e901529cb2d46335f06b109fbe50d865d15b1bc, 12169c6a58e48defef66b08bd5ea9d4023bd52cb], evidence: [docs/qa_evidence/2026-09-22-subagent-role-summary/], related: [], github: null}
---
# Prefix subagent tracker summaries with their role

## Issue
i dont think i want to show those in the tracker like that. instead, pre-pend "general: " or "explore: " to the summary

## Done means
The tracker identifier shows only the subagent ID, such as a1.
General summaries have no role prefix. Other roles use a bracketed prefix, such as [explore] or [signal].
Tracker selection, model controls and task pairing keep working.

## Decisions
"dont show it for general"
"and actually do [explore] and [signal]"

## Plan
Change tracker painting in src/SubagentsPanel.cpp; keep role metadata intact. Build and run the existing subagent and strip-layout tests, and inspect a rendered tracker under Xvfb.

## Tests
`ctest --test-dir build -R '^(subagents|striplayout)$' --output-on-failure`
manual: docs/qa_evidence/2026-09-22-subagent-role-summary/

## Execution Summary
Tracker identifiers show only a1/a2. General summaries have no prefix; other summaries use bracketed roles, including [explore] and [signal]. Reduced identifier column width recovers summary space. Existing subagent and strip-layout tests pass; Xvfb rendering confirms all three roles. Evidence: docs/qa_evidence/2026-09-22-subagent-role-summary/.
