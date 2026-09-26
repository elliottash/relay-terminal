# Verify BXMS — box picks the pane's model; "all models" and "model settings" rows (2026-09-25)

Pinned revision: 2db966438ab8bc61e0f9f1ff89a51853286ad0f8 (clean-worktree builds). Commit 12ef73d7 is an ancestor.

## Code
- `src/ModelRows.cpp:370` `gear:picker` ("model settings") and `:381` `gear:all` close the box list; the full list (`filtered`) omits "all models".
- `src/ModelsPane.cpp:255` — "No `onUse`: the picker then offers no 'use' and Enter picks nothing (card #BXMS)". No `Target::use` remains.

## Tests (clean worktree)
- `ctest -R '^(modelrows|modelpicker|filterpopup)$'` — **all pass** (modelrows incl. the gear rows; modelpicker 62/0; filterpopup green).
- `ctest -R '^modelspane$'` — 25 passed, 1 failed: only `theHelperIsOneRowUnderAllFiveTabsAndBuildsOneConsole`, the #E8V1 stale-string failure (#SYTR). **This card's own named tests pass**: `enterPicksNoPanesModel` + `theHeaderSaysTheSettingsAreForEveryPane` → 4 passed, 0 failed.
- The card's recorded `theFooterNamesTheRealKeys` failure is now gone (fixed since).

## Live drive (Xvfb :97, isolated profile)
- `01-box.png`: Alt+M box ends with "all models" and "model settings".
- `02-all-models-list.png`: "all models" reopens the box as the full scrollable list — role rows plus "other models": claude-haiku-4.5, gpt-6-astra, gpt-6-sol, gpt-6-luna, gpt-5.6-sol, gpt-5.6-terra…
- Models pane header reads "Shared model settings - choose an individual pane's active model in its model box" — no "for: <pane>" (02/04 screenshots; also visible in this sweep's N4PW/MH7P captures).
- Picking from the full list as an ordinary box pick: covered by the green modelrows/modelpicker suites; the live pick attempt got tangled in the first-run "instruction files" overlay (that dialog steals focus on fresh profiles) so the switch itself was not cleanly captured live this pass.

## Verdict
PASS (one live nicety not cleanly captured; test-covered).
