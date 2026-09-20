---
id: C7PF
type: work
status: needs-verification
assignee: agent
priority: 2
rank: zzzzzzzzzzzy
created: '2026-09-19'
links: {plans: [], commits: [f78e1035], evidence: [docs/qa_evidence/2026-09-19-card-tag-in-pane-header/], related: [], github: null}
---
# when pane is executing a card, put the # tag in the pane header

## Issue
when pane is executing a card, put the # tag in the pane header

its also clickable to get to the card (keep the one in the prompt box as well)

## Plan
**Goal.** While a terminal pane is executing a Switchboard card, the pane header shows the card's `#ID` beside the title; a click on it opens that card in the Switchboard. The prompt box keeps its work chip exactly as it is.

**Findings.**

- Execute opens a terminal pane and hands it the card: `BoardView::executeCard` (`src/BoardPane.cpp:4209`) → `onExecuteCard` (`src/RelayWindow.h:4688`) → `Pane::startBoardTask(text, cardId)` (`src/Pane.h:9859`), which parks `m_boardTask` / `m_boardTaskCard` until the worker is configured, then flushes it as the first ask with `entry.cards` (`src/Pane.h:15196`–15205). Verify-runner panes go through `startGuestBoardTask` into the same flush. Ordinary prompts pick cards up via `cardsFor(text)` (`src/Pane.h:11708`, protocol 17.6); steers carry `cards` too (`src/Pane.h:7730`).
- The prompt-box chip the owner wants kept is `m_workChip` (`src/Pane.h:8081`–8130), fed by `noteWorkCard()` / `m_workCards` (`src/Pane.h:8104`), whose menu opens a card through `onOpenCard` — already wired in `src/RelayWindow.h:4948` to `openBoardCard(id)`.
- The header row is built in `Pane` (`src/Pane.h:3444`–3492): title, `auto` badge, stretch, directory; `PaneChrome::buildStatus` inserts the state glyph, subagent badge, ssh/phone/usage chips at the front (`src/PaneChrome.h`). Eliding is the give-way ladder, `relay::panes::headerFit` (`src/PaneLayout.h:90`–120, `src/PaneLayout.cpp:122`), applied by `Pane::updateHeader()` (`src/Pane.h:14597`); a widget in the Pane's row that is none of titleEdit/titleAuto/cwdLabel is counted into `HeaderWants::chips` — never gives way, whole or absent.
- Header clicks already have a pattern: press/drag in `Pane::eventFilter` (`src/Pane.h:3362`–3412); a release inside `m_cwdLabel` without a drag fires `onToggleExplorer` / `onOpenPath` (`3399`–3402). That is the pattern a clickable card chip copies.
- An agent turn's own cards are not carried on `m_active` for direct-start asks (only queued ones set it, `src/Pane.h:11945`), so "the running turn's card" needs one small piece of state, not a read of `m_active`.

**Steps.**

1. In `Pane`, add `m_cardChip` — a `QLabel`, objectName `paneCardChip`, plain text, pointing-hand cursor, installed into the header row right after the `auto` badge (before the stretch), hidden by default. Accessible name "Switchboard card being executed".
2. Add `QString m_turnCard` (and keep the running turn's other card ids for the tooltip). Set it in `startAgentEntry` (`src/Pane.h:11912`) from the entry's `cards`; update it from a steer's cards in `sendSteerNow` (`~7748`); clear it where a finished agent turn is recorded (the `Facts::finishSerial` bump — grep `finishSerial`).
3. One helper, `refreshCardChip()`: show `#` + the id while `m_boardTaskCard` is set (card handed, turn not started) or while `m_turnCard` is set; hide otherwise; tooltip names every attached card (ids and titles from `m_cardIndex`) and says a click opens it; then call `updateHeader()`. Call it from `startBoardTask`, the flush at `~15196`, `startAgentEntry`, the steer path, the turn-finish path, and `noteWorkCard` (which every attach path already runs through).
4. Click: extend the header release handler beside the `m_cwdLabel` case (`src/Pane.h:3399`–3402) — release inside `m_cardChip`, no drag, fires `onOpenCard(id)`. Dragging from the chip still moves the pane, as it does from the directory.
5. Theme: a `QLabel#paneCardChip` rule in `src/Theme.cpp` beside `QLabel#paneTitle` / `QLabel#paneCwd` (~line 358–362) and its counterpart in the derived user-theme block that `tests/themeswitch_test.cpp` checks — 9 pt, the header's muted ink, demibold, quiet like `paneCwd`.
6. No ladder change: the chip is counted by `updateHeader`'s generic widget walk into `chips` (whole-or-absent, like the phone chip and the subagent badge). Document it in `docs/ARCHITECTURE.md`'s pane-header section (~113–150) in one sentence.
7. No protocol message or event changes, and no new shortcut, slash command or fast path — so nothing to add to `docs/AGENT-SESSIONS-PROTOCOL.md` or the shortcut-hint registry.

**Risks.**

- *Scope — board-run turns:* a 19.16 card turn (`board_ask`) runs inside the Switchboard pane itself, not a terminal pane, and the card it works on is already named in the card detail there. This change covers terminal panes (Execute, Verify runners, `#card` prompts) only. **Question for the owner:** should the Switchboard pane's own header also name the card its turn is on? Recommendation: no — the detail view already says it.
- *After the turn ends* the chip goes away, because the header states what is happening now; the prompt-box chip keeps the history. If the owner wants it to linger, that is a one-line change of the hide rule.
- *Narrow panes:* the chip never gives way, so a very crowded header can hit `shortfall` with it on — the same treatment the phone chip already has, and `#ID` is ~44 px.

**Verify.**

1. `scripts/relay-build`.
2. `ctest --test-dir build -R panelayout` (ladder untouched — confirms no regression) and `-R themeswitch` (the new selector, both theme blocks).
3. Live under Xvfb with an isolated `XDG_CONFIG_HOME`: Execute a card from the Switchboard → the new pane's header shows `#ID` beside the title from the moment the card is handed; clicking it opens the Switchboard on that card; the prompt-box work chip still shows the card and its menu; when the turn finishes the header chip disappears; a plain prompt with no `#card` shows no chip; dragging the header from the chip still moves the pane.
4. Evidence and QA checklist under `docs/qa_evidence/2026-09-19-card-tag-in-pane-header/` per the tracker conventions.

## QA checklist
- [x] Header shows `#ID` beside the title from the moment Execute hands the card (Xvfb drive, parked branch: `docs/qa_evidence/2026-09-19-card-tag-in-pane-header/` shots 01–02).
- [x] Tooltip names every attached card with its title and says a click opens it (shot 03).
- [x] Click on the chip opens the Switchboard on that exact card (shot 06).
- [x] No chip on a pane/prompt with no card (shots 01, 04: the plain pane's header beside it).
- [x] `ctest --test-dir build`: `-R '^panes$'` (the give-way ladder, `tests/panelayout_test.cpp`), `-R panestate`, `-R panestatus`, `-R themeswitch` (the new selector across all five themes) — green.
- [ ] With a configured agent (not drivable headless): the chip stays up through the whole turn and disappears when the turn finishes; after the turn the prompt box's work chip still names the card and its menu opens it.
- [ ] Dragging the pane starting from the chip still moves it (same press/release pair as the directory label).
