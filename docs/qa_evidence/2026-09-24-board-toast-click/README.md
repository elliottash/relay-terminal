# #XC75 — a board-write toast opens its card when clicked

Commit: fd9e91a5f335e752b07cd3e39f625f336905b555 on main.

## Done
- `src/Pane.h`: `PendingToast` carries a card id; `toastCard()` public; `showNextToast`
  makes a card toast interactive (mouse events, pointing-hand cursor, tooltip
  "Open #K7Q2 in the Board") and resets that when the toast goes down; `dismissToast()`;
  `noteBoardActivity` toasts via `toastCard(id, …)`; enqueueToast's dedupe compares the card.
- `src/PaneRuntime.cpp`: `Pane::eventFilter` — press+release on a card toast calls
  `openOutputTarget(relay::links::cardTarget(id), -1, false)`: the agent's context gets first
  refusal, then `onOpenCard` (wired by the window to `openBoardCard`).

## Tests
- `tests/consolemode_test.cpp` `aBoardToastClickOpensItsCard`:
  board_activity event → toast shows "#K7Q2", not transparent to mouse, pointing-hand cursor;
  press+release on the label → `onOpenCard("K7Q2")` fired once, toast dismissed, attribute reset;
  ordinary toast stays transparent, click does nothing.
- `./build/relay-consolemode-tests`: **21 cases, all passed** (QT_QPA_PLATFORM=offscreen).
- land.py build gate: exact tree `e6cd394fc629` built target `relay` clean.

## Not verified by machine
- The look: cursor + tooltip are the only affordances on a 4-second toast; whether that is
  discoverable enough is the owner's call.
