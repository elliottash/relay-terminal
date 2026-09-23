---
id: Y4MT
type: work
status: needs-verification
labels: [feature, gui, sessions]
assignee: codex
implemented_by: openai/gpt-6-sol via codex
rank: m
created: '2026-09-23'
source: Codex in a Relay pane, 2026-09-23
links: {plans: [], commits: [], evidence: [docs/qa_evidence/2026-09-23-sessions-preview-click/], related: [EV45], github: null}
---
# Preview Sessions rows on mouse click before resuming

## Issue
i also think that sessions shouldnt load on mouse click. you should have to press enter or click resume. i would want to see the preview first.

## Done means
- A mouse click or double click on a session row selects it and shows its preview without opening a session.
- Enter on a selected row and the Resume button still open the selected session.
- A focused GUI test proves these distinct interactions.

## Plan
**Goal.** Make the Sessions list a preview-first mouse interaction.

**Findings.** `src/Conversations.cpp` connects `QTreeWidget::itemActivated` to `activate(true)`; `currentItemChanged` already updates the preview. The existing key event filter and Resume button call `activate(true)` themselves.

**Steps.** Remove the mouse-dependent activation connection. Add a widget test that clicks and double clicks a row, then checks Enter and Resume.

**Risks.** Qt's `itemActivated` is platform dependent, so the test must exercise real mouse events rather than only emitting a signal.

**Verify.** Build `relay-conversations-tests`, run the new case and the conversations suite, and capture the selected-row preview in the test.

## Execution Summary
Removed the `QTreeWidget::itemActivated` resume connection in `src/Conversations.cpp`. Row selection still requests and displays the preview; Enter and the Resume button remain explicit open actions. Added a widget test using real mouse clicks and the explicit actions.

![Selected session and its visible preview](docs/qa_evidence/2026-09-23-sessions-preview-click/sessions-click-preview.png)

## Tests
- `scripts/relay-build --target relay-conversations-tests` — passed.
- `QT_QPA_PLATFORM=offscreen RELAY_SHOT_DIR="$PWD/docs/qa_evidence/2026-09-23-sessions-preview-click" build/relay-conversations-tests mouseSelectionPreviewsBeforeExplicitResume` — passed.
- `QT_QPA_PLATFORM=offscreen ctest --test-dir build -R '^conversations$' --output-on-failure` — passed (1/1).
- Manual evidence: `docs/qa_evidence/2026-09-23-sessions-preview-click/notes.md`.
