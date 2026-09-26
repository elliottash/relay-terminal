---
id: 6BY7
type: work
status: needs-verification
labels: [feature, switchboard, panes]
assignee: agent
implemented_by: deepseek/deepseek-v4.1-flash
session: c26b448b-eb6e-4580-9059-fc2b47a59782
rank: zzzzzzzzzzzzzzzzzzzzzzzzzzzw
created: '2026-09-25'
verify: {artifact: code, primary: script, also: [ai-text], human: none, criteria: 'consolemode ctest cases cover: chip toggles the drawer, board_card renders read-only, Done sends board_move status=done, no thread-writing message on submit; drawer ignores foreign request ids', sign_off: none, effort: medium, stakes: rework, blast: capability}
source: 'Owner on #C7RW, 2026-09-25'
links: {plans: [], commits: [f7dff8f41e91, 6822c9a56563], evidence: [docs/qa_evidence/2026-09-25-card-drawer-pane-chip/], related: [P2W8, C7RW, C7PF, Y2BA], github: null}
---
# Card drawer in a terminal pane: the header chip toggles the card inline (read-only plus Done)

## Issue
Wave-2 slice of #P2W8, the B-lite drawer recommended on #C7RW: a shell pane working a card shows that card inline — the header chip toggles a read-only drawer with the body, `## Plan`, `## Tasks` and stage, plus a Done action — and the pane's conversation is never written to the card's thread (#CTRN).

