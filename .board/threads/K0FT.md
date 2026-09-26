<!-- relay:entry 20260925T125150Z-hh author=owner kind=event pane=switchboard -->
- ✦ owner created this card in Inbox · .board/features/2026-09-25-phone-app-needs-to-have-the-same-filter-options.md

<!-- relay:entry 20260925T125153Z-fa author=owner kind=comment mode=plan -->
Plan this card.

<!-- relay:entry 20260925T125153Z-r5 author=owner kind=event pane=switchboard -->
- ✦ owner moved this card · Inbox → Planning · a Plan turn started

<!-- relay:entry 20260925T125539Z-mh author=agent kind=event model=glm-5.3 pane=switchboard turn=2d09290b5365725bcf70c664dde37b8c/2474dcf4c948421eb8378343f7b7d80d -->
- ✦ agent updated this card · replaced `## Done means`

<!-- relay:entry 20260925T125602Z-wb author=agent kind=event model=glm-5.3 pane=switchboard turn=2d09290b5365725bcf70c664dde37b8c/2474dcf4c948421eb8378343f7b7d80d -->
- ✦ agent updated this card · replaced `## Plan`

<!-- relay:entry 20260925T125621Z-8m author=agent kind=event model=glm-5.3 pane=switchboard turn=2d09290b5365725bcf70c664dde37b8c/2474dcf4c948421eb8378343f7b7d80d -->
- ✦ agent updated this card · replaced `## Plan`

<!-- relay:entry 20260925T125630Z-9v author=agent kind=comment mode=plan model=glm-5.3 turn=2d09290b5365725bcf70c664dde37b8c/2474dcf4c948421eb8378343f7b7d80d -->
I'll read the card and find the relevant code first.

The card is about the web remote (phone browser) board view missing the desktop's filters. Let me find the remote server code and its board page.

Still one ask (write the plan for #K0FT), so no todo list needed. Now let me find the phone-side board page the card is about.

The phone app lives in `app/`. Let me look at its structure and board rendering.

The phone board is `app/board.js`. Let me read it to see how the list is drawn and what filtering exists today.

The phone board already has text search. Now let me see how `paintList` works and what the desktop's filter options are, to name the gap exactly.



The phone board has text search, tabs, and section collapse, but no per-status filters. Now let me pin down exactly what the desktop offers — the section checkboxes and label chips in `src/BoardPane.h` / `src/BoardModel.h`.

The desktop's filters are: a term-language text box (`label:`, `status:`, `waiting:`, `@`, `#`), one untickable checkbox per section, and label chips — composing with each other. Let me check the phone's header/search wiring and its tests to ground the plan.

Let me look at the phone's list header (where tabs and search sit) and the desktop's `buildListTools` to see exactly what the phone's filter row should carry.





`src/BoardPane.cpp` is over the read limit. I'll pull the filter-row construction through search instead.

Let me read the phone's matching functions and search wiring precisely, so the plan names the right functions.





One more look: the desktop's exact filter-term language in `BoardModel`, so the plan can spec parity precisely.



The phone and desktop pictures are both clear now. Let me check what `tests/test_board_view.py` and `tests/test_remote_board.py` assert, so the plan's verify steps name real tests.

I have the full picture. One last check — how the phone app's files are served (embedded or from disk), so the plan's build step is right.

I have everything. Writing the card's `## Done means` first, then the plan.





`## Done means` is on the card. Now the plan itself.

One sentence in the plan read like drafting; let me clean it up on the card.

**Planned #K0FT.** The plan ports the desktop's two filter surfaces to the phone's Board list — a section-hide control (untick Discussing / Needs verification, remembered across reloads) and the full search-filter term language (`status:`, `label:`, `waiting:`, `folder:`, `@`, `#ID`) — all client-side in `app/board.js`/`app/board.css`, with no protocol change, verified through `tests/test_board_view.py` in a real browser.

One default worth knowing before Run: hidden sections stay phone-local (like folding today) rather than syncing to the desktop's checkboxes — syncing would be a protocol change, so say so if you want that instead.

<!-- relay:entry 20260925T125630Z-9w author=owner kind=event pane=switchboard -->
- ✦ owner moved this card · Planning → Planned · the plan is on the card
