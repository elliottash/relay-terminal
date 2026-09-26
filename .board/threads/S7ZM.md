<!-- relay:entry 20260925T140446Z-r9 author=agent kind=event model=glm-5.3 pane=1edbbcab turn=1ceb245c9ab743cc9e7e3f4dcb245779/2c143aef86594a08864e4fa6f6484344 -->
- ✦ agent created this card in Inbox · .board/changes/2026-09-25-panestatus-whogetsaband-and-bytypediffersbygroup.md

<!-- relay:entry 20260925T140458Z-cf author=agent kind=evidence model=glm-5.3 pane=1edbbcab turn=1ceb245c9ab743cc9e7e3f4dcb245779/2c143aef86594a08864e4fa6f6484344 -->
Measured 2026-09-25, clean export of HEAD (git archive into a scratch build dir, target relay-panestatus-tests, offscreen): "Totals: 24 passed, 2 failed" — FAIL PaneStatusTests::whoGetsABand (tests.fill #ff27241e vs board-by-group #ff302b22) and FAIL PaneStatusTests::byTypeDiffersByGroupShares (byGroup.size() 2, expected 1). The same two fail in the shared build/ with #4CXY's diff applied, so #4CXY is not the cause. Last commit touching PaneStatus: a605fe5e "Board vs Sessions & Projects: carry each pane's hue past its band (#MXMG)" — its per-pane hues are the likely reason the "every tool pane shares the tools tint" expectations no longer hold. That commit's message already records another pre-existing failure (#9ESY) in the same area.

<!-- relay:entry 20260925T140458Z-cg author=agent kind=event model=glm-5.3 pane=1edbbcab turn=1ceb245c9ab743cc9e7e3f4dcb245779/2c143aef86594a08864e4fa6f6484344 -->
- ✦ agent moved this card · Inbox → Discussing · the discussion started
