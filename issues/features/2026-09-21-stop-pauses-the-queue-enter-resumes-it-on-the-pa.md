---
id: 7JD1
type: work
status: planned
labels: [feature, queue, remote]
priority: 1
rank: zzzzzzzzzzzzzzzzz
created: '2026-09-21'
links: {plans: [], commits: [], evidence: [], related: [CTRN, AGNT, SXF1], github: null}
---
# Stop pauses the queue, Enter resumes it — on the pane and on a phone

## Issue
Owner, 2026-09-21, on the queue after a Stop: "why don't we just copy the functionality and have enter resume."

Today a Stop (Esc, ✕, a phone's `board_cancel`) pauses the worker's queue for that surface, and the only way back is the **Resume** button in the pane's queue-strip header. A device has no such button and no `resume_queue` request (REMOTE-PROTOCOL §17.1), so a prompt a phone queued behind a turn it stopped waits until someone at the desktop presses Resume.

Decision: one rule on every surface — **Stop pauses the queue; Enter resumes it.** Enter with text submits that prompt and resumes the queue behind it; Enter on an empty prompt box resumes without submitting. The Resume button may stay as the mouse path. A device gets the same: its next `ask`/`board_ask` resumes the surface's queue, and an empty send resumes. Nothing is moved back into the prompt box, cleared or duplicated.

Owner's earlier alternative (one queued item on touch, Stop returns the text to the box) was set aside for this: fewer states, and the phone copies the pane rather than getting its own behaviour.

## Plan
**Goal.** After a Stop, pressing Enter resumes the paused queue on every surface — pane, console, phone — with no new state.

**Steps.**
1. **Pane/console (`src/Pane.h`, `src/QueueSubmit.*`, `tests/queuesubmit_test.cpp`, `consolemode`):** a submit while `queuePaused()` sends the prompt and then `resume_queue` (or the worker resumes on the submit — pick the side that keeps a terminal pane's wire unchanged when nothing is paused); Enter on an empty box while paused calls `resumeAgentQueue()` instead of sending `Continue` — check the Ctrl+Enter rule (#SXF1: Ctrl+Enter on an empty box always sends `Continue`; plain Enter on an empty box does nothing today) and keep it. The Resume button stays. Applies to a card's console the same way, with the surface on the op (#CTRN).
2. **Worker (`backend/relay_core/queue.py`, `worker.py`, `tests/test_queue.py`):** a `submit` on a paused supervisor resumes it (`resume_queue` implied), so a device's `ask`/`board_ask` resumes; an explicit `resume_queue` from a device is accepted. Document in AGENT-SESSIONS-PROTOCOL §12.
3. **Remote (`backend/relay_core/remote_session.py`, `docs/REMOTE-PROTOCOL.md` §17.1/§17.4, the phone view):** a device's empty send resumes; §17.4's "waits for the desktop" paragraph goes. Live check with the phone harness (docs/REMOTE-AND-MULTIPLAYER-DESIGN.md, sphinxpad if a LAN test is needed) or the remote protocol's tests.

**Verify.** Queue tests for each surface; a live pane drive: stop a turn with a queued prompt, press Enter on the empty box → the queued prompt runs; type and Enter → the new prompt runs, then the queued one. Runs after #CTRN's final pass lands (it holds the queue code).

**Owner's decision, taken.** Enter resumes; the earlier one-item/return-to-box proposal is not built.
