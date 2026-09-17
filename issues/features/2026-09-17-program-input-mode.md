---
id: S976
type: work
status: ready
component: [gui, worker]
milestone: desktop-alpha
workstream: agent
rank: zzs9
created: '2026-09-17'
acceptance: typing into a running program from the prompt box works, with completion for at least one program's commands, and Terminal mode never starts an agent turn on a typo
source: '`issues/feature_intake.txt`, 2026-09-17: "in terminal mode, is it better to be able to type non-commands and they will just go through? ... it would be great if you could type commands and they will just go into the claude command box. or should we have an alternative input mode for that? i guess that would be best, so you could for example have autocomplete for claude / codex commands."'
links: {plans: [], commits: [], evidence: [], related: [], github: null}
---
# A program input mode: type into the running program, with its own completions

Owner: in terminal mode a non-command should not start an agent turn; and when a program like Claude Code or
Codex owns the terminal, typing in Relay's prompt box should go into that program's input, ideally with
completion for that program's own commands.

Proposal to work through:
- A fourth destination, **program**, offered automatically while a program is reading input (the take-over
  banner gains "type into it from here"), and selectable from the mode chip.
- In program mode the line goes to the program's stdin, with no routing and no agent turn; history is the
  program's, not the shell's.
- Completion: per-program tables (claude, codex, gh, psql, python) for their slash commands and arguments,
  starting with the ones Relay can detect.
- In Terminal mode proper, a line that is not a runnable command should offer a fix ("did you mean …?")
  rather than starting an agent turn, per the owner's preference.
