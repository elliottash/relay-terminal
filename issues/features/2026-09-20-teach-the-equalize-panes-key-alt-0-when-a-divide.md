---
id: GSJ7
type: work
status: needs-verification
labels: [feature, gui, keybindings]
assignee: codex
implemented_by: deepseek/deepseek-v4.1-flash
rank: zzzzzzzzzzzzzzzzi
created: '2026-09-20'
source: pane, 2026-09-20
links: {plans: [], commits: [], evidence: [docs/qa_evidence/2026-09-21-recover-inactive/README.md], related: [], github: null}
---
# Teach the equalize-panes key (Alt+0) when a divider is dragged, and move it off Ctrl+Alt+0

## Issue
when someone resizes a pane, send them the tutorial about alt+0 doing the auto-resize. change ctrl+alt+0 to alt+0 if it wont cause any problems. 

also add that in the shortcuts you see with "?"

## Decisions
Owner authorized recovery and landing of the inactive changes: “can you duoble check those again, and if they arent here merge them int o the active build, delete other branches and worktrees”.

## Plan
**Goal:** Recover the existing Alt+0 binding, divider-drag hint and searchable Actions entry into main and the active build.
**Findings:** Changes remain uncommitted in Keymap.h, RelayWindow.h, PaneLayout.h, panelayout_test.cpp and ARCHITECTURE.md. No extra branches or worktrees exist.
**Steps:** Review the recovered hunks; run the targeted build/tests and isolated GUI checks; land only the requested changes through the exact-tree build gate; rebuild the active binary.
**Risks:** Other sessions share this checkout. Select only the recovered work, leaving unrelated changes alone.
**Verify:** panelayout, hints, settingspane and engine view checks, plus an isolated GUI drive.

## Execution Summary
Recovered the Alt+0 equalize binding, divider-drag shortcut hint, and searchable auto-resize Actions entry. Evidence: docs/qa_evidence/2026-09-21-recover-inactive/README.md.

## Tests
`ctest -R panes`
`ctest -R hints`
manual: docs/qa_evidence/2026-09-21-recover-inactive/README.md

## QA checklist
- [ ] Drag a pane divider and observe the rate-limited Alt+0 hint.
- [ ] Alt+0 restores equal pane sizes.
- [ ] Search Actions for auto resize and check its live shortcut.
