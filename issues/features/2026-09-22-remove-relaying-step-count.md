---
id: STPC
type: work
status: needs-verification
labels: [feature, ui]
assignee: codex
implemented_by: openai/gpt-6-astra via codex
rank: mstpc
created: '2026-09-22'
source: Codex in a Relay pane, 2026-09-22
links: {plans: [], commits: [8af57af2], evidence: [docs/qa_evidence/2026-09-22-STPC/result.md], related: [R3YN], github: null}
---
# Remove the step count from the Relaying notifier

## Issue
remove the step count item from the relaying... notifier.

## Done means
- Active turns show action, elapsed seconds and stop/skip hint without a step count.
- Model-request status updates keep the notifier active without showing step counters.

## Tests
- `ctest -R consolemode`
- manual: docs/qa_evidence/2026-09-22-STPC/result.md

## Execution Summary
Removed the step-count item and its unused Pane storage. The Relaying notifier retains the current action, elapsed seconds and stop/skip shortcut. Relay builds and the existing consolemode test passes. Live visual evidence is recorded in docs/qa_evidence/2026-09-22-STPC/result.md.
