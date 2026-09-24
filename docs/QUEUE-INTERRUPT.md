# Queueing, interruption and terminal context

The agent and terminal have separate FIFO queues in each pane (card #XCXD).
Each resource can advance while the other is busy. A shell command must still wait
for a verified shell prompt: queued command text never becomes unsolicited input to
a foreground program. A standalone guest TUI owns one input channel, including its
`!command` submissions; those deliveries stay serialized.

## Composer keys

| Key | Behavior |
|---|---|
| Enter with text | Submit using the current route; queue when that resource is busy. |
| Empty Enter during an agent turn | Steer the oldest queued agent prompt into the running turn. |
| Next Enter in that sequence | Request interruption for that same prompt. If it was already delivered, report that instead of selecting another. |
| Ctrl+Enter with text | Send immediately to the agent, interrupting its current turn if necessary. |
| Ctrl+Enter, empty, agent idle | Send `Continue`. |
| Ctrl+Enter, empty, agent busy | Ask for a prompt; do not select queued work. |
| Ctrl+Shift+Enter | Submit to the terminal; this selects a destination, not an interrupt. |
| Shift+Enter | Insert a newline. |
| Up from an empty box | Recall queued work for editing. |
| Down while navigating queue rows | Move down and out of the queue selection. |
| Enter with a blank answer to an agent question | Skip that question; do not also operate the queue. |

Repeated Enter stays attached to one prompt across asynchronous routing and delivery.
There is no hidden age limit after which a visible queued prompt stops responding.
With agent prompts A and B waiting and a new draft D, Enter appends D; another
Enter steers A. Ctrl+Enter provides immediate delivery of D.

Questions own answer submission before queue actions. A program requesting a line
may receive terminal-directed text directly as its answer. Password fields and
native terminal input retain their own input handling.

## Stop controls

| Running resources | Esc in the composer | Alt+Esc |
|---|---|---|
| Agent only | Stop agent | No shell to interrupt |
| Terminal program only | Stop shell | Stop shell |
| Both | Stop agent | Stop shell |
| Neither | No stop | No stop |

Both running resources display their controls: **Stop agent (Esc)** and
**Stop shell (Alt+Esc)**. A sole running shell displays **Stop shell (Esc)**.
Controls remain available with no waiting items. Labels follow the live keymap.

Queue editing and agent questions do not consume Esc. Stopping preserves the draft.
Popups may consume Esc to close first. In native terminal mode Esc remains input to
the foreground program. Ctrl+C remains copying in the composer; native terminal
Ctrl+C retains the terminal's behavior. There is no additional Ctrl+Esc override.

A stop pauses the corresponding queue before cancellation completes. Waiting items
remain intact, and the other queue can keep running. A terminal interrupt sends the
normal terminal interrupt; it does not escalate automatically to a forced kill.
A command that handles the interrupt can remain running. A send-now replacement is
a separate operation from Stop: it arranges the next prompt rather than simply
pausing work. Queue resume controls name the resource they resume.

## Queue display

Agent work appears on the left, terminal work on the right. One populated queue
uses the available width; a narrow pane stacks the two. Reordering stays within a
resource queue. Each queue shows its own count and pause/resume state. Empty waiting
lists disappear, while controls for active work remain visible.

## Shell evidence at agent turn start

Automatic terminal context is resolved when the agent turn starts, including when
its prompt waited in a queue. New command results and revisions since the previous
snapshot are included with command, working directory, status, exit code when
available, and bounded output excerpts. Running output is explicitly incomplete.
Larger retained output is available through terminal read tools within the sharing
grant.

Explicitly attached output stays pinned to the chosen revision. Manual and disabled
sharing still apply, and changing sharing settings cannot revive a revoked grant.
New output is not inserted automatically into an already running agent conversation.
An explicit read or steering update can supply current evidence when authorized.

Fresh context does not imply a dependency: starting a turn does not wait for shell
completion. A future explicit dependency control must distinguish “wait for this
command” from ordinary independent queueing.

## Backend delivery

The worker's `TurnSupervisor` runs one agent turn at a time. `queue` appends;
`interrupt` schedules a replacement and cancels the current turn; `steer` delivers
inside the current turn at a supported boundary. A delivered steer is not submitted
again merely because another Enter arrived. Unconsumed steers return to waiting
work rather than disappearing.

`cancel` pauses agent work. Queue removal, reordering and resume use stable item IDs.
The GUI owns native shell readiness and delivery. See
[AGENT-SESSIONS-PROTOCOL.md](AGENT-SESSIONS-PROTOCOL.md) for the wire contract.
