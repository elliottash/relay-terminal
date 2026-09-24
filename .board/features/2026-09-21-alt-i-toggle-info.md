---
id: T6GK
type: work
status: needs-verification
labels: [feature, keyboard]
assignee: codex
implemented_by: openai/gpt-6-astra via codex
rank: m
created: '2026-09-21'
source: User request in Relay, 2026-09-21
links: {plans: [], commits: [878c544b863b6f7a07169602a8b308ca0d39cc7d], evidence: [docs/qa_evidence/2026-09-21-alt-i-toggle/], related: [CSMK], github: null}
---
# Alt+I toggles the info pane closed

## Issue
make pressing alt i again close the info pane.

## Execution Summary
The conversation-info action closes an existing info pane and restores focus to its owner. With an info pane focused, it uses that pane's owner even if another terminal was active more recently. Saved-session/thread navigation still opens its requested info view. The existing info-button shortcut hint already uses the live key binding.

## Tests
`manual: docs/qa_evidence/2026-09-21-alt-i-toggle/verification.md`

GUI driver: `python3 docs/qa_evidence/2026-09-21-alt-i-toggle/drive.py`
Build: `scripts/relay-build --target relay`

## QA checklist
- Press Alt+I in a terminal: info opens; press it again: info closes and typing returns to the owner composer.
- Press Alt+I a third time: info reopens. Escape still closes it.
- With two terminals, open info for each and confirm Alt+I closes the focused info pane and returns to its owner.
- The info button and a rebound agent.info shortcut use the same toggle.
