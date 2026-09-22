---
id: A7CP
type: work
status: needs-verification
labels: [feature, keyboard]
assignee: codex
implemented_by: openai/gpt-6-astra via codex
rank: m
created: '2026-09-22'
source: Codex in a Relay pane, 2026-09-22
links: {plans: [], commits: [], evidence: [docs/qa_evidence/2026-09-22-alt-a-close/], related: [WD83], github: null}
---
# Alt+A closes the active subagent pane

## Issue
alt a should close an active subagent pane

## Done means
- Alt+A in a subagent pane closes that pane and returns focus to its main agent.
- Alt+A from the main agent still opens its subagent pane; closing the view does not stop agents.
- Shortcut descriptions and mouse hints describe the new behavior.

## Plan
Use the existing pane close path, preserve the owner-focus callback, update shortcut guidance, then build and exercise the shortcut in an isolated GUI.

## Execution Summary
Alt+A closes the active subagent pane through the standard tool-pane close path and returns focus to its owner. Updated palette description and back-button tooltip; mouse close teaches Alt+A, while the back button teaches Esc. Closing the view does not invoke agent stop.

## Tests
- `ctest --test-dir build -R '^subagents$' --output-on-failure` — passed.
- manual: docs/qa_evidence/2026-09-22-alt-a-close/README.md
