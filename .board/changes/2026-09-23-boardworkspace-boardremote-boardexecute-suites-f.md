---
id: D9AQ
type: work
status: planned
labels: [bug, switchboard]
rank: zzzzzzzzzzzzzzzzzzz
created: '2026-09-23'
source: 'pane 2, 2026-09-24, while landing #ESDF'
links: {plans: [], commits: [], evidence: [], related: [], github: null}
---
# boardworkspace/boardremote/boardexecute suites fail at clean tip

## Issue
Unrelated fault noticed while landing #ESDF: boardworkspace, boardremote and boardexecute test suites fail at clean tip, before and after #ESDF's commit.

Measured on a clean export of tip `0d5b34e1` (2026-09-24, while landing #ESDF — `git archive HEAD` into `/tmp/esdf-tip`, built `relay-boardexecute-tests relay-boardremote-tests relay-boardworkspace-tests`, ran ctest):

- `boardworkspace` — 6 cases fail, all source-text greps: `aHelperThatDiesMidTurnPutsItsPanelsBack` wants `worker->onExit = [guard, tab](bool crashed) {` in `src/RelayWindow.h`, but that code now lives in `src/RelayWindowCore.cpp:772`; `theWindowMakesConsolesAndWiresThemAsPanesExceptWhereItMustNot` wants `wireAgentConsole` wiring in the header. The main.cpp→unit split (and later moves) outran the test's `windowSource()` reader.
- `boardremote` — `executeGoesThroughTheWindowsHookAndClaimsTheCard` fails (`Compared lists differ at index 0`).
- `boardexecute` — `theButtonNamesTheExecutingPaneAndRevealsIt`, `thePlanButtonNamesThePlanningPaneAndRevealsIt`, `theVerifyButtonNamesTheVerifyingPaneAndRevealsIt` fail: no `QPushButton#boardExecute` is found after the card arrives.

All three fail identically with and without #ESDF's `dc5b457`, so the cause is landed, not uncommitted. The other six board suites (`board`, `boardsections`, `boardsignals`, `boardpane`, `boardfilter`, `boardwatch`) pass at tip.

## Done means
On a clean export of `main` tip, `ctest -R 'boardworkspace|boardremote|boardexecute'` passes all three suites with no assertion weakened: each fixed expectation matches behaviour the code actually has, and any case that turned out to be a real production regression is fixed in `src/`, not in the test. The `boardworkspace` source greps follow code wherever it lives after the `RelayWindow` split, so a future move of a body between `RelayWindow*.cpp` files does not re-break them.

Failure is recognised by: any of the ten named cases still failing at clean tip, or a fix that only rewrites assertions to match a regression.

## Plan
**Goal.** Make the `boardworkspace`, `boardremote` and `boardexecute` suites green at clean tip again — fixing stale test expectations where the code legitimately moved or changed, and fixing the production code where the test is still right.

**Findings.**
- `boardworkspace`: `windowSource()` (`tests/boardworkspace_test.cpp:31-36`) reads only `src/RelayWindow.h`, but `RelayWindow` method bodies are now spread across `src/RelayWindow.cpp`, `src/RelayWindowCore.cpp` (`worker->onExit = [guard, tab](bool crashed) {` at :772, `wireAgentConsole` at :1043), `src/RelayWindowModels.cpp`, `src/RelayWindowSettings.cpp` and `src/RelayWindowWorkspace.cpp`. All 6 failures are greps against the header for text that moved.
- `boardremote`: `executeGoesThroughTheWindowsHookAndClaimsTheCard` (`tests/boardremote_test.cpp:376`) fails with "Compared lists differ at index 0". The execute path it drives is `src/BoardRemote.cpp:657-727`, which calls `board::executeTask(id, title, hasPlan, hasAcceptance)` and then sends `board_card_get` / `board_claim` to the worker. Actual cause unknown — needs the test's actual-vs-expected output.
- `boardexecute`: the three button tests build a `relay::BoardView`, call `openCard` with an executing/planning/needs-qa-llm card, and find no `QPushButton#boardExecute`. The button's action-row spec is built in `src/BoardPane.cpp` near :3307 (`execute.key = "boardExecute"`, `verify.key` at :3349). Whether the button's show conditions or the card-page wiring changed is unknown — needs live diagnosis.

**Steps.**
1. Reproduce in the shared checkout: `scripts/relay-build --target relay-boardworkspace-tests` (and the `relay-boardremote-tests` / `relay-boardexecute-tests` targets), then run each suite and capture the exact failure output. Claim the paths you will edit with `python3 scripts/land.py begin <me> <paths>` first (repo rule, CLAUDE.md).
2. `boardworkspace` (test-side fix): change `windowSource()` to read `src/RelayWindow.h` **plus every `src/RelayWindow*.cpp`** (concatenated), so the greps follow bodies wherever the split puts them. Then run the suite; if any of the 6 cases still fails because the expected *text* drifted (not just its file), check the invariant the case names still holds in the current code and update the literal to match it.
3. `boardexecute` (diagnose first): find why the button is absent — read the action-row construction around `src/BoardPane.cpp:3307` and its show conditions, and `git log -L` that block and `CardDetail::execute()`/`verify()` to see what changed since the test was written. If production regressed (the button should exist for these statuses), fix `src/BoardPane.cpp`; if the behaviour legitimately changed, update the three tests to the new truth while keeping the assertions that matter (button names the executing/planning/verifying pane and reveals it).
4. `boardremote` (diagnose first): run the single failing case, print the actual vs expected lists, and compare against the execute handler in `src/BoardRemote.cpp:657-727` and the current `board::executeTask`. Fix whichever side is wrong; if it is the test, update the expectation to the behaviour the desktop `Execute` button actually has today.
5. Re-run all three suites plus the six passing board suites (`board`, `boardsections`, `boardsignals`, `boardpane`, `boardfilter`, `boardwatch`) to confirm no collateral, then land with `python3 scripts/land.py commit` (its build gate will verify).

**Risks.**
- Step 2's blanket concat makes a grep able to match text in the *wrong* file that a test meant to pin to the header (e.g. a declaration-vs-definition distinction). Check each of the 6 cases' intent; where a case genuinely means "in the header", keep it reading only `RelayWindow.h` and instead update where it looks.
- Steps 3-4 may turn out to be real regressions, not stale tests — do not rewrite assertions to match a regression; each fix must say which side was wrong, in the Execution Summary.
- The shared tree holds other sessions' uncommitted code: diagnose on the clean export if a failure does not reproduce in the checkout.

**Verify.** `ctest --test-dir build -R 'boardworkspace|boardremote|boardexecute'` all pass; then `git archive HEAD` into a scratch dir, build the three targets there and re-run — the clean-export run is the evidence, since the shared tree can mask a fix.
