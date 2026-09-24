<!-- relay:entry 20260921T002544Z-75 author=agent kind=event model=kimi-k3 pane=switchboard turn=56a4a9ed565b4b3cac5307c3949f2568/42d7c2f3225a4579b98a927d6e3c99ec -->
- ✦ agent created this card in Inbox · issues/changes/2026-09-20-a-paired-device-s-grant-cannot-be-raised-short-o.md

<!-- relay:entry 20260921T002600Z-b1 author=agent kind=event model=kimi-k3 pane=switchboard turn=56a4a9ed565b4b3cac5307c3949f2568/42d7c2f3225a4579b98a927d6e3c99ec -->
- ✦ agent updated this card · appended to `## Discussion points`

<!-- relay:entry 20260924T043111Z-yv author=owner kind=comment mode=plan -->
Plan this card.

<!-- relay:entry 20260924T043111Z-zz author=owner kind=event pane=switchboard -->
- ✦ owner moved this card · Inbox → Planning · a Plan turn started

<!-- relay:entry 20260924T043428Z-wv author=agent kind=event model=kimi-k3 pane=switchboard turn=b07de14b092bb2791583da5503e8ef9b/8f955f7811564a00b2c3cc09d98d3283 -->
- ✦ agent updated this card · replaced `## Done means`

<!-- relay:entry 20260924T043506Z-k1 author=agent kind=event model=kimi-k3 pane=switchboard turn=b07de14b092bb2791583da5503e8ef9b/8f955f7811564a00b2c3cc09d98d3283 -->
- ✦ agent updated this card · replaced `## Plan`

<!-- relay:entry 20260924T043515Z-b0 author=agent kind=comment mode=plan model=kimi-k3 turn=b07de14b092bb2791583da5503e8ef9b/8f955f7811564a00b2c3cc09d98d3283 -->
**Planned:** #RSJY now has `## Done means` and a four-step plan — a `grant` message on the GUI↔sidecar line calling the existing `DeviceStore.set_capability`, a re-`welcome` push so a live phone unlocks in place, a grant picker in the share dialog mirroring the Passwords toggle, and clearer view-only copy in `app/app.js`.

One finding worth noting: capability enforcement already reads live, so no reconnect is needed — but nothing currently pushes a changed grant to a connected phone, which step 2 adds. The plan also guards that `grant` stays desktop-only (never in `CLIENT_TYPES`), so a phone can never raise its own grant. Nothing needed from you; it's ready to Run.

<!-- relay:entry 20260924T043515Z-g5 author=owner kind=event pane=switchboard -->
- ✦ owner moved this card · Planning → Planned · the plan is on the card
