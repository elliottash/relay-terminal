---
id: 9NBZ
type: work
status: needs-verification
labels: [bug, switchboard]
assignee: agent
implemented_by: glm/glm-5.3
session: b2ca06b3-0efd-42ab-95fa-f0d2c3196ca6
rank: zzzzzzzzzzzzzzzzzzi
created: '2026-09-25'
verify: {artifact: code, primary: script, also: [], human: none, criteria: 'Ctrl+F with the keyboard on an open card shows a find strip that jumps to and counts matches in the card document, steps with Enter/Shift+Enter and wraps; on the list page Ctrl+F focuses the existing filter; Esc closes the strip and puts the keyboard back on the card; a terminal pane in the same window keeps Ctrl+F for its own view.', sign_off: none, effort: medium, stakes: nuisance, blast: case}
links: {commits: [dcf3fbbcf866], evidence: [tests/boardfind_test.cpp]}
---
# No find function in cards: Ctrl+F does nothing on the Board, and an open card cannot be searched

## Issue
bug: no find function in cards.

## Done means
Ctrl+F with the keyboard anywhere on the Board answers `find.inView`: with a card open, a find strip appears over the card's document that jumps to the first match, counts matches, steps with Enter / Shift+Enter (and ↑/↓), wraps around both ends, prefills from the card's selection, and closes on Esc with the keyboard back on the document; with no card open, the existing list filter takes the focus. A terminal pane in the same window keeps Ctrl+F for its own view, and an explicit action target is never overridden.

## Execution Summary
`find.inView` (Ctrl+F) is dispatched in `RelayWindowCore.cpp` `runAction` to `pane->openFindInView()` — the terminal FindBar — and only after the `!pane` guard; the Switchboard holds no terminal pane, so on the Board the action fell through and did nothing. That is the bug.

- `BoardView::openFind()` (`src/BoardPane.h/.cpp`): the entry the window calls. With a card open (`detailOpen()`), the card page's find strip opens; otherwise it falls to `focusFilter()`, the list page's own full-text find that `/` already focuses.
- `CardDetail` gains the strip (`src/BoardPane.cpp`): a line edit, a match count, ↑/↓/× buttons over `m_doc`. `openFind()` prefills from the document's selection, `refreshFind()` restarts at the top on every edit and counts case-insensitively, `findStep()` steps and wraps both ends (as the terminal find does), and Esc closes with focus returned to the document. `render()` re-runs the search when a new card lands under an open strip, so the count never describes a document that is gone.
- The terminal's `FindBar` is deliberately not reused: it is built around a backend's scrollback and a conversation, and `relay-board` links neither.
- Wiring: `RelayWindow.h` gains `focusedBoardView()` beside `focusedConsole()` — asked of the focus widget, for the reason that comment already gives: the active leaf is the last terminal pane, which is not where the key went. `RelayWindowCore.cpp` answers `find.inView` from the board before the `!pane` guard, exactly as `agent.modelBox` does above it, and only when no explicit `pane` target was given; a terminal pane is never inside a board, so it keeps the key.

Landed as `dcf3fbbcf866` on main (verify slot built the exact tree: `relay` target, green).

## Tests
- `ctest --test-dir build -R '^boardfind$'` — new offscreen test `tests/boardfind_test.cpp`, passing (0.10s): the card page's whole find life — strip hidden until asked, first match and `3 matches` count on typing, Enter steps in document order, wraps after the last match, Shift+Enter steps back, a missing needle reads `No matches`, Esc hides the strip and returns the keyboard to the document — and the list page answering `openFind()` with the filter focused.
- Neighbours still green after the change, offscreen in build/: `ctest -R '^(boardfocus|boardfilter|boardpane|appcommands)$'` (all passed through tests_run), and `ctest -R '^consolemode$'` passed directly twice in a normal environment (9.1s) — it cannot pass through the tests_run harness regardless of this change: that harness's `XDG_RUNTIME_DIR` redirect alone breaks its shell-busy cases (single-variable bisect, evidence on #VZ8C, whose segfault-on-report this card's `6c6f75ef66ed` also guards).
- `scripts/relay-build` built `relay`; land.py's verify slot built the exact committed tree before the swap (green), for both commits.
- Evidence: `tests/boardfind_test.cpp`; commits `dcf3fbbcf866` (find in cards) and `6c6f75ef66ed` (the consolemode guard this work owed the signal).

## Try it
`docs/qa_evidence/2026-09-25-tryit-9NBZ/stage.sh` — opens Relay on a disposable four-card board (this checkout's build/relay, throwaway profile; delete `/home/elliott/.cache/relay/scratch/tryit/9nbz` to remove it all).

Open the card **Parallax drift on the horizon strip** and press **Ctrl+F**. Type `parallax` and step through what it finds with **Enter** (and **Shift+Enter** backwards); try a word the card does not contain; close it with **Esc**. Back on the card list, press **Ctrl+F** once more and type.

With the card open, did Ctrl+F and the strip do what you reached for — and did anything about it stop you or surprise you? (~2 min)

Expected: docs/qa_evidence/2026-09-25-tryit-9NBZ/expected.md (sealed until you answer)
