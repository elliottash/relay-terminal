# Ctrl+Shift+Z text redo — #BKMC

Implementation check on 2026-09-23:

| Check | Result |
|---|---|
| `scripts/relay-build --fast --target relay-editor-tests relay` | Passed (Qt 5, Linux) |
| `QT_QPA_PLATFORM=offscreen ctest --test-dir build-fast -R '^editor$' --output-on-failure` | Passed, 1/1 |
| `PYTHONPATH=backend python3 -m unittest tests.test_keybindings` | Passed, 32 tests |

`tests/editor_test.cpp` drives Qt key events through the shortcut helper on a
plain-text editor, a line edit, and a rich-text editor. It checks redo after a
successful undo, restore fallback when redo is exhausted or undo was a no-op,
and disarming after a new edit or focus change. It also checks that an unbound
Ctrl+Shift+Z cannot revive an old native redo stack after focus changes.

The separate verifier should try the same sequence in a Relay prompt with a
pane in Recently closed, then check that the pane stays closed during redo and
can still be reopened with a second Ctrl+Shift+Z.
