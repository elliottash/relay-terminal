# #6BY7 Card drawer in a terminal pane — implementer evidence

**Card:** #6BY7 *Card drawer in a terminal pane: the header chip toggles the card inline
(read-only plus Done)* — wave-2 slice of #P2W8.
**Date:** 2026-09-25 · **Session:** pane `c26b448b` (land session `8531c4b7`), kimi/k3.
**Landed revision:** `f7dff8f41e91` (its verify gate reported *the exact tree builds*).

## What the change is

The claims chip in a terminal pane's header no longer jumps to the Board. It toggles a
read-only card drawer docked under the header — stage, `#id`, title, the card document
without its title heading, `## Plan` and `## Tasks` with `- [x]` items drawn as ticks — plus
a `Done` that sends the Board's own `board_move`, an `Open in Board` that keeps the chip's
old behaviour, and a close `✕`.

- `src/CardDrawer.h` (new): the drawer widget. No `Q_OBJECT`, public `std::function`
  callbacks, no file access — `setCard` takes exactly the `board_card` helper answer.
- `src/Pane.h`: `onBoardRequest`; `toggleCardDrawer()` / `requestDrawerCard()` (lazily builds
  the drawer, inserts it at layout index 1, caps it at half the pane) and
  `handleBoardHelperEvent()` (answers only request ids prefixed `drawer-card-`, refetches on
  a `board_changed` naming the open card, renders an `error` under its own request id).
- `src/PaneUi.cpp`: the chip click and the claims menu now call `toggleCardDrawer(id)`.
- `src/RelayWindowCore.cpp`: the pane's helper channel at both construction sites (terminal
  pane and agent console) — `sendToHelper` out, `listenToHelper` back, registered against the
  tab's worker at the first ask and re-registered if the pane asks on another tab.
- `src/Theme.cpp`: `#cardDrawer` and its parts. `CMakeLists.txt`: the new header in both
  source lists.

The drawer never reads a card file, and nothing in the pane's conversation writes a card
thread: the pane sends the helper only `board_card_get` and `board_move`.

## Commands

```
python3 scripts/land.py try 8531c4b7 --tests consolemode \
  --only-hunk CMakeLists.txt:1,3 --only-hunk src/CardDrawer.h:1 \
  --only-hunk src/Pane.h:1,8,22,23,24,25,38 --only-hunk src/PaneUi.cpp:1 \
  --only-hunk src/RelayWindowCore.cpp:4,6 --only-hunk src/Theme.cpp:1 \
  --only-hunk tests/consolemode_test.cpp:1,2,3
```

The hunk selection is not a shortcut: this checkout is shared, and `src/Pane.h` and
`src/RelayWindowCore.cpp` carry another live session's uncommitted work (card #83YV, a Python
console) at the time of writing. `--only-hunk` lands exactly the hunks the authorship journal
attributes to card #6BY7; the multi-pane work stays in the working tree untouched. The
selected tree was checked directly: `diff` of the slot's `src/Pane.h` against `main` shows
only the seven hunks above, and no `PaneJournal.h` include.

## Results

- The tree built and linked; `relay-consolemode-tests` ran under ctest (see
  `consolemode.txt`).
- **The four new cases pass** — `theChipTogglesTheCardDrawer`,
  `theCardDrawerRendersTheHelperAnswerAndRefreshesOnChange`,
  `theCardDrawerDoneSendsBoardMoveAndShowsRefusals`,
  `aPromptOnAWorkedCardWritesNothingToTheBoard`. None appears in the ctest `FAIL` list.
- One other, pre-existing case fails in that tree at the tip of the run whose log is attached
  (`edited != path`, `line != 2`, `context.seen != …` — the open-path case at
  `consolemode_test.cpp:726/727/739`). It is **not** this change's: the same hunks passed it
  on tip `f8359d9557be`, it breaks only after tip moved to `5858d5c1` (#9MYY *view hot paths:
  hover linkAt cache, paintRow colours once* — paths this change does not touch), and it runs
  before the four new cases. The set of unrelated failures moves with the tip: on `5858d5c1`
  two tool-call-summary `text.contains` assertions also failed, and both pass again on
  `ec9412a0`. That is itself the evidence that they follow `main`, not this commit. The suite
  already carries an open machine signal, `ctest:consolemode` (card #VZ8C).

## What the verifier can check (Done means)

| Done-means line | Where it is proved |
| --- | --- |
| Chip toggles the drawer; card body, `## Plan`, `## Tasks`, stage rendered read-only, plus Done | `theChipTogglesTheCardDrawer`, `theCardDrawerRendersTheHelperAnswerAndRefreshesOnChange` |
| Several cards → the claims menu picks which; Board still reachable from the drawer | `theChipTogglesTheCardDrawer` (one card; `cardDrawerOpen` calls `onOpenCard`); the menu path is the same `toggleCardDrawer(id)` per item |
| Fed by the tab's helper, refetched on `board_changed`; nothing reads the card file | `theCardDrawerRendersTheHelperAnswerAndRefreshesOnChange`; `CardDrawer::setCard` takes only the event |
| Nothing typed in the pane reaches the card's thread | `aPromptOnAWorkedCardWritesNothingToTheBoard` |
| A refused Done shows inline | `theCardDrawerDoneSendsBoardMoveAndShowsRefusals` |
| Failure would show as: a chip that still jumps to the Board, a stale drawer, a silent Done, a thread entry with pane provenance | first assertion of `theChipTogglesTheCardDrawer` (`opened` empty); the `board_changed` refetch; the notice-line case; the no-board-write case |

Live (GUI) check: in a pane, send a prompt naming a card (`#6BY7`), click the chip, and
confirm the drawer opens with the card's stage and sections, that a second click hides it,
that `Done` moves the card (a gated card answers inline), and that a `font=`/model line in
the card thread gains nothing from the prompt.
