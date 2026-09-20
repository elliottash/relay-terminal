---
id: GE0Z
type: work
status: planned
rank: zzzzzzzzzzzzzzz
created: '2026-09-20'
links: {plans: [], commits: [], evidence: [], related: [], github: null}
---
# clicking hash tag in pane header doesnt link correctly

## Issue
i just clicked on this pane header hash tag, #B9V4
51a8f37535fb458594017ad88ee35331

but it took me to #2Y96

## Plan
## Goal

Clicking the `#<id>` card chip in a pane header must open the card the chip displays. Today a click on a chip showing `#B9V4` opened `#2Y96` instead (the card the board's detail view already had open, most likely).

## Findings

The click path, end to end:

1. **Chip** — `src/Pane.h:3471-3500`: `m_cardChip` is a plain-text `QLabel` in the header row. Its text is set only in `refreshCardChip()` (`Pane.h:8150`), which renders `"#" + cardChipCard()`, and `cardChipCard()` (`Pane.h:8140`) returns `m_turnCard` if set, else `m_boardTaskCard`.
2. **Click** — `Pane.h:3401-3403`: on mouse release, if `m_headerPressOn == m_cardChip` and the release is inside the chip's rect, it calls `onOpenCard(cardChipCard())` — the **live** card id, not necessarily the text the label is showing.
3. **Routing** — `Pane.h:625` `onOpenCard` → `src/RelayWindow.h:5038` → `openBoardCard(id)` (`RelayWindow.h:4425-4442`): finds a board pane, calls `selectCard(id)` then `openSelected()`; if the card is not yet in the board model it defers to `waitForBoardCard()` (`RelayWindow.h:4448-4461`).
4. **Board** — `BoardView::selectCard` (`src/BoardPane.cpp:4835-4841`) sets `m_selected = id`; `openSelected` (`BoardPane.cpp:4652-4672`) sends `board_card_get {card: m_selected}`. Worker side is exact: `backend/relay_core/board_protocol.py` dispatches `board_card_get` → `board_read` with `normalize_id` → `Board.card_by_id` (`backend/relay_core/board.py:1120`). No fuzzy matching anywhere worker-side.

So the id changes hands correctly everywhere it is passed explicitly. The mismatch can only come from state around the explicit passing. Four concrete suspects, in order of likelihood:

- **S1 · Stale chip text vs live `cardChipCard()`.** The label is repainted only in `refreshCardChip()`, called from exactly five places (`Pane.h:8138, 8187, 9256, 9979, 15355`). `m_turnCard`/`m_turnCards` change in `noteTurnCard` (`Pane.h:8171-8178`), at `agent_finished` (`Pane.h:9246-9256`), and `m_boardTaskCard` at `Pane.h:9970` and `Pane.h:15343-15346`. If any path mutates these without refreshing (or refreshes to a fallback id while the user still sees the old text), the chip displays `#B9V4` while `cardChipCard()` at click time already returns the fallback (`m_boardTaskCard`, e.g. the pane's previously Executed card `#2Y96`). Read every write to `m_turnCard`, `m_turnCards`, `m_boardTask`, `m_boardTaskCard` and check each is paired with `refreshCardChip()`.
- **S2 · `m_headerPressOn` is captured application-wide.** The pane installs a `qApp`-wide event filter (`Pane.h:504`), `eventFilter` forwards everything to `headerDragEvent` (`Pane.h:3195`), and the press arm stores `m_headerPressOn = qobject_cast<QWidget*>(object)` (`Pane.h:3363`) with no visible ownership guard. Every Pane instance sees every press in the app. Verify the release arm (`Pane.h:3380-3405`) cannot fire on the wrong pane or with a press captured from another widget (e.g. a board row for `#2Y96`), and that `m_cardChip` never even gets `installEventFilter` (it does not — compare `Pane.h:3455, 3462, 3494`) — it relies on the qApp filter alone.
- **S3 · `waitForBoardCard` abandons the navigation when a detail is open.** `RelayWindow.h:4458`: `if (!pane || !pane->board() || pane->board()->detailOpen()) return;` — if the model lacks the card and the detail view is already open on another card (`#2Y96`), the retry silently gives up, leaving the board showing `#2Y96` — exactly the reported symptom.
- **S4 · Detail pushes its card back into the selection.** `BoardPane.cpp:2780-2793`: when the detail is open and its card differs from `m_selected`, the code re-selects the *detail's* card (`selectCard(m_detail->cardId())`, line 2793). If this runs (on a `board_changed`/refresh) between `selectCard(B9V4)` and the `board_card` response, the board snaps back to the open card.

## Steps

1. **Reproduce first, with logging.** Build with `scripts/relay-build`, run under Xvfb with an isolated `XDG_CONFIG_HOME` (per `CLAUDE.md`). Set up: a terminal pane Executing card A (chip shows `#A`); the board pane with its detail view open on card B. Click the chip. Add temporary `qDebug` (or `relay.log`) lines at each hand-off: `cardChipCard()` and the label's current text at the click (`Pane.h:3401`), the id in `openBoardCard` (`RelayWindow.h:4425`), `m_selected` in `selectCard`/`openSelected`, and `card_id` of the `board_card` event (`BoardPane.cpp:3767`). The log line where `#B9V4` becomes `#2Y96` names the culprit among S1–S4.
2. **Fix the culprit:**
   - S1: pair every mutation of the chip's backing fields with `refreshCardChip()` (or make the click open the displayed text's id and assert text == cardChipCard()).
   - S2: guard the press capture in `headerDragEvent` to this pane's own header widgets (`m_titleLabel`, `m_titleEdit`, `m_cwdLabel`, `m_cardChip`, header chrome), and reset `m_headerPressOn` on release.
   - S3: in `waitForBoardCard`, don't give up because a detail is open — keep retrying until the card loads, then select and open it.
   - S4: make the detail→selection sync at `BoardPane.cpp:2780` yield to an explicit `selectCard`/`openSelected` in flight (e.g. skip the re-select while a `board_card_get` for a different card is pending).
   Fix only what the logging in step 1 proves; if more than one suspect fires, fix each, they are independent defects.
3. **Remove the temporary logging** (or keep one concise line if it matches existing logging style, `src/Logging.h`).

## Risks

- `src/Pane.h` is a 15k-line header; the event-filter press tracking may be load-bearing for header drag-to-move (`headerDragEvent` is also the drag path). Restricting S2's guard must not break dragging the pane by its header or the cwd-label click (`Pane.h:3389`).
- S4's re-select exists so the list follows the card being viewed; changing it can desync list highlight from the open card. Keep the sync, only defer it during an explicit open.
- No owner decision needed.

## Verify

- Targeted tests only (no full suite): board-side behaviour in `src/tests/boardmodel_test.cpp` — add a case that `selectCard`+`openSelected` while a detail is open on another card opens the requested card (mirrors S3/S4), and run `ctest --test-dir build -R boardmodel`. If the fix lands in `Pane.h`'s filter, cover it in the closest existing pane/backend test (`src/tests/backends_test.cpp` touches header menus) and run that `-R` target.
- Live check under Xvfb with isolated `XDG_CONFIG_HOME`: Execute a card so its chip shows in a terminal pane, open a **different** card's detail on the board, click the chip — the board must open the chip's card. Also click the chip right after its turn ends (chip falls back to `m_boardTaskCard`) and confirm text and destination agree.
- Evidence under `docs/qa_evidence/2026-09-20-pane-header-card-chip/` with the reproduction log lines and the after-fix behaviour.
