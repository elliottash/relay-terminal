---
id: VH4B
type: work
status: needs-qa-llm
labels: [feature]
component: [gui, agent]
milestone: desktop-alpha
workstream: routing
assignee: agent
implemented_by: Claude Opus 5 (Claude Code session), 2026-09-17
rank: ik
created: '2026-09-17'
acceptance: a non-Claude model QA session drives the app through the checklist and records it under `docs/qa_evidence/`
source: 'owner direction, 2026-09-17: "ctrl+shift+enter is always terminal -- if the command is invalid, the agent will fix it and, if it throws an error, the agent will try to fix it and re-run it"'
links: {plans: [], commits: [], evidence: [], related: [], github: null}
---
# Fix and re-run invalid or failing commands in terminal mode

## Behavior as implemented

- Applies to terminal mode only: Ctrl+Shift+Enter, `/shell `, or the Terminal selector.
  Auto-mode commands that fail are not auto-fixed.
- Invalid command: no run; the agent is asked to fix it (attempt 1).
- Valid command: runs in the user's shell. At the next prompt, a non-zero exit other than 130
  starts a fix turn.
- The fix prompt carries the command, terminal cwd and problem, and requires a final
  fenced `relay-run` block. Relay runs that command in the user's shell and watches its exit.
- Up to 3 fix attempts, then `✗ Still failing after 3 fix attempts`. Success prints
  `✓ Fixed command succeeded`. Stop agent or Interrupt shell clears the loop.
- If the agent is busy or unconfigured, the loop stops with a red note.

Known limits: the agent cannot read terminal scrollback, so it re-runs failing commands with
`run_command` to see errors, which executes them again. Tools run without approval.

Code: `dispatch()`, `runInTerminal()`, `startFix()`, `finishFixTurn()` and the ready branch
of `pollShell()` in `src/main.cpp`.

## Implementer check (not a QA verdict)

2026-09-17 under Xvfb with GLM-5.3: `lss -la` was fixed to `ls -la` and ran; `cat alpha.txtt`
exited 1, the agent reproduced it, and `cat alpha.txt` ran and printed the file.
Screenshot: `docs/qa_evidence/2026-09-17-inline-agent-output/implementer-fix-invalid-and-runtime.png`.

## QA checklist

1. Ctrl+Shift+Enter `lss -la` does not run `lss`; a fixed command runs and succeeds.
2. Ctrl+Shift+Enter `cat missing-file` in a folder with `missing_file` gets fixed and re-run.
3. A command that cannot be fixed stops after 3 attempts with the red note.
4. Ctrl+C during a failing fixed command stops the loop.
5. Enter on a valid command that fails, such as `false`, does not start a fix.

Independence: the implementer is Claude; QA must be a different model family and record both identities.
