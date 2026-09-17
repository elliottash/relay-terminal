# Check that a terminal command will run before it is sent

- **Status**: open
- **Component**: router, gui
- **Milestone**: desktop-alpha
- **Workstream**: routing
- **Acceptance evidence**: router unit tests for each check, plus a GUI run showing the indicator
  for a runnable command, a missing command and a syntax error
- **Assignee**: unassigned
- **Source**: `issues/feature_intake.txt`, "can you parse the terminal code before i send it, to check it will run, and if not,"

## Context

The intake sentence is cut off after "and if not,". The intended action is unknown.

What exists today, before submission:

- `bash -n` syntax checking in a separate non-executing process. Invalid syntax blocks
  terminal submission with a dialog.
- First-word resolution against builtins, `PATH`, and the live shell's aliases and functions.
- A live route label under the composer.
- With **Terminal first** on, input with an unknown first word, or text that isn't valid
  Bash, goes to the agent (`features/needs_qa_llm/2026-09-17-terminal-first-agent-fallback.md`).

Not checked today: every command in a pipeline or `&&` chain, whether a path argument
exists, and executable permission on `./script`-style commands.

## Open question for the owner

What should happen when the check fails? Options: warn but allow, block, send the text to
the agent to fix the command, or send the command and error to the agent to explain.

## Acceptance criteria (pending the answer above)

1. Every command word in a pipeline or list is resolved, not only the first.
2. The composer shows which check failed before submission.
3. The check never executes the input.
4. The failure action matches the owner's answer.
