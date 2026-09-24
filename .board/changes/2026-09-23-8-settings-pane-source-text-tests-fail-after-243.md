---
id: 15ZE
type: work
status: planned
labels: [bug, tests]
rank: zzzzzzzzzzzzzzzzzzw
created: '2026-09-23'
source: '#HJ1T session, 2026-09-23'
links: {plans: [], commits: [], evidence: [], related: [243T], github: null}
---
# 8 settings-pane source-text tests fail after #243T moved window bodies into .cpp files

## Issue
(found by the #HJ1T session, not a user request) `xvfb-run -a build/relay-settings-tests -silent` on main at d6f769f6: 40 passed, 8 failed. All 8 read src/RelayWindow.h as text for bodies that f6480574 (#243T) moved to .cpp files: everyRowHelperDeclaresADefault, workSignalsUnaskedSitsUnderBoardAndSaysWhatItDoes, theModelsPageHasNoTierListsAndNoChecklist ("modelsSection() is gone"), theModelsPageButtonOpensTheModelsPaneOnMain, everyProviderRowHasAModelsLinkIntoTheAvailableTab, everyProviderButTheFirstDrawsARuleAboveIt, theAdvancedProviderDialogIsGoneAndItsTwoPartsHaveHomes, localModelsComeRightAfterModels.

## Done means
`xvfb-run -a build/relay-settings-tests -silent` on main passes with zero failures. The 8 tests named in the Issue (everyRowHelperDeclaresADefault, workSignalsUnaskedSitsUnderBoardAndSaysWhatItDoes, theModelsPageHasNoTierListsAndNoChecklist, theModelsPageButtonOpensTheModelsPaneOnMain, everyProviderRowHasAModelsLinkIntoTheAvailableTab, everyProviderButTheFirstDrawsARuleAboveIt, theAdvancedProviderDialogIsGoneAndItsTwoPartsHaveHomes, localModelsComeRightAfterModels) each read the file their moved body actually lives in — src/RelayWindowSettings.cpp for settingsSections(), src/RelayWindowModels.cpp for modelsSection(), src/RelayWindow.h only for what stayed there — and their string assertions match the current sources. Failure looks like: any of the 8 still failing with "modelsSection() is gone", a missing heading or row, or a QVERIFY2 file-open failure on a path that no longer holds the body.

## Plan
**Goal** — Make the 8 source-text tests in tests/settingspane_test.cpp pass again by pointing each at the file its body moved to in #243T, without weakening what they assert.

**Findings** — The card was filed at d6f769f6, but the test file on disk (1748 lines) has already been partially updated: most of the 8 tests now open src/RelayWindowSettings.cpp or src/RelayWindowModels.cpp instead of src/RelayWindow.h (e.g. everyRowHelperDeclaresADefault reads RelayWindowSettings.cpp for `relay::resetRow(section`, which exists at RelayWindowSettings.cpp:1131; theModelsPageHasNoTierListsAndNoChecklist and the provider tests read RelayWindowModels.cpp for `RelayWindow::modelsSection(bool inModelsPane)`, which exists at RelayWindowModels.cpp:5). Spot-checks of the asserted strings against current sources mostly match: headingRow("Board") RelayWindowSettings.cpp:469, "Work signals unasked" :486, "signals_config" :498, `sections << agent;` :583, `sections << modelsSection();` :279 followed by `sections << localModels().section();` :283, models.prioritize row RelayWindowModels.cpp:54, models.available link :464 with `of %2 available` :471 / `link.indent = 1` :479 / `link.aliases` :478, `row.ruleAbove = !firstProvider;` :297, `fill.id = "models.tier.defaults"` :897, `guestSettingKey(cli, "permissions")` :494, `if (!inModelsPane) {\n            arranged << original.mid(defaultsAt);` :893-895. The row helpers (toggleRow, numberRow, textRow, hostListRow, choiceRow, buttonRow) stayed in src/RelayWindow.h:2280-2514 with their row.reset lambdas, so reading RelayWindow.h for those is still correct. So the current failure set is probably smaller or different from the 8 in the Issue — the first step is to measure, not to edit blindly. Where the bodies now live: settingsSections() → src/RelayWindowSettings.cpp:5, modelsSection(bool) → src/RelayWindowModels.cpp:5, resetRow usage → RelayWindowSettings.cpp:1131, createModelsPane() → still src/RelayWindow.h.

**Steps**
1. `git log --oneline -15 -- tests/settingspane_test.cpp` and `git status` — another session may be mid-fix in this shared checkout; if the file has uncommitted changes, coordinate before editing (per CLAUDE.md).
2. Run `xvfb-run -a build/relay-settings-tests -silent` and record exactly which of the 8 still fail and with which message. If all pass, the card is already done — say so with the output as evidence and stop.
3. For each still-failing test, update the `QFile(RELAY_SOURCE_DIR ...)` path to the file that holds the body (settingsSections → src/RelayWindowSettings.cpp, modelsSection → src/RelayWindowModels.cpp; keep src/RelayWindow.h for the row helpers and createModelsPane) and fix any string assertion that no longer matches the moved body's current text, keeping the assertion's intent (the strings verified above are the reference).
4. Check tests/test_security.py:228, which cites settingspane_test.cpp's localModelsComeRightAfterModels() pattern — if it reads a path that moved, update it the same way.
5. Rebuild through `scripts/relay-build --target relay-settings-tests` and re-run step 2 until green.

**Risks** — These tests encode design decisions (#MDL1, #24XJ, #C3Q2, #AQ6X); fix paths and stale strings only. If an asserted string is gone because the *behaviour* regressed (not because the body moved), that is a real bug — file it and do not edit the test to match. Shared checkout: land via scripts/land.py, claim only tests/settingspane_test.cpp (and test_security.py if touched).

**Verify** — `xvfb-run -a build/relay-settings-tests -silent` reports 0 failures (48 passed); if test_security.py changed, `python3 -m pytest tests/test_security.py -x -q` too.
