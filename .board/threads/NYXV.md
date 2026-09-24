<!-- relay:entry 20260923T183233Z-av author=agent kind=event model=glm-5.3 pane=fe15f16b turn=bf4b6c467eef4785b8de018d8c4b5b5a/e761a2eba201444288903a52f312ead4 -->
- ✦ agent created this card in Inbox · issues/changes/2026-09-23-tests-test-tier-lists-relay-pro-cloud-rows-fail.md

<!-- relay:entry 20260924T043123Z-8y author=owner kind=comment mode=plan -->
Plan this card.

<!-- relay:entry 20260924T043123Z-8z author=owner kind=event pane=switchboard -->
- ✦ owner moved this card · Inbox → Planning · a Plan turn started

<!-- relay:entry 20260924T043700Z-y8 author=agent kind=event model=kimi-k3 pane=switchboard turn=a9ddef4791aebdad40d9f56d13faa268/cf34c66433694c6f823b16affce34f3c -->
- ✦ agent updated this card · replaced `## Done means`

<!-- relay:entry 20260924T043727Z-g3 author=agent kind=event model=kimi-k3 pane=switchboard turn=a9ddef4791aebdad40d9f56d13faa268/cf34c66433694c6f823b16affce34f3c -->
- ✦ agent updated this card · replaced `## Plan`

<!-- relay:entry 20260924T043738Z-pq author=agent kind=comment mode=plan model=kimi-k3 turn=a9ddef4791aebdad40d9f56d13faa268/cf34c66433694c6f823b16affce34f3c -->
Found the mechanism. Let me confirm what `model-ranking.md` says for glm-5.3 so the plan states the exact divergence.















I need to re-read the exact test and `catalog_rows` body to pin down the failure precisely.

One more read of `tier_start_efforts` to nail the exact divergence for all three rows.









**Plan written on #NYXV.** The diagnosis: it's a name mismatch, not missing keys — `catalog_rows` computes `tier_effort` from the ranking-table name (`glm-5.3`, per `_ranking_name`'s "hosted role names never change defaults" contract), while the test recomputes from the public role name (`relay pro · high`), which is never in `model-ranking.md`, so it gets the pure rule fallback. The fix is a one-line test change (recompute with `P._ranking_name(preset_id, row['id'])`), with the runner told to reproduce first and confirm each failing assert matches the diagnosis before editing.

Nothing needed from you — the one judgement call (test fix vs. changing what the catalog stores) is settled by `_ranking_name`'s documented intent, and the plan's Risks section flags the alternative as out of scope if the runner disagrees.

<!-- relay:entry 20260924T043738Z-pr author=owner kind=event pane=switchboard -->
- ✦ owner moved this card · Planning → Planned · the plan is on the card
