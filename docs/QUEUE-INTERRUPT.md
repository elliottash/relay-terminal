# Interrupt vs. queue for new submissions

Source: `issues/feature_intake.txt`. The backend for agent prompts is
implemented in `backend/relay_core/queue.py` and wired into `backend/worker.py`.
The GUI does not use it yet. The shell-command variant is a design only.

## Agent prompts: rules

- One dispatcher thread runs every turn, so two turns never overlap.
- Every accepted prompt goes through one ordered queue and gets an item `id`.
- `when: "now"` (the default) keeps the old behavior. It is refused while a turn
  runs or while an unpaused queue is draining. It is allowed while the queue is paused.
- `when: "queue"` appends. The prompt starts immediately when the agent is idle.
- `when: "interrupt"` puts the prompt ahead of ordinary queued prompts, then stops
  the running turn with the existing `Agent.stop` semantics. Incomplete tool-call
  groups are rolled back and a "reinspect state" note is added to the history.
  The new prompt starts only after the old turn's thread has returned. A slow exit
  (a stalled read of up to 30 seconds, or a tool process being killed) delays it
  but never loses it. Several interrupts in a row run first-in, first-out.
- `cancel` stops the running turn, drops interrupts waiting for it, and **pauses**
  the queue. A failed turn (outcome `error`) also pauses the queue, because tool
  actions may already have run. Queued prompts then wait for `resume_queue`.
  `now` and `interrupt` submissions still run while paused. The pause clears
  automatically once the queue is empty.
- `configure` and `reset` are refused while a turn runs. When idle, both clear
  the queue, since queued prompts belonged to the old conversation.
- The queue holds at most 32 prompts. Prompts are validated on submission.

## Protocol (frontend to worker)

```json
{"type":"ask","id":"<request id>","text":"...","when":"now|queue|interrupt"}
{"type":"cancel"}
{"type":"resume_queue"}
{"type":"queue_remove","id":"<request id>","item":"<queued item id>"}
{"type":"queue_clear"}
```

`queue_remove` uses `item` for the queued prompt, because `id` is the request id
echoed on errors. Removing a prompt that already started is an error.

## Events (worker to frontend)

```json
{"event":"queued","id":"<item>","request_id":"<request id>","when":"queue","position":1}
{"event":"interrupting","id":"<running item>","by":"<new item>"}
{"event":"agent_started","id":"<item>"}
{"event":"agent_finished","id":"<item>","outcome":"done|error|cancelled"}
{"event":"queue_changed","running":"<item>|null","paused":false,
 "items":[{"id":"<item>","preview":"first 120 chars","forced":false}]}
```

Existing `delta`, `tool_*`, `done`, `error`, and `cancelled` events are
unchanged. `done`/`error`/`cancelled` still end each turn, followed by `agent_finished`.
Protocol errors still carry `agent_busy`.

## What the GUI must do

1. Treat `agent_started`/`agent_finished` as the busy signal. After `done` the next
   queued turn may start at once, so do not re-enable "now" submission on `done` alone.
2. Map `request_id` to `id` from `queued`, and render the queue from `queue_changed`,
   which is authoritative. Show a "paused" state with a Resume action.
3. When busy, submitting asks for a choice (Warp style). Suggested bindings: Enter
   queues, Ctrl+Shift+Enter interrupts, with a one-time explanation. Never default to
   interrupt: it cancels work in progress.
4. Say plainly in the log that an interrupt does not roll back completed actions.

## Shell commands while a foreground program runs (design only)

The shell bridge only injects a command when Readline is at a prompt: tty
noncanonical, shell owns the foreground process group, fresh prompt sequence.
While a program runs, injecting text would feed that program's stdin. That is
never done silently.

Proposed behavior for a shell-routed submission while the terminal is busy:

- **Queue (default).** Keep a frontend-side FIFO of commands, shown above the composer.
  On each new ready-prompt event (new sequence, readiness verified as today), stage and
  submit the head command through the normal acknowledged-hash path. Stop draining if
  the previous command's exit status is nonzero, unless the user opted into "continue on
  failure". This mirrors the agent's pause-on-error rule. The queue lives in the GUI,
  not the Python worker, because readiness comes from the shell bridge and tty checks.
- **Interrupt.** Send SIGINT to the foreground process group through the PTY (Ctrl+C),
  the same as the existing Interrupt shell action. Then treat the command as queued at the
  head. If no ready prompt arrives within a timeout (such as 3 seconds, for programs that
  trap SIGINT like vim or a REPL), do not escalate to SIGTERM or SIGKILL automatically.
  Leave the command queued and tell the user the program did not exit.
- **Never** interrupt a full-screen or alternate-screen program, or a remote SSH/tmux
  session, without an explicit confirmation. Those sessions are native-mode only in v0.1.
- The queue is cleared when native mode is entered, and when the shell exits or
  restarts, so stale commands cannot fire into a different context.

No worker protocol is needed for the shell variant. Routing already happens before
dispatch, and the GUI owns the ready-state machine.
