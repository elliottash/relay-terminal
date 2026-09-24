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

<!-- relay:entry 20260923T182142Z-tj author=owner kind=comment mode=discuss -->
refresh this and check whats still rleevant and see if you have other good ideas for it

<!-- relay:entry 20260923T182334Z-bd author=agent kind=event model=kimi-k3 pane=switchboard turn=131b068aeb1cec396668fb4eaddc5a09/8c87b1b23b90419a838693af5e5169bd -->
- ✦ agent updated this card · appended to `## Planning notes`

<!-- relay:entry 20260923T182346Z-cy author=agent kind=comment mode=discuss model=kimi-k3 turn=131b068aeb1cec396668fb4eaddc5a09/8c87b1b23b90419a838693af5e5169bd -->
Let me re-read the card and check the current state of the code to see what's still relevant.















I have the full picture now. Let me record the refresh on the card.

**Refreshed #5KMQ** — checked the repo; the card is still fully relevant, nothing on the GUI side is built yet. Key findings, now in `## Planning notes`:

**Already built since the card was written:**
- The **`chores` role** (Lite tier, "duplicate checks, labels, titles, note scans") — the model half of this card exists; titles already use it via `side_provider(role="chores")`.
- **`route_assist`** — a proven template for exactly this wire: fast no-tools side call, GUI debounce, timeout/watchdog, graceful null on failure.
- **The local fuzzy check** (`board_tools.duplicates()`, difflib) needs no model at all.

**New ideas:**
1. **Two-layer duplicates** — local similarity live while typing (instant, free), Lite semantic pass on Enter for paraphrases. Cheaper than the original "debounced Lite while typing" plan.
2. Accepted suggestions write real `links.related` entries at create time.
3. The same pass should serve cards filed from chat/console, riding the existing `possible_duplicates` flow.

The card stays **Ready** — Execute builds it whenever you want.

<!-- relay:entry 20260923T182434Z-c7 author=owner kind=event pane=switchboard -->
- ✦ owner claimed this card · assignee agent, Ready to start → Executing, session fc9ad858

<!-- relay:entry 20260923T182434Z-wq author=owner kind=progress pane=switchboard pane_token=fc9ad858-3122-4955-9ffa-23e0ea9b1dd6 -->
Claimed (fc9ad858) · working on it from a terminal pane

<!-- relay:entry 20260923T182645Z-ba author=agent kind=event model=gpt-6-sol pane=fc9ad858 turn=8a1d476b37464bb29847e70dbac7cc03/02e0c81ffc9a4c6380c261159844c291 -->
- ✦ agent updated this card · replaced `## Done means`

<!-- relay:entry 20260923T182724Z-jn author=agent kind=event model=gpt-6-sol pane=fc9ad858 turn=8a1d476b37464bb29847e70dbac7cc03/02e0c81ffc9a4c6380c261159844c291 -->
- ✦ agent updated this card · replaced `## Plan`

<!-- relay:entry 20260923T182731Z-2a author=agent kind=progress model=gpt-6-sol pane=fc9ad858 turn=8a1d476b37464bb29847e70dbac7cc03/02e0c81ffc9a4c6380c261159844c291 -->
Defined Done means and a plan after tracing quick add and board_create. Enter currently writes immediately; I am changing it to hold a draft for review, then will add fuzzy/live and Lite/on-Enter suggestions with explicit related-link acceptance.

<!-- relay:entry 20260923T184058Z-51 author=agent kind=progress model=gpt-6-sol pane=fc9ad858 turn=8a1d476b37464bb29847e70dbac7cc03/02e0c81ffc9a4c6380c261159844c291 -->
Pre-save draft is wired. Debounced local matches appear while typing; Enter opens issue/tab/labels review and requests a chores-role semantic pass. Related checkboxes are the only links carried into board_create. Focused Python and Qt tests pass, and an Xvfb capture shows the review before save.

<!-- relay:entry 20260923T184305Z-k9 author=agent kind=event model=gpt-6-sol pane=fc9ad858 turn=8a1d476b37464bb29847e70dbac7cc03/02e0c81ffc9a4c6380c261159844c291 -->
- ✦ agent updated this card · replaced `## Tests`

<!-- relay:entry 20260923T184316Z-dt author=agent kind=evidence -->
Check · 2 missing-evidence, 1 not-applicable, 3 passed; 3 advisory finding(s). The status is under `## Tests`; earlier checks are in this thread.

<!-- relay:entry 20260923T184409Z-10 author=agent kind=evidence -->
Check · 1 not-applicable, 5 passed; 1 advisory finding(s). The status is under `## Tests`; earlier checks are in this thread.