> "we should instead have cards attachable to panes and more of the card features integrated into standard panes" (owner, #C7RW Issue, 2026-09-25); "yes add it to p2w8" (owner on #C7RW, 2026-09-25)
> — elliott · [session:7bdd4488cc34018b5f751db46ca2a292](relay://session/7bdd4488cc34018b5f751db46ca2a292) · 2026-09-25

## Done means
- A terminal pane whose header chip names a card (the turn's card, or one claimed) toggles a card drawer on chip click: the card's body, `## Plan`, `## Tasks` and stage rendered read-only, plus a Done action. With several cards the claims menu picks which; the chip's old behaviour — open the Board on the card — stays reachable from the drawer.
- The drawer is fed by the tab's helper worker (`board_card_get`, refreshed on `board_changed`) — the same stream the Board pane reads — so card file changes reach it the way they reach the Board; nothing reads the card file directly.
- Nothing typed in the pane — user or agent conversation — is written to the card's thread; the thread stays the worker-written record with `model=`/`turn=` provenance (#CTRN).
- Failure would show as any of: chip click still jumping to the Board, a drawer that goes stale when the card file changes on disk, a Done that silently does nothing when the worker refuses it, or a thread entry carrying pane provenance.
- Focused tests: chip toggles the drawer, the drawer renders the claimed card read-only, Done sends `board_move`, and there is no write path from the pane's conversation to the thread.

## Plan
**Goal.** A terminal pane working a card shows that card inline: the header chip toggles a read-only drawer (body, `## Plan`, `## Tasks`, stage, Done action) fed by the tab's board helper, and the pane's conversation never writes to the card's thread. Wave-2 slice of #P2W8.

**Findings.**
- The chip is `ClaimsChip` (`src/PaneUi.cpp` ~47, objectName `paneCardChip`); its click handler (~60) calls `onOpenCard(id)` for a single card and `openClaimsMenu()` (`src/Pane.h:10473`, whose items also call `onOpenCard`) for several. `cardChipCards()` (`src/Pane.h:10383`) = turn cards + `m_claimedCards`; row data comes from `m_cardIndex` (`relay::board::IndexFeed`), refreshed by `refreshCardChip()` (`src/PaneEvents.cpp:579`) and board events at `src/Pane.h:15281`.
- Index rows carry no bodies since #7M6E (`src/BoardModel.h:24`). The full card travels as the helper request `board_card_get {id, card}` → answer event `board_card` with front matter, untruncated `body`, `body_truncated: false`, and the echoed request `id` (`backend/relay_core/board_protocol.py:1895`).
- The tab helper channel is `sendToHelper(page, msg)` (`src/RelayWindow.h:5123`, starts the worker if needed) plus `listenToHelper(page, owner, handle)` (`:5112`), fanned by `deliverToHelperPanes`. A non-Board surface already reads this way: `src/ReviewPane.h:185–235` sends `board_card_get` with a prefixed request id (`review-card-…`) and renders read-only Markdown; the Board refetches on `board_changed` the same way (`src/BoardPane.cpp:5060`).
- The Board's Done is `BoardView::doneSelected` (`src/BoardPane.cpp:6630`): `board_move {card, status: "done", section: "", reason: …}`; the worker's gates (Human QA, verify block) answer an error the surface shows.
- The pane's layout is one QVBoxLayout built in `buildUi()` (`src/PaneUi.cpp`): header row, `m_terminalHost` (stretch 1), composer. A drawer inserts between header and terminal host without touching pane minimum widths (#SDXE class of bug).
- Terminal panes get window callbacks where `onOpenCard` is wired: `src/RelayWindowCore.cpp:973` (shell panes) and `:1143` (agent consoles). Styling lives in `src/Theme.cpp` (qss strings, tested by `tests/themeswitch_test.cpp`).
- Test harness for chip behaviour: `tests/consolemode_test.cpp` (StubContext + `deliverWorkerEvent`, `findChild` for widgets, lambdas recording callbacks; the `onOpenCard` wiring is tested around `:795` and `:856`).

**Steps.**
1. New `src/CardDrawer.h`: `class CardDrawer : public QFrame`, no Q_OBJECT, public `std::function` callbacks (house style). `setCard(const QJsonObject &boardCardEvent)` fills: a header row — stage (`relay::board::statusTitle`), `#id`, title, `Done` button (hidden when status is already done), `Open in Board`, close `✕` — and a read-only `QTextBrowser` (`openExternalLinks(false)`, links copied as text) rendering the body without the title heading, then the `## Plan` and `## Tasks` sections (`- [x]` items rendered as ticks, not editable). Callbacks `onDone(cardId)`, `onOpenBoard(cardId)`, `onClose()`; objectName `cardDrawer`; an inline notice line for worker refusals. Add the card front-matter keys it reads from the `board_card` answer only — no file access.
2. `Pane` (`src/Pane.h`, bodies beside the chip code): `CardDrawer *m_cardDrawer`, `QString m_drawerCard`; `toggleCardDrawer(const QString &id)` lazily builds it, inserts it at layout index 1 (under the header), caps its maximum height at half the pane's height, sends `board_card_get {id: "drawer-card-<id>", card}` through a new public callback `onBoardRequest(const QJsonObject &)`, and hides it on toggle-off (keeps content). A second chip-click with a different single card switches the drawer to that card.
3. Chip behaviour: `src/PaneUi.cpp` click handler — single card calls `toggleCardDrawer(id)` instead of `onOpenCard`; `openClaimsMenu()` (`src/Pane.h:10473`) items call `toggleCardDrawer(id)` too. The drawer's `Open in Board` button keeps calling `onOpenCard(id)`.
4. Event routing on `Pane`: `handleBoardHelperEvent(const QJsonObject &)` — `board_card` whose `id` starts `drawer-card-` → `setCard`; `board_changed` naming `m_drawerCard` → re-send `board_card_get`; `error`/`board_error` carrying a drawer request id → show the message in the drawer's notice line. The Done button sends `board_move {card: m_drawerCard, status: "done", section: "", reason: "marked done from the pane's card drawer"}` through `onBoardRequest` — the worker's gates refuse what they must and the refusal renders inline, exactly as the Board does.
5. Window wiring (`src/RelayWindowCore.cpp:973` and `:1143`): `pane->onBoardRequest = [guard](const QJsonObject &request) { … sendToHelper(page, request); }` (capture the tab's `page` with the same guard pattern as `onOpenCard`) and `listenToHelper(page, pane, …)` forwarding to `pane->handleBoardHelperEvent`. The drawer must ignore events whose request id is not `drawer-card-` (other listeners' answers are fanned to it — same discipline as `ReviewPane`).
6. Styling in `src/Theme.cpp`: `#cardDrawer` and its buttons in the existing qss strings; keep `themeswitch_test.cpp` green (add the selector to its coverage if it asserts the palette).
7. Tests in `tests/consolemode_test.cpp`: (a) with one turn/claim card, clicking the chip shows a populated `cardDrawer` child and a second click hides it, `onOpenCard` never called and the drawer's Board button calling it; (b) a delivered `board_card` answer renders stage, `## Plan` text and task items, with no editable child in the drawer, and a following `board_changed` for the card re-sends `board_card_get` (record via a stub `onBoardRequest`); (c) Done sends `board_move` with `status: done`; (d) with a claimed card, submitting a user message sends no `board_comment`/`board_update_card`/thread-writing message and the card's thread is unchanged.

**Risks.**
- Helper events fan out to every listener in the tab; forgetting the `drawer-card-` prefix check makes the drawer eat other surfaces' answers — covered by test (b) asserting it ignores a foreign `board_card` id.
- `board_move` to done is refused worker-side for gated cards (open Human QA question, verify block); the drawer shows the refusal rather than pre-computing the gate — the Board keeps the full move surface.
- The drawer shares vertical space with the terminal; the height cap keeps the composer and terminal usable, and no minimum-size change may leak into `panelayout_test.cpp`.
- No owner decision is needed; the drawer pins to the card it was opened on and the claims menu picks among several.

## Execution Summary
Landed on main as `f7dff8f41e91` (evidence-file tweak `6822c9a56563`), verify gate: *the exact tree builds*.

The pane's claims chip now toggles a read-only card drawer under the header instead of jumping to the Board. `src/CardDrawer.h` (new) is the drawer: stage, `#id`, title, the card document without its title heading, `## Plan`/`## Tasks` with `- [x]` drawn as ticks, `Done`, `Open in Board` (the chip's old behaviour), `✕`, and a notice line for worker refusals. `src/Pane.h` adds `onBoardRequest`, `toggleCardDrawer()`/`requestDrawerCard()` and `handleBoardHelperEvent()` — which answers only request ids prefixed `drawer-card-`, refetches on a `board_changed` naming the open card, and renders an `error` under its own id inline. `src/PaneUi.cpp` points the chip and the claims menu at the drawer; `src/RelayWindowCore.cpp` gives both pane kinds the tab's helper channel (registered at the first ask, re-registered if the pane asks on another tab); `src/Theme.cpp` styles it; `CMakeLists.txt` lists the new header. The drawer reads no card file, and the pane sends the helper only `board_card_get` and `board_move` — nothing typed in the pane reaches a card thread.

The landing used `land.py`'s `--only-hunk` authorship selection: this shared checkout holds another live session's uncommitted work in `src/Pane.h` and `src/RelayWindowCore.cpp` (card #83YV, a Python console), and only the hunks the journal attributes to #6BY7 were taken. Those other hunks stayed in the working tree, untouched — verified by diffing the slot tree against `main` (only the seven intended `Pane.h` hunks, and no `PaneJournal.h` include).

## Tests
`python3 scripts/land.py try 8531c4b7 --tests consolemode --only-hunk …` — the exact tree builds, and the four new cases in `tests/consolemode_test.cpp` pass (none appears in the ctest `FAIL` list):

- `theChipTogglesTheCardDrawer` — the chip toggles the drawer, `onOpenCard` is not called by the chip, the drawer's Board button is, and the first ask is `board_card_get` with id `drawer-card-6BY7`.
- `theCardDrawerRendersTheHelperAnswerAndRefreshesOnChange` — a `board_card` answer renders stage/title/body/`## Plan`/`## Tasks` (no editable child, `- [x]` as ✓), another surface's answer is ignored, and a `board_changed` naming the card re-asks the helper.
- `theCardDrawerDoneSendsBoardMoveAndShowsRefusals` — Done sends `board_move {status: done, section: ""}` under the drawer id, the worker's refusal renders in the notice line (a foreign error does not), and a card already done shows no Done.
- `aPromptOnAWorkedCardWritesNothingToTheBoard` — a prompt on a worked card sends nothing board-writing and no helper request.

Raw ctest log: `docs/qa_evidence/2026-09-25-card-drawer-pane-chip/consolemode.txt`. One pre-existing case fails in that tree (the open-path case, `consolemode_test.cpp:726/727/739`); it is not this change's — the same hunks passed it on tip `f8359d95`, it broke with tip `5858d5c1`, it runs before the new cases, and its paths are untouched here; the suite already carries the open machine signal `ctest:consolemode` (card #VZ8C).
