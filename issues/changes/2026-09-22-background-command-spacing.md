---
id: BGSP
type: work
status: needs-verification
labels: [bug, terminal]
assignee: codex
implemented_by: openai/gpt-6-astra via codex
rank: mbgsp
created: '2026-09-22'
source: Codex investigation in a Relay pane, 2026-09-22
links: {plans: [], commits: [b352aa9efe34861e4e4e465e0418d63b6433ffff], evidence: [docs/qa_evidence/2026-09-22-background-command-spacing/reproduction.md, docs/qa_evidence/2026-09-22-background-command-spacing/fix.md], related: [SG4P], github: null}
---
# Background job completion removes the gap before the next shell command

## Issue
can you check, the line breaks before and after commands/prompts in the terminal, it seems like it didnt add those some times, see this snippet:

W: Target CNF (non-free/cnf/Commands-all) is configured multiple times in /etc/apt/sources.list.d/opera-stable.list:4 and /etc/apt/sources.list.d/opera.list:1
[1]+  Done                    sudo apt update
ls

10.tsv           app           gateway                          reports
AGENTS.md        backend       issues                           research_notes

## Discussion points
Confirmed with a real interactive Bash PTY using the existing BashSession harness. Background output arrives after PS1's spacing; Bash then emits a pending Done notification during the next staged command's Readline redisplay. The command immediately follows that notification. The DEBUG hook still inserts the lower blank row. See linked reproduction. No runtime code changed during this investigation.

## Done means
- The next staged command retains an empty row above it after late background output and a pending job-completion notification.
- Normal, multiline and native commands retain correct input marking and spacing, without accumulating additional gaps.

## Tests
`python3 -m unittest tests.test_shell -v`
`RELAY_ENGINE_TEST=SessionTest build/engine/relay-engine-tests`
`RELAY_ENGINE_TEST=CoreTest build/engine/relay-engine-tests theRowRoleOscMarksItsLine theScannerReportsAnErasedRow osc133PromptMarks`
`bash -n shell/integration.bash`
`manual: docs/qa_evidence/2026-09-22-background-command-spacing/fix.md`

## Plan
Goal: restore one blank row above staged shell commands after late output/job notifications.

Findings: shell/integration.bash __relay_load executes after Bash's pending job notifications and before command redisplay. PS1 spacing alone cannot survive asynchronous output.

Steps: emit an OSC 7772;input-gap marker from __relay_load; process it in order in TerminalSession, using active-screen text to insert only missing rows; add engine and real Bash tests; document and build the change.

Risks: preserve existing gaps, scrollback, alternate-screen programs, fragmented escape sequences and native input. The new behavior requires the rebuilt terminal and a newly opened shell.

Verify: targeted engine session tests, real Bash PTY tests, and isolated live GUI reproduction.

## Execution Summary
Fixed command loading after asynchronous output/job notifications with an ordered input-gap request. TerminalSession adds only missing blank rows, preserving row marking and existing gaps, and ignores requests on alternate screens. Verified the original Done → ls scenario in a real Bash PTY and an isolated live Relay window. Evidence: docs/qa_evidence/2026-09-22-background-command-spacing/fix.md and fixed.png. Restart the rebuilt Relay to load both the engine and shell changes.
