# Window header and notification centre — implementer run, 2026-09-17

Relay's own title bar (no OS decorations) and the bell behind it. Run by the implementing Claude
session under Xvfb `:91` (1400x900), Qt5/KF5 build, `xdotool` for input. **There is no window
manager on that display**, so `startSystemMove` / `startSystemResize` find no WM and Relay's own
fallback path does the moving and resizing; on a real desktop the WM takes the drag instead (that
path is untested here). `showMaximized()` likewise only sets the state — the geometry stays put
without a WM, but the header's padding and glyph follow the state correctly.

| File | Shows |
|---|---|
| `implementer-header-default.png` | Default header: Relay icon left, tabs, then bell · gear · minimize · maximize · close. No OS title bar. |
| `implementer-bell-empty.png` | The bell's popup with nothing in it. |
| `implementer-notification-entry.png` | After `sleep 32` in the pane: "Command finished · Exit 0 after 32 s in …", green (success) dot, relative time, dismiss, "Clear all". The badge cleared when the popup opened. |
| `implementer-maximized.png` | Maximize button pressed: window padding gone, glyph switched to "restore". |
| `implementer-restored.png` | Double-click on the empty tab row restored it: padding and the square glyph are back. |
| `implementer-system-title-bar.png` | `window/native_frame=true`: minimize/maximize/close leave the header (the desktop draws them); the bell and gear stay. |
| `implementer-palette-entries.png` | The palette (Ctrl+Shift+A, "notif"): "Notifications…", "Desktop notifications" (on), "System title bar" (off). |

Measured with `xdotool getwindowgeometry` during the run:

- Drag the empty tab row 100,100 → window moved exactly 100,100 (0,0 → 100,100).
- Drag the right edge 200 px left → 1260x810 → 1060x810.
- Drag the left edge 100 px right → position 100 → 200, width 1060 → 960 (origin moves, min width respected).
- The close button closed the window (single pane, nothing busy, so no confirmation — same rule as before).
- A finished agent turn in the pane the user was watching posted **no** notification, as intended.

Not covered here: minimize (needs a WM to restore from), WM-driven move/resize/snapping, multi-window
badge counts, and "click a notification to go back to its pane" across windows.
