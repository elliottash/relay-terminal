# One combined queue for terminal commands and agent prompts

- **Status**: needs-qa-llm
- **Component**: gui
- **Milestone**: desktop-alpha
- **Workstream**: terminal
- **Acceptance evidence**: a non-Claude model QA session runs the checklist and records it under `docs/qa_evidence/`
- **Assignee**: implemented by Claude Opus 5 (GUI D subagent), 2026-09-17
- **Source**: `issues/feature_intake.txt` (2026-09-17): "allow queueing of terminal commands and agent commands -- make them colored or show up differently in the queue eg a different icon."; owner decision 9 in `issues/features/2026-09-17-agent-sessions-planning-subagents.md`

## Behavior as implemented

- Each pane has one ordered queue in the GUI. An item starts immediately when nothing is queued ahead of it and its resource is free (agent idle / shell at an idle prompt); otherwise it waits. Only one queued item runs at a time, so an agent prompt queued after a command waits for that command, and a command queued after an agent prompt waits for that turn.
- Items started directly (nothing queued) do not block the other resource: a command can run while an agent turn started earlier is still working.
- Agent items are sent to the worker with `when: "now"` only when it is their turn; the worker's own queue is no longer used by the GUI.
- Fix-loop turns (Ctrl+Shift+Enter) are inserted at the head.
- Strip over the bottom of the terminal: `QUEUE` / `QUEUE · PAUSED`, the running item, then rows with `$` (amber) for commands and `✦` (cyan) for agent prompts, and × to remove. Drag rows to reorder. Resume and Clear buttons; palette items "Clear queue" and "Resume queue".
- Pauses: a queued command exits non-zero; the agent turn is stopped or fails (not when replaced by an interrupt); the worker refuses a prompt; editing the next item.
- Keyboard selection and editing: see `2026-09-17-agent-queue-steering-and-editing.md`.

## Implementer check (not a QA verdict)

Under Xvfb with an isolated config and Kimi K3:
- During `sleep 7`: queued `echo queued-one`, an agent prompt, `echo queued-three` → ran in exactly that order (`implementer-strip-terminal-and-agent.png`, `implementer-order-one-agent-three.png`).
- `false` queued behind `sleep 4`, then `echo after-false` → queue PAUSED; palette Resume ran `after-false` (`implementer-failure-pauses-resume.png`).
- Drag `echo drag-C` above `drag-B` → ran A, C, B (`implementer-drag-reorder.png`); × removed `x-two` → ran x-one, x-three (`implementer-remove-x.png`).
- Evidence folder: `docs/qa_evidence/2026-09-17-combined-queue/`.

## Known gaps

- When an agent turn and a queued command run at the same time, the agent's inline header can land on the redrawn shell prompt line (Readline redraw timing).
- The queue is per pane and not persisted across restarts.

## QA checklist

1. Queue a mix of commands and agent prompts while both a command and an agent turn run; confirm execution order equals entry order.
2. A failing queued command pauses the queue; Resume continues; Clear empties it.
3. Drag reorder and × remove; Up-selection keys; editing the next item.
4. Ctrl+Shift+Enter fix loop still runs its fix turn and re-run before other queued items.
