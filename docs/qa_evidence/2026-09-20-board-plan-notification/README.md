# #NQP9 — plan-ready notifications + notifications.jump (implementer evidence)

When a Switchboard card's Plan turn ends, the notification centre (the bell) learns about it —
posting only, never a focus move — and `notifications.jump` (Ctrl+Shift+1) goes to the newest
notification's pane/card, walking older entries on repeated presses.

## What changed

- `src/Notifications.{h,cpp}` — `NotificationCenter::markSeen(id)`: marks one entry seen and emits
  `changed` only when it actually turned, so `notifications.jump` can drop the badge one entry at
  a time beside the popup's `markAllSeen`.
- `src/BoardPane.{h,cpp}` — `BoardView::onTurnEnded(id, mode, outcome)` callback, invoked in the
  terminal-event branch (`done`/`error`/`cancelled`) with the turn's mode read before
  `m_cardTurns.remove()`. Cleanup runs are not card turns and never reach it.
- `src/RelayWindow.h`
  - `createBoardPane` wires `onTurnEnded`: mode `plan` + `done` → "Plan ready: #ID" (success),
    `error` → "Plan failed: #ID" (error); body is the card title from the pane's model. Source is
    `board:<workspace>#<id>`. Discuss turns and cancellations post nothing. **No focus call on
    this path** — the card's "don't instantly move the active pane" rule.
  - `openNotificationSource(source)`: tokens → `WindowManager::focusPane` as before;
    `board:<workspace>#<id>` → an open Switchboard on that workspace (this window first, another
    window's raised) via `boardPaneFor` + `revealBoardCard` (select, open, retry via
    `waitForBoardCard`), or a new board pane beside the active leaf.
  - `jumpToNotification()` (`notifications.jump` in the action chain): newest-first entries with a
    source, walk index `m_notificationJumpIndex`, single-shot `m_notificationJumpReset` (~4 s),
    index reset on `NotificationCenter::changed` unless a walk is running, empty list →
    "No notifications." toast. The popup's `onOpenSource` now routes through
    `openNotificationSource` and shows the `notifications.jump.mouse` shortcut hint.
- `src/Keymap.h` — `notifications.jump` (window), default **Ctrl+Shift+1**.
- `docs/ARCHITECTURE.md` — the Notification centre section documents the `board:` source, the
  jump/cycle behaviour and `markSeen`.
- `tests/notifications_test.cpp` — `markSeenOneEntry`: one entry seen, badge drops by one,
  `changed` fires once; already-seen and unknown ids change nothing.

## The key: Ctrl+Shift+1, not the plan's Ctrl+Shift+M

The plan recommended Ctrl+Shift+M, surveyed as free in all four preset tables — but the *default*
table gave it to `agent.resume` ("Manage Sessions") on 2026-09-19, after that survey. No
Ctrl+Shift+letter remains: D/P/Y are claimed in preset tables, C/V are terminal copy/paste, Q
"quits other terminals" and U is the input method's Unicode entry (the reasons recorded beside
`ssh.connect` in Keymap.h). Ctrl+Shift+1 is free in the default table and all four presets, acts
while a program owns the terminal (`actsInsidePrograms`: Ctrl+Shift chords pass under
`program_keys: "shift-only"`), and the digit counts the walk — 1 the newest, again for the 2nd.
Owner suggested F1; `docs/F-KEYS.md` keeps F1 the shortcuts list and F-keys for toggles.

## Verification (2026-09-20)

- `scripts/relay-build` — green (`built in 43s`), after one fix (`board::Card` →
  `relay::board::Card` in the RelayWindow lambda).
- `ctest --test-dir build -R notifications` — `1/1 Test #35: notifications .... Passed` (includes
  the new `markSeenOneEntry` case).
- `PYTHONPATH=backend python3 tests/test_keybindings.py` — `Ran 19 tests ... OK` (the registry
  parser accepts `notifications.jump`; no collision with `help.shortcuts`; id is
  worker-acceptable). pytest is not installed on this machine; the file is unittest-based.
- Xvfb smoke: `build/relay` under `:9914` with isolated `XDG_{CONFIG,DATA,STATE}_HOME`, 10 s —
  starts, no `gui_crash`, no keymap-conflict lines in `relay.log` (`logs/` here).

## For QA to check live

1. Plan a card from another pane (a board pane open somewhere, the active pane a terminal): when
   the Plan turn ends, the bell badge gains one entry ("Plan ready: #ID", body the card title) and
   the active pane does not move. A failed plan posts "Plan failed: #ID". A cancelled plan and
   discuss turns post nothing.
2. Click the entry in the popup: the board opens on that card (in the window already showing that
   board, if any; a fresh pane beside the active leaf otherwise) and the "Next time: Ctrl+Shift+1"
   hint shows once.
3. Ctrl+Shift+1 jumps to the newest notification's pane/card and marks only that entry seen;
   pressing again within ~4 s walks to the 2nd newest; after a pause the next press is back at the
   newest. With nothing to jump to it says "No notifications."
