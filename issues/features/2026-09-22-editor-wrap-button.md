---
id: WWB2
type: work
status: needs-verification
labels: [feature, editor]
assignee: codex
implemented_by: openai/gpt-6-astra via codex
rank: mwwb2
created: '2026-09-22'
source: User in Relay, 2026-09-22
links: {plans: [], commits: [], evidence: [docs/qa_evidence/2026-09-22-editor-click-wrap/], related: [4TNY], github: null}
---
# Word-wrap toggle in the file editor header

## Issue
also the editor needs a word wrap button in the top pane.

## Done means
A visible Word wrap toggle wraps long source lines and can turn wrapping off without modifying file contents. It is hidden for rendered Markdown and non-text previews.

## Plan
Connect the existing edit callback for Ctrl+click in src/Pane.h; add a checkable header control in src/FilePanes.cpp. Verify routing and wrap behavior with focused GUI tests, build Relay, and capture Xvfb evidence.

## Execution Summary
Added a checkable Word wrap button in the text/source header. The choice survives Markdown view switches and changes no document bytes. Xvfb screenshots show both states.

## Tests
- `scripts/relay-build --target relay relay-filepanes-tests relay-consolemode-tests` — passed.
- `ctest --test-dir build -R '^(consolemode|filepanes)$' --output-on-failure` — 2/2 passed with isolated XDG_CONFIG_HOME under Xvfb.
- manual: docs/qa_evidence/2026-09-22-editor-click-wrap/results.md
