---
id: CEW2
type: work
status: needs-verification
labels: [bug, editor]
assignee: codex
implemented_by: openai/gpt-6-astra via codex
rank: mcew2
created: '2026-09-22'
source: User in Relay, 2026-09-22
links: {plans: [], commits: [], evidence: [docs/qa_evidence/2026-09-22-editor-click-wrap/], related: [SEJ2], github: null}
---
# Ctrl+click file links opens edit mode

## Issue
ctrl + click didnt open a file in edit mopde. fix that.

## Done means
Ctrl+click on a text file link opens editable source, retaining its line number. Plain click still previews; Shift+click still opens externally.

## Plan
Connect the existing edit callback for Ctrl+click in src/Pane.h; add a checkable header control in src/FilePanes.cpp. Verify routing and wrap behavior with focused GUI tests, build Relay, and capture Xvfb evidence.

## Execution Summary
Ctrl+click now invokes the existing edit callback before card/context routing and preserves the source line. Regression covers edit routing, plain click, Shift+click and directories.

## Tests
- `scripts/relay-build --target relay relay-filepanes-tests relay-consolemode-tests` — passed.
- `ctest --test-dir build -R '^(consolemode|filepanes)$' --output-on-failure` — 2/2 passed with isolated XDG_CONFIG_HOME under Xvfb.
- manual: docs/qa_evidence/2026-09-22-editor-click-wrap/results.md
