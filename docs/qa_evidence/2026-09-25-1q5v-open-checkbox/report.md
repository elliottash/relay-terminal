# #1Q5V — an always-visible Open checkbox on the Sessions buttons row

2026-09-25. The owner:

> there also needs to be an open checkbox always visible that can be unchekced

## Change

- `src/Conversations.cpp` / `src/Conversations.h` — a **Open** checkbox (`sessionsOpen`) sits
  on the buttons row beside **Subagent threads**, always visible, unchecked at first and
  uncheckable. Ticked it narrows the list to the conversations a pane holds open right now —
  the "open" badge as a filter: sessions, signal threads and subagent threads all filter by
  their own open key (guest conversations by `source:id`). Open-ness is live window state,
  so the filter is applied in `rebuildTree` client-side rather than sent to the worker;
  `setOpenSessions` already rebuilds on change, so the list stays live while it is on.
- Off unless asked for, like Subagent threads (owner, 2026-09-18); no chip in the chip row
  and `clearFilters` leaves it alone, for the same reason as Subagent threads.

## Evidence

- `sessions-first.png` — the Sessions top from the conversations suite: field on row 1,
  Project/Model/Sort ▾/More ▾/**Open**/Subagent threads on row 2.
- `ConversationsTest::openCheckboxNarrowsToOpenConversations` — ticked with `a` open, the
  list holds only `a`'s row; unticked, both rows are back.

## Commands

    scripts/relay-build --target relay-conversations-tests
    ctest --test-dir build -R conversations                        # 1/1 passed
    QT_QPA_PLATFORM=offscreen ./build/relay-conversations-tests openCheckboxNarrowsToOpenConversations   # PASS
