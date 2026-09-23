---
id: P7CK
type: work
status: needs-verification
labels: [bug, ui]
assignee: codex
implemented_by: openai/gpt-6-astra via codex
rank: mplck
created: '2026-09-22'
source: Codex in a Relay pane, 2026-09-22
links: {plans: [], commits: [5bbbc029b1811b129a36c82194bc5139eea77c5e], evidence: [docs/qa_evidence/2026-09-22-plan-chip-click/], related: [], github: null}
---
# Click PLAN to leave plan mode

## Issue
clicking the "plan" button in the prompt box should disable it.

## Done means
- Clicking the visible PLAN chip switches the pane to build mode and hides the chip.
- The prompt draft remains intact and typing can continue in the prompt box.
- The chip retains its compact styling and teaches the configured plan shortcut.

## Tests
- manual: docs/qa_evidence/2026-09-22-plan-chip-click/README.md

## Execution Summary
PLAN is now a styled button that requests build mode through the existing mode setter, returns focus to the composer, and shows the configured shortcut hint. Real-Pane mouse event checks verify the build request, hidden chip after acknowledgment, preserved draft and focus. App build passed; the broader console run found an unrelated queue assertion recorded on QFF1. TestsCommands.check_card (tests_check fallback) reports no findings.
Landed as 5bbbc029. The exact commit tree builds relay-consolemode-tests; its focused plan tests also passed under Xvfb/xcb with isolated settings.
