<!-- relay:entry 20260920T024058Z-47 author=owner kind=event pane=switchboard -->
- ✦ owner created this card in Inbox · issues/features/2026-09-19-when-you-do-the-auto-organize-panes-make-the-swi.md

<!-- relay:entry 20260920T024416Z-e8 author=owner kind=event pane=switchboard -->
- ✦ owner updated this card · replaced `## Issue`

<!-- relay:entry 20260920T024416Z-e9 author=owner kind=rewrite pane=switchboard -->
- ✦ rewrote ## Issue

<details><summary>before</summary>

```
when you do the "auto-organize panes", make the switchboard wide enough to allow the separate card split
```

</details>

<details><summary>after</summary>

```
when you do the "auto-organize panes", make the switchboard wide enough to allow the separate card split

also, when you "execute" from the switchboard, protect the switchboard size so that it will still split. shrink the other panes instead
```

</details>

<!-- relay:entry 20260920T041032Z-fs author=owner kind=event pane=switchboard -->
- ✦ owner flagged this card · priority +1

<!-- relay:entry 20260920T041813Z-tb author=owner kind=event pane=switchboard -->
- ✦ owner updated this card · replaced `## Issue`

<!-- relay:entry 20260920T041813Z-tc author=owner kind=rewrite pane=switchboard -->
- ✦ rewrote ## Issue

<details><summary>before</summary>

```
when you do the "auto-organize panes", make the switchboard wide enough to allow the separate card split

also, when you "execute" from the switchboard, protect the switchboard size so that it will still split. shrink the other panes instead
```

</details>

<details><summary>after</summary>

```
when you do the "auto-organize panes", prioritize makeing the switchboard wide enough to allow the separate card split 

also, when you "execute" from the switchboard, protect the switchboard size so that it wont shrink further than the minimum space needed for the list-card split. shrink the other panes instead.
```

</details>

<!-- relay:entry 20260920T041817Z-m2 author=owner kind=comment mode=plan -->
Plan this card.

<!-- relay:entry 20260920T041817Z-xa author=owner kind=event pane=switchboard -->
- ✦ owner moved this card · Inbox → Planning · a Plan turn started

<!-- relay:entry 20260920T042316Z-j9 author=agent kind=event model=glm-5.3 pane=switchboard turn=3b48850649e04021a3ab01b1871fe35b/9c54fdb017064a098d3f79454b1dec3f -->
- ✦ agent updated this card · replaced `## Plan`

<!-- relay:entry 20260920T042319Z-k9 author=agent kind=comment mode=plan model=glm-5.3 turn=3b48850649e04021a3ab01b1871fe35b/9c54fdb017064a098d3f79454b1dec3f -->
I have the full picture now. The card's two asks map onto three places in the code: the equal-share logic, the dock sizing, and the Switchboard's own 900 px split threshold. Writing the plan:

**Plan written to #BXCN.** The "auto-organize panes" is `pane.equalize`, and the fix is one shared floor — the Switchboard's own 900 px `kStackedWidth` — pinned in `sizesAfterEqualize` (organize) and in a floor-aware `sizesAfterDock` behind `insertBeside`, so Execute/Verify take the new terminal's width from the neighbouring panes instead of halving the board. Nothing needed from you; the one judgement call (900 px = the width at which the list/card split survives, not the ~576 layout minimum, and hand-drags stay free) is recorded in the plan's Risks.

<!-- relay:entry 20260920T042319Z-qb author=owner kind=event pane=switchboard -->
- ✦ owner moved this card · Planning → Planned · the plan is on the card

<!-- relay:entry 20260920T042537Z-tc author=owner kind=event pane=switchboard -->
- ✦ owner updated this card · assignee: (unset) → agent

<!-- relay:entry 20260920T042537Z-td author=owner kind=event pane=switchboard -->
- ✦ owner moved this card · Planned → Executing · Execute: handed to a terminal pane

<!-- relay:entry 20260920T042538Z-7y author=owner kind=progress pane=switchboard -->
Execute · handed to a new terminal pane beside the Switchboard, whose agent works on it and records its commits in `links.commits`.

<!-- relay:entry 20260920T042550Z-1y author=agent kind=progress model=glm-5.3 pane=a5ec6627 turn=bd440a59c7cd44df926ce8f5f18abe3c/1073111738a042d691d8dd1e2c35770a -->
Starting implementation: reading BoardPane/PaneLayout/RelayWindow, then the floor plumbing, tests, build, live check, evidence.

<!-- relay:entry 20260920T060129Z-7z author=agent kind=event model=kimi-k3 pane=6dfa0643 turn=bd440a59c7cd44df926ce8f5f18abe3c/56053f5d0cc14165a0339c256e60aaf1 -->
- ✦ agent updated this card · links: {"plans": [], "commits": [], "evidence": [], "related": [], "github": null} → {"plans": [], "commits": ["f4beffd3bcba026d96a6d0b951d0fc3dafe49b09", "83a94001e…; appended to `## QA checklist`

<!-- relay:entry 20260920T060136Z-jy author=agent kind=progress model=kimi-k3 pane=6dfa0643 turn=bd440a59c7cd44df926ce8f5f18abe3c/56053f5d0cc14165a0339c256e60aaf1 -->
Landed in two commits: `f4beffd3` (the floor arithmetic — `sizesAfterEqualize`, `sizesAfterDock`'s `anchorFloor`, `boardSplitFloor`, equalize/Execute/Verify wiring, unit tests) and `83a94001` (evidence: unit-test run, the pixel drive, and the saved-layout ground truth that reads the board's real width from Relay's own `windows.json`). One deviation from the plan: `BoardPane.cpp`'s switch to the shared constant is two one-line hunks left uncommitted — the file is mid-flight with three other sessions' edits (#3ZAP, #CYM9) and landing them half-done would break their work; the landed tree keeps the identical local `kStackedWidth`, so behaviour is unchanged. The live runs also measured the real floor the view needs: 900 + the ToolPane's 2 px layout margins, read off the layout at runtime so a chrome change can't silently undercut it. Evidence and the QA checklist are on the card; moving it to needs-verification.

<!-- relay:entry 20260920T060140Z-mj author=agent kind=event model=kimi-k3 pane=6dfa0643 turn=bd440a59c7cd44df926ce8f5f18abe3c/56053f5d0cc14165a0339c256e60aaf1 -->
- ✦ agent moved this card · Executing → Needs verification · Implementation landed (f4beffd3, 83a94001): equalize pins the board at its 902 floor, Execute/Verify take the new pane's width from the neighbouring panes; unit tests 45/45, live ground truth from the app's own saved layout. · evidence docs/qa_evidence/2026-09-20-board-split-floor/ · implemented_by kimi/kimi-k3
