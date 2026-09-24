---
id: 2K7Q
type: work
status: needs-verification
labels: [feature, keyboard, files]
assignee: agent
implemented_by: openai/gpt-6-sol via codex
session: 83f552c6-372f-4f64-9b01-2beb4e493f9c
rank: zzzzzzzzzzzzzzzzy
created: '2026-09-23'
source: pane 83f552c6, 2026-09-23
links: {plans: [], commits: [f48d3db5323cabe61543c22ff25a12a07830885f], evidence: [tests/filepanes_test.cpp], related: [], github: null}
---
# Navigate directories with Left and Right in File Explorer

## Issue
in the file explorer, make left go up a directory, and right go into a directory

## Tests
`scripts/relay-build --target relay-filepanes-tests`
`ctest --test-dir build -R '^filepanes$' --output-on-failure`
`QT_QPA_PLATFORM=offscreen ./build/relay-filepanes-tests leftAndRightNavigateDirectoriesInTheList`

## Done means
In the File Explorer list, Left goes to the parent directory and Right opens the selected directory. A file selection, empty list, or filesystem root does not navigate unexpectedly. Search and the rest of the app keep their existing Left/Right behavior; the window's pane-focus shortcuts still work outside the explorer.

## Plan
**Goal:** Add intuitive directory traversal to File Explorer without taking Left/Right away from search or pane navigation.

**Findings:** `src/FilePanes.cpp` contains `FileExplorer::eventFilter`, `goUp()`, `setRoot()`, the tree view and filter; both list and filter currently declare local Alt+Up only. `tests/filepanes_test.cpp` already tests traversal through `activateRow()` and Alt+Up. `src/RelayWindow.h` gives `relayLocalKeys` handlers precedence over global pane-focus shortcuts.

**Steps:**
1. In the explorer list, Left navigates to the parent; Right enters the selected directory only, not a file.
2. Make filesystem-root, empty selection, and file selection safe no-ops; retain existing Alt+Up and other focus-context behavior.
3. Add focused regression tests and run the targeted file-pane test.

**Risks:** Left/Right are also pane-navigation commands and text editing keys outside the list. Scope the new behavior strictly to the explorer tree view; do not add it to the filter.

**Verify:** Build with `scripts/relay-build --target relay-filepanes-tests`; run `ctest --test-dir build -R '^filepanes$' --output-on-failure`. Exercise real key presses in tree focus.

## Execution Summary
Committed as `f48d3db5`. In the File Explorer list, Left goes to the parent directory and Right enters the selected directory. Right on a file or empty selection does nothing; the filter retains normal arrow editing. Added key-driven regression coverage in `tests/filepanes_test.cpp`. The file-pane build, full file-pane test suite, and focused key test passed.
