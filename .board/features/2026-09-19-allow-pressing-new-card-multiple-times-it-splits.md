---
id: Y2BA
type: work
status: executing
labels: [feature, switchboard, panes]
assignee: y2ba-cards
rank: zzzzzzzzzzzzw
created: '2026-09-19'
links: {plans: [], commits: [0e198884, 459c2e59, d850939f504d], evidence: ['docs/qa_evidence/2026-09-25-card-panes/'], related: [], github: null}
---
# Several cards open at once: solo card panes, then a Card pane kind of its own

## Issue
allow pressing new card multiple times, it splits the second "new card" pane vertically

## Plan
Slice 1 of #P2W8 (Discussion points, "Cards as artifact panes"; owner: "go big and build the whole thing"). The owner's own request on this card is the same want: pressing new card again should open a second card page beside the first.

**Goal.** More than one card open in a tab, each in its own pane, each with its own docked agent conversation (`<tab>/card:<ID>`, already per card in the worker, #DR4K/#CTRN).

**Findings.** One Board pane per tab: `RelayWindow::openBoardCard` (`src/RelayWindow.h:4778`) finds "the one the tab already has" and `toggleBoardPane` focuses it. One `CardDetail` per `BoardView` in a `QSplitter` (`src/BoardPane.cpp:4811-4819`); `openCardSolo` (`:9000`) hides the list until the page closes. `CardContext` (`:4589`) is already the card's Context; `CardDetail` (`:1830`) borrows model lookups, selection follow, worker requests and hash-checked saves from `BoardView` (`:5049-5084`, `:8689-8875`). Layout node `{"board": {workspace, tab}}` (`serializeNode`, `relay::windowstate`).

**Steps.**
1. *A0 — solo card panes.* `BoardView::pinSolo(id)`: like `openCardSolo` but the list never comes back, Esc and the page's close button call a new `onClosePane` callback instead of `closeDetail`, and the filter bar and tab strip are hidden. `RelayWindow::openBoardCardInNewPane(id)` creates `createBoardPane(workspace)`, docks it beside the anchor (`dockBeside`), calls `pinSolo(id)` and `waitForBoardCard`. Entry points: **Shift+Enter** on a list row, a **⤴ "Open in its own pane"** button beside the card page's Edit pencil, and "new card" pressed while a card page is already open (the owner's request). `openBoardCard(id)` from a `#ID` link prefers a solo pane already showing that id, then the tab's list Board. Layout node gains `card` and `solo: true`; restore re-pins. `panesIn`/`leavesIn` unchanged (a solo Board is still a `ToolPane`).
2. *Kind::Card.* Move `CardDetail` to `src/CardPane.{h,cpp}` behind a `relay::board::CardController` interface (card lookup, sections, request/write with hash, open link, console factory) that `BoardView` implements; add `ToolPane::Kind::Card` hosting a `CardDetail` fed by the tab's `BoardModel` (one `QFileSystemWatcher` per tab, shared through the window). Step 1's node becomes `{"card": {workspace, id}}` with a one-release reader for `board.solo`. `openBoardCardInNewPane` builds a `Kind::Card` pane instead. Board list keeps its preview split.
3. Docs: `docs/ARCHITECTURE.md` Board section (the two kinds, the node), keys in the in-app help.

**Files.** `src/BoardPane.h`, `src/BoardPane.cpp`, `src/RelayWindow.h` (only `openBoardCard`, `toggleBoardPane`, `openBoardCardInNewPane`, `serializeNode`/restore for the board node), `src/PaneChrome.h` (step 2), `src/CardPane.{h,cpp}` (new, step 2), `CMakeLists.txt` (step 2), `tests/boardmodel_test.cpp` or a new `tests/cardpane_test.cpp`, `tests/windowstate_test.cpp` if it exists. **Not** `src/FilePanes.*`, `src/ArtifactWorkspace.*`, `src/RelayWindowWorkspace.cpp` (held by another session).

**Risks.** `src/RelayWindow.h` and `src/Pane.h` carry other sessions' uncommitted hunks: claim with `land.py begin`, land with `--dry-run` first, exclude any hunk that is not yours. A card open in two panes of one tab shares one conversation; both consoles draw it (that is the tab rule, #AGNT).

**Verify.** `scripts/relay-build && ctest --test-dir build -R 'board|windowstate|cardpane'`; live under Xvfb: open two cards in two panes, Plan on both, both strips run; restart, both panes come back on their cards. Screenshots to `docs/qa_evidence/<date>-card-panes/`.

## Tasks

- [x] A0: pinSolo, openBoardCardInNewPane, Shift+Enter / pop-out button / new-card-while-open, layout node, link routing <!-- t:ss --> (0e198884)
- [ ] Kind::Card with CardController and CardPane.{h,cpp}; shared per-tab model <!-- t:wb blocked_by=ss -->
- [ ] Docs and live evidence <!-- t:g5 blocked_by=wb -->
