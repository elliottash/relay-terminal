# Separate Switchboards across tabs (#TTYB) — implementer evidence

**The change.** `BoardView::handleEvent`'s `board_card` branch is now gated on `mine` — the
request-id prefix the pane stamps on every message it sends (`BoardView::send`), which the worker
echoes back on the answer (`backend/relay_core/board_protocol.py`). The window's per-worker event
fan-out (`RelayWindow.h`, `onEvent` → every board pane of that workspace) is unchanged: broadcast
data (`board`, `board_changed`) still reaches every pane, so the rows stay in sync — only each
pane's own card detail stops being replicated. The hand-feeding tests in `tests/boardmodel_test.cpp`
were taught the real handshake (`openCard()`: select + ask, deliver the answer under the pane's own
request id), and `tests/boardpane_test.cpp` pins the two-pane contract itself.

## The exact tree that was verified

The shared checkout mixes several sessions' uncommitted work (a delete-confirm feature among
them, whose WIP test hangs the shared `build/`'s `board` suite — see "Not ours" below). So the
verification tree is **tip + this change only**, built and run on its own:

- `src/BoardPane.cpp` = tip + the `mine` gate
- `tests/boardmodel_test.cpp` = tip + the `openCard` handshake (28 feed sites, 7 capture
  setups, the helper)
- `tests/boardpane_test.cpp`, `CMakeLists.txt` = new test + target

## Unit (limited, as asked — no full suite)

```
ctest --test-dir <exact-tree-build> -R "^boardpane$|^board$" --output-on-failure
  1/2 Test #50: board ............................ Passed   0.92 sec   (the reworked suite)
  2/2 Test #53: boardpane ........................ Passed   0.08 sec   (new)
  100% tests passed
```

Log: `logs/exact-tree-ctest.txt`. `boardpane` holds the card's whole story in two slots:

- `aCardOpenedInOnePaneDoesNotOpenInTheOther` — two `BoardView`s on one workspace, the same
  `board` snapshot, A's `board_card` answer (carrying A's request id) delivered to **both**: A
  opens the card, B stays on its list with its own selection and folds; an id-less answer opens
  nothing anywhere.
- `boardDataStillReachesBothPanes` — a `board_changed` upsert moves the card in **both** models
  (data sync survives the fix), B's navigation is untouched, and A's open card re-reads under
  A's own id, which B ignores.

## Live, under Xvfb (isolated HOME/CONFIG/RUNTIME, RELAY_KEYRING=off)

`drive.sh` (same shape as `2026-09-20-switchboard-delete-card/drive.sh`) builds a fixture board
(Alpha/Bravo inbox, Charlie ready), opens Relay on it, puts a board in each of two tabs, and
screenshots + OCRs each step. Full log with per-shot OCR headlines: `logs/live-drive.txt`;
shots `01…09*.png`. What it proves on the exact tree:

- `02` tab 1 has Alpha's card open ("Back to board" header, card title).
- `03`/`03b` tab 2's own freshly-opened board is **on its list** — filter bar, its own rows,
  no Back header: the exact reported bug, gone.
- `05` after activity in the other tab, that board is still on its list.
- `06` a pane that never asked opens its own card fine (Alpha, Back header).
- `08` the same card open again; `09` no `gui_crash` in the whole run.

What the drive could **not** pin live, and why the unit test carries it instead: xdotool's tab
cycling and card-move chords are unreliable inside the board pane under a WM-less Xvfb (several
Ctrl-combos are swallowed by its editors), so the keyboard move in one pane landing in the other
pane's rows, and a comment on a card open in both panes flowing to the second, are asserted by
`boardpane`'s second slot (the same wire events the worker sends) rather than by screenshot. An
earlier run did show a move and a comment flowing into the other pane's open card thread live,
but its screenshots were overwritten by the reruns; the kept artifacts are the run above.

## Not ours, noticed on the way

The shared `build/`'s `board` suite currently hangs in
`theDeleteKeyAndButtonDeleteTheCardAndTheUndoToastSurvives` — inside `relay::confirmDeleteCard`'s
`QDialog::exec` (backtrace in the session log), which belongs to another session's uncommitted
delete-confirm feature (155 insertions in the working `src/BoardPane.cpp`, absent from `main`).
On tip + this change the suite is green, so the hang is that WIP's, not this card's.

## Rebuild / rerun

```
scripts/relay-build
ctest --test-dir build -R "^boardpane$|^board$"        # on a tree without other sessions' WIP
bash docs/qa_evidence/2026-09-20-separate-switchboards-across-tabs/drive.sh [build-dir] [out-dir]
```
