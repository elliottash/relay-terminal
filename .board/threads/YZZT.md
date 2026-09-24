<!-- relay:entry 20260923T214752Z-na author=agent kind=event model=gpt-6-sol pane=49dbf51d turn=bf07ea53fe1e47b383aeb5e7025ce0da/83e87d8fc35a427d9728ee1d2323a369 -->
- ✦ agent created this card in Inbox · issues/changes/2026-09-23-mid-turn-guest-model-switch-leaves-the-old-harne.md

<!-- relay:entry 20260924T043146Z-fp author=owner kind=comment mode=plan -->
Plan this card.

<!-- relay:entry 20260924T043146Z-jf author=owner kind=event pane=switchboard -->
- ✦ owner moved this card · Inbox → Planning · a Plan turn started

<!-- relay:entry 20260924T044024Z-36 author=agent kind=event model=kimi-k3 pane=switchboard turn=e8b1c734aaca58ce21a71beca71d1bcf/ea207627a5f041e3a63ef3c3a18fd116 -->
- ✦ agent updated this card · replaced `## Done means`

<!-- relay:entry 20260924T044101Z-t5 author=agent kind=event model=kimi-k3 pane=switchboard turn=e8b1c734aaca58ce21a71beca71d1bcf/ea207627a5f041e3a63ef3c3a18fd116 -->
- ✦ agent updated this card · replaced `## Plan`

<!-- relay:entry 20260924T044111Z-ng author=agent kind=comment mode=plan model=kimi-k3 turn=e8b1c734aaca58ce21a71beca71d1bcf/ea207627a5f041e3a63ef3c3a18fd116 -->
Now let me look at the code the card is about.

























The tool results were cleared from context earlier; let me re-read both cards to pin down the exact defect.









The wiring looks like it may have changed since the card was filed. Let me check the related card #MSW7, which the `defer_model` docstring credits with the `apply_model` mechanism.





The main deferred path looks covered by tests. Let me re-check the two helpers the card names — `switch_model` in `guest_harness_provider.py` and the role-pick caller in `worker.py`.



One more check — re-reading #B9V4 to see whether its landed work already closed this gap.













**Done:** Plan written on #YZZT. The key finding: the defect appears already fixed — #MSW7's `apply_model` threading (`switch_model` → `request_model` → `defer_model` → `_land_switch`) routes the deferred landing through `apply_target`, which starts, attaches and closes harnesses correctly, and `test_deferred_guest_pick_starts_only_at_landing_and_serves_next_step` already asserts the harness starts only at the landing. The plan therefore verifies the existing tests, adds the two genuinely untested quadrants (mid-turn guest→different guest, guest→same guest model change), and only touches code if one of them reproduces the bug. Nothing needed from the owner — the card is ready to Run.

<!-- relay:entry 20260924T044111Z-nh author=owner kind=event pane=switchboard -->
- ✦ owner moved this card · Planning → Planned · the plan is on the card
