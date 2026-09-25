---
id: NX72
type: work
status: needs-verification
labels: [bug, switchboard, voice]
assignee: agent
implemented_by: glm/glm-5.3
session: b23a9951-dea8-403e-818e-6c451e307672
rank: zzzzzzzzzzzzzzzzzz
created: '2026-09-25'
verify: {artifact: system, primary: script, also: [ai-text], human: none, criteria: 'tests/boardworkspace_test.cpp gains a regression case reading RelayWindow::createBoardPane''s onExecuteCard body: the runInBackground branch adopts into a hidden background window (newEmptyWindow + adoptLeafAsTab before startBoardTask) and never calls insertBeside / spreadAfterAdding / setActive / focusLeaf / backgroundPaneWhenWorking; the foreground branch keeps insertBeside + focusLeaf. ctest -R boardworkspace passes on a clean build of the landed tree.', sign_off: none, effort: low}
source: pane 1, 2026-09-27
links: {plans: [], commits: [], evidence: [docs/qa_evidence/2026-09-27-nx72-board-run-in-background/], related: [], github: null}
---
# Board Run in background still flashes a pane in the layout

## Issue
The card page's Run button (background run) creates its agent pane in the foreground first: `onExecuteCard` inserts it beside the board, activates and focuses it, then `backgroundPaneWhenWorking` moves it to a hidden background window a second later once the agent reports working. The visible effect is a pane opening beside the board and then closing. A background run should never reshape the foreground layout: create the pane straight into a hidden background window and start the task there.

> bug: when you do "run" (in background) in the board, it still opens a pane and then closes it
> — elliott · [session:5d87c4c8dbad4dbcbead00c1670ecbde](relay://session/5d87c4c8dbad4dbcbead00c1670ecbde) · 2026-09-25

## Done means
Pressing **Run** (background run) on a card page never reshapes the foreground layout: no pane appears beside the board, no focus steal, no later collapse. The agent pane is created directly inside a hidden background window (the same destination `backgroundPane` moves panes to), attached to the board's workspace, and the task starts there. The card still gets its claim/session chip, the done / needs-you / failed notices still arrive from `refreshBackgroundTasks`, and the chip still reveals the pane via `focusPane`. **Run in pane** (foreground run) behaves exactly as before.

## Execution Summary
`RelayWindow::createBoardPane`'s `onExecuteCard` (src/RelayWindow.h): the `runInBackground` branch now returns early — it marks the pane, adopts it into a fresh hidden background window (`newEmptyWindow` + `adoptLeafAsTab`, the same destination `backgroundPane` moves panes to), attaches the tab to the board's workspace, parks the task with `startBoardTask`, and posts the existing "Job continues in the background" notice + Sessions→Background hint. It no longer calls `insertBeside` / `spreadAfterAdding` / `setActive` / `focusLeaf`, so the board's layout never moves and focus never leaves it. The foreground branch (Run in pane) is unchanged. `backgroundPaneWhenWorking` stays for the phone path (#E728) and its comment now says so. The arrival is notice-safe: a marked pane with no owned requests reports `working` (BackgroundTasks.h), so `refreshBackgroundTasks` records the state without posting, and the settle (done / needs you / failed) notices still arrive; the card's Running chip still reveals the pane (`focusPane` → `openBackgroundPane`).

Regression test `BoardWorkspaceTests::aBackgroundRunOpensStraightIntoABackgroundWindow` in tests/boardworkspace_test.cpp pins the wiring the way that file pins the rest of RelayWindow's board wiring (the window needs a whole Qt app to run): the background branch must adopt before `startBoardTask` and must not contain any layout/focus call, and the foreground branch keeps its insert and focus.

## Tests
- `ctest --test-dir build -R boardworkspace` — 27/27 pass, including the new `aBackgroundRunOpensStraightIntoABackgroundWindow` (log: docs/qa_evidence/2026-09-27-nx72-board-run-in-background/ctest.log).
- `scripts/relay-build` full `relay` target could not run in the shared tree this session: another session's uncommitted src/Conversations.cpp drift fails `-Werror=unused-function` (`instantWords`). The land commit's verify slot builds the clean tree (tip + these paths) and is the compile gate for the header change.
- Manual check the wiring compiles: the same header text is compiled by every RelayWindow*.cpp; the verify-slot build of the landed tree must be green before `done`.

## Try it
**Staged situation.** Once the #NX72 commit is on `main` and `build/relay` is rebuilt (`scripts/relay-build`), run:

```bash
RELAY_BIN=$PWD/build/relay docs/qa_evidence/2026-09-27-nx72-board-run-in-background/scenario/stage.sh \
  /tmp/nx72-staged docs/qa_evidence/2026-09-27-nx72-board-run-in-background/scenario/person-pass
```

It stages its own disposable board (card TRY1, “Count to three”) under Xvfb and leaves screenshots at 0 / 300 / 1500 ms plus the panes/sections/notice JSON next to them. (The AI's pass is in `scenario/ai-pass.md`.)

**Task.** Open a board card page in your own Relay, press **Run** — once to arm the “no plan yet” confirmation, once to hand it over — and watch the window: the card moves to Executing with a “Running (…)” chip, and clicking that chip reveals the pane in the foreground.

**Question.** During that Run, did the layout stay completely still — no pane flashing open beside the board and closing again a second later — and did the reveal happen only when you clicked the chip?
