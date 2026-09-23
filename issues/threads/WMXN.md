<!-- relay:entry 20260923T180918Z-1x author=owner kind=comment mode=plan -->
Plan this card.

<!-- relay:entry 20260923T180918Z-y6 author=owner kind=event pane=switchboard -->
- ✦ owner moved this card · Inbox → Planning · a Plan turn started

<!-- relay:entry 20260923T181425Z-z8 author=agent kind=event model=kimi-k3 pane=switchboard turn=7de503e81b3c236891131b09eb219f66/0efa79d2a1c94a8eb4520b2378435eb4 -->
- ✦ agent updated this card · replaced `## Done means`

<!-- relay:entry 20260923T181509Z-8a author=agent kind=event model=kimi-k3 pane=switchboard turn=7de503e81b3c236891131b09eb219f66/0efa79d2a1c94a8eb4520b2378435eb4 -->
- ✦ agent updated this card · replaced `## Plan`

<!-- relay:entry 20260923T181525Z-da author=agent kind=comment mode=plan model=kimi-k3 turn=7de503e81b3c236891131b09eb219f66/0efa79d2a1c94a8eb4520b2378435eb4 -->
I'll read the card and the code it points at.

Now let me look at the actual source files.





The `hooks.recap` setter isn't in `src/Pane.h` anymore. Let me find where the pane sets it.





















The pane code lives in `src/Pane.h`, which is over the read limit — the executing agent will need to grep it directly. Let me check the remote side of the chain and the protocol docs.

One more check — the tests that touch this path.





**Done:** #WMXN is planned. The plan takes the card's GUI-side fix — the `hooks.recap` lambda in `src/Pane.h` sends `manual` instead of `remote`, one word — after tracing the whole chain (`app/pane.js` → `remote/gui_host.py` → `src/RemoteShare.cpp` → pane hook → worker) and confirming no reason crosses the phone↔desktop wire, so no protocol contract changes. `manual` is also semantically right: it always runs, where `away`/`resume` dedupe-skip.

One thing for the owner: the plan records the GUI-side choice over the worker-side alternative in Risks — if you'd rather grow `RECAP_REASONS` and the protocol doc instead, say so before pressing **Execute**.

<!-- relay:entry 20260923T181525Z-db author=owner kind=event pane=switchboard -->
- ✦ owner moved this card · Planning → Planned · the plan is on the card
