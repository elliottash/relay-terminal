# Composer Down-arrow boundary (#DNR7)

The task/subagent and jobs guards used blockNumber(), treating a wrapped paragraph as one row.
They now use RichEditor::onBottomRow(), also used by history navigation.

Validation:
- `scripts/relay-build --target relay-editor-tests relay`: passed, build 2026-09-22.19H.08.
- `QT_QPA_PLATFORM=offscreen ctest --test-dir build -R '^editor$' --output-on-failure`: passed (23 Qt test cases).
- With a fresh XDG_CONFIG_HOME, `xvfb-run -a build/relay-editor-tests bottomRowFollowsVisualLines arrowsBrowseOnlyFromTheFirstAndLastRows upInsideMultilineTextMovesCursor`: 5 passed including setup/cleanup. Exercises a shown editor with real Qt key events, wrapped rows, explicit newlines, empty drafts, and trailing blank rows.
- Running the entire editor suite under bare Xvfb yielded 22 passes and a focus failure in the unrelated caret-blink test; the complete offscreen suite passed. Bare Xvfb has no window manager.
- Board check reports pre-existing errors/warnings elsewhere; none concern DNR7.

Independent full-pane visual verification remains: show tasks (also try subagents/jobs), type a wrapped prompt, and press Down from its first row. The caret should move within the editor until its final row; another Down enters the strip.
