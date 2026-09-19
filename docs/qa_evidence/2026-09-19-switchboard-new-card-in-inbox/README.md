# New cards start in Inbox, not in the selected card's section — implementer evidence (#5G43, 2026-09-19)

## The bug

`n` and the "+ New card" button called `quickAdd()` → `quickAddIn(focusedSection())`,
and `focusedSection()` is the section holding the *selected card*. With a card
selected in In progress, every new card was created `status: in-progress`. Owner's
words: "new cards were someties getting put into in progress rather than inbox …

maybe that section was highlighted or something. but that shouldnt happen by default."

## The fix (src/BoardPane.cpp, src/BoardPane.h)

- `quickAdd()` now targets the board's **intake section**: the section whose
  `dropStatus` is `inbox`, else the first section that takes new cards (a board
  reordered to have no Inbox column). Never the selection's section.
- `focusedSection()` removed (it had no other caller).
- A section's own **+** button still adds straight into that section
  (`quickAddIn(columnId)`), and `quickAddIn(done)` still falls back — only the
  selection-driven default changed.
- Toolbar tooltip: "New card in the focused section (n)" → "New card in Inbox (n)".

## Verification

See `verify.log`. In short:

- `ctest -R '^board$'`: `BoardModelTests::quickAddNamesTheSectionItAddsTo` now
  asserts `n` with a *Ready* card selected sends `board_create` with
  `status: "inbox"` and the field names Inbox; a new assertion pins the explicit
  per-section path `quickAddIn("ready")` ("Ready to start"); the Done fallback is
  unchanged. Passed.
- `ctest -R boardsections|boardworkspace`: passed. `scripts/relay-build`: clean.
- GUI starts under Xvfb (isolated `HOME`/`XDG_CONFIG_HOME`, xcb): worker and
  integration shell spawn, window mapped.
- `./scripts/test.sh`: 3408 tests, 3 errors in `test_failover.py` that predate this
  change (another session's uncommitted `backend/relay_core` work; no file this
  change touches).
