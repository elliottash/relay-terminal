---
id: AGN8
type: work
status: needs-qa-llm
labels: [feature]
component: [gui, worker, agent]
milestone: desktop-alpha
workstream: agent
rank: c6
created: '2026-09-17'
acceptance: a recorded run showing Esc interrupt, queue-after-turn, steer-at-next-tool-call, interrupt-and-send, and keyboard reordering/editing of queued prompts
source: '`issues/feature_intake.txt`, 2026-09-17: - "pressing escape interrupts the agent thinking." - "pressing enter while an agent is thinking / working queues the command. to discuss -- should we send at the next tool call (claude style) or wait until agent is done (warp style)? one idea is, its warp style by default, but press enter again to do claude style and send at next tool call, and then ctrl+enter to interrupt and send immediately." - "press up to start highlighting queued commands. ctrl up/down to move a command up/down the queue (can also drag and drop). press enter on a highlighted queued command to edit it in the prompt box (if its the top command, that will pause queuing). help me think through the various" (the sentence was unfinished when filed)'
links: {plans: [], commits: [], evidence: [], related: [], github: null}
---
# Queue steering, Esc to interrupt, and keyboard editing of queued prompts

## Current behavior (commit eaf9f0d)

- Enter while the agent is busy queues the prompt as a separate turn (Warp style).
- Ctrl+Alt+Enter interrupts the running turn and sends the prompt (composer only).
- Ctrl+Enter always sends to the agent; Esc in the composer switches to native terminal input (take control).
- The queue strip shows running and queued prompts with × remove, Paused/Resume, and Clear. No reorder or edit.
- Backend modes: `now`, `queue`, `interrupt`. No "steer at the next tool call" mode.

## Conflicts with the proposal

1. **Esc** today means "take control of the terminal" from the composer. Interrupting on Esc is the Claude Code convention.
2. **Ctrl+Enter** today means "always the agent". While the agent is busy that meaning is redundant (busy prompts already go to the agent), so Ctrl+Enter can safely mean "interrupt and send now" only while busy.
3. **Up** on the composer's first line browses prompt history.

## Proposed design (for discussion)

| Key | Agent idle | Agent busy |
|---|---|---|
| Enter | Send (route as today) | Queue after the current turn (Warp style) |
| Enter again on an empty composer, right after queuing | — | Upgrade the last queued prompt to **steer**: deliver it at the next tool-call boundary (Claude style) |
| Ctrl+Enter | Send to the agent | Interrupt the turn and send now (replaces Ctrl+Alt+Enter as default) |
| Esc | Take control of the terminal (unchanged) | Interrupt the agent (prompt text stays in the composer) |
| Up (composer empty, queue non-empty) | — | Select the last queued prompt; Up again moves up; past the top falls through to history |
| Ctrl+Up / Ctrl+Down | — | Move the selected queued prompt |
| Enter on a selected queued prompt | — | Pull it into the composer for editing and remove it from the queue; if it was next in line, pause the queue until it is re-sent |
| Delete on a selected queued prompt | — | Remove it |
| Esc in queue selection | — | Leave selection (does not interrupt) |

Mouse: drag and drop in the queue strip to reorder.

Backend work: a `steer` mode that appends the user message after the current step's tool results and before the next model request (never between a tool call and its result); `queue_move {item, index}`; edit = remove + resubmit preserving position. Steered and queued items appear distinctly in the strip ("next tool call" vs "after turn").

## Open questions

1. The unfinished sentence: which cases should be thought through?
2. If a steer prompt arrives while the model is producing its final answer (no further tool call), should it become a queued turn or interrupt?
3. Should Esc interrupt everything (tool command included), or only the model call, letting a running command finish?

## Resolution (2026-09-17, GUI D, implemented by Claude Opus 5)

Implemented in `src/main.cpp` together with the combined queue
(`issues/features/needs_qa_llm/2026-09-17-combined-terminal-agent-queue.md`):

- Enter while busy queues (agent prompts behind the running turn; terminal commands behind the running command), in entry order.
- Ctrl+Enter: always the agent; while the agent is busy it interrupts and sends now. Keymap `agent.interrupt` defaults to Ctrl+Return/Ctrl+Enter plus the old Ctrl+Alt+Return/Enter aliases (prompt box only).
- Esc in the prompt box while the agent is busy stops it; the prompt text stays. Esc otherwise keeps its old meaning.
- Up on an empty prompt box with items queued selects the last item; Up/Down move; past the top falls back to prompt history; Ctrl+Up/Down reorder; Enter pulls the item into the prompt box (editing the next item pauses the queue, and the resubmission goes back to the head and resumes); Delete/Backspace remove; Esc leaves the selection. The edited item keeps its kind (agent or terminal) for one submission without changing the pane's input mode.

**Not implemented:** "Enter again to steer at the next tool call". It needs the backend `steer` mode, which does not exist yet; tracked in `issues/features/2026-09-17-agent-sessions-planning-subagents.md`. Open questions 2 and 3 of this issue remain open.

Implementer check under Xvfb (not a QA verdict): `docs/qa_evidence/2026-09-17-combined-queue/implementer-select-move-edit-head.png`, `implementer-edit-queued-agent-item.png`, `docs/qa_evidence/2026-09-17-agent-esc-ctrl-enter/implementer-esc-stop-ctrl-enter-interrupt.png` (Esc printed "Stopped"; Ctrl+Enter while counting printed "Interrupting the current turn", then `INTERRUPTED-OK`).

QA checklist:
1. With a long agent turn running, Enter queues a prompt; Esc stops the turn; the queue shows PAUSED; Resume runs the prompt.
2. Ctrl+Enter while busy interrupts and runs the new prompt next; queued items keep their order.
3. Up/Down/Ctrl+Up/Ctrl+Down/Enter/Delete/Esc on queued items behave as above; Up past the top shows history.
4. Editing the next item holds the queue; resubmitting puts it back first and resumes.
