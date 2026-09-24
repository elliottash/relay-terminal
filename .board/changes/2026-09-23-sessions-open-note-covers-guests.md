---
id: J8QP
type: work
status: needs-verification
labels: [bug, sessions, gui]
assignee: codex
implemented_by: openai/gpt-6-sol via codex
rank: m
created: '2026-09-23'
source: User in Relay, 2026-09-23
links: {plans: [], commits: [b81cc1145d5c70c3540e786e7feccdb12d739b89], evidence: [docs/qa_evidence/2026-09-23-sessions-focus-open/], related: [E7FP, R6J0], github: null}
---
# Sessions open note covers guest sessions and other windows

## Issue
go ahead and add the "is open" note before rebuilding

## Done means
An open Relay, Claude, or Codex conversation has a visible open note on its Sessions row.
The note reflects panes in any Relay window and disappears after the pane closes or changes sessions.
Source-specific guest identities cannot mark a different guest's row as open.

## Plan
**Goal.** Make the Sessions list's existing open badge describe every conversation that the Resume action would find.

**Findings.** `openSessionIds()` lists only Relay session IDs in its own window, while Resume separately finds guest panes across all windows. The list is fed at creation and on closed-list changes, so its badge can become stale.

**Steps.** Collect open identities across windows, key guest IDs by source, and refresh the list from the existing window status poll. Keep the existing badge and Enter behavior.

**Risks.** A guest harness pane has both a Relay wrapper session and a guest session; both are open and may appear as separate rows.

**Verify.** Targeted Sessions tests, Relay build, and an isolated GUI run.

## Execution Summary
The Sessions list's existing `open` badge now uses source-qualified guest IDs and collects live panes across Relay windows. The window status poll refreshes the badge when pane identity changes. `openBadgeDistinguishesGuestSources` verifies that a Claude session does not mark a Codex or Relay row with the same ID as open. The isolated GUI run verified repeated Shift+Enter focus, but its synthetic sessions did not complete provider configuration, so that screenshot does not show the badge.

## Tests
- `scripts/relay-build --target relay relay-conversations-tests` — passed.
- `QT_QPA_PLATFORM=offscreen build/relay-conversations-tests openBadgeDistinguishesGuestSources` — 3 passed (including setup and cleanup).
- `QT_QPA_PLATFORM=offscreen build/relay-conversations-tests keysReachEveryAction rowsCarryTheirSummaryAndTags openBadgeDistinguishesGuestSources aGuestRowResumesForksAndSaysWhoseItIs` — 6 passed.
- Manual: `docs/qa_evidence/2026-09-23-sessions-focus-open/`; the synthetic profile did not finish configuring its provider, so badge visibility remains for a verifier to check with a live session.
