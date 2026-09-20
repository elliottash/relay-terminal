<!-- relay:entry 20260920T052057Z-fp author=owner kind=event pane=switchboard -->
- ✦ owner created this card in Inbox · issues/features/2026-09-20-clicking-hash-tag-in-pane-header-doesnt-link-cor.md

<!-- relay:entry 20260920T052127Z-vh author=owner kind=event pane=switchboard -->
- ✦ owner updated this card · replaced `## Issue`

<!-- relay:entry 20260920T052127Z-vi author=owner kind=rewrite pane=switchboard -->
- ✦ rewrote ## Issue

<details><summary>before</summary>

```
clicking hash tag in pane header doesnt link correctly
```

</details>

<details><summary>after</summary>

```
i just clicked on this pane header hash tag, #B9V4
51a8f37535fb458594017ad88ee35331

but it took me to #2Y96
```

</details>

<!-- relay:entry 20260920T052129Z-zt author=owner kind=comment mode=plan -->
Plan this card.

<!-- relay:entry 20260920T052129Z-zu author=owner kind=event pane=switchboard -->
- ✦ owner moved this card · Inbox → Planning · a Plan turn started

<!-- relay:entry 20260920T053321Z-87 author=agent kind=event model=kimi-k3 pane=switchboard turn=bf0a4a7f05eb48e38927e455cda7e042/fad419630b0e42c1a9cedc07a945a0fd -->
- ✦ agent updated this card · replaced `## Plan`

<!-- relay:entry 20260920T053332Z-7c author=agent kind=comment mode=plan model=kimi-k3 turn=bf0a4a7f05eb48e38927e455cda7e042/fad419630b0e42c1a9cedc07a945a0fd -->
The GUI-side path looks exact so far. Let me check the worker side that actually serves "open this card".











The worker side resolves the id exactly, so let me look at how the GUI handles the `board_card` response and the chip's press tracking.

Found a strong lead. Let me verify the chip-refresh call sites.





Let me check the code around the chip click handler and the refresh logic more precisely.



One more check — how the header mouse helper is dispatched from the event filter, and whether there's an ownership guard.





I have enough to write the plan. The whole chain from chip to board is mapped; the plan pinpoints where `#B9V4` can turn into `#2Y96`.

**Done:** the plan is on #GE0Z. The click path is exact id-passing from chip (`Pane.h:3401`) through `openBoardCard` (`RelayWindow.h:4425`) to the worker's `board_card_get`, so the plan names four concrete suspects where the id can change hands: stale chip text vs live `cardChipCard()`, the qApp-wide `m_headerPressOn` capture, `waitForBoardCard` giving up when a detail is already open (closest match to the symptom), and the detail view pushing its own card back into the selection (`BoardPane.cpp:2780-2793`).

It asks the executor to reproduce with logging at each hand-off first — the line where `#B9V4` becomes `#2Y96` picks the fix. Nothing needed from you; it's ready for Execute.

<!-- relay:entry 20260920T053332Z-s7 author=owner kind=event pane=switchboard -->
- ✦ owner moved this card · Planning → Planned · the plan is on the card
