---
id: PJ8K
type: work
status: done
labels: [feature, sessions]
implemented_by: anthropic/claude-opus-5-5 via claude-code
verified_by: anthropic/claude-opus-5-5 via claude-code
rank: zzzzzzzzzzzzzzzzzzzzzzzz
created: '2026-09-25'
links: {plans: [], commits: [], evidence: [], related: [RC7Z, C8KM], github: null}
---
# Recently closed: no 25-item cap, and Windows/Tabs/Panes check boxes

## Issue
Stop dropping the oldest items from Recently closed once there are 25: every closed pane, tab and window is kept until the owner drops it or clears the list. Add a check box for each kind (Windows, Tabs, Panes) beside the filter, so the list can show just the kinds the owner wants.

> in the "recently closed", remove the max 25 cap. and have check boxes for windows, tabs, and panes.
> — elliott · [session:9d5cb98a7f3147eeb1df3f422e8cd700](relay://session/9d5cb98a7f3147eeb1df3f422e8cd700) · 2026-09-25

## Execution Summary
Landed in `8581b721`.

- Cap removed: `kMaxItems` is gone from `src/ClosedStack.h`; `closed::push()` trims only when given a positive limit (nothing passes one), `closed::load()` and `WindowManager::remember()` no longer trim. The list is emptied only by Delete on a row or Clear list. Wording that said "the last 25" (`closed.list` description, palette hint, comments) now describes an uncapped list.
- Check boxes: `closedShowWindows`, `closedShowTabs`, `closedShowPanes` sit beside the filter in `src/ClosedList.cpp`, all on by default; `ListView::setKindShown` / `kindShown` drive them. With every box off, the empty message says to tick one. The ticks are not persisted across rebuilds of the view.
- Trade-off: a closed pane's saved terminal text (`state/scrollback/<id>.txt`, each at most 5000 lines / 512 KB) is kept as long as its record is, so disk use grows until the list is cleared.

## Tests
- `ctest --test-dir build -R 'closedlist|closedstack'`: 2/2 pass.
- `closedstack_test::fileRoundTripIsPrivateAndUncapped`: 60 records survive push, save and load; an explicit limit still trims.
- `closedlist_test::kindCheckBoxesHideRows`: unticking Panes/Tabs hides those rows, `setKindShown` moves the box.
- `land.py` verify slot built `relay` from the exact landed tree.
