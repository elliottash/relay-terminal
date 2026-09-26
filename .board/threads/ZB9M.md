<!-- relay:entry 20260925T011633Z-jh author=agent kind=event model=claude-opus-5-5 pane=d7900abc turn=ac598e144c144e34a442ba28dd6ed4c3/af28e942d2e34407a8240d18aa090b52 -->
- ✦ agent created this card in Discussing · .board/features/2026-09-24-board-ceremony-costs-more-than-a-small-change-30.md

<!-- relay:entry 20260925T011643Z-7b author=agent kind=event model=claude-opus-5-5 pane=d7900abc turn=ac598e144c144e34a442ba28dd6ed4c3/af28e942d2e34407a8240d18aa090b52 -->
- ✦ agent updated this card · waiting_on: (unset) → owner; replaced `## Planning notes`

<!-- relay:entry 20260925T040100Z-fh author=owner kind=comment mode=plan -->
Plan this card.

<!-- relay:entry 20260925T040100Z-fi author=owner kind=event pane=switchboard -->
- ✦ owner moved this card · Discussing → Planning · a Plan turn started

<!-- relay:entry 20260925T040416Z-cw author=agent kind=event model=glm-5.3 pane=switchboard turn=0ae5022447067ffac13a69ae1fbeb567/2f9400c5f3b2456687730fb3ff099fa3 -->
- ✦ agent updated this card · replaced `## Done means`; verify defaulted from skill deliver

<!-- relay:entry 20260925T040424Z-cv author=agent kind=event model=glm-5.3 pane=switchboard turn=0ae5022447067ffac13a69ae1fbeb567/2f9400c5f3b2456687730fb3ff099fa3 -->
- ✦ agent updated this card · replaced `## Plan`

<!-- relay:entry 20260925T040428Z-qd author=agent kind=comment mode=plan model=glm-5.3 turn=0ae5022447067ffac13a69ae1fbeb567/2f9400c5f3b2456687730fb3ff099fa3 -->
The card is about the `deliver` skill's ceremony — let me read the skill itself.

The deliver skill's tier table is the target of item 1. Now let me find where these pieces live as files: the skill, the board tools (`board_move_card`, `board_try`), and the case ledger.













The file is over the read limit; I'll pull the `_move` docstring and the case-ledger functions via search.











I have what I need: the procedure text's two sources, the `_move`/`_board_try` touchpoints, and the case ledger. Writing `## Done means` first.

Wrong hash on my part — re-reading the card.



Now the plan itself.

Plan written to #ZB9M with a revised `## Done means`. It fixes the deliver procedure's text at its two sources (`board_policy.md` + the bundled `deliver` skill, regenerating `.board/POLICY.md`), adds three code changes — `sections` on `board_move_card` for one-write close-out, a `verify.primary == person` gate in `_board_try`, and a ceremony-vs-implementation case-ledger row — each with a targeted test in `tests/test_board_tools.py`. It needs two owner decisions: the new tier wording ("needs eyes" = new or rearranged UI) and whether `board_try` unlocks only on the verify block or also on a spoken "stage it anyway".

<!-- relay:entry 20260925T040428Z-qe author=owner kind=event pane=switchboard -->
- ✦ owner moved this card · Planning → Planned · the plan is on the card

<!-- relay:entry 20260925T215041Z-dw author=owner kind=event pane=switchboard -->
- ✦ owner claimed this card · assignee agent, Planned → Running, session ca66ac8d

<!-- relay:entry 20260925T215041Z-rb author=owner kind=progress pane=switchboard pane_token=ca66ac8d-f133-4333-adbd-b69a70ec1ba6 -->
Claimed (ca66ac8d) · working on it from a terminal pane

<!-- relay:entry 20260925T222853Z-6z author=agent kind=event model=k3 pane=ca66ac8d turn=0ed464e0b5614c6bb9d746ce628750a1/bcd7b9a0bff5474194b8d5a894ae905c -->
- ✦ agent moved this card · Running → Needs verification · All six plan steps landed in one commit (668e381a): tier wording, evidence-in-one-commit, try-it gate, one-call close-out, ceremony metering, POLICY regenerated; text evidence rides the commit and ## Tests per the new rule. · evidence docs/qa_evidence/2026-09-25-zb9m/ · implemented_by kimi/k3

<!-- relay:entry 20260925T222929Z-jj author=agent kind=event model=k3 pane=ca66ac8d turn=0ed464e0b5614c6bb9d746ce628750a1/bcd7b9a0bff5474194b8d5a894ae905c -->
- ✦ agent updated this card · links: {"plans": [], "commits": ["668e381ad13d"], "evidence": ["docs/qa_evidence/2026-0… → {"commits": ["668e381ad13d"], "evidence": [], "github": null, "plans": [], "rela…; replaced `## Execution Summary`

<!-- relay:entry 20260925T222940Z-p4 author=agent kind=event model=k3 pane=ca66ac8d turn=0ed464e0b5614c6bb9d746ce628750a1/bcd7b9a0bff5474194b8d5a894ae905c -->
- ✦ agent updated this card · replaced `## Tests`

<!-- relay:entry 20260925T224237Z-ay author=agent kind=event mention=BYHN model=glm-5.3 pane=switchboard turn=55b5f1933800fc34299d3ef05c9b05e3/5d68172c2e464a92bc05f284a9fda529 -->
mentioned in #BYHN · 2026-09-25 · agent
