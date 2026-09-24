---
id: SG4P
type: work
status: needs-verification
labels: [feature, terminal]
assignee: codex
implemented_by: openai/gpt-6-astra via codex
rank: msg4p
created: '2026-09-21'
source: Codex in a Relay pane, 2026-09-21
links: {plans: [], commits: [b773633fa3a0d1da97cdb6475765998eb9f1813c], evidence: [docs/qa_evidence/2026-09-21-shell-command-spacing/], related: [7QFW], github: null}
---
# Blank lines around shell commands

## Issue
there should be an extra line break before and after shell comamnds, same as we do with agent prompts

## Execution Summary
Added a blank row above shell command input in PS1 and below its echo in the first-command DEBUG hook. Input row marking runs before the bottom gap. Live isolated Relay screenshot: docs/qa_evidence/2026-09-21-shell-command-spacing/commands.png. Applies to newly opened pane shells.

## Tests
`python3 -m unittest tests.test_shell -v`
`manual: docs/qa_evidence/2026-09-21-shell-command-spacing/README.md`

## QA checklist
- [ ] In a new pane, confirm one empty row above and below each cyan command band.
- [ ] Confirm typed, staged and multiline commands still run normally and the gaps are outside the band.
