---
id: K4SQ
type: work
status: needs-verification
labels: [feature, switchboard]
assignee: agent
implemented_by: glm/glm-5.3
session: 7d4ada28-5968-4825-9a61-1e270948311b
rank: zzzzzzzzzzzzzzzzzzzzzzw
created: '2026-09-24'
verify: {artifact: code, primary: script, also: [ai-visual], human: none, criteria: the unit test drives openCardSolo on a wide view and the list pane stays hidden until closeDetail, sign_off: none, effort: low, stakes: rework, blast: capability}
links: {commits: [e0d642c8, 4bd0884], evidence: [docs/qa_evidence/2026-09-24-card-link-solo/, docs/qa_evidence/2026-09-24-tryit-K4SQ/], github: null, plans: [], related: []}
---
# Card links open the board on the card alone, not beside the list

## Issue
lets make clicking a card link opens the board in single pane mode, not showing the full list.

## Done means
Clicking a card link (`#ID` in chat, terminal output, a notification) opens the board pane showing that card's page with the whole pane to itself — the list is not shown beside it — at any pane width, exactly as a pane narrower than `kCardSplitWidth` already shows an open card.
Esc (or the close button) returns to the list, and the split view still appears when a card is opened by hand from the list. In-board links inside a card page keep the layout the pane already has.

Failure would look like: a link click showing the list + detail split in a wide pane, or the list failing to come back after Esc, or an in-board `#ID` link inside a solo page popping the list back in.

## Execution Summary
Implemented in `e0d642c8df0c`.

- `BoardView::openCardSolo(id)` (new, `src/BoardPane.{h,cpp}`) marks the pane solo and opens the card; `updateDetailLayout()` stacks the open page over the list when `m_soloReveal` is set, exactly as it already did for a pane narrower than `kCardSplitWidth` — so the card has the whole pane at any width.
- Only `closeDetail()` and `closeSignal()` clear it (back to the list). Opening another card from the one on screen (`openCard`, the in-board `#ID` path) keeps it, so the list does not pop back in beside the next card.
- `RelayWindow.h`: `openBoardCard`, `waitForBoardCard`'s retry and `revealBoardCard` (notification clicks) call `openCardSolo` instead of `selectCard` + `openSelected`.
- `BoardView::listPaneVisible()` added for tests.
- Test `aCardLinkOpensTheBoardOnTheCardAlone` in `tests/boardpane_test.cpp`; evidence shots `board-card-split.png` / `board-card-solo.png` in `docs/qa_evidence/2026-09-24-card-link-solo/`.

## Tests
- `QT_QPA_PLATFORM=offscreen ./build/relay-boardpane-tests aCardLinkOpensTheBoardOnTheCardAlone` — **passed** (new). A 995-wide view (above `kCardSplitWidth` = 900): an ordinary `openCard` keeps the list beside the card; `openCardSolo` hides it (`!listPaneVisible()`); an in-board `openCard` keeps it hidden; `closeDetail` brings the list back and the next ordinary open splits again.
- `QT_QPA_PLATFORM=offscreen ./build/relay-boardpane-tests` — **passed**, 16/16, including `navigationSurvivesReload`, `aCardOpenedInOnePaneDoesNotOpenInTheOther`, `boardDataStillReachesBothPanes`.
- `scripts/relay-build` (target `relay`) — **passed**; `land.py`'s verify slot rebuilt the exact landed tree of `e0d642c8df0c` before the swap.
- Evidence: `docs/qa_evidence/2026-09-24-card-link-solo/board-card-split.png` and `board-card-solo.png`, written by the test with `RELAY_SHOT_DIR` set.

## Try it
**Open**: `docs/qa_evidence/2026-09-24-tryit-K4SQ/stage.sh` — Relay is running on display `:187` showing card `AA01` as a card link opened it (I made that click through the app's `open` seam — the same call a chat `#ID` makes; a real chat link needs a model turn, which staging has none of).

**One task (~2 min, your mouse)**: 1) click the `#AA02` reference inside AA01's body, 2) press `Esc` once, 3) open AA02 by hand from the list.

**One question**: Steps 1 and 3 should look different on purpose — the link's card and the in-board link keep the card alone, while a by-hand open shows the list beside it. Is that what you meant by "single pane mode, not showing the full list", and would you want anything about it changed?

**Answer**: (your answer)
