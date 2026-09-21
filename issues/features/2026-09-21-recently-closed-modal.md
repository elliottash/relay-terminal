---
id: C8KM
type: work
status: needs-verification
labels: [feature, actions, sessions]
assignee: codex
implemented_by: openai/gpt-6-astra via codex
rank: m
created: '2026-09-21'
source: Codex in Relay, 2026-09-21
links: {plans: [], commits: [], evidence: [docs/qa_evidence/2026-09-21-recently-closed-modal/], related: [H8SS], github: null}
---
# Open Recently closed in a modal from Actions

## Issue
same with recently closed

## Plan
Remove the expanded recently-closed submenu from Actions. Make its existing launcher open a modal containing the shared Recently closed ListView, preserving search, previews and restore behavior. Keep the Sessions tab available. Verify the list tests and a full GUI close/reopen cycle.

## Execution Summary
Actions has one Recently closed… launcher opening a searchable modal, with the expanded per-item menu removed. The shared list factory preserves previews, discard/clear controls and the Sessions tab. Build and closedlist tests passed; isolated GUI close/reopen flow passed. Evidence: docs/qa_evidence/2026-09-21-recently-closed-modal/.

## Tests
ctest:closedlist
manual: docs/qa_evidence/2026-09-21-recently-closed-modal/evidence.md

## QA checklist
- [ ] Actions shows one Recently closed… launcher, with no inline closed-item list.
- [ ] Open the modal, filter and preview a closed item; Enter or Reopen restores it and closes the modal.
- [ ] Escape/Close cancels; discard and confirmed clear still work.
- [ ] Sessions › Recently closed remains available.
