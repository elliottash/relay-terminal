---
id: JEWY
type: work
status: needs-verification
labels: [feature, shortcuts, qa]
assignee: agent
implemented_by: openai/gpt-6-sol via codex
session: c74ef5a7-e3b2-4741-b4d4-251871e1398e
rank: zzzzzzzzzzzzzzzzzzzr
created: '2026-09-24'
verify: {artifact: code, primary: script, also: [probe], human: none, sign_off: none, effort: low, stakes: rework, blast: capability}
source: owner, Relay conversation, 2026-09-24
links: {plans: [], commits: [fd0c8150], evidence: [tests/keymap_test.cpp], related: [BX7B, QWAS], github: null}
---
# Ctrl+Shift+R opens the Review pane

## Issue
shoudl ctrl shift r open the review pane?

## Plan
Goal: make Ctrl+Shift+R open or focus the Review pane.

Findings: the key is free in the current default keymap; pane.restartShell lost that key under #QWAS. The Review pane already exists behind the Board's Review button.

Steps: register review.open with the default key, route it to openReviewPane, show it in Actions and app actions, and teach the shortcut from the Board button. Check a keymap test and the targeted app command and pane tests.

Risk: existing user key overrides can take precedence; preserve them. Verify with the focused build and tests.

## Done means
Ctrl+Shift+R opens or focuses the Review pane in the active tab, without invoking shell restart. Review appears in Actions with that key. Clicking Board › Review teaches the live shortcut. User key overrides still take precedence.

## Tests
- `ctest -R '^keymap$'` — tests/keymap_test.cpp (passed locally)
- `ctest -R '^appcommands$'` — tests/appcommands_test.cpp (passed locally)
- `ctest -R '^reviewpane$'` — tests/reviewpane_test.cpp (passed locally)
- `scripts/relay-build --target relay-keymap-tests --target relay-appcommands-tests --target relay` (passed)
- `scripts/land.py commit jewy-review-key` (exact landed tree built)

### Check 2026-09-25 21:01
- passed · ctest:keymap — ctest -R keymap passed for this revision on spark-dcc9, 2026-09-26T01:01:27Z
- passed · ctest:appcommands — ctest -R appcommands passed for this revision on spark-dcc9, 2026-09-26T00:55:41Z
- passed · ctest:reviewpane — ctest -R reviewpane passed for this revision on spark-dcc9, 2026-09-24T19:35:46Z
history: thread
## Execution Summary
Added `review.open` with Ctrl+Shift+R across the built-in keymaps. The action opens or focuses the existing Review pane, appears in Actions and the agent-safe action catalog, and the Board Review button teaches the current binding. The shell restart action remains available through its banner and Actions. Landed as `fd0c8150`; the exact committed tree built.
