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

<!-- relay:entry 20260925T151302Z-3t author=agent kind=event model=glm-5.3 pane=56ecb4d2 turn=389453d7a47f4baea147d690cb6bcdad/9bcecca24c0e4b45beda24bd5d73e731 -->
- ✦ agent claimed this card · assignee agent, Planned → Executing, implemented_by glm/glm-5.3, verify defaulted from skill deliver, session 56ecb4d2

<!-- relay:entry 20260925T151302Z-fc author=agent kind=progress model=glm-5.3 pane=56ecb4d2 turn=389453d7a47f4baea147d690cb6bcdad/9bcecca24c0e4b45beda24bd5d73e731 pane_token=56ecb4d2-36a8-4036-847c-52a00620b9ed -->
Claimed (56ecb4d2) · working on it from a terminal pane

Executing the plan on the card: thinking rows move from Block::Agent to Block::Call so thoughts+tools sit single-spaced and prose keeps a blank line around it.

<!-- relay:entry 20260925T151321Z-0h author=agent kind=event model=glm-5.3 pane=56ecb4d2 turn=389453d7a47f4baea147d690cb6bcdad/9bcecca24c0e4b45beda24bd5d73e731 -->
- ✦ agent updated this card · appended to `## Done means`

<!-- relay:entry 20260925T153745Z-s2 author=agent kind=event model=glm-5.3 pane=56ecb4d2 turn=389453d7a47f4baea147d690cb6bcdad/9bcecca24c0e4b45beda24bd5d73e731 -->
- ✦ agent updated this card · appended to `## Tests`

<!-- relay:entry 20260925T154151Z-4v author=agent kind=event model=glm-5.3 pane=56ecb4d2 turn=389453d7a47f4baea147d690cb6bcdad/9bcecca24c0e4b45beda24bd5d73e731 -->
- ✦ agent moved this card · Executing → Needs verification · Implemented in 16a1e6b6: thinking folds classify as Block::Call, so thoughts+tools render single-spaced and prose keeps exactly one blank line either side. New headless case in consolemode + before/after PNG evidence. · evidence docs/qa_evidence/2026-09-25-tjbc-thinking-spacing/ · implemented_by glm/glm-5.3

<!-- relay:entry 20260925T154216Z-1g author=agent kind=progress model=glm-5.3 pane=56ecb4d2 turn=389453d7a47f4baea147d690cb6bcdad/9bcecca24c0e4b45beda24bd5d73e731 -->
- ✦ agent is preparing Try it for this card

<!-- relay:entry 20260925T160605Z-5b author=agent kind=decision model=glm-5.3 pane=56ecb4d2 turn=389453d7a47f4baea147d690cb6bcdad/9bcecca24c0e4b45beda24bd5d73e731 -->
Owner, 2026-09-25 (12:05, mid-verification): "skip further testing and land it" — the live Xvfb re-run fixture was dropped; the landed commit's headless case and the before/after PNGs are the evidence.

<!-- relay:entry 20260925T160645Z-w8 author=agent kind=event model=glm-5.3 pane=56ecb4d2 turn=389453d7a47f4baea147d690cb6bcdad/9bcecca24c0e4b45beda24bd5d73e731 -->
- ✦ agent updated this card · appended to `## Try it`
