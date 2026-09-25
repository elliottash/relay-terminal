---
id: 0FBB
type: work
status: needs-verification
labels: [feature, pane, board, accessibility]
assignee: agent
implemented_by: anthropic/claude-fable-5-1 via claude-code
session: 983a6a3c-af99-4377-8b09-3e819936784e
rank: zzzzzzzzzzzzzzzzzzzr
created: '2026-09-24'
verify: {artifact: visual, primary: script, also: [ai-visual, person], human: optional, criteria: 'In the pane header the claimed card''s code sits immediately before the title, reads ''#ID (n)'' when more than one card is claimed, and clicking it lists every claim; the ''[n]'' pane badge is gone.', sign_off: none, effort: medium, stakes: rework, blast: capability}
links: {plans: [], commits: [], evidence: [docs/qa_evidence/2026-09-25-0FBB/], related: [C7PF, R9G7], github: null}
---
# Show pane claims before the title: latest card code, total count, and accessible dropdown

## Issue
feature: where can we smoothly and accessibly show the card codes that have been claimed by a pane? maybe in the header somewhere? like the most recent one shows the code right before the pane title with a number in parens if there are more than 1, and you click and it shows all

## Done means
- A pane whose agent has claimed one Board card shows `#ID` immediately before its title; with several open claims it shows the latest one and the total, `#ID (3)`. It stays up between turns and goes when the pane holds no open claim.
- Clicking the chip (or Tab to it and Enter/Space) opens a list of every open claim, newest first, each row naming code, title and stage; choosing a row opens that card in the Board. Escape closes the list and focus returns to the chip. Screen readers get an accessible name that spells out the count and the latest code.
- A card carried by the running turn (`#id` in the prompt, Board Run) still shows as before (#C7PF): it counts as the latest while the turn runs.
- The muted `[n]` pane-address badge is gone from the header.
- Failure looks like: the chip stays after the card is closed or reassigned, the count counts closed cards, the title elides before the chip does not, or the chip cannot be reached from the keyboard.

## Plan
**Goal.** The pane header names the cards this pane has claimed: the latest code immediately before the title, `(n)` when there are several, one click for the whole list. The `[n]` pane-address badge goes.

**Findings.**
- The header already has a card chip, `m_cardChip` (`src/PaneUi.cpp:52`, `src/Pane.h:9850` `cardChipCard()` / `refreshCardChip()`, #C7PF): a QLabel after the title that names the *running turn's* card only and goes when the turn ends; a click is caught by the header drag filter (`src/Pane.h:5506`).
- A claim writes the pane's session token into the card (`Card.session`, `src/BoardModel.h`, #R9G7). The pane's own worker sends `board_changed` on every board tool write, and the pane's `IndexFeed` (`m_cardIndex`) applies it (`src/Pane.h:14518`); the index is fetched by `requestCardIndex()` when something first needs it.
- The `[n]` badge is `m_paneBadge` / `updatePaneBadge()` (`src/Pane.h:9723`), fed by the cross-pane roster (#R5TC); the handle itself stays in use by the roster.
- The header ladder (`src/PaneLayout.h`, `headerFit`) already keeps every non-title widget whole and elides the title first, so a wider chip costs the title, never the code.
- The pane's token is minted per run (`src/Pane.h:657`), so claims are the ones made in this pane's life.

**Steps.**
1. `relay::board::Model::claimedBy(session)` (+ `IndexFeed` forward): the open cards whose `session` is this pane, most recently updated first. Test in `tests/boardmodel_test.cpp`.
2. `relay::panes::claimChipCards(turnCards, claimed)`, `claimChipText(cards)` and `claimChipAccessibleName(cards)` in `src/PaneLayout.{h,cpp}`: the running turn's cards first, then the claims, deduplicated; `#ID` or `#ID (n)`. Test in `tests/panelayout_test.cpp`.
3. `Pane`: keep `m_claimedCards` newest-first — a card that newly carries this pane's token is prepended when the index applies an event; one that loses it or closes drops out — and ask for the index once the pane has a board. `refreshCardChip()` draws from the merged list.
4. The chip becomes a `QToolButton` placed before the title (Tab-focusable, Enter/Space/click opens a `QMenu` of every card: `#ID · title — Stage`, choosing one calls `onOpenCard`). Accessible name spells out the count and the latest code. The drag-filter click branch goes.
5. Remove `m_paneBadge` and `updatePaneBadge()`.
6. `src/Theme.cpp`: the chip's rule moves from `QLabel#paneCardChip` to `QToolButton#paneCardChip` (same quiet face, visible focus ring, no menu indicator); `tests/themeswitch_test.cpp` follows.

**Risks.** `src/Pane.h` and `src/PaneUi.cpp` are held by other live land sessions; my hunks land through the confirm review. Closed cards keep the pane's token on disk: they are excluded on purpose (the header is about current work). Dragging the pane by the chip is no longer possible; the title and directory still drag.

**Verify.** `ctest --test-dir build -R '^(panes|board|themeswitch)$'`; a staged Relay under Xvfb with one claimed card and a screenshot of the header, under `docs/qa_evidence/2026-09-25-0FBB/`.

## Execution Summary
The pane header now names the cards this pane has claimed, immediately before the title.

- **The claims chip** (`src/PaneUi.cpp`, `src/Pane.h` `cardChipCards()` / `refreshCardChip()` / `openClaimsMenu()`): a `QToolButton` before the title reading `#K7Q2`, or `#K7Q2 (2)` with the latest first. One card: a click, Space or Enter opens it in the Board. Several: the same opens a `QMenu` of every card, `#ID · title — Stage`, newest first; arrows move, Enter opens the card, Escape closes and focus returns. Tab reaches the chip; its accessible name says "Claimed cards: #K7Q2 and 1 more. Opens the list." The tooltip lists them all. It stays up between turns and goes when the pane holds no open claim.
- **Where the claims come from:** `relay::board::Model::claimedBy(session)` (`src/BoardModel.{h,cpp}`) lists the open cards whose `session` is this pane's token, most recently updated first; the pane keeps them newest-claim-first (`refreshClaims()`) as the index applies events, and merges the running turn's cards in front (`relay::panes::claimChipCards`, `src/PaneLayout.{h,cpp}`) so #C7PF's behaviour is unchanged. Closed cards are left out: the header is about current work.
- **A gap found on the way, fixed:** a board tool write announces itself to its own pane with the card's id alone (`upserts: ["K7Q2"]`), and a terminal pane never asked for the rows — so its own agent's claim never reached its index. `Pane::noteBoardWrite()` now asks for the board (or a `board_refresh`) on such an event.
- **The `[n]` pane badge is gone** (`m_paneBadge`, `updatePaneBadge()`): the pane's address still serves the roster and cross-pane messaging, it just is not drawn in the header.
- **Theme:** `QToolButton#paneCardChip` keeps the old quiet face, gains a focus ring and no menu indicator (`src/Theme.cpp`; `tests/themeswitch_test.cpp` follows).

Evidence, from a staged Relay under Xvfb on a throwaway project with two cards claimed by the one pane (`docs/qa_evidence/2026-09-25-0FBB/stage.sh`, `machine-pass.sh`):

![The header: #K7Q2 (2) before the title, no [n] badge](docs/qa_evidence/2026-09-25-0FBB/01-header-chip.png)

![Clicking the chip lists both claims with title and stage](docs/qa_evidence/2026-09-25-0FBB/02-chip-menu.png)

![In a narrow pane the chip stays whole and the title gives way](docs/qa_evidence/2026-09-25-0FBB/03-narrow-pane.png)

## Tests
`ctest --test-dir build -R '^panes$'` (theClaimsChipNamesTheLatestAndCountsTheRest)
`ctest --test-dir build -R '^board$'` (claimedByListsThisPanesOpenCardsNewestFirst)
`ctest --test-dir build -R '^themeswitch$'`
manual: docs/qa_evidence/2026-09-25-0FBB/
