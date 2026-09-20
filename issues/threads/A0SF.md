<!-- relay:entry 20260919T224942Z-e7 author=owner kind=event pane=switchboard -->
- ✦ owner created this card in Inbox · issues/features/2026-09-19-dragging-a-pane-onto-the-tab-pane-can-be-done-on.md

<!-- relay:entry 20260919T225010Z-ce author=owner kind=event pane=switchboard -->
- ✦ owner updated this card · replaced `## Issue`

<!-- relay:entry 20260919T225010Z-cf author=owner kind=rewrite pane=switchboard -->
- ✦ rewrote ## Issue

<details><summary>before</summary>

```
dragging a pane onto the tab pane can be done on an existing tab or a new tab
```

</details>

<details><summary>after</summary>

```
dragging a pane onto the tab pane can be done on an existing tab or a new tab. right now it always creates a new tab. but if you drag onto another tab header mark, it moves the pane into that other tab.
```

</details>

<!-- relay:entry 20260919T225011Z-d3 author=owner kind=comment mode=plan -->
Plan this card.

<!-- relay:entry 20260919T225435Z-5s author=agent kind=event model=glm-5.3 pane=switchboard turn=6d7f71c51a9745129b2098956091c428/ed552bfca10f4451b1bbd2cd47603e7f -->
- ✦ agent updated this card · replaced `## Plan`

<!-- relay:entry 20260919T225443Z-ht author=agent kind=comment mode=plan model=glm-5.3 turn=6d7f71c51a9745129b2098956091c428/ed552bfca10f4451b1bbd2cd47603e7f -->
The source has a readable older copy in `build-clean/`. Let me read the drag region there.



















I have the full picture. Writing the plan to the card now.

**Plan written on #A0SF.** The change is small and localized to `src/RelayWindow.h`: `dropTarget`/`dragPaneEnd` currently treat any tab-bar drop as "new tab" (`takeLeaf` + `adoptLeafAsTab`); the plan resolves `QTabBar::tabAt()` at the drop point and, when it hits an existing tab's label, docks the pane into that tab's page with the existing `insertBeside` helper instead — plus per-tab drop-highlight feedback in `dragPaneMove`.

One thing for you: the plan docks the pane as a horizontal split beside the target tab's first leaf. If you'd rather it land somewhere else in that tab, say so before pressing **Execute**.

<!-- relay:entry 20260920T024451Z-79 author=owner kind=event pane=switchboard -->
- ✦ owner updated this card · assignee: (unset) → agent

<!-- relay:entry 20260920T024451Z-j0 author=owner kind=event pane=switchboard -->
- ✦ owner moved this card · Inbox → In progress · Execute: handed to a terminal pane

<!-- relay:entry 20260920T024451Z-ta author=owner kind=progress pane=switchboard -->
Execute · handed to a new terminal pane beside the Switchboard, whose agent works on it and records its commits in `links.commits`.

<!-- relay:entry 20260920T024501Z-25 author=agent kind=progress model=glm-5.3 pane=b5c186ed turn=a311ea3e03764b3bae6dabce69a9a7d1/5d956cb763c145d2ad6f1663ec2e43b0 -->
Starting execution: reading the live `src/RelayWindow.h` drag code, then implementing the plan (tabAt-aware TabBar branch, per-tab highlight, help text), building with `scripts/relay-build` and verifying under Xvfb.

<!-- relay:entry 20260920T025916Z-zk author=agent kind=event model=glm-5.3 pane=b5c186ed turn=a311ea3e03764b3bae6dabce69a9a7d1/5d956cb763c145d2ad6f1663ec2e43b0 -->
- ✦ agent updated this card · links: {"plans": [], "commits": [], "evidence": [], "related": [], "github": null} → {"commits": ["81ff60f8", "15aafa59"], "evidence": ["docs/qa_evidence/2026-09-20-…; appended to `## QA checklist`

<!-- relay:entry 20260920T025923Z-rk author=agent kind=event model=glm-5.3 pane=b5c186ed turn=a311ea3e03764b3bae6dabce69a9a7d1/5d956cb763c145d2ad6f1663ec2e43b0 -->
- ✦ agent moved this card · In progress → Needs QA (LLM) · Implemented and on main (RelayWindow.h half rode into #VKFV's 81ff60f8 in the shared-checkout race; the Pane.h texts + evidence landed as 15aafa59). Build green; tests skipped at owner's request. · evidence docs/qa_evidence/2026-09-20-drag-pane-onto-tab-label/ · implemented_by glm/glm-5.3

<!-- relay:entry 20260920T025923Z-rl author=agent kind=progress model=glm-5.3 pane=b5c186ed turn=a311ea3e03764b3bae6dabce69a9a7d1/5d956cb763c145d2ad6f1663ec2e43b0 -->
Landed. Two commits carry it, because of a shared-checkout race: the whole `src/RelayWindow.h` change (tabBarIndexAt helper, dragPaneEnd's label-joins-tab branch, dragPaneMove's per-label highlight, Options help text) was swept into **81ff60f8** — #VKFV's Switchboard commit — when that session's landing picked up my working-tree hunks. Verified byte-identical to my edit (its 51-line RelayWindow.h delta is exactly this card's diff). The `src/Pane.h` tooltip/comment texts and the evidence README then landed cleanly as **15aafa59** (`Implemented-By: anthropic/claude-code`), which also documents the mix-up in its message. `scripts/relay-build` green (45 s); the verify build inside `land.py commit` built the exact landed tree. Tests and the live Xvfb pass skipped at the owner's request (22:55); the QA checklist carries the checks.
