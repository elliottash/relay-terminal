# Queue or interrupt shell commands while a foreground program runs

- **Status**: open
- **Component**: gui, shell-integration
- **Milestone**: desktop-alpha
- **Workstream**: terminal
- **Acceptance evidence**: a recorded GUI run: submit two commands during `sleep 5`; both run
  in order after the prompt returns; an interrupt sends one Ctrl+C
- **Assignee**: unassigned
- **Source**: `issues/feature_intake.txt`, "the interrupt vs queue feature for new commands"

## Context

The intake item covers "new commands", not only agent prompts. Today a terminal-routed
submission while a program runs is refused with "A foreground program is running."
A design exists in `docs/QUEUE-INTERRUPT.md`, section "Shell commands while a foreground
program runs". It is not implemented. Split from the agent-prompt issue because it lives in
the GUI and shell bridge, not the worker.

## Desired behavior

- The command queue lives in the GUI and drains one command per verified ready prompt.
- A non-zero exit stops draining and shows the remaining queue.
- Interrupt sends Ctrl+C once and never escalates by itself.
- Full-screen programs, SSH and tmux sessions require an explicit confirmation before queueing.

## Acceptance criteria

1. Commands submitted while busy are listed and run in order.
2. A failing command pauses the queue with the failure visible.
3. Interrupt sends exactly one Ctrl+C; nothing is typed into the running program.
4. The terminal-first fallback still inspects each queued command's exit status.
