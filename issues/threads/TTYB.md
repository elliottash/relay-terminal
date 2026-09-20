<!-- relay:entry 20260920T035206Z-40 author=owner kind=event pane=switchboard -->
- ✦ owner created this card in Inbox · issues/features/2026-09-19-alloe-separate-switchboards-across-tabs.md

<!-- relay:entry 20260920T035239Z-a6 author=owner kind=event pane=switchboard -->
- ✦ owner updated this card · replaced `## Issue`

<!-- relay:entry 20260920T035239Z-b8 author=owner kind=rewrite pane=switchboard -->
- ✦ rewrote ## Issue

<details><summary>before</summary>

```
alloe separate switchboards across tabs
```

</details>

<details><summary>after</summary>

```
right now, if you have a switchboard in two tabs, moving around in the switchboard in one tab gets replciated in the other tab. they should be independent.
```

</details>

<!-- relay:entry 20260920T035242Z-jm author=owner kind=comment mode=plan -->
Plan this card.

<!-- relay:entry 20260920T035242Z-qk author=owner kind=event pane=switchboard -->
- ✦ owner moved this card · Inbox → Planning · a Plan turn started

<!-- relay:entry 20260920T035907Z-rt author=agent kind=event model=glm-5.3 pane=switchboard turn=ae00f2b0202242c6aebe454254321b10/dd271122efab483c98a8fa18627c3428 -->
- ✦ agent updated this card · replaced `## Plan`

<!-- relay:entry 20260920T035911Z-ny author=agent kind=comment mode=plan model=glm-5.3 turn=ae00f2b0202242c6aebe454254321b10/dd271122efab483c98a8fa18627c3428 -->
I have the full picture now. Writing the plan to the card.

**Plan written to #TTYB.** The replication is one missing check: a tab's Switchboard pane handles `board_card` answers meant for another tab's pane (the per-window worker fans every event out to every board pane), so opening a card anywhere opens it everywhere — the fix is to gate that branch on the pane's own request id, plus a two-view regression test, leaving broadcast data sync (`board_changed`) untouched. Nothing needed from you; the plan has no open decisions.

<!-- relay:entry 20260920T035911Z-rq author=owner kind=event pane=switchboard -->
- ✦ owner moved this card · Planning → Planned · the plan is on the card

<!-- relay:entry 20260920T042612Z-w3 author=owner kind=event pane=switchboard -->
- ✦ owner updated this card · assignee: (unset) → agent

<!-- relay:entry 20260920T042612Z-w4 author=owner kind=event pane=switchboard -->
- ✦ owner moved this card · Planned → Executing · Execute: handed to a terminal pane

<!-- relay:entry 20260920T042612Z-w5 author=owner kind=progress pane=switchboard -->
Execute · handed to a new terminal pane beside the Switchboard, whose agent works on it and records its commits in `links.commits`.

is this plan ready

<!-- relay:entry 20260920T054951Z-a9 author=agent kind=event model=kimi-k3 pane=9aa2e0f3 turn=909ad2d917cd4151bf6ff3dfea73e8af/427dbc564d9f4ea1857e5eb5ea1293ca -->
- ✦ agent updated this card · appended to `## QA checklist`

<!-- relay:entry 20260920T055006Z-v4 author=agent kind=progress model=kimi-k3 pane=9aa2e0f3 turn=909ad2d917cd4151bf6ff3dfea73e8af/427dbc564d9f4ea1857e5eb5ea1293ca -->
Implemented and verified; landing now. The gate is one condition on the `board_card` branch (`&& mine`); `tests/boardmodel_test.cpp`'s 28 hand-fed card answers now carry the pane's own request id via a new `openCard()` handshake helper, and new `tests/boardpane_test.cpp` holds the two-pane contract (A's answer opens A only; `board_changed` still reaches both models). Because the shared checkout mixes other sessions' WIP (an uncommitted delete-confirm feature whose WIP test hangs the shared `board` suite), the change was verified on **tip + this change only**: `board` and `boardpane` both green, plus a live two-tab Xvfb drive (shots + OCR in the evidence dir) showing tab B staying on its list while tab A has a card open. ARCHITECTURE.md needs no line: it does not describe the event routing. Evidence: docs/qa_evidence/2026-09-20-separate-switchboards-across-tabs/

<!-- relay:entry 20260920T055006Z-vr author=agent kind=event model=kimi-k3 pane=9aa2e0f3 turn=909ad2d917cd4151bf6ff3dfea73e8af/427dbc564d9f4ea1857e5eb5ea1293ca -->
- ✦ agent moved this card · Executing → Needs verification · Implemented and verified on tip+change (unit: board, boardpane green; live two-tab Xvfb drive in the evidence dir); QA checklist on the card. · evidence docs/qa_evidence/2026-09-20-separate-switchboards-across-tabs/ · implemented_by kimi/kimi-k3

<!-- relay:entry 20260920T055431Z-wj author=agent kind=event model=kimi-k3 pane=9aa2e0f3 turn=909ad2d917cd4151bf6ff3dfea73e8af/427dbc564d9f4ea1857e5eb5ea1293ca -->
- ✦ agent updated this card · links: {"plans": [], "commits": [], "evidence": ["docs/qa_evidence/2026-09-20-separate-… → {"plans": [], "commits": ["a17ef6ad"], "evidence": ["docs/qa_evidence/2026-09-20…

<!-- relay:entry 20260920T055431Z-xg author=agent kind=note model=kimi-k3 pane=9aa2e0f3 turn=909ad2d917cd4151bf6ff3dfea73e8af/427dbc564d9f4ea1857e5eb5ea1293ca -->
Landed as a17ef6ad (gate + openCard handshake + boardpane test + evidence), exact tree built by land.py and `board`/`boardpane` re-run green on the landed bytes. Heads-up for the #CYM9 delete-feature session (cym9-delete, pane eacfa50b): this gate means a `board_card` answer with no request id no longer opens a card, so the two hand-fed feeds inside `theDeleteKeyAndButtonDeleteTheCardAndTheUndoToastSurvives` will need the same `openCard(view, sent, …)` stamp when that feature lands — I left that region of tests/boardmodel_test.cpp untouched for you (my conversions there were backed out of my commit; everything else is converted on main). Same for any new hand-fed card answers: ask through the pane, then echo its id.
