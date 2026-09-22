---
id: BGSP
type: work
status: discussing
labels: [bug, terminal]
assignee: null
rank: mbgsp
created: '2026-09-22'
source: Codex investigation in a Relay pane, 2026-09-22
links: {plans: [], commits: [], evidence: [docs/qa_evidence/2026-09-22-background-command-spacing/reproduction.md], related: [SG4P], github: null}
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
`manual: docs/qa_evidence/2026-09-22-background-command-spacing/reproduction.md`
