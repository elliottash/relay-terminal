---
id: BKMC
type: work
status: needs-verification
labels: [feature, keyboard]
assignee: agent
implemented_by: openai/gpt-6-sol via codex
session: a28c246c-e7f1-4701-8c1b-3e850015f76f
rank: zzzzzzzzzzzzzzzzy
created: '2026-09-23'
source: Relay pane, 2026-09-23
links: {plans: [], commits: [], evidence: [docs/qa_evidence/2026-09-23-redo-shortcut/], related: [QWAS, RC7Z], github: null}
---
# Make Ctrl+Shift+Z redo a recent text undo, otherwise restore closed

## Issue
lets make ctrl shift z do redo, rather than undo last closed, if the user has just done z ctrl z undo. scope that and tell me if it will cause any issues

## Done means
- After a successful Ctrl+Z undo in the focused editable text field, Ctrl+Shift+Z redoes that field’s change; the closed-item stack is unchanged.
- Without a pending redo in that same field, Ctrl+Shift+Z restores the last closed pane, tab, or window under the Relay default keymap.
- Terminal job-control Ctrl+Z, Board undo, read-only views, custom keybindings, and the Warp/VS Code/Konsole presets keep their established behavior.

## Plan
**Goal:** Make the Relay preset’s Ctrl+Shift+Z choose redo only when it follows an actual text undo in the same focused field; otherwise keep `closed.restore`.

**Findings:** `src/Keymap.h` binds `closed.restore` to Ctrl+Shift+Z in the Relay preset; `src/RelayWindowCore.cpp` intercepts that action before editable widgets; `src/RichEditor.cpp` enables native undo/redo. Board undo in `src/BoardPane.cpp` and ModelPicker undo in `src/ModelPicker.cpp` have no redo command. Other presets bind `closed.restore` differently.

**Steps:**
1. Define a pending text redo by the focused editable widget’s actual redo availability after Ctrl+Z; keep it tied to that widget and clear it on a new edit or focus change. Handle both ShortcutOverride and KeyPress so Qt cannot route half of a chord to a different command.
2. Route the chord through the editor only when the keymap resolves to `closed.restore` and the same field has a pending redo; otherwise run restore. Keep palette and `app_action_run` restore unconditional.
3. Update shortcut hints and docs to explain conditional behavior. Add focused keyboard tests for successful undo/redo, no-op undo, redo invalidated by a new edit, focus switching, a nonempty closed stack, terminal Ctrl+Z, custom bindings, and other presets.

**Risks:** One key will have two context-dependent meanings. A stale redo flag could reopen a pane unexpectedly or apply an old edit to the wrong field. Qt text fields differ in redo APIs, so start with editable fields whose successful undo can be observed; do not infer redo from a keypress alone. The earlier #QWAS decision reserved Ctrl+Shift+Z for restore and stated a preference against unrelated Ctrl/Ctrl+Shift letter actions; this request is a targeted revision.

**Verify:** Build with `scripts/relay-build`, run editor/keymap/window shortcut tests, then manually undo text with a closed pane in the stack and press Ctrl+Shift+Z; check that text returns first and the closed pane remains available for restore.

## Decisions
Owner: “any text field, where redo is active from an immediate ctrl z, the hotkey works. further text edits or moving out of the text field stops the redo hotkey.”

Interpretation: Ctrl+Shift+Z redoes after a successful Ctrl+Z in the same focused editable text field. A new text edit or leaving that field clears the temporary redo shortcut; Relay's default then restores the most recently closed item.

## Tests
`QT_QPA_PLATFORM=offscreen ctest --test-dir build-fast -R '^editor$' --output-on-failure`
`PYTHONPATH=backend python3 -m unittest tests.test_keybindings`
`scripts/relay-build --fast --target relay-editor-tests relay`
manual: `docs/qa_evidence/2026-09-23-redo-shortcut/`

## Execution Summary
Added `src/TextRedoShortcut.h` and routed Ctrl+Z/Ctrl+Shift+Z in `src/RelayWindowCore.cpp`. For editable QLineEdit, QPlainTextEdit and QTextEdit widgets, a successful Ctrl+Z arms redo in that field; text changes and focus exit clear it. Ctrl+Shift+Z uses redo only while armed; otherwise Relay's `closed.restore` keeps the key. Other keymap actions keep precedence. Updated README and architecture shortcut text. The focused Qt test checks redo, restore fallback, no-op undo, edits, focus switching, and stale redo in an unbound preset. Evidence: `docs/qa_evidence/2026-09-23-redo-shortcut/`.
