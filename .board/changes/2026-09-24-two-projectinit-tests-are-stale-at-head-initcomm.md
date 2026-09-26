---
id: FV0P
type: work
status: planned
labels: [bug, tests, switchboard]
rank: zzzzzzzzzzzzzzzzzzzi
created: '2026-09-24'
source: 'pane 1, 2026-09-24 (found while landing #NSYT)'
links: {plans: [], commits: [], evidence: [], related: [], github: null}
---
# Two projectinit tests are stale at HEAD: InitCommand in Pane.h, window text in RelayWindow.h

## Issue
While verifying #NSYT (2026-09-24): two assertions in tests/projectinit_test.cpp fail at HEAD itself, unrelated to any working-tree edit — `thePaneRaisesEveryTriggerAndBlocksNone` expects `Trigger::InitCommand` in src/Pane.h (0 occurrences at HEAD), `theWindowOffersItPassivelyAndRemembersTheAnswer` expects `Initialize a project here…` in src/RelayWindow.h (0 occurrences at HEAD; the string lives in src/Pane.h and src/RelayWindow.cpp). Measured: `git show HEAD:src/Pane.h | grep -c "Trigger::InitCommand"` → 0, same at HEAD for RelayWindow.h; `./build/relay-projectinit-tests` → 20 passed, 2 failed.

## Done means
`./build/relay-projectinit-tests` passes every test at HEAD — including `thePaneRaisesEveryTriggerAndBlocksNone` and `theWindowOffersItPassivelyAndRemembersTheAnswer` — with no assertion deleted or weakened: every trigger rule and passive-offer rule those two tests pin is still asserted, pointed at the file(s) where that wiring actually lives. Failure looks like the binary still reporting failures on a clean HEAD tree, or a diff that drops or loosens a `QVERIFY` instead of re-pointing it.

## Plan
### Goal
Make the two stale text-reading tests in `tests/projectinit_test.cpp` pass at HEAD by re-pointing them at the files that now hold the Pane and RelayWindow project-init wiring — the wiring was legitimately moved (card #243T split the method bodies out of the headers), so the tests follow the code; no assertion is dropped.

### Findings
- These tests read sources as text (`fileText` + `bodyOf`) because `Pane`/`RelayWindow` need a whole application to link. They were written when that wiring was in `src/Pane.h` and `src/RelayWindow.h`.
- At the card's HEAD (2026-09-24, 20 passed / 2 failed): `Trigger::InitCommand` had left `src/Pane.h` — it sits in the `/init` slash-command handler, `askProjectInit(relay::projectinit::Trigger::InitCommand, ...)` at `src/Pane.cpp:299` today; `Initialize a project here…` and `Next time: type /init in any prompt box` had left `src/RelayWindow.h` — both in the palette item at `src/RelayWindow.cpp:401`/`408` today.
- The tree has drifted further since the card was measured (other sessions' commits and uncommitted work): `handleBoardInitRequest`/`beginProjectInit`/`projectInitBoardState`/`projectInitProposals` are no longer in `src/Pane.h`; `board_init_request` handling appears in `src/BoardRemote.cpp:617`; the `pane->onProjectInit*` wiring is in `src/RelayWindowCore.cpp:888–922`; `ProjectInitBlock` is its own header (`src/ProjectInitBlock.h`). So the anchor→file map must be re-measured at HEAD when this runs — do not code to this snapshot, and expect today's failure list to be longer than two.

### Steps
1. Re-measure at HEAD (the shared checkout holds other sessions' edits; `git show HEAD:<path> | grep -n` for each pinned string across the Pane and RelayWindow units: `src/Pane.h`, `src/Pane*.cpp`, `src/RelayWindow.h`, `src/RelayWindow*.cpp`, `src/BoardRemote.cpp`, `src/ProjectInitBlock.h`). Build and run `./build/relay-projectinit-tests` first to get the real failure list.
2. `python3 scripts/land.py begin <me> tests/projectinit_test.cpp`.
3. Add a small helper (e.g. `unitText({files…})`) that concatenates a unit's files, and re-point `thePaneRaisesEveryTriggerAndBlocksNone` at the Pane unit's files at HEAD. Keep every assertion; keep each `bodyOf(...)` scoped to the one file that holds that function so the body extract stays exact.
4. Re-point `theWindowOffersItPassivelyAndRemembersTheAnswer` at `src/RelayWindow.h` plus the `.cpp`(s) holding the palette item and the `onProjectInit*`/`decline` wiring. Keep the negative assertion (`!contains("askProjectInit(relay::projectinit::Trigger::AgentWork")`) evaluated over the same combined text — if combining files would change its meaning, keep per-file scopes so negatives stay honest.
5. Refresh the stale file-header comment ("Pane and RelayWindow are one translation unit…") to say the split unit's files are read.
6. Verify: `scripts/relay-build --target relay-projectinit-tests && ./build/relay-projectinit-tests` → all tests pass, 0 failed.
7. Land: `python3 scripts/land.py commit <me> -m "tests: point projectinit text tests at the split Pane/RelayWindow sources" --verify-tests projectinit` (check `ctest --test-dir build -N -R projectinit` names it; otherwise verify with step 6 and drop `--verify-tests`).

### Risks
- The tree moves under the work: other sessions land Pane/RelayWindow refactors mid-fix. land.py's snapshot/confirm flow handles it — if hunks come back contested, re-measure; do not `--confirm` blindly.
- If an anchor no longer exists *anywhere* at HEAD, a rule was removed, not moved — that is the owner's call, not this card's: leave the assertion in place, stop, and put a `question` comment on this card instead of deleting it.
- Scope creep: only `tests/projectinit_test.cpp` changes; a genuine behaviour bug found on the way becomes a new card in bugs, never a detour.

### Verify
- `./build/relay-projectinit-tests` → 0 failures, all tests green (not just the two named ones).
- The landed diff touches only `tests/projectinit_test.cpp` and removes no `QVERIFY` whose rule is not re-expressed against the file that now holds the code.
