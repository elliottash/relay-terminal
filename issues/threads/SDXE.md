<!-- relay:entry 20260920T122223Z-wh author=owner kind=event pane=switchboard -->
- ✦ owner created this card in Inbox · issues/features/2026-09-20-bug-pane-sizes-jiggle-in-response-to-content.md

<!-- relay:entry 20260920T122311Z-d3 author=owner kind=event pane=switchboard -->
- ✦ owner updated this card · replaced `## Issue`

<!-- relay:entry 20260920T122311Z-dy author=owner kind=rewrite pane=switchboard -->
- ✦ rewrote ## Issue

<details><summary>before</summary>

```
bug: pane sizes jiggle in response to content
```

</details>

<details><summary>after</summary>

```
bug: pane sizes jiggle in response to content.

when agents / shells are working and content is coming into panes, they can become wider dynamically, in a jerky / ugly way. pane sizes should not change in response to content.
```

</details>

<!-- relay:entry 20260920T122313Z-86 author=owner kind=comment mode=plan -->
Plan this card.

<!-- relay:entry 20260920T122313Z-b4 author=owner kind=event pane=switchboard -->
- ✦ owner moved this card · Inbox → Planning · a Plan turn started

<!-- relay:entry 20260920T122811Z-8g author=agent kind=event model=kimi-k3 pane=switchboard turn=fa346a205bb4448ab789bc20e4e2faa4/470d01c639974da1a0a9e459bed1da8a -->
- ✦ agent updated this card · replaced `## Plan`

<!-- relay:entry 20260920T122822Z-qb author=agent kind=comment mode=plan model=kimi-k3 turn=fa346a205bb4448ab789bc20e4e2faa4/470d01c639974da1a0a9e459bed1da8a -->
The layout docs already name the mechanism. Let me look at the chrome chips' size hints, which are the content-driven suspects.





I have enough to write the plan. The mechanism is documented in `PaneLayout.h` and the content-driven minimum-size suspects are in `PaneChrome.h`.

**Plan written on #SDXE.** The mechanism is already documented in `PaneLayout.h`: `setChildrenCollapsible(false)` splitters must satisfy every child's minimum size, so any widget whose `minimumSizeHint()` follows content forces a pane wider. The prime suspects are the header chrome widgets in `src/PaneChrome.h` — the usage chip (its text changes constantly while agents work), the subagent badge, and header chips before/without a ladder allowance.

The plan measures first (Xvfb, log `minimumSizeHint` vs splitter `sizes()` while content streams), then clamps each chrome widget's minimum to its ladder floor, audits the rest of the pane layout, and adds a `panelayout` regression test. Nothing needed from you — it's ready for **Execute** whenever you want.

<!-- relay:entry 20260920T122822Z-qc author=owner kind=event pane=switchboard -->
- ✦ owner moved this card · Planning → Planned · the plan is on the card
