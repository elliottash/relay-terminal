---
id: QG4C
type: work
status: needs-verification
labels: [bug, guest, terminal]
assignee: codex
rank: m
created: '2026-09-21'
source: 'Claude Code in a Relay pane, 2026-09-21'
links: {plans: [], commits: [16fd7eddbec2dd261be180fb22714cdb61bdf789, c90b6135f010d9abe8583e3cfbf8ed7fbe910a1e], evidence: [docs/qa_evidence/2026-09-21-harness-steering/README.md], related: [], github: null}
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

### Check 2026-09-23 19:18
- missing-evidence · unittest:tests.test_guest_harness_steer — no run of tests/test_guest_harness_steer.py for this revision, from any host, and no attached result
- passed · unittest:tests.test_guest_harness_provider — tests/test_guest_harness_provider.py passed for this revision on spark-dcc9, 2026-09-23T23:18:12Z
- missing-evidence · unittest:tests.test_guest_harness_codex — no run of tests/test_guest_harness_codex.py for this revision, from any host, and no attached result
- missing-evidence · unittest:tests.test_guest_harness_claude — no run of tests/test_guest_harness_claude.py for this revision, from any host, and no attached result
- missing-evidence · unittest:tests.test_queue — no run of tests/test_queue.py for this revision, from any host, and no attached result
- not-applicable · manual:docs/qa_evidence/2026-09-21-harness-steering/README.md — manual evidence, recorded by hand: docs/qa_evidence/2026-09-21-harness-steering/README.md
- notice · unittest:tests.test_guest_harness_steer — tests/test_guest_harness_steer.py: 2 of 14 never ran here (test_leased_input_stays_visible_and_cannot_be_withdrawn, test_reset_clears_any_idle_lease)
- notice · unittest:tests.test_guest_harness_provider — tests/test_guest_harness_provider.py: 1 of 71 are slow (test_guest_startup_and_changes_report_each_models_supported_efforts)
- notice · unittest:tests.test_queue — tests/test_queue.py: 2 of 41 never ran here (test_a_submit_runs_now_and_resumes_the_paused_queue, test_a_queued_submit_resumes_too_and_relays_own_does_not)
- notice · unittest:tests.test_queue — tests/test_queue.py: 1 of 41 are not in the project any more (test_now_runs_while_queue_paused)
history: thread
## QA checklist
- [ ] Owner tests next-tool-call steering in fresh Claude and Codex harness panes.
- [ ] Verify normal queued prompts still wait and no steering appears twice.
- [ ] Owner tests separately implemented TUI fixes.
