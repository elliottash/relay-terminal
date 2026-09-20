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

<!-- relay:entry 20260920T143931Z-48 author=owner kind=event pane=switchboard -->
- ✦ owner updated this card · assignee: (unset) → agent

<!-- relay:entry 20260920T143931Z-nh author=owner kind=event pane=switchboard -->
- ✦ owner moved this card · Planned → Executing · Execute: handed to a terminal pane

<!-- relay:entry 20260920T143932Z-wa author=owner kind=progress pane=switchboard pane_token=612194b9-6b73-4cb4-ac7a-dadd482c9c5d -->
Executing (612194b9) · handed to a new terminal pane beside the Switchboard, whose agent works on it and records its commits in `links.commits`.

<!-- relay:entry 20260920T150542Z-m1 author=agent kind=progress model=claude-opus-5 pane=612194b9 -->
Measured first, as the plan said, and the measurement moved the fix off two of the three suspects. `RELAY_LAYOUT_LOG=1` (new) makes every pane print its width, its minimum and what each widget in its header contributes to that minimum once a second; `docs/qa_evidence/2026-09-20-pane-width-jiggle/measure.py` drives three panes under Xvfb with nothing in them but content — a CPU burner and the long pane name an agent writes.

Before: pane #1 went 296 → 335 px when the burner started (its usage chip put 61 px into the pane's minimum) and → 339 px on the rename, and both times the Switchboard beside it and the third pane gave the room up. An earlier run in a wider window recorded a minimum of 714 px for a pane whose title was one sentence long.

Two causes, and the second was not on the suspect list. `PaneUsageChip` was `QSizePolicy::Fixed` — and a Fixed widget cannot shrink, so Qt takes its **sizeHint()** as its minimum and an override of `minimumSizeHint()` on it counts for nothing, which is why the plan's step 2 would not have worked as written. And a `QLabel`'s own minimum is its whole text, so the title the model rewrites as the work moves on was dragging the pane with it.

After: same scene, same content, every pane keeps one width. The chips still show what they showed — they ask for their text through `sizeHint()` and get it whenever the header has the room; what they may no longer do is make the room. `PaneHeaderChip` (the ssh and phone chips) and `PaneSubagentBadge` had the same shape and are clamped the same way; the badge is still whole or absent, now by a guard in its paintEvent rather than by holding the pane open.

The rest of the audit (plan step 3) is in the same log: a terminal pane's minimum is its header plus 16 px of margins, the terminal host contributes 10 and the composer 0, and the Switchboard pane's 525 is its list/card floor from #BXCN — all constants, none of them content.

<!-- relay:entry 20260920T150542Z-m2 author=agent kind=note model=claude-opus-5 pane=612194b9 -->
Deliberately left: a pane's minimum still moves by 8 px — one row gap — as the usage chip comes and goes, because a `QHBoxLayout` spends its spacing between every *visible* item, so a chip that takes no width still costs one gap when it appears. It cannot move a pane that is wider than its minimum (296 px against 278 px in the measured scene, and no width moved there). Closing it means taking the 8 px gaps out of the header row and giving every element its own padding — re-spacing every pane header by hand, which changes how the header looks and so is yours to decide, not a side effect of a bug fix.

<!-- relay:entry 20260920T150542Z-m3 author=agent kind=event model=claude-opus-5 pane=612194b9 -->
- ✦ agent moved this card · Executing → Needs verification · b2461d52: the usage chip, the subagent badge, the ssh/phone chips and the title label stop putting what they are showing into the pane's minimum; measured before and after under Xvfb, and `tests/panelayout_test.cpp` reproduces the incident and pins Qt's rule · evidence docs/qa_evidence/2026-09-20-pane-width-jiggle/ · implemented_by anthropic/claude-opus-5
