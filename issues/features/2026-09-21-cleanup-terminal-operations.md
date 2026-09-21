---
id: C1NP
type: work
status: needs-verification
labels: [feature, switchboard]
assignee: codex
rank: m
created: '2026-09-21'
source: 'Codex in a Relay pane, 2026-09-21'
links: {plans: [], commits: [], evidence: [docs/qa_evidence/2026-09-21-cleanup-terminal/README.md], related: [8YQ9], github: null}
---
# Show cleanup operations in the Switchboard agent terminal

## Issue
and the clean up button operations should show up in the switchboard agent terminal.

## Plan
Goal: keep cleanup operations in the agent transcript.
Findings: RelayWindow already delivers every worker event to the tab consoles. Pane renders tool calls normally, but board_activity only toasts, and cleanup start/summary events have no transcript handler.
Steps: format cleanup start, each board operation, and completion/refusals/changelog as transcript notes; keep normal tool rendering; test event formatting and verify in an isolated GUI.
Risks: do not synthesize another agent turn or duplicate streamed report text; preserve preview/apply behavior.
Verify: targeted Qt tests, relay build, and live Xvfb capture using an isolated worker fixture.

## Execution Summary
Cleanup start, board operations, completion, refusals and changelog paths now print in the Switchboard console transcript. Existing tool/prose rendering and preview/apply behavior remain in use.

Evidence: docs/qa_evidence/2026-09-21-cleanup-terminal/README.md

## Tests
`ctest -R ^boardpane$`

## QA checklist
- [ ] Click Clean up and confirm preview progress appears in the Switchboard terminal.
- [ ] Apply a preview and confirm each operation and the final outcome remain readable in terminal scrollback.
- [ ] Check a stopped or failed run reports its outcome and any refusals.
