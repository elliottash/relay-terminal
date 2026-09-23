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

<!-- relay:entry 20260922T043927Z-7q author=owner kind=comment mode=discuss -->
This isn’t working at least on the phone

<!-- relay:entry 20260922T044020Z-2e author=agent kind=note model=kimi-k3 pane=switchboard turn=066c2d5b8e4b09a53b67ffa968351aaf/6147e4e489bf4473ad9b6281030ce288 -->
"This isn't working at least on the phone" — expected, and here's why: the phone board is a separate implementation in `app/board.js`, not the desktop's `board::Model`. The Flat/Stage-grouping work lives in `src/BoardModel.*` + `src/BoardPane.cpp` and only changes what the desktop Switchboard renders. The phone renders its own sections: `app/board.js` builds ordered sections from `board.yaml`'s `columns:` (lines 117–160) and sorts rows inside each (`sortRows`, lines 160+, used at 622/638). It never sees a grouping flag — nothing in the `board_cards` events carries one. Fix path: add a flat "recent" view to `app/board.js` itself (a toggle that renders one list sorted by `updated` desc with a stage chip per row), or surface the desktop's grouping through the remote protocol (`REMOTE-PROTOCOL.md` §17) — the first is smaller and self-contained.

<!-- relay:entry 20260922T044029Z-fk author=agent kind=comment mode=discuss model=kimi-k3 turn=066c2d5b8e4b09a53b67ffa968351aaf/6147e4e489bf4473ad9b6281030ce288 -->
I'll look at what was actually implemented and how the phone renders the board, to see where it breaks.





This is a single ask, no todo list needed. I found the cause — recording it on the card.

**Noted on #ESDF.** The phone board is a completely separate implementation: `app/board.js` builds its own sections from `board.yaml`'s `columns:` and sorts rows inside each. The Flat/Stage-column work landed in `src/BoardModel`/`BoardPane.cpp`, which only drives the desktop Switchboard — the phone never sees a grouping flag, so nothing could change there.

Two ways forward, noted on the card:

1. **Add the flat "recent" view directly to `app/board.js`** — a toggle rendering one list sorted by `updated` desc with a stage chip per row. Small and self-contained.
2. **Carry the desktop's grouping through the remote protocol** (REMOTE-PROTOCOL §17) so the phone mirrors the desktop's choice. More plumbing.

I'd recommend option 1. Want me to extend this card's scope to cover the phone, or file the phone flat view as its own card?
