<!-- relay:entry 20260925T142754Z-7t author=agent kind=event model=glm-5.3 pane=9bb7f3dd turn=d2a85ebe7b53481495cee7a648fa0f4c/5de14ecc24764f04b8069c7c76269329 -->
- ✦ agent created this card in Inbox · .board/features/2026-09-25-board-sortable-viewed-column-to-get-back-to-rece.md

<!-- relay:entry 20260926T011508Z-6s author=owner kind=comment mode=plan -->
Plan this card.

<!-- relay:entry 20260926T011508Z-6t author=owner kind=event pane=switchboard -->
- ✦ owner moved this card · Inbox → Planning · a Plan turn started

<!-- relay:entry 20260926T011658Z-ff author=agent kind=event model=z-ai/glm-5.3-flashx pane=switchboard turn=b864d0562f917a75e059d5f7708db6d2/666b62b2597c4419baf730e9bc136f05 -->
- ✦ agent updated this card · replaced `## Done means`

<!-- relay:entry 20260926T011712Z-f6 author=agent kind=event model=z-ai/glm-5.3-flashx pane=switchboard turn=b864d0562f917a75e059d5f7708db6d2/666b62b2597c4419baf730e9bc136f05 -->
- ✦ agent updated this card · replaced `## Plan`

<!-- relay:entry 20260926T011717Z-jr author=agent kind=comment mode=plan model=z-ai/glm-5.3-flashx turn=b864d0562f917a75e059d5f7708db6d2/666b62b2597c4419baf730e9bc136f05 -->
I'll read the card first, then look at the Board code to find how columns and sorting work.

No "viewed" tracking exists yet. Let me find the Board UI's column/sort code and where a card page is opened.







The model shows how sorting works and that `unread` is already local (QSettings). Now let me see the rest of the sort plumbing and where the list's column headers and card opening live.



The glob with a path prefix matched nothing — let me search inside `src/` directly.



The sort plumbing is clear. Now let me confirm how `unread` (the existing local per-card state) is stored, and find where a card page is opened — that's where "viewed" would be stamped.









The model side is fully mapped. Two last checks: where `Card::unread` is filled (the existing local-state precedent) and what `BoardView::openCard` looks like, where the stamp would go.



One more check — which test files cover the Board model's sorts, so Verify names real tests.

Now I'll write the plan onto the card — first `## Done means`, then `## Plan`.

Hash typo — re-reading the card for the exact hash.





**Plan written to #FKSN.** The Viewed column reuses the existing Created/Updated sort plumbing in `src/BoardModel.h`/`BoardPane.cpp` and stamps "last viewed" in `BoardView::openSelected()` — the single funnel every card-page open goes through — into a local QSettings map (per machine, never into git). No owner decision needed unless the fifth column proves too narrow on small panes, which the plan flags as a risk.

<!-- relay:entry 20260926T011717Z-re author=owner kind=event pane=switchboard -->
- ✦ owner moved this card · Planning → Planned · the plan is on the card
