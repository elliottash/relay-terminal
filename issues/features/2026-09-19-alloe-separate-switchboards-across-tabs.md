---
id: TTYB
type: work
status: needs-verification
assignee: agent
implemented_by: kimi/kimi-k3
rank: zzzzzzzzzzzzzzw
created: '2026-09-19'
links: {plans: [], commits: [], evidence: [docs/qa_evidence/2026-09-20-separate-switchboards-across-tabs/], related: [], github: null}
---
# alloe separate switchboards across tabs

## Issue
right now, if you have a switchboard in two tabs, moving around in the switchboard in one tab gets replciated in the other tab. they should be independent.

## Plan
**Goal.** Two Switchboard panes in different tabs of one window, looking at the same project, must not move each other: opening or refreshing a card in one leaves the other's list, selection and detail exactly where it was. Card *data* still syncs between them — the board is one set of files — only each pane's own navigation stops being replicated.

**Findings.**

- Each tab's Switchboard is already its own widget with its own state: `RelayWindow::toggleBoardPane` (`src/RelayWindow.h:4356`) → `createBoardPane` (`src/RelayWindow.h:4638`, `new relay::BoardView(workspace)` at 4641), and `BoardView` holds its own `board::Model m_model`, `m_selected`, `m_collapsed`, filter and sort (`src/BoardPane.h`). Arrow-key navigation and folding are local already.
- There is one `BoardWorker` per workspace per window (`RelayWindow::boardWorker`, `src/RelayWindow.h:4528`, `m_boardWorkers`), and its `onEvent` fan-out (`src/RelayWindow.h:4535–4541`) walks every tab's leaves and calls `tool->board()->handleEvent(event)` on every board pane whose workspace matches. That is right for broadcast events (`board`, `board_changed`, `board_thread_appended`).
- The leak is `board_card` — the answer to one pane's own `board_card_get`. In `BoardView::handleEvent` (`src/BoardPane.cpp`, the `type == "board_card"` branch around 3658–3690) the handler runs `m_detail->setChoices(…)`, `m_detail->show(event)`, `m_detail->setVisible(true)` and the focus juggling after it with **no `mine` check**, while the sibling answer branches do check it: `board_folder_changed` (~3551), `board_problems` (~3725), `board_written` (~3735), `board_undone` (~3764), `error` (~3895). `mine` is computed at the top of `handleEvent` (~3540) from the request-id prefix `sb<view>-…` invented in the constructor (~2451) and stamped on every outgoing message by `BoardView::send` (~3458). The worker echoes the requester's id on the answer (`backend/relay_core/board_protocol.py:1073`).
- So when tab A opens a card — `openSelected()` (~4551), the open card's re-fetch when `board_changed` touches it (~3650), or the post-conflict re-read (~3907) — the fan-out delivers that `board_card` to tab B as well, and B opens the same card detail. That is the reported "moving around in one tab gets replicated in the other".
- Turn streaming (`delta`, `thinking_*`, `status`) is already scoped by `card_id == m_detail->cardId()` (~3834), so it only paints a card that pane already has open; `board_thread_appended` (~3711) is scoped the same way. Leave those shared — they are the same card's live turn.

**Steps.**

1. In `BoardView::handleEvent` (`src/BoardPane.cpp`), gate the whole `board_card` branch on `mine` (add `!mine` to the branch condition, or `if (!mine) return;` first thing inside it), so a pane only shows card details it asked for. Change nothing else inside the branch.
2. Re-read the fan-out in `RelayWindow.h:4535–4541` and leave it alone: broadcast events must keep reaching every pane. (Routing by request prefix in the fan-out was considered and rejected — the `sb…` prefix belongs to `BoardView`, not the window.)
3. Add a regression test: a C++ widget test with `QT_QPA_PLATFORM=offscreen`, new `tests/boardpane_test.cpp` (+ CMake target, in the style of the existing pane tests). Two `BoardView`s on one workspace; feed both the same hand-made `board` snapshot through `handleEvent`; capture A's `board_card_get` via `onSend` and deliver the `board_card` answer carrying A's request id to **both** views; assert A's `detailOpen()` is true and B's is false with `selectedCard()` unchanged. Also deliver a `board_changed` upsert to both and assert both models applied it — data sync must survive the fix.
4. Check `docs/ARCHITECTURE.md`'s Switchboard/worker description: if it describes the per-window worker's event fan-out, add one line saying request-scoped answers (`board_card`, `board_written`, …) are ignored by panes that did not ask. If it does not describe routing, no doc change.

**Risks.**

- A pane that should show a card without having asked: none found — every open path (`o`/Enter, `p`/`x`/`v` before the card arrives, `openBoardCard` from a `#ID` click in a terminal, the conflict re-read) sends its own `board_card_get` through `send()`, which stamps that pane's prefix.
- An older worker that does not echo `id` on `board_card` would make `mine` false and break opening cards; the current worker always echoes (`board_protocol.py:1073`) and GUI and worker ship together.
- Two panes with the *same* card open: each re-fetches under its own id after a change and both refresh; a live turn on that card still streams into both. No owner decision needed.

**Verify.**

- `scripts/relay-build`, then `ctest --test-dir build -R boardpane` for the new test and `-R board` for the board model/sections/workspace tests.
- Live, under Xvfb with an isolated `XDG_CONFIG_HOME` (per `WARP.md`): run Relay on this repo, open two tabs, Ctrl+Shift+S in both; open a card in tab A and check tab B stays on its list with its own selection and folds; move a card in A and check B's rows update (data sync) without B's detail or selection changing; then repeat with the same card open in both tabs and edit it in one.

## QA checklist
- [ ] `ctest --test-dir <build> -R "^boardpane$|^board$"` green on a tree without other sessions' WIP (on the shared checkout, `board` currently hangs inside another session's uncommitted delete-confirm feature — not this change; tip+this-change is green, see the evidence README).
- [ ] Live, two tabs, Ctrl+Shift+S in both: open a card in tab A → tab B stays on its list with its own selection and folds (drive.sh shots 02/03 prove it on the exact tree).
- [ ] Move a card in one tab → the other tab's rows update (data sync) while its own view stays put; same card open in both tabs, comment in one → the other's open card shows it (covered by `boardpane`'s `boardDataStillReachesBothPanes` slot; the xdotool choreography for it was flaky under Xvfb).
- [ ] A `#ID` click in a terminal pane (`openBoardCard`) still opens that card in its own board pane.
