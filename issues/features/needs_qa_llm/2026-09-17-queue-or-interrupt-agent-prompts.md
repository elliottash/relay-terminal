# Queue or interrupt agent prompts sent while the agent is busy

- **Status**: needs-qa-llm
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
| GUI wiring and queue panel | source-tested | Xvfb run 2026-09-17, `docs/qa_evidence/2026-09-17-queue-interrupt-gui/` |
| Approval-related queue events | deferred | Removed with agent approvals on 2026-09-17 |

## GUI implementation (2026-09-17)

Implemented by Claude Opus 5 (Claude Code session).

- Agent prompts always go through the worker queue: `when:"queue"` (or `"now"` while the queue is
  paused, which the backend allows). They start at once when idle and wait when busy. The prompt
  is echoed ("› …") when its turn starts, not when queued.
- `agent.interrupt`, default Ctrl+Alt+Enter (and keypad Enter), acts only from the prompt box: it
  sends the prompt-box text with `when:"interrupt"` and prints "Interrupting the current turn;
  completed actions are not rolled back." With an idle agent it behaves like a normal submit.
- Busy state follows `agent_started` / `agent_finished`; `done` no longer clears it.
- Queue strip floats over the bottom of the terminal (no terminal resize, so the shell does not redraw
  its prompt mid-output): running prompt, numbered queued prompts with ×, "PAUSED · Resume", "Clear"
  when more than one is queued. Hidden when empty and not paused.
- Request ids map to queue item ids via `queued`; prompts removed or cleared are forgotten.
- Inline output is closed (prompt redrawn) only when no further turn will start, which fixes the prompt
  landing inside the next queued turn's output.
- Fix-and-rerun turns are queued behind a busy agent instead of being refused, tagged so their
  `relay-run` block is read from their own turn. Program-running context is attached at submit time.
- Palette: "Clear agent queue" and "Resume agent queue" in the Agent section when applicable.

Implementer check under Xvfb with Kimi K3: A (tool `sleep 25`) running, B and C queued; × removed C;
Ctrl+Alt+Enter with D stopped A ("Stopped …") and ran D-DONE then B-DONE; C never ran. E running +
F queued, Stop agent → "AGENT QUEUE · PAUSED" with F; palette "Resume agent queue" ran F-DONE.
Ctrl+Shift+Enter `lss -la` still fixed to `ls -la` and succeeded. During one run the worker reported
"Agent error (AttributeError)" on stop; a direct worker check minutes later cancelled cleanly, and the
concurrent skills-feature backend edits were in progress at the time — re-check on QA.

## QA checklist (GUI)

1. Queue three prompts while one runs; order of execution matches the strip; × removes the right one.
2. Ctrl+Alt+Enter with text interrupts; with an empty prompt box it only shows a hint.
3. Stop agent with prompts queued shows PAUSED; Resume and palette resume both work; a new prompt while paused runs immediately.
4. Stopping or interrupting never shows "Agent error"; the queue is not paused by an interrupt.
5. New chat and model switching are refused while a turn runs and clear the queue when idle.
6. Fix loop (Ctrl+Shift+Enter on a typo) while another agent turn runs: the fix is queued and still runs the fixed command.
