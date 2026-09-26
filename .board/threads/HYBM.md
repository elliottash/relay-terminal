<!-- relay:entry 20260924T141125Z-fk author=agent kind=event model=glm-5.3 pane=58506d2c turn=5940d53f4af943a68afe7efde80da937/955e1703b91e4e5882713c2785a179dd -->
- ✦ agent created this card in Inbox · issues/changes/2026-09-24-trackeromitsroletagsincludinghistoricalrows-scre.md

<!-- relay:entry 20260925T052619Z-x5 author=owner kind=comment mode=plan -->
Plan this card.

<!-- relay:entry 20260925T052619Z-x6 author=owner kind=event pane=switchboard -->
- ✦ owner moved this card · Inbox → Planning · a Plan turn started

<!-- relay:entry 20260925T053021Z-aj author=agent kind=event model=glm-5.3 pane=switchboard turn=b694604ac9b7d990179b39ada17e728e/42008299f4b242bf916caacf226a0d89 -->
- ✦ agent updated this card · replaced `## Done means`

<!-- relay:entry 20260925T053038Z-2d author=agent kind=event model=glm-5.3 pane=switchboard turn=b694604ac9b7d990179b39ada17e728e/42008299f4b242bf916caacf226a0d89 -->
- ✦ agent updated this card · replaced `## Plan`

<!-- relay:entry 20260925T053044Z-dx author=agent kind=comment mode=plan model=glm-5.3 turn=b694604ac9b7d990179b39ada17e728e/42008299f4b242bf916caacf226a0d89 -->
The test compares two full-window grabs whose size the window manager controls — I need to see what `SubagentsPanel` does on show/resize, and how the suite is run, before writing the plan.















Two of my reads were cleared before I saw them — let me re-read `sizeHint()` and `paintEvent()` so the plan's findings are verified, not guessed.



Both findings are now verified. Writing the card's `## Done means` first, then the plan.







Planned #HYBM: the flake is the window manager, not a prior test's width — the test `show()`s two top-level `SubagentsPanel` windows (minimum width 200, `src/SubagentsPanel.h:190`) and compares grabs the WM may size differently (1499 vs 300), with `hasFocus()` as a second WM-dependent input (`src/SubagentsPanel.cpp:762`). The plan deletes the `show()`/`qWaitForWindowExposed` pair so both strips render windowless at the test's size, following the existing pattern at `tests/diffview_test.cpp:190`, and verifies with a screenshot check plus 10 clean full-suite runs. Nothing needed from you — it's ready for **Run**.

<!-- relay:entry 20260925T053044Z-q7 author=owner kind=event pane=switchboard -->
- ✦ owner moved this card · Planning → Planned · the plan is on the card
