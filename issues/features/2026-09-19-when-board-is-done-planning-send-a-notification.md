---
id: NQP9
type: work
status: needs-verification
assignee: agent
priority: 2
rank: zzzzzzzzzz
created: '2026-09-19'
links: {plans: [], commits: [], evidence: [docs/qa_evidence/2026-09-20-board-plan-notification], related: [], github: null}
---
# when board is done planning, send a notification, but dont instantly move the ac…

## Issue
when board is done planning, send a notification, but dont instantly move the active pane there.

do we have a hotkey to,  move to most recent notification? if not we should add that, maybe f1 for example.  and you can press f1 again to move to the 2nd most recent notification, etc.

## Plan
**Goal.** When a Switchboard card's Plan turn ends, post an entry to the notification centre (bell) instead of making the user watch the board — and never move the active pane on its own. Add a hotkey that jumps to the most recent notification's pane; pressing it again walks to the 2nd most recent, and so on.

**Findings.**
- Card turn end, board side: `BoardView::handleEvent` (`src/BoardPane.cpp:2912`) routes per-card turn events; the terminal branch (`done`/`error`/`cancelled`, ~`src/BoardPane.cpp:3190-3200`) does `m_cardTurns.remove(card)`, `m_detail->setBusy(false)` and `showNotice("#%1: %2")`. **Today nothing posts a notification and nothing moves the active pane** when a plan finishes — the "don't instantly move" requirement is satisfied by posting only, but it means the user currently has no way to know a plan finished from another pane. The turn's mode (`"discuss"`/`"plan"`) lives in `m_cardTurns[card].mode` (`src/BoardPane.h`, `struct CardTurn`).
- Terminal panes already do this right: `Pane::notify` (`src/Pane.h:9705`) posts to `NotificationCenter` (`src/Notifications.h`) with the pane's session token as `source`; posting never touches focus, and desktop notify-send only fires when the window is inactive.
- The bell popup (`NotificationsPopup`, `src/WindowChrome.h:243`) opens a clicked entry through `onOpenSource` → `m_manager->focusPane(token)` (`src/RelayWindow.h:5902`), but `WindowManager::focusPane`/`findPaneByToken` (`src/WindowManagerImpl.h`, `src/RelayWindow.h:539`) only match **terminal panes by session token** — a board pane (`ToolPane`, `src/PaneChrome.h:385`) has no token, so board notifications need a namespaced source routed separately.
- Going to a card already exists: `RelayWindow::openBoardCard(id)` (`src/RelayWindow.h:4427-4530`) opens the Switchboard if needed, retries `openSelected` every 250 ms for 6 s (~line 4447), then `setActiveLeaf(tool)`. `createBoardPane(workspace, …)` (line 4738) creates a board pane for a given workspace.
- No jump-to-notification hotkey exists. **F1 is taken** by `help.shortcuts` (`src/Keymap.h:365`), and `docs/F-KEYS.md` settles it: F1 stays help, and F-keys toggle surfaces while Ctrl chords act — jumping to a pane is an act, so it belongs on a Ctrl+Shift chord (which also works while a program owns the terminal under `program_keys: "shift-only"`, `Keymap::actsInsidePrograms`). Ctrl+Shift+M is free in all four preset tables (`src/Keymap.h` presetJson).
- Keymap actions dispatch in `RelayWindow` (`src/RelayWindow.h` ~1076-1102, the `id ==` chain). Shortcut hints go through the registry (`relay::ShortcutHints`, `src/Hints.h`; convention in `docs/ARCHITECTURE.md` "Shortcut hints") — the WARP.md standing rule requires one for the new key.

