---
id: KZHX
type: work
status: inbox
labels: [bug, remote]
rank: zzzzzzzzzzzzzzzzzzzzzzzzzzz
created: '2026-09-25'
links: {plans: [], commits: [], evidence: [], related: [], github: null}
---
# Two tests fail at HEAD: guest scratch parity and board digest leak

## Issue
At clean HEAD (3512773d), two tests fail: tests.test_guest_board_bridge.BridgeTests.test_native_catalog_parity_and_relay_policy_dispatch (native pane tools scratch_dir and scratch_release are not bridged to guest sessions — the #DVV2 scratch tools landed without guest parity) and tests.test_board_chat.WorkerConsoleTest.test_the_same_tab_gets_its_conversation_back_and_another_tab_does_not ("the board today" listing leaks into the Session-so-far digest). Found while running targeted suites for #GREM; verified both fail on a clean export of HEAD, so they predate it.
