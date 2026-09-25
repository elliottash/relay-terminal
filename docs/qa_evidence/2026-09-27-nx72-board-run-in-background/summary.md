# #NX72 evidence — Board Run in background no longer opens a pane

Bug: the card page's Run (background) inserted its agent pane beside the board, activated and
focused it, and moved it to a hidden background window about a second later once the agent
reported working — the board visibly split and closed again.

Fix: `RelayWindow::createBoardPane`'s `onExecuteCard` (src/RelayWindow.h) adopts the pane
straight into a hidden background window (`newEmptyWindow` + `adoptLeafAsTab`), attaches the
tab to the board's workspace, parks the task with `startBoardTask`, and posts the existing
"Job continues in the background" notice. No `insertBeside` / `spreadAfterAdding` /
`setActive` / `focusLeaf` in the background branch. Foreground "Run in pane" unchanged.

## Verification

- `ctest --test-dir build -R boardworkspace` — 27/27 pass, including the new regression
  `BoardWorkspaceTests::aBackgroundRunOpensStraightIntoABackgroundWindow`, which pins the
  wiring as text (the file's established technique for RelayWindow members). Log: ctest.log
  (untracked; *.log is ignored by docs/qa_evidence/.gitignore).
- Clean build of the working tree in a scratch export: `cmake --build … --target relay`
  exit 0 (`[100%] Built target relay`) — the changed header compiles and links.
  The shared checkout's `build/` could not produce this: another session's uncommitted
  src/Conversations.cpp WIP fails `-Werror=unused-function` there (fault card #M13J; main at
  tip also misses `SessionManager::setBoardRoot`, which blocked the formal land.py verify
  slot — see the #NX72 thread).
- Mechanical pass over the staged situation under Xvfb: `scenario/stage.sh`, record in
  `scenario/ai-pass.md`, sealed expectations in `scenario/expected.md`. One foreground pane
  at every sampled moment; claim recorded; no split.
