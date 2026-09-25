---
id: X6XV
type: work
status: needs-verification
labels: [bug, agent]
assignee: agent
implemented_by: glm/glm-5.3
session: 1e29c6d7-27b1-4017-9d49-c073e0f3b89c
rank: zzzzzzzzzzzzzzzzzzzzi
created: '2026-09-24'
verify: {artifact: code, primary: script, also: [], human: none, criteria: '`tests.test_queue` new case passes (the one board-plan failure is pre-existing and has its own card); `relay-consolemode-tests` runs `anIdleQueueChangedClearsABusyFlagNothingWillFinish` with no FAIL line.', sign_off: none, effort: low}
links: {plans: [], commits: [], evidence: [], related: [], github: null}
---
# Esc cannot clear a pane wedged busy by a mid-turn model switch

## Issue
"can you see whats going on in this pane, it seems stuck. check if its a bug 90a4f0c2" / "esc isnt working to interrupt it, so we need to fix the bug preventing that"

## Done means
- A `set_model` switch or compaction that runs as the queue's exclusive task emits `queue_changed` when it takes the running slot and when it frees it, so no pane is left holding a running id that nothing will ever finish.
- A pane wedged busy (a refused ask carrying `agent_busy`, or a stale running id) recovers the moment the queue reports nothing running, queued or steering — exactly what Esc's `cancel` is answered with on an idle queue, so Esc always clears it. `m_guestBusy` is untouched: a TUI guest runs in-pane, not through the queue.
- Both covered by tests that fail before the change.

## Tests
- `python3 -m unittest tests.test_queue` — `SupervisorTests.test_exclusive_tasks_announce_the_running_slot` passes (44 ran; one pre-existing board-plan failure with its own card: `test_a_card_turn_is_refused_the_writers_at_call_time_and_told_why`).
- `relay-consolemode-tests` (offscreen) — `anIdleQueueChangedClearsABusyFlagNothingWillFinish` passes, zero FAIL lines (a timing-flaky `h2kqStripStopGone` from session 234z failed once, passed on rerun; unrelated path).
- land.py verify build of the exact landed tree passed for both commits (`63d77a72`, `e3772311`).

## Execution Summary
Root cause (observed on pane 90a4f0c2, 2026-09-24 16:54–16:55): switching a busy pane from kimi-k3 to guest:claude refused the in-flight switch's ask (`protocol_error kind=ask "An agent turn is already active."`, worker.log 20:54:45) and ran the switch as the queue's `set_model` exclusive task. That task takes the queue's running slot without a turn and never emitted `queue_changed` when it ended, and the GUI clears its busy flag only from an `agent_finished` matching the running id it holds — so the pane wedged busy with nothing in flight. Esc sends `cancel`, the queue is idle, and the `queue_changed(running=None)` it is answered with did not touch `m_agentBusy`: Esc was structurally unable to clear the state.

Fix: `backend/relay_core/queue.py` — `start_exclusive_locked` and the exclusive runner's finally both emit `_changed_locked()`. `src/PaneEvents.cpp` — the `queue_changed` handler clears a stale `m_agentBusy` (with the agent_finished teardown: `m_currentItem.clear()`, `stopTurnClock()`, `m_idleTip.start()`) when the event reports nothing running, queued or steering. Landed as `63d77a72` (code + python test) and `e3772311` (consolemode test hunks 1 and 4 only; hunks 2, 3, 5 are session 234z's in-flight work, left uncommitted for them).

The already-running binary keeps the wedge until restart: close/reopen the pane or restart Relay to unstick the live instance.
