---
id: QG4C
type: work
status: needs-verification
labels: [bug, guest, terminal]
assignee: codex
rank: m
created: '2026-09-21'
source: 'Claude Code in a Relay pane, 2026-09-21'
links: {plans: [], commits: [16fd7eddbec2dd261be180fb22714cdb61bdf789], evidence: [docs/qa_evidence/2026-09-21-harness-steering/README.md], related: [], github: null}
---
# The command queue does not work with claude / codex

## Issue
the command queue doesnt seem to work with claude / codex

## Discussion points
Two different surfaces answer to "claude / codex", and the queue is a different mechanism in each:

- **A TUI guest in the pane** (§26.8: `claude` or `codex` running as the foreground program, the
  Relay composer typing into it). Here a submitted line is typed into the guest when it waits at
  its input and held in the pane's queue while `guest_busy`.
- **A guest harness preset** (§29, `guest:claude` / `guest:codex` as the pane's own model, which is
  what the model box now picks). Here a queued prompt is an ordinary agent entry and waits on
  `agent_finished` like any provider's.

The GUI log for this evening shows only harness panes in Relay (`preset=guest:claude`,
`guest_in_front=` empty), and the three TUI `claude`/`codex` processes on the machine belong to
Warp, not to Relay — so the reproduction matters before anything is changed.

## Plan
**Goal:** Deliver harness steering during the guest turn and acknowledge only accepted input.
**Findings:** `HarnessProvider.complete` spans the whole CLI turn; `Agent.ask` drains steering only outside it. `TurnSupervisor.take_steer` currently acknowledges immediately.
**Steps:**
1. Validate installed native steering protocols.
2. Add transactional queue delivery and harness tool-boundary injection, preserving request context.
3. Cover timing, failures, duplicates and unchanged ordinary queue behavior using fake processes.
**Risks:** Guest protocols differ; an input accepted near completion must never be discarded as stale.
**Verify:** Targeted Python harness/queue tests, then owner live acceptance test; no paid guest turn.

## Tasks
- [x] Implement and test harness steering. <!-- t:h1 -->
- [ ] Owner tests harness and separately implemented TUI fixes live. <!-- t:h2 -->

## Execution Summary
Harness steering now uses native Codex turn/steer and Claude UUID-correlated stream input at tool events. Queue and ledger delivery wait for acknowledgement. Unaccepted input is retained, ambiguous transport failures pause the turn, and Claude result boundaries do not discard follow-ups. Two existing cancellation test fixtures now wait for provider entry, removing start-event timing races. TUI changes are owned by the separate sibling agent.

## Tests
`tests/test_guest_harness_steer.py`
`tests/test_guest_harness_provider.py`
`tests/test_guest_harness_codex.py`
`tests/test_guest_harness_claude.py`
`tests/test_queue.py`
`manual: docs/qa_evidence/2026-09-21-harness-steering/README.md`

## QA checklist
- [ ] Owner tests next-tool-call steering in fresh Claude and Codex harness panes.
- [ ] Verify normal queued prompts still wait and no steering appears twice.
- [ ] Owner tests separately implemented TUI fixes.
