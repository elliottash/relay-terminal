---
id: CPRQ
type: work
status: needs-verification
labels: [feature, keyboard, composer]
implemented_by: openai/gpt-6-sol via codex
rank: p
created: '2026-09-22'
source: Owner in a Relay pane, 2026-09-22; keyboard-system discussion
links: {plans: [], commits: [], evidence: [docs/qa_evidence/2026-09-22-keyboard-set/], related: [QWAS, KYPR, H8VP, H7N4], github: null}
---
# Ctrl+Q clears the prompt box as one undoable edit

## Issue
another suggestion: ctrl q to clear the prompt box (like ctrl shift c in claude / codex). or tell me if q should do something

ctrl z is undo text edit; ctrl shift z is undo close.

## Decisions
Owner: "ctrl q to clear the prompt box (like ctrl shift c in claude / codex)".
Owner: "ctrl z is undo text edit; ctrl shift z is undo close."

## Planning notes
- New action `prompt.clear` in `src/Keymap.h`, category agent, keys Ctrl+Q and Ctrl+Shift+Q: the same command on both (#QWAS). Relay binds nothing on Ctrl+Q today; the SSH comment in `src/Keymap.h` only notes that Q quits other terminals.
- The prompt box is `RichEditor`, a `QPlainTextEdit` with undo enabled (`src/RichEditor.cpp:67`). Clear is one edit block on the document (select all, remove), so a single Ctrl+Z brings the whole draft back even when several edits preceded the clear.
- Empty box: nothing happens and nothing is pushed on the undo stack.
- Scope: only the focused pane's prompt box. It never stops the agent (`agent.stop`), never clears queued prompts (`agent.clearQueue`), never touches the conversation, never closes anything. Attachment chips (@ files, screenshots) are cleared with the text and the same undo step brings them back: the clear keeps the chip list beside the document's undo stack. A cleared draft is not written to prompt history (#H8VP, #H7N4).
- Focus: in the prompt box both chords clear. With a terminal program holding the keyboard, Ctrl+Q reaches the program (XON) and Ctrl+Shift+Q clears the pane's prompt box, following the shift-only policy in `actsInsidePrograms()`. The global `eventFilter` in `src/RelayWindow.h` routes the plain chord to the composer only, the way the composer-only actions are excepted today (#KYPR).
- Hint: the composer's shortcut hints (`relay::ShortcutHints`) teach it once, like the other composer keys.

## Done means
- With text in the prompt box, Ctrl+Q and Ctrl+Shift+Q empty it, and one Ctrl+Z restores every character and every attachment. Failure shows as a partial restore or as two undo steps being needed.
- With an empty prompt box the chords do nothing, and a Ctrl+Z afterwards undoes the previous real edit rather than a no-op.
- The agent, its queue and the conversation are untouched: a running turn keeps running and queued prompts stay queued.
- With a program owning the keyboard, Ctrl+Q reaches the program.

## Tests
`ctest -R '^editor$'` (clear is one undo step after several edits; empty box is a no-op with nothing pushed; @attachments come back; mid-browse returns to the draft)
`ctest -R prompthistory` (a cleared draft is not recorded)
`PYTHONPATH=backend python3 -m unittest tests.test_keybindings` (prompt.clear on Ctrl+Q and Ctrl+Shift+Q, no collision)
manual: docs/qa_evidence/2026-09-22-keyboard-set/ (screenshots 11-13: draft, Ctrl+Q cleared with its notice, Ctrl+Z restored)

## Execution Summary
RichEditor::clearAsOneEdit() and Pane::clearPrompt() (98184a88); prompt.clear on Ctrl+Shift+Q and prompt-box-only Ctrl+Q (e914d65c, cd7dcfcb). Attachments are @path text, so the same undo restores them. A "Prompt box cleared · Ctrl+Z brings it back" notice is shown. Evidence: docs/qa_evidence/2026-09-22-keyboard-set/ 11-13.
