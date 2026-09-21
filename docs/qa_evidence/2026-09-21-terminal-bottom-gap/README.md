# B7SP: missing lower terminal rows

The owner's report was about approximately seven blank rows inside the terminal above the composer during a silent Codex interval. The original live session was not captured. A deterministic engine GUI reproduction established a matching rendering fault: after prose compresses real rows, following output lies outside the single core frame that TerminalView requested. The view painted those rows as background.

The fix gathers the remaining visible real rows into the local rendering snapshot, preserves the ordinary core frame for remote consumers, and translates hit tests and selection endpoints into the appropriate core viewport temporarily.

Before the fix, the initial reproduction failed to find `after three`, showing `older output|older output|older output|older output|older output|older output|one two one two one two one two one two one two |one two one two||||`. The final regression compresses eight old prose rows into one and checks six following output rows plus the cursor row, the last row's hyperlink and drag selection, scrolling away and back, new output, and the ordinary frame size exposed to remote consumers.

Build: `scripts/relay-build --target relay-engine-tests`.

GUI verification (isolated temporary XDG_CONFIG_HOME, Xvfb, QT_QPA_PLATFORM=xcb, RELAY_ENGINE_TEST=ViewTest):

```sh
build/engine/relay-engine-tests compressedProseKeepsFollowingOutputVisible foldOpensUnderItsAnchorAndShutsAgain foldRowsCountInTheScrollRangeAndScrollOneByOne aFoldStaysUnderItsLineAcrossAResize proseReflowsOnResize selectionCrossesTheFoldBoundaryInVisualOrder markdownLinkLabelsAreClickable
```

Result: seven test functions passed on libvterm (nine passes including setup/cleanup). Ghostty is not enabled in this build. `libvterm.png` shows the exposed terminal widget under Xvfb, including the last output row and its selected text, with only the normal cursor row below.

An independent double-click selection problem found during validation is recorded as W7DC. No double-click fix is included here. The original Codex scenario still needs visual confirmation in a running Relay using the new binary.
