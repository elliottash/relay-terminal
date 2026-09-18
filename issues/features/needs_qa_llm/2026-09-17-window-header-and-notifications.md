---
id: HR4B
type: work
status: needs-qa-llm
labels: [feature]
component: [gui]
milestone: desktop-alpha
workstream: terminal
assignee: implemented by Claude Opus 5 (Relay agent session), 2026-09-17
rank: r2
created: '2026-09-17'
acceptance: '`docs/qa_evidence/2026-09-17-window-header/` (implementer run under Xvfb, no window manager); a non-Claude model QA session runs the checklist below on a real desktop and records it there'
source: 'user request, Relay agent session, 2026-09-17: "can we remove the OS window header? relay icon at top left, at the top right, notifications, settings, account, minimize, maximuze, close." then "(similar to warp)", "build the real notifications, settings, minimize, maximize. and actually we dont have accounts"'
links: {plans: [], commits: [], evidence: [], related: [], github: null}
---
# Relay draws its own title bar, with a notification centre behind a bell

## Request

Drop the OS window header. The tab row becomes the title bar: the Relay icon on the left, and on
the right the bell, settings, minimize, maximize and close — Warp's layout. No account button
(Relay has no accounts). The bell is a real notification centre; "settings" is the actions palette
that already exists (Ctrl+Shift+A), not a new dialog.

## Change

**Frameless window** (`src/main.cpp`, `RelayWindow`). `Qt::FramelessWindowHint` unless
`window/native_frame` is on. The window keeps 5 px of padding (`kFrameMargin`); a press inside it
resizes (`edgesAt`, `cursorForEdges`), a press on the empty tab row moves (`headerDrag`), and a
double-click there maximizes. Both hand the drag to the window manager first
(`startSystemMove` / `startSystemResize`) so snapping and tiling keep working, and fall back to
moving and resizing the window directly from the press point when no WM takes it. Maximizing drops
the padding and swaps the glyph to "restore" (`changeEvent` → `updateChromeState`).

**Header** (`buildWindowChrome`). Two `QTabWidget` corner widgets on the tab row: the Relay icon
left; bell, gear and — only when Relay draws the frame — minimize, maximize and close on the right.
`ChromeButton` paints each glyph with `QPainter` rather than using a font character, so a desktop
without an emoji font still gets a bell, and hover/close colours follow the theme. The close button
calls `close()`, so the "panes are busy" confirmation is unchanged.

**Notification centre** (`src/Notifications.{h,cpp}`, new `relay-notifications` library). One centre
per process, shared by every window: newest-first entries with a kind (info/success/warning/error),
an unread count for the bell badge, dismiss, "Clear all", and a 200-entry cap. `Pane::notifyIfAway`
became `Pane::notify`: the centre always keeps the entry, `notify-send` still only fires when Relay
is not the active window and `notifications/desktop` is on. Sources: command finished (>30 s), out
of memory, password prompt waiting, subagent finished, and — new — the main agent's turn finishing
or failing. A turn that finished in the pane the user was watching posts nothing (`Pane::watched`).
Clicking an entry goes back to the pane that posted it, in whatever window it now lives
(`WindowManager::focusPane` → `RelayWindow::revealPane`).

**Palette entries** (the settings surface, per the request): "Notifications…", "Desktop
notifications" and "System title bar" (`window/native_frame`, applies to windows opened after it).

## Test

`tests/notifications_test.cpp` (new ctest target `notifications`): order and unread counts, empty
titles ignored, mark-seen/remove/clear, the 200-entry cap dropping the oldest, the desktop setting
round-tripping, and `relativeTime` ("now", "2 min ago", "2 h ago", "Wed 09:15", "12 Aug 09:15").
`ctest --test-dir build` and `./scripts/test.sh` pass (13/13, 2026-09-17).

GUI behaviour was exercised under Xvfb with `xdotool`; see the evidence folder for what that run
covers and what it cannot (no window manager on that display).

## QA checklist

- [ ] On a real desktop (X11 and Wayland): the window opens with no OS title bar, and the header shows icon · tabs · bell · gear · minimize · maximize · close.
- [ ] Dragging the empty tab row moves the window and the desktop's snapping/tiling still works (drag to an edge).
- [ ] Double-click on the empty tab row maximizes and restores; the glyph and the window padding follow.
- [ ] Minimize, maximize and close all work; closing with a busy agent still asks first.
- [ ] Every window edge and corner resizes, with the right cursor, and the window cannot go below its minimum size.
- [ ] Dragging a tab still reorders it, the ⧉ detach button and + still work, and none of them move the window.
- [ ] The bell badge counts unseen notifications, clears when the popup opens, and the popup's "Clear all" and per-row dismiss work.
- [ ] A long command, a password prompt, a finished subagent and a failed agent turn each land in the bell; a turn finished in the pane you are watching does not.
- [ ] Clicking a notification switches to the pane that posted it, including when that pane is in another window.
- [ ] `notify-send` popups still appear only when Relay is not the active window, and "Desktop notifications" off stops them while the bell keeps the entry.
- [ ] Actions › System title bar on, then a new window: the system decorations are back, the header keeps the bell and gear, and dragging the tab row no longer moves the window.
