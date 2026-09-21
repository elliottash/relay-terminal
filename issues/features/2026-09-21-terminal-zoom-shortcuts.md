---
id: Z00M
type: work
status: needs-verification
labels: [feature, terminal]
assignee: codex
rank: m
created: '2026-09-21'
source: 'Codex in a Relay pane, 2026-09-21'
links: {plans: [], commits: [], evidence: [docs/qa_evidence/2026-09-21-terminal-zoom/README.md], related: [], github: null}
---
# Terminal zoom with Ctrl and wheel or plus/minus

## Issue
feature request: ctrl + mousewheel, or ctrl + +/- to zoom

## Plan
**Goal:** Zoom the terminal text with Ctrl+wheel and Ctrl+plus/minus; Ctrl+0 resets.
**Findings:** `engine/view/TerminalView.cpp` already supports Ctrl+Shift zoom and backend context-menu zoom. `src/Keymap.h` has no zoom actions.
**Steps:**
1. Register zoom actions for the active pane, including composer focus, and teach menu users the live shortcut.
2. Handle Ctrl+wheel before terminal mouse reporting, with separate fractional wheel accumulation; accept Ctrl keyboard zoom in the standalone engine.
3. Run targeted engine tests and an isolated Xvfb check, build, and land with evidence.
**Risks:** Ordinary scrolling and terminal input must remain unaffected; preserve existing Ctrl+Shift bindings.
**Verify:** Engine input/geometry assertions, application build, and live Xvfb input check.

## Tasks
- [x] Implement shortcuts and wheel zoom <!-- t:a1 -->
- [x] Verify and record evidence <!-- t:a2 -->

## Execution Summary
Registered terminal zoom actions in the Keymap for composer focus, kept standalone engine Ctrl+Shift shortcuts and added Ctrl-only variants. Ctrl+wheel zooms before program mouse handling and accumulates partial notches separately from scrolling. Context-menu entries show the live shortcut and teach it through `terminal.zoom.menu`.

## Tests
manual: docs/qa_evidence/2026-09-21-terminal-zoom/tests.txt
manual: docs/qa_evidence/2026-09-21-terminal-zoom/README.md

## QA checklist
- [ ] Ctrl+plus (or Ctrl+=) and Ctrl+minus resize terminal text from the composer and terminal.
- [ ] Ctrl+0 restores the original size; existing Ctrl+Shift variants work.
- [ ] Ctrl+wheel resizes the hovered terminal, including a full-screen program; ordinary wheel still scrolls.
- [ ] Context menu shows zoom shortcuts and teaches the current key after mouse selection.
