<!-- relay:entry 20260925T121432Z-e6 author=agent kind=event model=claude-fable-5-1 pane=be471986 turn=1fcbe747559b4edca3c622e83cb41fbe/238762ec372b426e92b3cfb3fb9cda76 -->
- ✦ agent created this card in Discussing · .board/features/2026-09-25-several-cards-open-at-once-card-panes-cards-link.md

<!-- relay:entry 20260925T121457Z-6y author=agent kind=event model=claude-fable-5-1 pane=be471986 turn=1fcbe747559b4edca3c622e83cb41fbe/238762ec372b426e92b3cfb3fb9cda76 -->
- ✦ agent updated this card · waiting_on: (unset) → owner; replaced `## Discussion points`

<!-- relay:entry 20260925T121501Z-ac author=agent kind=question model=claude-fable-5-1 pane=be471986 turn=1fcbe747559b4edca3c622e83cb41fbe/238762ec372b426e92b3cfb3fb9cda76 -->
1. Does the Board list keep a preview split at all, or become list-only with cards opening as panes? Recommendation: keep a single reusable preview (Enter), and Shift+Enter / pop-out button makes a standalone card pane. 2. Should a card pane ever be able to grow a shell? Recommendation: no; Run opens a terminal pane beside it and the two are linked (#E85D group roles card/runner). 3. Should a terminal pane attached to a card show the card inline (drawer under the header with body, Plan, Tasks, Done)? Recommendation: yes, read-only plus status actions, and never write the pane's turns to the thread. 4. Ship A0 (N solo Board panes per tab) first, or go straight to a ToolPane::Kind::Card? Recommendation: A0 first.

<!-- relay:entry 20260925T133430Z-8v author=agent kind=event model=claude-fable-5-1 pane=be471986 turn=1fcbe747559b4edca3c622e83cb41fbe/9a37207cbf934a7996a9c5f68658887d -->
- ✦ agent moved this card · Discussing → Dropped · Owner: "lets make this discussion part of #P2W8". The options and questions move to #P2W8's Discussion points; this card is the record of the first framing only.

<!-- relay:entry 20260925T220342Z-7d author=owner kind=comment mode=discuss -->
was this dropped because it was superseded?

<!-- relay:entry 20260925T220423Z-p5 author=agent kind=event model=glm-5.3 pane=switchboard turn=7bdd4488cc34018b5f751db46ca2a292/c405f19346144c9da44718ad7128be9d -->
- ✦ agent updated this card · duplicate_of: (unset) → P2W8

<!-- relay:entry 20260925T220430Z-0w author=agent kind=comment mode=discuss model=glm-5.3 turn=7bdd4488cc34018b5f751db46ca2a292/c405f19346144c9da44718ad7128be9d -->
**Yes — superseded, not rejected.** You dropped it yourself mid-discussion ("lets make this discussion part of #P2W8"): the options and open questions were folded into #P2W8's `## Discussion points` under *"Cards as artifact panes (folded in from #C7RW)"*, and the build work continues there as wave-1 slice **#Y2BA** (cards as artifact panes, currently blocked on its builder). This card was kept as the record of the first framing.

I've set `duplicate_of: P2W8` on #C7RW so the front matter records the supersession, not just the thread. If you'd rather reopen it as its own card, say so and I'll move it back to Discussing.

<!-- relay:entry 20260925T222043Z-1y author=owner kind=comment mode=discuss -->
can you double check, was there anything planned here that did not become part of p2w8 / y2ba

<!-- relay:entry 20260925T222145Z-rs author=agent kind=note model=k3 pane=switchboard turn=7bdd4488cc34018b5f751db46ca2a292/2c1a71abe5504f3dbe272b8c262efb2d -->
Audit of this card vs #P2W8/#Y2BA (owner asked, 2026-09-25): everything carried except one item. A/A0 card panes → #Y2BA steps 1–2, landed (0e198884, 459c2e59, 9a13cfe2, in needs-verification); Q1 (list keeps its preview split) decided there too. C (card linked to its runner via the #E85D group model) → #2FQ9 pop-out/dock-back + #E85D groups (executing). Q2/Q4 moot — answered by the one-pane model and by #Y2BA shipping A0 *then* Kind::Card. The four design-risk invariants are folded into #P2W8's "same as a regular pane" paragraph. **Not carried: the B-lite card drawer** — the read-only card view (body, Plan, Tasks, Done) under the header chip of a *terminal* pane, recommended here and asked as Q3. It appears in #P2W8 only inside the rejected option B line; no task or card covers it, and the chip still only opens the Board.

<!-- relay:entry 20260925T222154Z-4z author=agent kind=comment mode=discuss model=k3 turn=7bdd4488cc34018b5f751db46ca2a292/2c1a71abe5504f3dbe272b8c262efb2d -->
Checking #Y2BA's scope and the full #P2W8 body to compare against what #C7RW planned.

Audit done — recording it on the thread so the answer lives with the card.

**Double-checked: one thing did not carry over — the B-lite card drawer.**

Everything else from #C7RW is in #P2W8/#Y2BA, and most of it is already built:

- **A + A0 (card panes)** → #Y2BA steps 1–2, landed and in needs-verification (16/16 live pass). Q1's "list keeps its preview split" was decided there.
- **C (card linked to its runner)** → #2FQ9 pop-out/dock-back + #E85D groups, both in #P2W8's waves.
- **Q2/Q4** are moot — the one-pane model answered them, and #Y2BA shipped A0 *then* `Kind::Card`, exactly the order recommended.
- The four design-risk invariants are folded into #P2W8's "same as a regular pane" paragraph.

**The gap:** the **B-lite drawer in a terminal pane** — the chip toggling the card's body/Plan/Tasks inline, read-only plus Done, recommended here and asked as Q3 — exists in #P2W8 only inside the *rejected* option-B line. No slice or card covers it, and the header chip still only opens the Board.

I've noted the audit on #C7RW's thread. Want me to add the drawer to #P2W8 as a wave-2 slice (or a small card of its own), or drop the idea?

<!-- relay:entry 20260925T223226Z-yt author=owner kind=comment mode=discuss -->
yes add it to p2w8

<!-- relay:entry 20260925T223516Z-y3 author=agent kind=event mention=6BY7 model=glm-5.3 pane=switchboard turn=7bdd4488cc34018b5f751db46ca2a292/d8484eb02fb44016ac77fde66d586976 -->
mentioned in #6BY7 · 2026-09-25 · agent

<!-- relay:entry 20260925T223621Z-x0 author=agent kind=decision model=glm-5.3 pane=switchboard turn=7bdd4488cc34018b5f751db46ca2a292/d8484eb02fb44016ac77fde66d586976 -->
Owner: "yes add it to p2w8" — the B-lite card drawer (the one item the audit found unplanned, Q3 on this card) is now wave-2 slice #6BY7 of #P2W8: read-only card view (body, Plan, Tasks, stage) plus Done, toggled by the header chip of a shell pane; the pane's conversation never becomes the thread.

<!-- relay:entry 20260925T223646Z-s1 author=agent kind=comment mode=discuss model=glm-5.3 turn=7bdd4488cc34018b5f751db46ca2a292/d8484eb02fb44016ac77fde66d586976 -->
Adding the B-lite drawer to #P2W8 as a wave-2 slice. First, a fresh read of #P2W8 (it moved since my last read) and its file.

Creating the slice card first, then wiring it into #P2W8's waves.

Now wiring #6BY7 in: its Done-means, and #P2W8's tasks, verify criteria and wave list.

Two thread records, then a check that the #P2W8 file edit is clean.

Verifying the #P2W8 file after the edits:

The task wiring resolved cleanly — the new item got id `mj` and `7w` now blocks on it by id. One last check that the Plan wave list got line 10:

**Done — #C7RW's missing piece is now wave-2 slice #6BY7 of #P2W8.**

- **#6BY7** created (planned, features): *Card drawer in a terminal pane* — the header chip toggles the card's body, `## Plan`, `## Tasks` and stage read-only, plus Done; the pane's conversation never becomes the thread (#CTRN). Done-means written, related to #P2W8/#C7RW/#C7PF/#Y2BA.
- **#P2W8** updated in three places: new task (blocked on the hold + #Y2BA), wave-2 line 10 in the Plan (reuses #Y2BA's `CardPane`), and the verify criteria now list #6BY7; the program-verification task blocks on it.
- Also synced wave-1 task kc to **done** — #Y2BA reached needs-verification at 22:45 and the task text was stale.
- Decision recorded on #C7RW's thread, note on #P2W8's so its session sees the new slice.
