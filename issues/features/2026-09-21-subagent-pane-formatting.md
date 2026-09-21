---
id: S8FT
type: work
status: needs-verification
labels: [feature, subagents, ui]
assignee: codex
implemented_by: openai/gpt-6-astra via codex
rank: m
created: '2026-09-21'
source: User request in Relay, 2026-09-21
links: {plans: [], commits: [], evidence: [docs/qa_evidence/2026-09-21-subagent-pane-formatting/], related: [M9T4, TK9C], github: null}
---
# Match subagent transcript formatting to regular panes

## Issue
make the subagent panes have similar text / tool / thinking formatting to regular panes

## Plan
Reuse the regular pane's Markdown renderer for prose and reasoning in `src/SubagentTranscript.cpp`. Keep concise tool rows and their folds. Add live reasoning folds that settle when thinking finishes, following the existing display preference. Verify streamed Markdown, thinking lifecycle, and tool folds with the subagents Qt tests and an isolated Xvfb widget run.

## Execution Summary
Subagent prose now uses the regular pane's incremental Markdown renderer for headings, emphasis, code, and lists. Thinking streams in a muted fold, shows elapsed time when finished, respects collapse/always/never, and remembers a user's fold choice. Tool rows retain concise labels and folded details. Evidence: `docs/qa_evidence/2026-09-21-subagent-pane-formatting/`.

## Tests
`ctest -R ^subagents$`
manual: docs/qa_evidence/2026-09-21-subagent-pane-formatting/

## QA checklist
- Compare regular and subagent panes: prose is readable, headings/emphasis/code render, and tool details fold.
- Watch thinking stream, finish, collapse, and reopen. Check always/never preferences.
- Open an earlier thinking fold while prose streams; confirm there is no duplicated or lost text.
- Message a subagent and restore its tab after restart; confirm existing behavior.
