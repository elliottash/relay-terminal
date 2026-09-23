---
id: GBN4
type: work
status: inbox
labels: [bug, guests, switchboard]
rank: m
created: '2026-09-23'
source: 'Found by Claude Code in a Relay pane, 2026-09-23, while running the board tests for the write-limit change (bd48f2c4)'
links: {plans: [], commits: [], evidence: [], related: [MEMS], github: null}
---
# A guest's board comment is missing from the thread when its turn fails afterwards

## Issue
Found while running the board tests; not a user request. `tests.test_guest_board_bridge.BridgeTests.test_provider_turn_binds_native_context_and_revokes_on_failure` fails on main.

## Planning notes
Reproduced on a clean `git archive` export of HEAD at bd48f2c4 (after the #MEMS commits 28e74f5e and f4cd1def touched the bridge), so it is not the write-limit change:

```
PYTHONPATH=backend python3 -m unittest tests.test_guest_board_bridge.BridgeTests.test_provider_turn_binds_native_context_and_revokes_on_failure
AssertionError: 'native identity' not found in '<!-- relay:entry … author=agent kind=event turn=setup -->
- ✦ agent created this card in Inbox · issues/features/2026-09-23-bridge-test.md
'
```

The guest calls `board_comment` (no error returned), then the turn raises. The thread holds only the setup entry, so the comment either never reached disk or was rolled back with the failed turn. Decide which is intended: the test says a write made before the failure stays and carries `model=claude-live-model`.

## Done means
The test passes, and a guest's board write made before its turn fails is either kept in the thread with its model stamp or deliberately discarded with the test changed to say so.
