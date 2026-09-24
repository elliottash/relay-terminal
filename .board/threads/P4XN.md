- id: 20260924T045500Z-p4xn
  ts: '2026-09-24T04:55:00Z'
  kind: note
  by: bd9e4ae0
  body: |
    Filed while delivering #Q8TM (measured on a clean export of main at 79dd56a, before that
    card's files were applied — this is not caused by 11847c).

<!-- relay:entry 20260924T043138Z-zw author=owner kind=comment mode=plan -->
Plan this card.

<!-- relay:entry 20260924T043139Z-12 author=owner kind=event pane=switchboard -->
- ✦ owner moved this card · Inbox → Planning · a Plan turn started

<!-- relay:entry 20260924T043323Z-mj author=agent kind=event model=kimi-k3 pane=switchboard turn=f0e9984d9b9b17ca9cf22456d2ceaee8/db76c868f1ce409a873417ef3cb1748b -->
- ✦ agent updated this card · replaced `## Done means`

<!-- relay:entry 20260924T043359Z-d1 author=agent kind=event model=kimi-k3 pane=switchboard turn=f0e9984d9b9b17ca9cf22456d2ceaee8/db76c868f1ce409a873417ef3cb1748b -->
- ✦ agent updated this card · replaced `## Plan`

<!-- relay:entry 20260924T043411Z-px author=agent kind=comment mode=plan model=kimi-k3 turn=f0e9984d9b9b17ca9cf22456d2ceaee8/db76c868f1ce409a873417ef3cb1748b -->
Reading the card and finding the code that builds the plan-execute prompt.





The prompt is built by `planning.execution_prompt` — reading that now.







**Plan written on #P4XN.** The working tree already tells most of the story: `execution_prompt` in `backend/relay_core/planning.py` now opens "Run the plan in {path}:", matching the long-plan fallback in `_plan_execute` — so the rewording was likely deliberate and the test is the stale side. The plan has Run re-measure on a clean export of current main (the issue's HEAD may already be stale), confirm via `git log -S` which commit changed the wording, then fix whichever side regressed — most likely updating the assertion in `tests/test_session_protocol.py`. Nothing needed from the owner; it's ready for Run.

<!-- relay:entry 20260924T043411Z-py author=owner kind=event pane=switchboard -->
- ✦ owner moved this card · Planning → Planned · the plan is on the card
