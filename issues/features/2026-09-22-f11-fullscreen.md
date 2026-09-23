---
id: F11S
type: work
status: needs-verification
labels: [feature, keyboard]
assignee: codex
implemented_by: openai/gpt-6-astra via codex
rank: mf11s
created: '2026-09-22'
source: Codex in a Relay pane, 2026-09-22
links: {plans: [], commits: [f71fe850400a3f20b03537d9e8ee66c437ad6cbc], evidence: [docs/qa_evidence/2026-09-22-f11-fullscreen/results.md], related: [], github: null}
---
# F11 toggles fullscreen

## Issue
make f11 go full screen to hide the task manager etc

## Done means
- F11 enters desktop fullscreen and a second press restores the prior normal or maximized state.
- The action is available from Actions and teaches the configured shortcut.
- Fullscreen removes the resize margins and uses the whole display.

## Execution Summary
Added rebindable window.fullscreen (F11), using Qt fullscreen state to cover desktop panels and preserve normal/maximized state. Added an Actions entry using the existing live shortcut hint path. Built and exercised both round trips under isolated Xvfb/KWin; evidence: docs/qa_evidence/2026-09-22-f11-fullscreen/results.md.

## Tests
- manual: docs/qa_evidence/2026-09-22-f11-fullscreen/results.md
