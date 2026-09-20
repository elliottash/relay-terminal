<!-- relay:entry 20260919T234020Z-5x author=owner kind=event pane=switchboard -->
- ✦ owner created this card in Inbox · issues/features/2026-09-19-lite-triage-of-new-issues.md

<!-- relay:entry 20260919T234034Z-2p author=owner kind=event pane=switchboard -->
- ✦ owner moved this card · unchanged · changed in the Switchboard

<!-- relay:entry 20260919T234044Z-kt author=owner kind=event pane=switchboard -->
- ✦ owner updated this card · replaced `## Issue`

<!-- relay:entry 20260919T234044Z-ku author=owner kind=rewrite pane=switchboard -->
- ✦ rewrote ## Issue

<details><summary>before</summary>

```
lite triage of new issues
```

</details>

<details><summary>after</summary>

```
when you enter a new issue, gemini flash lite assigns it to features / bugs / etc.
```

</details>

<!-- relay:entry 20260920T035918Z-sv author=owner kind=event pane=switchboard -->
- ✦ owner flagged this card · priority +1

<!-- relay:entry 20260920T041845Z-m3 author=owner kind=comment mode=discuss -->
think about this -- is there something else that a lite model could help with chores when entering new issues

<!-- relay:entry 20260920T041845Z-rv author=owner kind=event pane=switchboard -->
- ✦ owner moved this card · Inbox → Discussing · the discussion started

<!-- relay:entry 20260920T041949Z-ha author=agent kind=note model=glm-5.3 pane=switchboard turn=92f3e121e2af49f4b12150ef6c4c187f/2e5f61b7245b429380a643bae4a506de -->
Brainstorm — other Lite chores at issue entry, all doable in **one** structured call (temperature 0, fixed enums, tiny output) so entry stays a single round-trip:

1. **Tab** — features/bugs/design/… (the card's core ask).
2. **Labels** — the `bug`/`feature` pick plus area labels, as a fixed enum drawn from labels already on the board.
3. **Title + slug** — draft the one-line title from the raw intake text and the filename slug; Lite already writes session titles, same chore shape.
4. **Duplicates & related** — new text vs the board's open card titles → likely-duplicate ids (feeding the existing fuzzy check) and a `related:` prefill.
5. **Split detection** — "this prompt holds three asks" → propose one card per request.
6. **Priority hint** — urgency markers (crash, regression, data loss) suggest a flag; owner confirms.

Guardrail: everything pre-fills the new card as suggestions the owner accepts or overrides at save — never silently applied. Same pass could later run over `*_intake.txt` lines when they're filed.

<!-- relay:entry 20260920T041959Z-fb author=agent kind=comment mode=discuss model=glm-5.3 turn=92f3e121e2af49f4b12150ef6c4c187f/2e5f61b7245b429380a643bae4a506de -->
Yes — several, and they can all ride in the **same Lite call** as the tab assignment (temperature 0, enum outputs, one round-trip). Recorded on #5KMQ:

1. **Labels** — the `bug`/`feature` pick plus area labels, from a fixed enum of labels already on the board.
2. **Title + slug** — draft the card's one-line title and filename slug from the raw text; Lite already writes session titles, same shape.
3. **Duplicates & related** — new text vs the board's open card titles → likely-duplicate ids for the existing fuzzy check, plus a `related:` prefill.
4. **Split detection** — notice a prompt holds several asks and propose one card per request.
5. **Priority hint** — urgency markers (crash, regression) suggest a flag; you confirm.

All as suggestions that pre-fill the card for you to accept or override at save — never silently applied. My picks if we trim: **labels** and **duplicates** give the most for the least, since tab + labels is one classification and the duplicate check already exists to receive the answer.

<!-- relay:entry 20260920T114738Z-9f author=owner kind=comment mode=discuss -->
duplicates and related i think are the most important -- that should be done and shown right wneh you press "enter" after new issues and while you are typing the  first issue text

<!-- relay:entry 20260920T114824Z-t3 author=agent kind=event model=glm-5.3 pane=switchboard turn=750260e80ae44156b4c097dfe7c99df3/50397c5c1f6d40cba09c9b9058990e18 -->
- ✦ agent updated this card · labels: (unset) → ["feature", "switchboard"]; appended to `## Decisions`; replaced `## Issue`

<!-- relay:entry 20260920T114824Z-t4 author=agent kind=rewrite model=glm-5.3 pane=switchboard turn=750260e80ae44156b4c097dfe7c99df3/50397c5c1f6d40cba09c9b9058990e18 -->
- ✦ rewrote ## Issue

<details><summary>before</summary>

```
when you enter a new issue, gemini flash lite assigns it to features / bugs / etc.
```

</details>

<details><summary>after</summary>

```
when you enter a new issue, gemini flash lite assigns it to features / bugs / etc.

Priority one is duplicates and related: as you type the first issue text — and right when you press Enter — the composer shows likely-duplicate cards and a related-cards prefill. Tab assignment and the other lite chores can ride the same pass afterwards.
```

</details>

<!-- relay:entry 20260920T114833Z-y3 author=agent kind=decision model=glm-5.3 pane=switchboard turn=750260e80ae44156b4c097dfe7c99df3/50397c5c1f6d40cba09c9b9058990e18 -->
"duplicates and related i think are the most important -- that should be done and shown right wneh you press "enter" after new issues and while you are typing the first issue text" — so the live composer pass (debounced while typing) and the on-Enter check both surface likely duplicates and related cards before save; this feeds the existing fuzzy duplicate check rather than replacing it.

<!-- relay:entry 20260920T114838Z-m5 author=agent kind=event model=glm-5.3 pane=switchboard turn=750260e80ae44156b4c097dfe7c99df3/50397c5c1f6d40cba09c9b9058990e18 -->
- ✦ agent moved this card · Discussing → Ready to start · Scope agreed: duplicates + related first, shown live while typing and on Enter; ready for Execute.

<!-- relay:entry 20260920T114843Z-y6 author=agent kind=comment mode=discuss model=glm-5.3 turn=750260e80ae44156b4c097dfe7c99df3/50397c5c1f6d40cba09c9b9058990e18 -->
**Agreed and recorded on #5KMQ** — duplicates + related are priority one, and they surface at entry time: live while you type the first issue text (debounced Lite call over open card titles) and right on Enter, feeding the fuzzy duplicate check that already exists. Quoted your words as a decision, folded it into the `## Issue` text, and moved the card to **Ready** — the **Execute** button hands it to a terminal pane whenever you want it built.
