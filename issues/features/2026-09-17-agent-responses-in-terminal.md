# Show agent responses inline in the terminal

- **Status**: open
- **Component**: gui, shell-integration
- **Milestone**: desktop-alpha
- **Workstream**: terminal
- **Acceptance evidence**: a recorded GUI run where an agent answer appears in the terminal
  stream in a distinct color, without entering shell history or disturbing a running program
- **Assignee**: unassigned
- **Source**: `issues/feature_intake.txt`, "put the agent responses in the terminal, as different colored echo commands?"

## Context

Agent output currently appears in a separate pane beside the terminal. The request is for
answers to appear in the terminal itself, visually distinct, possibly by echoing them.

Constraints found while filing:

- Relay's only write path into Konsole is `TerminalInterface::sendInput`, which types input.
  Literal `echo` commands would run in the user's shell, land in shell history, show the
  echo command itself, and could not be used while a program runs.
- Agent text must never be executed. Quoting model output into a shell command is a
  command-injection risk, even with escaping.
- Printing ANSI-colored text to the terminal's output side, rather than its input, avoids
  those problems. The shell bridge could print it at a ready prompt, from a staged file,
  the way commands are staged today.

## Open questions for the owner

1. Should the side pane remain, as a history or for tool detail, or be replaced?
2. Should tool previews and command output also go inline, or only the final answer?
3. While a full-screen program such as vim runs, should agent output wait or stay in the pane?

## Acceptance criteria

1. An agent answer is shown in the terminal in a distinct color.
2. Nothing is added to shell history, and no agent text is ever executed.
3. Output is not injected into a running foreground program.
4. Copying from the terminal yields the plain text without escape codes.
