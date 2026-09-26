<!-- relay:entry 20260925T221933Z-6y author=agent kind=note -->
Filed by the models-verification sweep (revs 54018502 → 9a13cfe2, HEAD moved mid-sweep). Evidence: docs/qa_evidence/2026-09-25-verify-bug-cards/NOTES.md.

<!-- relay:entry 20260925T225929Z-58 author=agent kind=event model=k3 pane=164b2cbb turn=6173581165764472b6c340786cfdcc26/7dbfd40692e043f0b4ab6d09e876fd2c from=D4BJ orig=20260925T222312Z-zy -->
- ✦ agent created this card in Inbox · .board/changes/2026-09-25-half-landed-rename-settings-modelspane-tests-exp.md

<!-- relay:entry 20260925T225929Z-qj author=agent kind=evidence model=k3 pane=164b2cbb turn=6173581165764472b6c340786cfdcc26/7dcf283a26f744f08a7ef0a60a29f160 from=D4BJ orig=20260925T223540Z-7t -->
Corroborated by the WBFM C2 agent (2026-09-25): `relay-modelspane-tests` subtest `theHelperIsOneRowUnderAllFiveTabs…` fails the same way at tip — expects "Helper Agent (Alt+Q)", `src/SettingsPane.cpp:1611` renders "Agent (Alt+Q)". The mismatch traces to #E8V1 commit `e6febb9f` (tests updated, source not). Also seen by the C1 agent in the `settings` suite. Two independent verify-slot runs, clean main.

<!-- relay:entry 20260925T225929Z-qk author=agent kind=event model=k3 pane=164b2cbb turn=6173581165764472b6c340786cfdcc26/7dcf283a26f744f08a7ef0a60a29f160 from=D4BJ orig=20260925T223540Z-km -->
- ✦ agent moved this card · Inbox → Discussing · the discussion started

<!-- relay:entry 20260925T225929Z-ql author=agent kind=event model=k3 pane=164b2cbb turn=6173581165764472b6c340786cfdcc26/7aeccffbcbab4e24b0c8129df98ba560 -->
- ✦ agent merged #D4BJ into this card · Same fault filed twice within the hour: the half-landed "Helper Agent"→"Agent" rename from #E8V1 (commit e6febb9f). SYTR names all four stale Qt tests and files; D4BJ was the C1/C2 agents' independent sightings of two of them.

<!-- relay:entry 20260926T003549Z-2f author=agent kind=event mention=3MH4 model=claude-fable-5-1 pane=f35051fe turn=dbc9af56def4427c91f4fecd2d277da4/1a6acee91a96495087b757d1ef3ee4e7 -->
mentioned in #3MH4 · 2026-09-26 · agent

<!-- relay:entry 20260926T005435Z-b9 author=agent kind=event mention=WBFM model=gpt-6-luna pane=296d1b26 turn=6173581165764472b6c340786cfdcc26/e640a286e6b34dd1b7ddd4a736082925 -->
mentioned in #WBFM · 2026-09-26 · agent

<!-- relay:entry 20260926T005436Z-e0 author=agent kind=event mention=WK7C model=gpt-6-luna pane=296d1b26 turn=6173581165764472b6c340786cfdcc26/e640a286e6b34dd1b7ddd4a736082925 -->
mentioned in #WK7C · 2026-09-26 · agent
