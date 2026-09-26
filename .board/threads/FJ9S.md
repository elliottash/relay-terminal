<!-- relay:entry 20260923T171943Z-kf author=owner kind=event pane=switchboard -->
- ✦ owner created this card in Inbox · issues/features/2026-09-23-bug-ctrl-shift-s-would-not-open-sessions-from-an.md

<!-- relay:entry 20260925T125213Z-qy author=owner kind=comment mode=plan -->
Plan this card.

<!-- relay:entry 20260925T125213Z-r7 author=owner kind=event pane=switchboard -->
- ✦ owner moved this card · Inbox → Planning · a Plan turn started

<!-- relay:entry 20260925T130353Z-0j author=agent kind=event model=glm-5.3 pane=switchboard turn=f5d5f0dd0ebb2a4bb351a691bb4791cb/e914ced9d364461cb021a61078cc52a7 -->
- ✦ agent updated this card · replaced `## Done means`

<!-- relay:entry 20260925T130412Z-5y author=agent kind=event model=glm-5.3 pane=switchboard turn=f5d5f0dd0ebb2a4bb351a691bb4791cb/e914ced9d364461cb021a61078cc52a7 -->
- ✦ agent updated this card · replaced `## Plan`

<!-- relay:entry 20260925T130418Z-8f author=agent kind=comment mode=plan model=glm-5.3 turn=f5d5f0dd0ebb2a4bb351a691bb4791cb/e914ced9d364461cb021a61078cc52a7 -->
Now let me find the code behind the shortcut.

























































































































Root cause found. Let me check the test landscape before writing the plan.











Root cause confirmed. Now writing the card's Done means, then the plan.







Plan written on #FJ9S. The dispatch chain is healthy; the bug is `RelayWindow::openSessions` (`src/RelayWindow.h:4028`) silently returning when the current tab holds no terminal `Pane` — exactly the text-editor-only-tab case — and the plan replaces that return with the code's own idiom for an ownerless manager (create the pane, dock it beside the active leaf, give it an owner).

One decision for you, noted in the plan's Risks: the fix also spawns a terminal pane beside the manager in such tabs, because that is what the existing code already does for an ownerless Sessions pane — say so on the card if you'd rather it opened alone.

<!-- relay:entry 20260925T130418Z-mf author=owner kind=event pane=switchboard -->
- ✦ owner moved this card · Planning → Planned · the plan is on the card
