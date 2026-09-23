---
id: D2PX
type: work
status: needs-verification
labels: [bug, sessions]
assignee: codex
implemented_by: openai/gpt-6-sol via codex
rank: m
created: '2026-09-23'
source: User report in Relay pane, 2026-09-23
links: {plans: [], commits: [], evidence: [docs/qa_evidence/2026-09-23-linked-sessions/], related: [J8QP], github: null}
---
# Linked guest and Relay sessions appear twice in Sessions

## Issue
also there is a weird bug, where these first two rows both open the same session

## Done means
- When a Relay session wraps a Codex or Claude session, the combined Sessions list shows one row for that conversation.
- Selecting only the guest source still finds the guest transcript, and the guest session remains directly resumable.
- The link survives index migration and a restart, and removing the Relay session reveals the guest row again.

## Plan
**Goal:** make the Sessions list use the persisted Relay-to-guest link when showing a combined source view.

**Findings:** the Relay session file stores `guest` and `guest_session`, but the conversations index does not. It therefore returns both the Relay session and its guest transcript as separate rows.

**Steps:** index that link with an in-place schema migration and backfill; hide the guest row in combined searches when a linked Relay row exists; add a focused search and migration test.

**Risks:** source-only views must continue to expose the guest transcript, and old agent rows must be reindexed after migration.

**Verify:** targeted `test_conv_index.py` cases and an isolated index built from linked Relay and guest sessions.

## Execution Summary
The index now persists each Relay session's linked guest source and id. Combined Sessions searches fold linked Claude and Codex guest rows into their Relay row; selecting a guest source alone still exposes the native transcript. A v6 index migrates in place and reindexes existing session files on the next reconcile. Read-only inspection found 35 Claude and 95 Codex linked pairs in the user's current data. [Evidence](docs/qa_evidence/2026-09-23-linked-sessions/README.md).

## Tests
- `PYTHONPATH=backend:tests python3 -m unittest test_conv_index.GuestRowTests.test_linked_relay_session_appears_once_in_combined_list test_conv_index.MigrationTests.test_a_v6_index_backfills_the_link_to_its_guest_session` — passed, both sources and migration.
- `PYTHONPATH=backend:tests python3 -m unittest test_conv_index` — 124 tests passed.
