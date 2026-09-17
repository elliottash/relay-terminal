# Queue or interrupt agent prompts sent while the agent is busy

- **Status**: in-progress
- **Component**: gui, worker
- **Milestone**: desktop-alpha
- **Workstream**: agent
- **Acceptance evidence**: backend: `tests/test_queue.py`; GUI: a recorded run where a prompt
  submitted mid-turn is queued, another interrupts, and the queue panel shows both
- **Assignee**: unassigned (backend implemented by Claude Opus 5, 2026-09-16)
- **Source**: `issues/feature_intake.txt`, "the interrupt vs queue feature for new commands"

## Context

Warp lets a new prompt either wait for the running agent turn or stop it. Relay's worker
used to refuse any prompt while a turn ran. The backend now supports both modes; the
app does not use them yet, so a busy agent still rejects new prompts in the GUI.

Protocol and rules: `docs/QUEUE-INTERRUPT.md`. Implementation: `backend/relay_core/queue.py`.

## Desired behavior

- Submitting while busy queues the prompt by default. A separate binding interrupts.
  Interrupt is never the default because it cancels work in progress.
- The agent pane shows running and queued prompts, lets the user remove one, and shows a
  paused state with Resume after a cancel or failed turn.
- Busy state follows `agent_started` / `agent_finished`, not `done`.

## Acceptance criteria

1. Enter while busy sends `ask` with `when: "queue"`; the prompt runs after the current turn.
2. The interrupt binding sends `when: "interrupt"`; the log says completed actions are not rolled back.
3. Queue list renders from `queue_changed`; remove and clear work; Resume appears when paused.
4. The terminal-first fallback (see `features/needs_qa_llm/2026-09-17-terminal-first-agent-fallback.md`)
   queues instead of dropping text when the agent is busy.

## Promise ledger

| Commitment | State | Evidence |
|---|---|---|
| Backend dispatcher: queue, interrupt, remove, clear, pause/resume, no concurrent turns | source-tested | `tests/test_queue.py`, 79-test suite passing 2026-09-17 |
| GUI wiring and queue panel | planned | |
| Approval-related queue events | deferred | Removed with agent approvals on 2026-09-17 |
