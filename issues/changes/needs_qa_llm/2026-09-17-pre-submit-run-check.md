---
id: 90JF
type: work
status: needs-qa-llm
component: [router, gui]
milestone: desktop-alpha
workstream: routing
rank: i1
created: '2026-09-17'
acceptance: router unit tests for each check, plus a GUI run showing the indicator for a runnable command, a missing command and a syntax error
source: '`issues/feature_intake.txt`, "can you parse the terminal code before i send it, to check it will run, and if not,"'
links: {plans: [], commits: [], evidence: [], related: [], github: null}
---
# Check that a terminal command will run before it is sent

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

## Owner decision (2026-09-17)

The owner answered the open question:

| Input | Destination |
|---|---|
| Enter, text is not a valid command | Agent, by default |
| Enter, text is a valid command | Terminal |
| Ctrl+Enter | Always the agent |
| Ctrl+Shift+Enter | Always the terminal. If the command is invalid, the agent fixes it. If it runs and exits with an error, the agent fixes it and re-runs it. |

"Valid" means `bash -n` passes and every command word resolves: across pipelines, lists,
newlines, subshells, groups, `!`, and command substitutions. Words resolve as builtins,
keywords, live aliases or functions, executables on the shell's `PATH`, or existing
executable paths. The check never executes input. Heredocs, arithmetic, `case` and array
assignments fall back to `bash -n` plus the first command word.

Router implementation (2026-09-17, Claude Opus 5): `classify()` in
`backend/relay_core/router.py` now returns `valid` and `invalid_reason`, and no longer
produces the `ambiguous` route. Tests: `ValidityTests` in `tests/test_router.py`.
GUI wiring for the fix-and-re-run loop is separate work.

## Resolution (2026-09-17)

GUI wiring landed in the same change, implemented by Claude Opus 5 (Claude Code session):

- Auto mode sends invalid input to the agent and prints the invalid reason as a note.
- The route label shows `TERMINAL · <reason> · the agent will fix it` in terminal mode.
- The terminal's working directory is passed to the router so `./script` checks resolve correctly.
- The fix-and-re-run loop is tracked in `features/needs_qa_llm/2026-09-17-fix-and-rerun-terminal-commands.md`.

Status: needs-qa-llm. Implementer evidence, not a QA verdict:
`docs/qa_evidence/2026-09-17-inline-agent-output/implementer-invalid-auto-to-agent.png`.

QA checklist:

1. Enter on `ls | nonexistentcmd123` goes to the agent and nothing runs in the terminal.
2. Enter on `git status` runs in the terminal.
3. Enter on an alias from `~/.bashrc`, after the first prompt, runs in the terminal.
4. Ctrl+Enter on `ls` goes to the agent.
5. Typing `./x.sh` in a directory with an executable `x.sh` shows a valid terminal route.
