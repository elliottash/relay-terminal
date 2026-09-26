---
id: MDQ8
type: work
status: needs-verification
labels: [bug, panes]
assignee: agent
implemented_by: anthropic/claude-opus-5-5 via claude-code
session: d773a199-4070-4356-9f2e-9b80b1734733
rank: zzzzzzzzzzzzzzzzzzzzzzzzzzzzzzy
created: '2026-09-26'
links: {plans: [], commits: [], evidence: [], related: [], github: null}
---
# on restart, check for the bug where a pane content wont load until you type a command

## Issue
on restart, check for the bug where a pane content wont load until you type a command

## Done means
- After a restart, a restored terminal pane shows its saved text above the first prompt without the user typing anything.
- `ctest -R restorereplay` passes and fails if the fix is removed.

## Execution Summary
Root cause: `__relay_prompt_end` (shell/integration.bash) runs `compgen | python event.py ready` as a foreground pipeline, so `state.json` holds `ready` while that pipeline is still the tty's foreground process group. If the pane's shell poll read the event in that window, `shellIdleAtPrompt()` failed its `foregroundPid() == shellPid()` check and `replayRestoredScrollback()` returned with "the next prompt tries again". The next prompt only came after a typed command. A second route was the same: the replay ran before the ready branch cleared auto native mode. Under restart load (many panes starting at once) the window is easy to hit.

Fix: `Pane::pollShell()` (src/PaneRuntime.cpp) retries a pending replay on every tick once a prompt has been reported. The check costs nothing once the text is printed.

Commit e957f1f0, submitted with `relay-land submit` (request `mdq8-restore-replay-1`).

## Tests
- New `restorereplay` ctest (`relay-consolemode-tests --mdq8-only`): a `python3` wrapper first on PATH sleeps 1.5 s after each `ready` event, which makes the race certain. Without the fix it fails (text never appears, 15 s timeout). With the fix it passes, including 5/5 with `--repeat 5`.
- `linkedshell` passes. `consolemode` fails in console-transcript and memory cases, but it is on `.relay/known-ctest-failures.txt` and this change does not touch those paths.
- Live check still to do: restart Relay with several restored panes and confirm each shows its text without typing.
