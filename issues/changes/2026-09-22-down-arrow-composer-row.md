---
id: DNR7
type: work
status: needs-verification
labels: [bug, composer]
assignee: codex
rank: m
created: '2026-09-22'
source: 'Codex in a Relay pane, 2026-09-22'
links: {plans: [], commits: [ac680c85d680021615e489decb0602769027a607], evidence: [docs/qa_evidence/2026-09-22-down-arrow/README.md], related: [], github: null}
---
# Down leaves the composer only from its bottom visual row

## Issue
there is a bug where, when there are tasks (and maybe subagents), if i press down it always goes down there, even if i am in the top row of the rich text box. it should only move down if i am in the bottom row of the text box

## Done means
Down moves through wrapped and explicit text rows before entering the tasks/subagents or jobs strip.
Down from the bottom visual row at the draft still enters the strip.
History navigation retains its current visual-row behavior.

## Plan
Goal: use visual rows for composer-to-strip navigation.
Findings: src/Pane.h checks paragraphs; src/RichEditor.cpp already checks visual rows for history.
Steps: expose the editor's bottom-row check, use it for both strips, add wrapped/multiline regression coverage.
Risks: preserve history routing and modifier handling.
Verify: targeted editor tests under Xvfb and compile Relay.

## Execution Summary
Shared RichEditor's visual bottom-row predicate with the task/subagent and jobs strip entry guards. Built Relay successfully; ready for independent visual verification.

## Tests
`ctest --test-dir build -R '^editor$' --output-on-failure` (QT_QPA_PLATFORM=offscreen; passed)
manual: docs/qa_evidence/2026-09-22-down-arrow/README.md
