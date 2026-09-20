<!-- relay:entry 20260920T004745Z-1g author=owner kind=event pane=switchboard -->
- ✦ owner created this card in Inbox · issues/features/2026-09-19-switchboard-improvements.md

<!-- relay:entry 20260920T005243Z-2k author=owner kind=event pane=switchboard -->
- ✦ owner updated this card · replaced `## Issue`

<!-- relay:entry 20260920T005243Z-vn author=owner kind=rewrite pane=switchboard -->
- ✦ rewrote ## Issue

<details><summary>before</summary>

```
switchboard improvements
```

</details>

<details><summary>after</summary>

```
remove the current first colum, those icons arent useful because they just reflect sections. instead add a flag icon, and its an empty circle i can click on to priotize. left click increases priority, right click reduces. priority zero (default) is clear. priority -1 is yellow. +1 is white, +2 is pale green, +3 is bright green. and you can sort on that. 

but the # code as a second column before the title. 

clean up should annotate bug, feature, gui, etc and should also suggest tags.  those should become a second set of filter  next to the section list. 

there seem to be 3 date columns, remove the leftmost of the 3.
```

</details>

<!-- relay:entry 20260920T005244Z-1k author=owner kind=comment mode=plan -->
Plan this card.

<!-- relay:entry 20260920T005902Z-60 author=agent kind=event model=glm-5.3 pane=switchboard turn=57859ff4b6434ad2b7d670667082ecba/bb030350d33a4f00acd3f45600bf0125 -->
- ✦ agent updated this card · replaced `## Plan`

<!-- relay:entry 20260920T005909Z-t4 author=agent kind=question model=glm-5.3 pane=switchboard turn=57859ff4b6434ad2b7d670667082ecba/bb030350d33a4f00acd3f45600bf0125 -->
1. "Remove the leftmost of the 3 date columns" — a card row has two real date columns (Created, Updated) plus the age badge ("5 w") directly left of Created, which reads as a third date column. Remove the age badge? **Recommendation: yes — drop the age badge and keep Created and Updated.** (If you meant the Created column instead, say so and the plan drops that one.)

<!-- relay:entry 20260920T005918Z-ky author=agent kind=comment mode=plan model=glm-5.3 turn=57859ff4b6434ad2b7d670667082ecba/bb030350d33a4f00acd3f45600bf0125 -->
The file is too large to read whole; I'll reconstruct the key areas with targeted searches.



The file is over the read limit, so I'll page through the key regions with match-everything searches.















I have the full picture across the GUI (`src/BoardPane.cpp`, `src/BoardModel.*`), the worker (`backend/relay_core/board*.py`) and the cleanup brief. Writing the plan and asking the one genuinely ambiguous thing — the "3 date columns", since rows only have two real date columns plus an age badge beside them.



**Plan written to #VKFV.** It covers all four asks: the status glyph becomes a clickable priority flag (−1…+3 with your colour scheme, sortable via a ⚑ header cell), the `#ID` moves to a fixed second column, labels become a filter chip row beside the section checkboxes with **Clean up** annotating them via its brief, and the leftmost date-like column goes.

**Need:** one answer to question #1 on the card — rows have only two real date columns (Created, Updated) plus an age badge ("5 w") beside them that reads as a third; I planned to drop the age badge, but confirm if you meant Created instead.

<!-- relay:entry 20260920T010545Z-ea author=owner kind=event pane=switchboard -->
- ✦ owner updated this card · assignee: (unset) → agent

<!-- relay:entry 20260920T010545Z-pa author=owner kind=event pane=switchboard -->
- ✦ owner moved this card · Inbox → In progress · Execute: handed to a terminal pane

<!-- relay:entry 20260920T010545Z-pb author=owner kind=progress pane=switchboard -->
Execute · handed to a new terminal pane beside the Switchboard, whose agent works on it and records its commits in `links.commits`.

<!-- relay:entry 20260920T025713Z-ke author=agent kind=event model=glm-5.3 pane=36c88395 turn=2d12531022504deb97bcc8a69140d7c2/7359ac9160a04bb0880b8301d640a34d -->
- ✦ agent updated this card · links: {"plans": [], "commits": [], "evidence": [], "related": [], "github": null} → {"plans": [], "commits": ["81ff60f8", "c0208584", "eec29cdd", "97aa5639", "24612…

<!-- relay:entry 20260920T025724Z-1c author=agent kind=event model=glm-5.3 pane=36c88395 turn=2d12531022504deb97bcc8a69140d7c2/7359ac9160a04bb0880b8301d640a34d -->
- ✦ agent updated this card · appended to `## QA checklist`

<!-- relay:entry 20260920T025730Z-4n author=agent kind=progress model=glm-5.3 pane=36c88395 turn=2d12531022504deb97bcc8a69140d7c2/7359ac9160a04bb0880b8301d640a34d -->
Implemented and landed. `81ff60f8` is the main change (worker + GUI + docs + tests), `eec29cdd` re-lands the worker's `board_tools.py` hunks after `c0208584` repaired a landing that caught three hunks of another session's in-flight work on the same file — main now carries exactly the #VKFV hunks there, and that session's work is untouched in the shared tree. `97aa5639` + `246125b8` are the evidence.

Landing notes: the "third date column" was the age badge, removed per the plan's recommendation (question #1 stayed unanswered, so the plan's reading stood). The first live pass found a real crash — the flag click's reference into `m_rows` dangled across the rebuild it triggers — fixed by taking the id by value, like the section handlers do. Clean up's label annotation is the brief v2 text; a dry run needs a keyed model, which the isolated Xvfb sandbox has not, so that one item is the QA checklist's to walk.

<!-- relay:entry 20260920T025738Z-nt author=agent kind=event model=glm-5.3 pane=36c88395 turn=2d12531022504deb97bcc8a69140d7c2/7359ac9160a04bb0880b8301d640a34d -->
- ✦ agent moved this card · In progress → Needs QA (LLM) · Implemented: flag column with clamped left/right clicks and the ⚑ sort, #ID as a fixed second column, age badge gone, label chip filters with the cleanup brief annotating labels, plus docs and tests. Evidence and QA checklist on the card. · evidence docs/qa_evidence/2026-09-20-switchboard-priority-flags/ · implemented_by glm/glm-5.3