**Steps.**
1. `src/Notifications.h/.cpp`: add `markSeen(const QString &id)` (mark one entry seen, emit `changed`) alongside `markAllSeen`.
2. `src/BoardPane.h`: add `std::function<void(const QString &id, const QString &mode, const QString &outcome)> onTurnEnded` to `BoardView` (same wiring style as `onExecuteCard`). In `BoardView::handleEvent`'s terminal-event branch, before `m_cardTurns.remove(card)`, capture the turn's mode and call it. Cleanup events (`cleanup: true`) are excluded — they already have their own panel.
3. `src/RelayWindow.h` `createBoardPane`: wire `onTurnEnded` to post to the centre — mode `"plan"` + outcome `done` → `"Plan ready: #<id>"` with the card title as body, `kindSuccess`; outcome `error` → `"Plan failed: #<id>"`, `kindError`; `cancelled` and `discuss` turns post nothing. Source: `board:<workspace>#<id>`. No focus calls anywhere on this path — that is the "don't move the active pane" half of the card.
4. `src/RelayWindow.h`: add `openNotificationSource(const QString &source)` and use it in the `onOpenSource` lambda (line 5902). A `board:` source parses workspace + card id: walk the windows for an open `BoardView` on that workspace and reveal it (`setActiveLeaf`/`focusLeaf`, then `openSelected` via the 250 ms/6 s retry helper); if none is open, `createBoardPane(workspace)` in the current window and select the card the same way. Anything else delegates to `m_manager->focusPane(token)` as today.
5. `src/Keymap.h`: `add("notifications.jump", "window", "Go to the newest notification's pane (again for the next older)", {QStringLiteral("Ctrl+Shift+M")})`. No preset-table entries needed; all four inherit the default.
6. `src/RelayWindow.h`: dispatch `notifications.jump` in the action chain. Cycle state: `int m_notificationJumpIndex = 0` and a single-shot `QTimer m_notificationJumpReset` (~4 s). On trigger: entries newest-first (`NotificationCenter::entries()`), skip entries with an empty source, take the one at the index (clamped; wrap to 0 past the end), `openNotificationSource(source)`, `markSeen(id)`, `++index`, restart the timer; on timeout reset the index to 0. Also reset the index to 0 on `NotificationCenter::changed` (a fresh post redefines "most recent"). Empty list → status-bar message "No notifications."
7. Shortcut hint (standing rule): when a notification row is clicked with the mouse — the slow path — show registry hint `notifications.jump.mouse` ("Next time: <live keymap text>") from the window's `onOpenSource` wiring via `ShortcutHints`/`hint()`.
8. Docs: update `docs/ARCHITECTURE.md` "Notification centre" (the jump action, cycling, and the `board:` source scheme).

**Risks.**
- **Owner decision — the key.** The card suggests F1, but F1 is the shortcuts list and `docs/F-KEYS.md` reserves F-keys for toggles ("F1 stays help"). Recommendation: **Ctrl+Shift+M** (M for message; free in every preset; acts while a program has the keyboard). Say the word if you'd rather rebind F1 or pick another chord.
- Cross-window: the centre is process-wide; a jump from window A to a card whose board is open in window B should raise B's board. Reuse `WindowManager`'s window walk; if no window shows that board, creating the pane in the current window is the documented fallback.
- Scope is Plan turns only; Discuss turns and cleanup runs stay silent (cleanup has its panel). Extending the same `onTurnEnded` hook later is a one-liner.

**Verify.**
- Build with `scripts/relay-build` (never bare cmake).
- `ctest --test-dir build -R notifications` — extend `tests/notifications_test.cpp` with `markSeen(id)` cases (single entry seen, badge count drops, `changed` fires).
- `python3 -m pytest tests/test_keybindings.py` — new action parses, no conflicts in any preset.
- Live under Xvfb with an isolated `XDG_CONFIG_HOME`: Plan a card in the Switchboard from another pane → bell badge increments, active pane does **not** move; click the entry → the board opens on that card; Ctrl+Shift+M jumps to the newest notification, again within 4 s to the 2nd newest, after a pause back to the newest; the mouse click shows the "Next time" hint once.

## QA checklist
- [ ] Plan a card from another pane: the bell gains one entry, "Plan ready: #ID" with the card title as the body, and the active pane does not move
- [ ] A failed Plan posts "Plan failed: #ID" in the error colour; a cancelled Plan and discuss turns post nothing
- [ ] Clicking the entry opens the board on that card — the window already showing that board (raised) when there is one, otherwise a new board pane beside the active leaf — and the "Next time: Ctrl+Shift+1" hint shows once
- [ ] Ctrl+Shift+1 goes to the newest notification, again within ~4 s to the 2nd newest, after a pause back to the newest; only the entry landed on loses its unread dot
- [ ] The key works while a program owns the terminal (Ctrl+Shift chord under `program_keys: "shift-only"`), and with nothing jumpable it says "No notifications."
- [ ] `ctest --test-dir build -R notifications` and `PYTHONPATH=backend python3 tests/test_keybindings.py` still pass (implementer run: green, 2026-09-20, `docs/qa_evidence/2026-09-20-board-plan-notification/`)
