<!-- relay:entry 20260921T233840Z-7n author=owner kind=event pane=switchboard -->
- ✦ owner created this card in Inbox · issues/features/2026-09-21-switchboard-needs-an-easy-way-to-show-me-recent.md

<!-- relay:entry 20260921T234002Z-bg author=owner kind=event pane=switchboard -->
- ✦ owner updated this card · replaced `## Issue`

<!-- relay:entry 20260921T234002Z-bn author=owner kind=rewrite pane=switchboard -->
- ✦ rewrote ## Issue

<details><summary>before</summary>

```
switchboard needs an easy way to show me recent cards without the section ordering.
```

</details>

<details><summary>after</summary>

```
switchboard needs an easy way to show me recent cards without the section ordering.

and i want to discuss -- whether the ordered sections are the best approach on the switchboard now.  i think it would be better if that was one of the sort options. instead the stage should be a column
```

</details>

<!-- relay:entry 20260921T234004Z-5n author=owner kind=comment mode=plan -->
Plan this card.

<!-- relay:entry 20260921T234004Z-dn author=owner kind=event pane=switchboard -->
- ✦ owner moved this card · Inbox → Planning · a Plan turn started

<!-- relay:entry 20260921T234337Z-jc author=agent kind=event model=kimi-k3 pane=switchboard turn=963bd5d34574e865768245561b752432/919d7370f0a3474a93f1d8617e199ea2 -->
- ✦ agent updated this card · replaced `## Plan`

<!-- relay:entry 20260921T234348Z-6c author=agent kind=comment mode=plan model=kimi-k3 turn=963bd5d34574e865768245561b752432/919d7370f0a3474a93f1d8617e199ea2 -->
I'll read the card and then look at the Switchboard code.



This is a single planning ask — no todo list needed. Now let me look at how the Switchboard sorts and groups cards today.











One more look at how the pane draws the column header and persists the sort, so the plan can name exact functions.







I have the full picture now — writing the plan into the card.

**Plan written on #ESDF.** It adds a `Grouping` mode to `board::Model` (Sections ⇄ Flat) toggled by a new **Stage** column header: Flat is one unsectioned list sorted by Recently updated — your "recent cards" view — with the stage shown as a per-row cell, while the existing sort cycles, checkboxes and filter keep working in both modes and the choice persists per pane.

One decision is left for you in the plan's Risks: whether Flat + Recently updated becomes the *default* for new panes (I recommended yes, matching your Issue), and whether losing drag-onto-section and the Verified/Done special headers in Flat is acceptable.

<!-- relay:entry 20260921T234348Z-jd author=owner kind=event pane=switchboard -->
- ✦ owner moved this card · Planning → Planned · the plan is on the card

<!-- relay:entry 20260921T235202Z-c0 author=owner kind=event pane=switchboard -->
- ✦ owner claimed this card · assignee agent, Planned → Executing, session d863fae3

<!-- relay:entry 20260921T235202Z-et author=owner kind=progress pane=switchboard pane_token=d863fae3-5226-479c-9caa-0d1a39081d0c -->
Claimed (d863fae3) · working on it from a terminal pane

yes and yes
