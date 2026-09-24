<!-- relay:entry 20260919T234527Z-zp author=owner kind=event pane=switchboard -->
- ✦ owner created this card in Inbox · issues/features/2026-09-19-when-pane-is-executing-a-card-put-the-tag-in-the.md

<!-- relay:entry 20260919T234613Z-ja author=owner kind=event pane=switchboard -->
- ✦ owner updated this card · replaced `## Issue`

<!-- relay:entry 20260919T234613Z-jb author=owner kind=rewrite pane=switchboard -->
- ✦ rewrote ## Issue

<details><summary>before</summary>

```
when pane is executing a card, put the # tag in the pane header
```

</details>

<details><summary>after</summary>

```
when pane is executing a card, put the # tag in the pane header

its also clickable to get to the card (keep the one in the prompt box as well)
```

</details>

<!-- relay:entry 20260920T022945Z-qv author=owner kind=comment mode=plan -->
Plan this card.

<!-- relay:entry 20260920T023820Z-h0 author=agent kind=event model=glm-5.3 pane=switchboard turn=5263547c43a74507ac3715ee343afba9/44c9a12e88ef4af18c4f3ab11700f8c5 -->
- ✦ agent updated this card · replaced `## Plan`

<!-- relay:entry 20260920T023827Z-xz author=agent kind=comment mode=plan model=glm-5.3 turn=5263547c43a74507ac3715ee343afba9/44c9a12e88ef4af18c4f3ab11700f8c5 -->
I have the full picture now — writing the plan onto the card.

**Plan written to #C7PF.** It adds a small clickable `#ID` chip beside the title in a terminal pane's header, shown from the moment a card is handed to the pane until its turn ends (the prompt-box work chip stays as is), opening the card through the existing `onOpenCard` → `openBoardCard` path, with no give-way-ladder changes.

One question is in the plan's Risks: a 19.16 card turn runs *inside* the Switchboard pane rather than a terminal pane — I've scoped this to terminal panes and recommended leaving the board pane alone since its card detail already names the running card; say so on #C7PF if you want that covered too.

<!-- relay:entry 20260920T040956Z-3j author=owner kind=event pane=switchboard -->
- ✦ owner flagged this card · priority +1

<!-- relay:entry 20260920T040957Z-8p author=owner kind=event pane=switchboard -->
- ✦ owner flagged this card · priority +2

<!-- relay:entry 20260920T041142Z-xn author=owner kind=comment mode=plan -->
Plan this card.

<!-- relay:entry 20260920T041142Z-xo author=owner kind=event pane=switchboard -->
- ✦ owner moved this card · Inbox → Planning · a Plan turn started

<!-- relay:entry 20260920T041235Z-fz author=owner kind=event pane=switchboard -->
- ✦ owner updated this card · assignee: (unset) → agent

<!-- relay:entry 20260920T041235Z-g0 author=owner kind=event pane=switchboard -->
- ✦ owner moved this card · Planning → Executing · Execute: handed to a terminal pane

<!-- relay:entry 20260920T041236Z-8w author=owner kind=progress pane=switchboard -->
Execute · handed to a new terminal pane beside the Switchboard, whose agent works on it and records its commits in `links.commits`.

<!-- relay:entry 20260920T051458Z-hd author=agent kind=progress model=glm-5.3 pane=switchboard -->
Landed per the plan: `#ID` chip (`QLabel#paneCardChip`) after the `auto` badge in the pane header, from the moment a card is handed over until the running turn ends; click opens the card (`onOpenCard`); tooltip names every attached card with its title. No ladder change, work chip untouched. Built green (`scripts/relay-build`); targeted tests pass (`ctest` `panes`/`panestate`/`panestatus`/`themeswitch`). Live drive under Xvfb with an isolated config on a throwaway board: chip at handoff, tooltip, click-through, no chip on plain panes — screenshots in `docs/qa_evidence/2026-09-19-card-tag-in-pane-header/`. Not drivable headless (on the checklist): the chip hiding at `agent_finished` and the work chip naming the card once the task flushes, both of which need a configured agent.
