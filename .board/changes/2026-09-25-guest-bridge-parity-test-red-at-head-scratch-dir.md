---
id: G19V
type: work
status: inbox
labels: [bug, guest, scratch]
rank: zzzzzzzzzzzzzzzzzzr
created: '2026-09-25'
links: {plans: [], commits: [], evidence: [], related: [], github: null}
---
# Guest bridge parity test red at HEAD: scratch_dir and scratch_release not in harness equivalents

## Issue
tests.test_guest_board_bridge.BridgeTests.test_native_catalog_parity_and_relay_policy_dispatch fails at git HEAD 2f848bec: "Items in the first set but not the second: 'scratch_dir', 'scratch_release'" — the native tool catalog offers the scratch-ledger tools (#DVV2) but the guest bridge's harness_equivalents set does not map them to the harness equivalents, so Relay-managed Codex/Claude guests lack the scratch ledger the native agents have (also memory #26BN: guest tool parity). Reproduced on a clean copy of HEAD's backend (only board.py/board_tools.py/board_policy.md/tools.py forced to HEAD; result unchanged). Measured 2026-09-25 while landing #EMWF.
