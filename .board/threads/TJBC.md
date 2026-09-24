<!-- relay:entry 20260920T033847Z-wj author=owner kind=event pane=switchboard -->
- ✦ owner created this card in Inbox · issues/features/2026-09-19-line-spacing-adjustment.md

<!-- relay:entry 20260920T033959Z-dq author=owner kind=event pane=switchboard -->
- ✦ owner updated this card · replaced `## Issue`

<!-- relay:entry 20260920T033959Z-dr author=owner kind=rewrite pane=switchboard -->
- ✦ rewrote ## Issue

<details><summary>before</summary>

```
line spacing adjustment
```

</details>

<details><summary>after</summary>

```
example trace summary from a terminal pane:



▸ ✦ thought for 10 s

▸ ran cat +9 · 7 lines · exit 0

▸ ✦ thought for 22 s
The app exited (my blind click likely closed the last pane). Relaunching and driving precisely with OCR-derived coordinates:

▸ ran relay +6 · 2 lines · exit 0 · 7.6 s
▸ ran xdotool +10 · 4 lines · exit 0 · 8.9 s

▸ ✦ thought for 5 s

▸ ran xdotool +4 · 26 lines · exit 0 · 1.8 s

▸ ✦ thought for 5 s

▸ ran tesseract +2 · 20 lines · exit 0

▸ ✦ thought for 6 s
Rows are exactly as designed. Picking Flash and verifying the write + reconfigure:

▸ ran tesseract +7 · 12 lines · exit 0 · 6.7 s

▸ ✦ thought for 7 s

▸ ran convert +8 · 6 lines · exit 0

▸ ✦ thought for 18 s

▸ ran ls +8 · 16 lines · exit 0


this has two things i want to shift:

need a line break before agent-to-user messages (in between tools / thoughts and messages)

no line break between tools and thoughts.
```

</details>

<!-- relay:entry 20260920T034112Z-wq author=owner kind=event pane=switchboard -->
- ✦ owner updated this card · replaced `## Issue`

<!-- relay:entry 20260920T034112Z-wr author=owner kind=rewrite pane=switchboard -->
- ✦ rewrote ## Issue

<details><summary>before</summary>

```
example trace summary from a terminal pane:



▸ ✦ thought for 10 s

▸ ran cat +9 · 7 lines · exit 0

▸ ✦ thought for 22 s
The app exited (my blind click likely closed the last pane). Relaunching and driving precisely with OCR-derived coordinates:

▸ ran relay +6 · 2 lines · exit 0 · 7.6 s
▸ ran xdotool +10 · 4 lines · exit 0 · 8.9 s

▸ ✦ thought for 5 s

▸ ran xdotool +4 · 26 lines · exit 0 · 1.8 s

▸ ✦ thought for 5 s

▸ ran tesseract +2 · 20 lines · exit 0

▸ ✦ thought for 6 s
Rows are exactly as designed. Picking Flash and verifying the write + reconfigure:

▸ ran tesseract +7 · 12 lines · exit 0 · 6.7 s

▸ ✦ thought for 7 s

▸ ran convert +8 · 6 lines · exit 0

▸ ✦ thought for 18 s

▸ ran ls +8 · 16 lines · exit 0


this has two things i want to shift:

need a line break before agent-to-user messages (in between tools / thoughts and messages)

no line break between tools and thoughts.
```

</details>

<details><summary>after</summary>

```
example trace summary from a terminal pane:



▸ ✦ thought for 10 s

▸ ran cat +9 · 7 lines · exit 0

▸ ✦ thought for 22 s
The app exited (my blind click likely closed the last pane). Relaunching and driving precisely with OCR-derived coordinates:

▸ ran relay +6 · 2 lines · exit 0 · 7.6 s
▸ ran xdotool +10 · 4 lines · exit 0 · 8.9 s

▸ ✦ thought for 5 s

▸ ran xdotool +4 · 26 lines · exit 0 · 1.8 s

▸ ✦ thought for 5 s

▸ ran tesseract +2 · 20 lines · exit 0

▸ ✦ thought for 6 s
Rows are exactly as designed. Picking Flash and verifying the write + reconfigure:

▸ ran tesseract +7 · 12 lines · exit 0 · 6.7 s

▸ ✦ thought for 7 s

▸ ran convert +8 · 6 lines · exit 0

this has two things i want to shift:

need an extra line break before agent-to-user messages (in between tools / thoughts and messages)

no extra line break between tools and thoughts (same as you see between two consecutive two calls)
```

</details>

<!-- relay:entry 20260920T034119Z-az author=owner kind=comment mode=plan -->
Plan this card.

<!-- relay:entry 20260920T034119Z-c0 author=owner kind=event pane=switchboard -->
- ✦ owner moved this card · Inbox → Planning · a Plan turn started

<!-- relay:entry 20260920T035403Z-v2 author=owner kind=comment mode=discuss -->
is this plan done

<!-- relay:entry 20260920T035427Z-4m author=agent kind=comment mode=discuss model=glm-5.3 turn=cc24b708840d4301882fb2ecdcef1c10/dfc93ec2a1ca456c8d61a9b93b4fd601 -->
**No — nothing was planned.** The Plan turn moved #TJBC into Planning but left no plan: the body still has only `## Issue`, `links.plans` is empty, and no plan file for it exists anywhere in the repo.

Press **Plan** on the card again to actually run it. The change itself is small and well-specified (extra blank line before agent-to-user messages in the trace view; none between tool and thought rows), so an Execute turn could also take it directly without a plan.

<!-- relay:entry 20260920T035730Z-mz author=owner kind=comment mode=plan -->
Plan this card.

<!-- relay:entry 20260920T035730Z-rk author=owner kind=event pane=switchboard -->
- ✦ owner moved this card · Planning → Planning · a Plan turn started

<!-- relay:entry 20260920T040112Z-y4 author=agent kind=event model=glm-5.3 pane=switchboard turn=cc24b708840d4301882fb2ecdcef1c10/e3426edeba444f0598feb2fb0c3e0979 -->
- ✦ agent updated this card · replaced `## Plan`

<!-- relay:entry 20260920T040115Z-a2 author=agent kind=comment mode=plan model=glm-5.3 turn=cc24b708840d4301882fb2ecdcef1c10/e3426edeba444f0598feb2fb0c3e0979 -->
The gaps rule lives in `src/TranscriptGaps.h` and is applied by `src/Pane.h`. Now I need to see how the thinking fold (the `✦ thought for N s` rows) is classified.

























I have the full picture: the gap rule is `src/TranscriptGaps.h`, applied by `src/Pane.h::beginBlock`, and thinking rows are classified as `Block::Agent` in exactly two places (the live fold start at `Pane.h:4373` and the replay path `printAnchoredRow` at `Pane.h:4749`). Reclassifying them to `Block::Call` yields exactly the asked-for spacing through the existing rule — the table itself doesn't change. Writing the plan:

Wrote the plan to #TJBC. It's a two-line classification change — thinking rows move from `Block::Agent` to `Block::Call` at `Pane.h:4373` (live fold) and `Pane.h:4749` (replay), plus the legacy single-✦ fallback — and the existing gap table then produces exactly the asked-for spacing with no rule change. Nothing needs deciding from you; press **Execute** to hand it to a terminal pane.

<!-- relay:entry 20260920T040115Z-a3 author=owner kind=event pane=switchboard -->
- ✦ owner moved this card · Planning → Planned · the plan is on the card
