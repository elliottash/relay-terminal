---
id: X5D1
type: work
status: needs-qa-llm
component: [gui, shell-integration]
milestone: desktop-alpha
workstream: terminal
rank: cr
created: '2026-09-17'
acceptance: a recorded GUI run where an agent answer appears in the terminal stream in a distinct color, without entering shell history or disturbing a running program
source: '`issues/feature_intake.txt`, "put the agent responses in the terminal, as different colored echo commands?"'
links: {plans: [], commits: [], evidence: [], related: [], github: null}
---
# Show agent responses inline in the terminal

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

## Owner decision (2026-09-17)

The owner accepted the output-side colored rendering. The agent pane is removed. Tool
output also goes inline.

## Owner correction (2026-09-17, later)

"Tool output also goes inline" misread the decision: the owner meant the tool *calls* belong in the
pane, not every byte they print. `docs/SWITCHBOARD-DESIGN.md` 4.3 is the correct model — output
collapses and expands on demand. So a tool now prints its `⚙ …` call line and a result line carrying
the size it did not show (`exit 0 · 214 lines`); a write tool's diff stops after 8 lines with
"… N more lines"; the turn's "✦ N tool calls · T s" link opens the whole thing in the turn pane.
Agent options › **Show tool output** (`agent/show_tool_output`, default off) restores the old stream.

## Resolution (2026-09-17)

Implemented by Claude Opus 5 (Claude Code session). Mechanism: `docs/ARCHITECTURE.md`,
"Inline agent output". Relay writes to Konsole's session through its D-Bus-registered
`onReceiveBlock` slot, then asks Readline to redraw the prompt. Nothing is typed into the shell.

- User prompt in bold cyan, a model header, agent text, `⚙ $ command` / `⚙ read path` /
  `⚙ write path` in amber, tool output in gray, diffs in green and red, errors in red.
- C0 and C1 control characters are stripped from model and tool output.
- Output that arrives while a foreground program runs is buffered until the next prompt.
- New chat and Stop agent moved to the toolbar.

Open question 3 is settled by the buffering rule. Known gaps: Konsole's semantic-shell
margin marks sometimes color the left edge of inline lines; the D-Bus hook depends on
Konsole internals and is unverified on KF6.

Status: needs-qa-llm. Implementer evidence, not a QA verdict:
`docs/qa_evidence/2026-09-17-inline-agent-output/implementer-inline-tools-and-diff.png`.

QA checklist:

1. A Ctrl+Enter question prints the prompt, model header and answer inline, then a clean prompt.
2. A tool call shows a compact `⚙` line and its output; a write shows a colored diff.
3. Nothing from the agent appears in `history`.
4. During `sleep 10`, a Ctrl+Enter prompt's output appears only after the prompt returns.
5. A model reply containing an escape sequence, such as `\x1b]0;title\x07`, does not change the window title.