<!-- relay:entry 20260923T184508Z-3z author=agent kind=evidence -->
Check · 1 not-applicable, 5 passed; 1 advisory finding(s). The status is under `## Tests`; earlier checks are in this thread.

<!-- relay:entry 20260923T184759Z-2e author=agent kind=event model=gpt-6-sol pane=fc9ad858 turn=8a1d476b37464bb29847e70dbac7cc03/02e0c81ffc9a4c6380c261159844c291 -->
- ✦ agent updated this card · links: {"plans": [], "commits": [], "evidence": [], "related": [], "github": null} → {"plans": [], "commits": ["27f849b"], "evidence": ["docs/qa_evidence/2026-09-23-…

<!-- relay:entry 20260923T184815Z-xb author=agent kind=event model=gpt-6-sol pane=fc9ad858 turn=8a1d476b37464bb29847e70dbac7cc03/02e0c81ffc9a4c6380c261159844c291 -->
- ✦ agent updated this card · replaced `## Execution Summary`

<!-- relay:entry 20260923T184835Z-hy author=agent kind=evidence -->
Check · 1 not-applicable, 5 passed; 1 advisory finding(s). The status is under `## Tests`; earlier checks are in this thread.

<!-- relay:entry 20260923T185006Z-mf author=agent kind=event model=gpt-6-sol pane=fc9ad858 turn=8a1d476b37464bb29847e70dbac7cc03/02e0c81ffc9a4c6380c261159844c291 -->
- ✦ agent updated this card · links: {"plans": [], "commits": ["27f849b"], "evidence": ["docs/qa_evidence/2026-09-23-… → {"plans": [], "commits": ["27f849b", "5ac8a99"], "evidence": ["docs/qa_evidence/…

<!-- relay:entry 20260923T185036Z-2x author=agent kind=evidence -->
Check · 1 not-applicable, 5 passed; 1 advisory finding(s). The status is under `## Tests`; earlier checks are in this thread.

<!-- relay:entry 20260923T190224Z-hf author=agent kind=evidence -->
Check · 1 not-applicable, 5 passed; 1 advisory finding(s). The status is under `## Tests`; earlier checks are in this thread.

<!-- relay:entry 20260923T190308Z-gb author=agent kind=evidence -->
Check · 1 not-applicable, 5 passed; 1 advisory finding(s). The status is under `## Tests`; earlier checks are in this thread.

<!-- relay:entry 20260923T190335Z-qk author=agent kind=event model=gpt-6-sol pane=fc9ad858 turn=8a1d476b37464bb29847e70dbac7cc03/a3cf853d2cf848de97fc4279471189cb -->
- ✦ agent updated this card · links: {"plans": [], "commits": ["27f849b", "5ac8a99"], "evidence": ["docs/qa_evidence/… → {"plans": [], "commits": ["27f849b", "5ac8a99", "36ca8f6"], "evidence": ["docs/q…

<!-- relay:entry 20260923T190359Z-1p author=agent kind=evidence -->
Check · 1 not-applicable, 5 passed; 1 advisory finding(s). The status is under `## Tests`; earlier checks are in this thread.

<!-- relay:entry 20260923T190453Z-6t author=agent kind=evidence model=gpt-6-sol pane=fc9ad858 turn=8a1d476b37464bb29847e70dbac7cc03/a3cf853d2cf848de97fc4279471189cb -->
Implementation is on main in 27f849b, 5ac8a99 and 36ca8f6. At 36ca8f6, the five listed automated checks passed (run 20260923T190355Z-5876); the full Board protocol module passed 180/180 locally. The GUI review capture is docs/qa_evidence/2026-09-23-5kmq/01-draft.png. The three signals opened while running the module are resolved; #PJ2H and #0VEG are done. The first commit's message accidentally omitted #5KMQ, although its hash is linked here; the later commits name it.

<!-- relay:entry 20260923T190457Z-r1 author=agent kind=event model=gpt-6-sol pane=fc9ad858 turn=8a1d476b37464bb29847e70dbac7cc03/a3cf853d2cf848de97fc4279471189cb -->
- ✦ agent moved this card · Executing → Needs verification · Pre-save duplicate/related review and owner-selected real links are implemented; listed automated tests pass at 36ca8f6 and GUI evidence is attached. Separate verifier should check Done means and Tests. · evidence docs/qa_evidence/2026-09-23-5kmq/01-draft.png · implemented_by openai/gpt-6-sol via codex
