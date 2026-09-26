---
id: 60SP
type: work
status: done
labels: [bug, board]
assignee: agent
implemented_by: glm/glm-5.3
verified_by: glm/glm-5.3
rank: zzzzzzzzzzzzzzzzzw
created: '2026-09-25'
verify: {artifact: code, primary: script, also: [], human: none, criteria: 'A card page open in a narrow pane keeps the reader''s keyboard focus when the card is re-read or a thread entry arrives; opening a different card still takes the focus. Evidence: ctest -R boardfocus (red on the old branch, green on the fix).', sign_off: none, effort: low, stakes: nuisance}
links: {commits: [3b3092dc5db4f15327b882bfab78e80a6a557138, db8a0b1d3fc0731c770bd17d01103c541a8f5fc7]}
---
# Board steals pane focus when the open card is updated

## Issue
bug -- the board keeps garbbing the pane focus when there are updated to a card, if that card is open

## Done means
- A card open on a Board pane keeps the reader's keyboard focus wherever it was when the card's file changes under it (another session's comment, a status move, any update) and when a thread entry arrives on it.
- Opening a *different* card in that pane still brings the focus to the card page, as before.
- A regression test covers both halves and fails on the old behaviour.

## Execution Summary
`BoardView::handleEvent` answered every `board_card` event — and one arrives whenever the open card's file changes under it (#N5JJ) — by calling `m_detail->focusDocument()` whenever the card page was stacked over the list (a narrow pane or a solo reveal) and focus was not already inside the detail. That condition is true whenever the reader is typing in any other pane, so every update to an open card pulled the window's keyboard focus into the board.

The focus chain now reads the page's card id *before* `m_detail->show(event)` overwrites it, and hands the focus over only when the event's card differs from the one already showing. Opening a different card keeps the old behaviour (the reader is brought to the page); re-reads of the open card and thread appends leave the focus alone. Every other `setFocus` in the card page is behind a user action (edit, reply, restore-after-refusal) and is untouched.

Landed as `3b3092dc5db4` (the test, relay-boardfocus-tests) and `db8a0b1d3fc0` (the fix + CMake target); the verify-slot build of the exact tree passed before the swap onto main.

## Tests
- `relay-boardfocus-tests` (`ctest --test-dir build -R boardfocus`, tests/boardfocus_test.cpp, new): one window shared by a line edit (the pane the reader types in) and a narrow BoardView. Asserts opening K7Q2 still takes the focus, then that a re-read of the open card (twice) and a `board_thread_appended` on it leave the focus on the line edit. Run against the unguarded branch it fails at the first re-read — red before, green after.
- `ctest --test-dir build -R 'boardpane|cardtests'` (existing card-page coverage: open, edit, refused save, link reveal, fan-out) — passed.
- `scripts/relay-build` clean, and land.py's verify slot built the exact landed tree.
