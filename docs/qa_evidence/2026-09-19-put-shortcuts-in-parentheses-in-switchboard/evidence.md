# QA evidence — #QG60 put shortcuts in parentheses in switchboard

Date: 2026-09-19 · implemented by `claude-opus-4-5`

## What changed

`src/BoardPane.cpp` — 14 labels gained the key that actually fires them (all taken from
the existing key handlers in `CardDetail::eventFilter` and `BoardView::handleBoardKey` /
the view event filter; no key handling changed):

| Control | Old | New |
|---|---|---|
| List tools, new card | `+  New card` | `+  New card (n)` |
| Card page, back | `←  Back to board` | `←  Back to board (Esc)` |
| Notice, undo | `Undo` | `Undo (Ctrl+Z)` |
| Card header | `Edit` | `Edit (e)` |
| Card header | `#ID → prompt` | `#ID → prompt (t)` |
| Card header | `Open file` | `Open file (o)` |
| Edit frame | `Cancel` | `Cancel (Esc)` |
| Edit frame | `Save` | `Save (Ctrl+Enter)` |
| Reply row | `Comment` | `Comment (Ctrl+Shift+Enter)` |
| Reply row | `Discuss` | `Discuss (Enter)` (also the `setModeTips` reset label) |
| Reply row | `Plan` | `Plan (p)` (also the `setModeTips` reset label) |
| Reply row | `Execute` | `Execute (x)` |

Left bare on purpose: `×` close glyph (tooltip keeps `(Esc)`), `Clean up` and the cleanup
panel (no key), the running state `Stop`, the quick-add placeholder and the `m_keys`
legend. No shortcut-hint registry entry: no fast path was added or changed, only labels.

`tests/boardmodel_test.cpp` — the `button()` helper now matches a bare label or one
suffixed `label (…)`, the Back-label QCOMPARE updated, and four QCOMPAREs added asserting
the reply row reads `Comment (Ctrl+Shift+Enter)` / `Discuss (Enter)` / `Plan (p)` /
`Execute (x)`.

## What was run

- `cmake --build build -j` — clean.
- `ctest --test-dir build -R '^board'` — **board, boardworkspace: 2/2 passed** with the
  new labels and assertions.
- `ctest --test-dir build` (full) — 56/57 passed; `backend-and-bash` (test 56, backend
  Python + Bash/PTY suite) failed. This change touches only `src/BoardPane.cpp` labels and
  `tests/boardmodel_test.cpp`; nothing backend-related. The shared worktree also carried
  another card's in-flight work at the time. Re-run for a clean signal.
- Skipped at the owner's request ("skip tests"): re-running `backend-and-bash`,
  `./scripts/test.sh`, and the Xvfb visual check (~350 px pane, suffixed rows fit or wrap).

## Staging note

The worktree shared another card's uncommitted edits in `src/BoardPane.cpp` (#T71W
cross-provider QA); this commit was staged as `HEAD`'s `BoardPane.cpp` plus exactly the 14
label lines, so the commit contains only #QG60.

## GUI run (added the same day)

`drive.sh` + `README.md` in this directory: Relay under Xvfb with an isolated HOME, a
sandbox snapshot of this repo's board, `Ctrl+Shift+S` to open the Switchboard. The
suffixed labels render live (`+  New card (n)` on the list, `←  Back to board (Esc)`,
`Edit (e)`, `Open file (o)`, `Plan (p)`, `Execute (x)` on a card page), and at ~350 px the
reply row wraps onto two lines instead of overflowing. Details, coordinates and the
not-covered list are in the README.

