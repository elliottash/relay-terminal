---
id: EQM2
type: work
status: needs-verification
labels: [feature, panes]
assignee: codex
implemented_by: openai/gpt-5.6-sol via codex
rank: m
created: '2026-09-21'
source: Codex in a Relay pane, 2026-09-21
links: {plans: [], commits: [829f6428fd59154366f67899ba5b2941c70a9c66], evidence: [docs/qa_evidence/2026-09-21-opening-new-panes-evenly/README.md, docs/qa_evidence/2026-09-21-opening-new-panes-evenly/], related: [], github: null}
---
# Opening a pane evenly redistributes the tab

## Issue
opening new panes should evenly spread out the other panes on the tab

## Done means
- Opening a new terminal pane redistributes every splitter in that tab to even shares.
- Existing pane movement and docking still preserve hand-adjusted sizes.
- A targeted pane-layout test demonstrates the redistribution arithmetic.

## Plan
Goal: make a newly opened terminal pane leave the whole tab in a tidy, even layout.

Findings: `RelayWindow::splitToward` is the shared opening path for the new-pane actions, while `insertBeside` is also used by pane movement and therefore must retain its size-preserving behavior. `equalizeActivePage` already handles nested splitters and Switchboard width floors.

Steps: refactor equalization to accept an explicit tab page; queue it after the newly opened pane and its splitter have settled; add a layout regression test for insertion followed by whole-splitter equalization.

Risks: equalizing inside `insertBeside` would also alter pane moves and drags, so the trigger stays in the new-pane path only.

Verify: build `relay-panes-tests`, run `ctest --test-dir build -R '^panes$'`, and build Relay through `scripts/relay-build --target relay`.

## Tasks
- [x] Redistribute the tab after opening a new pane. <!-- t:qn -->
- [x] Add and run the targeted pane-layout regression test. <!-- t:bp -->
- [x] Land the change with verification evidence. <!-- t:4g -->

## Execution Summary
New terminal panes now queue whole-tab equalization after their splitter settles. The move/drag insertion path remains size-preserving.

Evidence: docs/qa_evidence/2026-09-21-opening-new-panes-evenly/README.md

## Tests
`scripts/relay-build --target relay-panes-tests`

`ctest --test-dir build -R '^panes$' --output-on-failure`

`scripts/relay-build --target relay`
