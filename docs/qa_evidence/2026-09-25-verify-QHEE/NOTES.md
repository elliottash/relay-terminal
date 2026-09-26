# Verify QHEE — Local models group in Sources (2026-09-25)

Pinned revision: 2db966438ab8bc61e0f9f1ff89a51853286ad0f8. The two commits named in Execution Summary — `967735e1` and `a25932e4` — are both ancestors (verified with `git merge-base --is-ancestor`); `links.commits` is empty on the card.

## Code at HEAD
- `src/LocalModelsSettings.cpp:460` `LocalModelsSettings::compactSection()` — the concise Local models section; appended into Sources at `src/RelayWindow.h:1433` (`sources.rows += localModels().compactSection().rows;`). The Options › Local models page is untouched (full section remains; settings tests for #24XJ still present at tests/settingspane_test.cpp:235).
- Coverage: no separate `relay-localmodels-tests` target exists at HEAD — `relay-localmodels` is a static library (CMakeLists.txt:227); the Sources group is asserted in `tests/modelspane_test.cpp:236-237` (heading "Local models").

## Tests (clean worktree, this sweep)
- `ctest -R '^modelspane$'`: **25 passed, 1 failed** — the failure is the #E8V1 stale-string rename (#SYTR) in `theHelperIsOneRowUnderAllFiveTabsAndBuildsOneConsole`, a function unrelated to the Local models group; the Sources/Local-models cases pass.
- `ctest -R '^settings$'`: **51 passed, 1 failed** — same #SYTR cause.
- At the card's 2026-09-23 check both suites were green; the current single failures postdate it (E8V1 landed 2026-09-25 10:27).

## Live drive (Xvfb :97, isolated profile, `drive.sh`)
- `01-sources-local-group.png`: Sources shows the concise **Local models** group after the import-keys row: status "Start a pane's agent, then reload local models.", "Reload local models" action, "Set up a local model" helper row ("The agent checks this machine and guides setup", Ask agent…), More-local-settings link — the compact form, no per-model table on Sources.
- The per-model table state (a live local registry) is not exercisable on this rig (no local server); it is covered by the implementer's `02-pulling-local-models.png` and the green modelspane cases.

## Verdict
PASS — the card's own unfinished item (build + screenshot of Sources with the Local models group) is captured here.
