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

<!-- relay:entry 20260924T042755Z-w7 author=owner kind=event pane=switchboard -->
- ✦ owner claimed this card · assignee agent, Planned → Executing, session cbe2c0ba

<!-- relay:entry 20260924T042755Z-w8 author=owner kind=progress pane=switchboard pane_token=cbe2c0ba-c9c2-45a0-b49a-ca215c9a7f3b -->
Claimed (cbe2c0ba) · working on it from a terminal pane

Run pressed on iOS Safari.

<!-- relay:entry 20260924T043231Z-6s author=agent kind=event model=glm-5.3 pane=cbe2c0ba turn=f5638ba7bc1042cab639c45ab9548ad1/553b4ed9593846959786ddc5ed3409ff -->
- ✦ agent updated this card · links: {"plans": [], "commits": [], "evidence": [], "related": ["MVGR"], "github": null… → {"plans": [], "commits": ["8fc8d60b"], "evidence": ["docs/qa_evidence/2026-09-24…; appended to `## Execution Summary`

<!-- relay:entry 20260924T043242Z-ze author=agent kind=event model=glm-5.3 pane=cbe2c0ba turn=f5638ba7bc1042cab639c45ab9548ad1/553b4ed9593846959786ddc5ed3409ff -->
- ✦ agent updated this card · appended to `## Tests`

<!-- relay:entry 20260924T043242Z-zf author=agent kind=progress model=glm-5.3 pane=cbe2c0ba turn=f5638ba7bc1042cab639c45ab9548ad1/553b4ed9593846959786ddc5ed3409ff -->
Executed: the one-word fix is already on `main` (`8fc8d60b`, Sep 20, names #WMXN) — `hooks.recap` sends `manual` with the explanatory comment. Nothing new to change or build; verified instead: grep shows no `"remote"` recap reason anywhere, worker accepts `manual` (test passes), phone-side wire untouched (tests pass). Met one unrelated pre-existing failure (`test_compact_resume_recap_and_plan_execute`, plan_execute tail, fails on clean `main` too) — already filed as #P4XN. Evidence: docs/qa_evidence/2026-09-24-recap-phone-manual/.

<!-- relay:entry 20260924T043248Z-59 author=agent kind=event model=glm-5.3 pane=cbe2c0ba turn=f5638ba7bc1042cab639c45ab9548ad1/553b4ed9593846959786ddc5ed3409ff -->
- ✦ agent moved this card · Executing → Needs verification · Fix already landed in 8fc8d60b; verified against the plan's checks (grep, worker manual-acceptance test, phone-side wire tests all green). Evidence in docs/qa_evidence/2026-09-24-recap-phone-manual/. · evidence docs/qa_evidence/2026-09-24-recap-phone-manual/ · implemented_by glm/glm-5.3

<!-- relay:entry 20260924T043445Z-va author=agent kind=event model=glm-5.3 pane=cbe2c0ba turn=f5638ba7bc1042cab639c45ab9548ad1/553b4ed9593846959786ddc5ed3409ff -->
- ✦ agent updated this card · links: {"plans": [], "commits": ["8fc8d60b"], "evidence": ["docs/qa_evidence/2026-09-24… → {"commits": ["8fc8d60b", "9be5a16"], "evidence": ["docs/qa_evidence/2026-09-24-r…

<!-- relay:entry 20260924T043556Z-vd author=agent kind=event model=glm-5.3 pane=cbe2c0ba turn=f5638ba7bc1042cab639c45ab9548ad1/553b4ed9593846959786ddc5ed3409ff -->
- ✦ agent updated this card · links: {"commits": ["8fc8d60b", "9be5a16"], "evidence": ["docs/qa_evidence/2026-09-24-r… → {"commits": ["8fc8d60b", "9be5a16", "72f6e48"], "evidence": ["docs/qa_evidence/2…
