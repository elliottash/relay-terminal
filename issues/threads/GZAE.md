<!-- relay:entry 20260920T215207Z-a5 author=owner kind=event pane=switchboard -->
- ✦ owner created this card in Inbox · issues/features/2026-09-20-if-i-did-ctrl-alt-m-in-the-switchboard-it-moved.md

<!-- relay:entry 20260920T215229Z-q5 author=owner kind=event pane=switchboard -->
- ✦ owner updated this card · replaced `## Issue`

<!-- relay:entry 20260920T215229Z-q6 author=owner kind=rewrite pane=switchboard -->
- ✦ rewrote ## Issue

<details><summary>before</summary>

```
if i did ctrl alt m in the switchboard, it moved me to a pane
```

</details>

<details><summary>after</summary>

```
if i did ctrl alt m in the switchboard, it moved me to a pane. it shouldnt do that. it should ideally use that for the switchboard agent. but otherwise just dont do anything.
```

</details>

<!-- relay:entry 20260920T215231Z-x1 author=owner kind=comment mode=plan -->
Plan this card.

<!-- relay:entry 20260920T215231Z-x2 author=owner kind=event pane=switchboard -->
- ✦ owner moved this card · Inbox → Planning · a Plan turn started

<!-- relay:entry 20260920T220044Z-ja author=agent kind=event model=kimi-k3 pane=switchboard turn=7c690ffcba464b779d4f51463fc20651/e3ee2922561d4ba698697356c5aa6ce8 -->
- ✦ agent updated this card · replaced `## Plan`

<!-- relay:entry 20260920T220103Z-7s author=agent kind=comment mode=plan model=kimi-k3 turn=7c690ffcba464b779d4f51463fc20651/e3ee2922561d4ba698697356c5aa6ce8 -->
**Plan written to #GZAE.** The bug is in `runActionNow` (src/RelayWindow.h ~1141): Ctrl+Alt+M only routes to the helper when the helper composer has focus; otherwise it fires `openModelPicker()` on `m_active`'s terminal pane, which drags you out of the Switchboard. The plan widens that branch so the key drives the Switchboard agent's model picker/box whenever the active leaf is the board (Alt+M included), falls back to doing nothing otherwise, and starts with an Xvfb reproduction to confirm exactly which focus case misfires. Nothing needed from you — it's ready for Execute.

<!-- relay:entry 20260920T220103Z-d6 author=owner kind=event pane=switchboard -->
- ✦ owner moved this card · Planning → Planned · the plan is on the card
