---
id: DRP7
type: work
status: needs-verification
labels: [feature, composer]
assignee: codex
implemented_by: openai/gpt-6-astra via codex
rank: m
created: '2026-09-21'
source: Codex in a Relay pane, 2026-09-21
links: {plans: [], commits: [7329eee2e3cbbad779cc1db6032234aaa017f294], evidence: [docs/qa_evidence/2026-09-21-remove-issues-dropdown/], related: [], github: null}
---
# Remove the prompt issues dropdown

## Issue
i dont think we need the little issues dropdown. what do you think? what is it doing that is necessary

## Decisions
“yeah, remove it.”

## Plan
Remove the work button, menu and their private history. Preserve task refresh callbacks and the current-card header. Keep the plan indicator at the left of the strip. Plans still open when written and their file paths remain in the conversation. Build and inspect an isolated GUI.

## Execution Summary
Removed the prompt issues dropdown, its menu, and its private recent-card/latest-plan bookkeeping. Preserved task refresh wiring and the current-card header. The PLAN indicator stays on the strip’s left. Plan files still open when written, with their paths printed in the conversation. Alt+I is unchanged.

## Tests
manual: docs/qa_evidence/2026-09-21-remove-issues-dropdown/notes.md

## QA checklist
- Confirm there is no issues dropdown beside the prompt.
- Toggle plan mode with Shift+Tab: PLAN appears at the left.
- Verify tasks still refresh during a turn and Ctrl+Shift+K opens the task list.
- Open a current card from the pane header; use # to reference cards and Ctrl+Shift+S for the Switchboard.
- Confirm a written plan still opens and its printed file path can reopen it.
