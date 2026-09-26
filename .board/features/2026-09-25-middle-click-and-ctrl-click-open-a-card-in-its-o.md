---
id: HKY4
type: work
status: needs-verification
labels: [feature, gui, switchboard]
assignee: agent
implemented_by: kimi/k3
session: 6aaee940-3f4f-4a9f-9ee6-42ee39a14377
rank: zzzzzzzzzzzzzzzzzzzzzzzzzzzzi
created: '2026-09-25'
verify: {artifact: system, primary: script, also: [], human: optional, criteria: 'In a live window, middle-click and Ctrl+click on a Board card row and on a #ID link on a card page each dock a pinned card pane beside the Board showing that card; a plain click still opens in the same Board pane, and terminal #ID links still behave as before.', sign_off: none, effort: low}
links: {plans: [], commits: [4d1ba7c023ad, a5e732456710], evidence: [docs/qa_evidence/2026-09-25-hky4-middle-ctrl-click-card-pane/], related: [], github: null}
---
# Middle-click and Ctrl+click open a card in its own pane

## Issue
In the Board pane, middle-click and Ctrl+click on a card row or card link/chip should open that card in its own pinned card pane (the #Y2BA pane Shift+Enter and the ⤴ button already make), like a browser opening a link in a new tab. Terminal output `#ID` links are untouched — the click opens elsewhere anyway.

> for the board, i think middle click and ctrl click should both open new pane, like a browser opening a link in a new tab. deliver that
> — elliott · [session:068046613259444ba9b13f19ad048b93](relay://session/068046613259444ba9b13f19ad048b93) · 2026-09-25

## Done means
- Middle-click on a card row in the Board's list docks that card in a pinned pane of its own; the list keeps its selection and no card page opens in the Board.
- Ctrl+click on a row, on a `#ID` link in a card page's document, on a Live-surface card chip, or on a skill page's Linked chip does the same.
- A plain click or Enter still opens the card in the same pane.
- A shortcut hint (`board.cardOwnPane`) fires after a slow same-pane open, teaching the middle-click.
- Terminal output `#ID` links keep their existing behaviour — no gestures added there, since middle-click is paste in the terminal.

All gestures share one path, `BoardView::openInOwnPane`, which Shift+Enter and the page's ⤴ already used. A pinned pane refuses it, being already one card's pane. Landed as `4d1ba7c0`; the verify slot built the exact tree before the swap.

## Tests
- `relay-boardgestures-tests` (new executable, `ctest -R '^boardgestures$'`): rows (middle-click, Ctrl+click, plain click still in place), a `#ID` link in the card page's document (middle-click), a skill page's Linked chip (Ctrl+click) — 3/3 PASS, evidence `docs/qa_evidence/2026-09-25-hky4-middle-ctrl-click-card-pane/tests.txt`.
- The Live chip's middle-click rides the same by-name filter as the skills chips; that surface's own test (#C52H's theLivePageLists…) covers its chips once that lands.
- `land.py commit` verify slot: target `relay` builds on the exact landed tree.

## Execution Summary
One path, `BoardView::openInOwnPane`, now backs Shift+Enter, the page's ⤴, middle-click and Ctrl+click. Ctrl+click is read from `QApplication::keyboardModifiers()` at the four click-shaped open points (card page links, signal page links, `relay://card/` links, skill chips). Middle-click is caught where each surface can give it up: a filter on the list's viewport for rows (before the list swallows the press), a filter on the card page document's viewport for `#ID` anchors (a `QLabel` has no anchorAt, so the meta line's links take Ctrl+click only), and a by-name filter on the Live surface — strip (#TBRH) or tab (#C52H), whichever shape it has — plus the `"card"` property on skill chips, whose ignored middle presses walk up to the page. A pinned pane refuses the gesture.

The landing ran through the contested-hunk review: #C52H's Live-page rework and #YJ4A's row-chip work in `src/BoardPane.cpp` were left in the tree (excluded hunks and FOREIGN holds), and the row middle-click was moved into the eventFilter so it needed no edit inside #YJ4A's contested region. `tests/boardpane_test.cpp` was deliberately untouched: it is held by several sessions at once and was mid-rewrite during this work, so the test lives in its own executable, `tests/boardgestures_test.cpp`, like `boardfocus` and its siblings.
