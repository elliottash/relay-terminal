---
id: FR72
type: work
status: needs-qa-llm
labels: [feature]
component: [gui]
milestone: desktop-alpha
workstream: terminal
assignee: agent
implemented_by: Claude Opus 5 (GUI D subagent), 2026-09-17
rank: j5
created: '2026-09-17'
acceptance: a non-Claude model QA session runs the checklist and records it under `docs/qa_evidence/`
source: '`issues/feature_intake.txt` (2026-09-17): "command suggestion (see how warp / claude do this)"; owner decision 7 (this issue covers suggestion kind (a), ghost text from history; AI next-command and next-prompt suggestions are backend work tracked in `2026-09-17-agent-sessions-planning-subagents.md`)'
links: {plans: [], commits: [], evidence: [], related: [], github: null}
---
# Ghost-text command suggestions from history

## Behavior as implemented

- When the prompt box holds one line, the cursor is at the end, and the input mode is not Agent, the newest command starting with the typed text is shown as dim ghost text: commands run from this pane's current directory first, then the prompt history, then the shell history file (`$HISTFILE` or `~/.bash_history`, last 512 KiB).
- → (Right) at the end or Ctrl+F accepts the whole suggestion; Alt+→ or Ctrl+Shift+→ accepts one word (these take priority over pane navigation only while a suggestion is shown).
- Actions › "Command suggestions from history" toggles it (`composer/history_suggestions`, on by default).
- `RichEditor::setGhost/acceptGhost` paint and accept the suggestion; unit test `ghostTextAcceptsWholeOrWord`.

## Implementer check (not a QA verdict)

Under Xvfb: after running `echo ghost-suggestion-sample`, typing `echo gho` showed `st-suggestion-sample` dimmed; → completed it (`docs/qa_evidence/2026-09-17-history-suggestions/implementer-ghost-and-accept.png`).

## QA checklist

1. Suggestions prefer commands run in the same directory.
2. Alt+→ accepts one word and pane navigation still works when no suggestion is shown.
3. No suggestion in Agent mode, for multi-line text, or while the @ picker is open.
4. The palette toggle turns it off immediately.
