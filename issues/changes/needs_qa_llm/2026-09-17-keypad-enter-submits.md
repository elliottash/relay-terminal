---
id: Q7MK
type: work
status: needs-qa-llm
labels: [bug]
component: [gui]
milestone: desktop-alpha
workstream: terminal
assignee: implemented by Claude (Relay agent session), 2026-09-17
rank: r2
created: '2026-09-17'
acceptance: '`docs/qa_evidence/2026-09-17-keypad-enter-submits/` (implementer run: QtTest key events on the real composer widget); a non-Claude model QA session runs the checklist below and records it there'
source: user report, Relay agent session, 2026-09-17: "the other \"enter\" on my keyboard did a newline rather than inputting. it should work the same as return."
links: {plans: [], commits: [], evidence: [], related: [], github: null}
---
# Numpad Enter submits like Return instead of inserting a newline

## Before

The keypad's Enter arrives in Qt as `Qt::Key_Enter` **with `Qt::KeypadModifier` set**. `RichEditor::keyPressEvent`
matched `Qt::Key_Enter` but compared raw modifiers (`mods == Qt::NoModifier`, `== Qt::ControlModifier`, …), so no
branch matched: the key fell through to `QPlainTextEdit::keyPressEvent`, which inserted a newline. Plain keypad
Enter submitted nothing; Ctrl+keypad-Enter and Ctrl+Shift+keypad-Enter also failed to submit.

The window-level `handleComposerKey` already masked out `Qt::KeypadModifier`, which is why popup/queue keys worked
with keypad Enter while plain submission did not — the bug only showed once the key reached `RichEditor`.

## Change

`src/RichEditor.cpp`, `keyPressEvent`: clear `Qt::KeypadModifier` from the event's modifiers before the Enter
branches, so keypad Enter takes exactly the same branches as Return (auto / Shift newline / Ctrl agent /
Ctrl+Shift shell). Alt/Meta behaviour is untouched (still falls through to a newline).

## Test

`tests/editor_test.cpp`, new slot `keypadEnterMatchesReturn`: types text, presses keypad Enter (expects one
`auto` submission and no newline), Shift+keypad Enter (newline inserted, no submission), Ctrl+keypad Enter and
Ctrl+Shift+keypad Enter (agent / shell routes). Verified the test fails on the unfixed code (routes empty, text
grew a newline) and passes with the fix.

## QA checklist

- [ ] Keypad Enter in the prompt box submits (auto route) instead of inserting a newline.
- [ ] Shift+keypad Enter still inserts a newline; Ctrl+keypad Enter forces Agent; Ctrl+Shift+keypad Enter forces Terminal.
- [ ] Main Return behaves exactly as before in all four forms.
- [ ] Alt+Enter and Meta+Enter still fall through to a newline (no accidental submit).
- [ ] `ctest --test-dir build` and `./scripts/test.sh` pass (implementer ran both green, 2026-09-17).
