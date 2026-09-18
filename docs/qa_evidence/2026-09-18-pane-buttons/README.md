# The pane's buttons stop reacting to the pointer (2026-09-18)

The owner's report: "permanent pane icons were added, but the responsive active overlay wasn't
removed." Both were true at once — the four buttons were permanent, and pointing at a pane still
grew the row with a drag grip and "new pane below" and lifted it onto a raised tile.

| Shot | What it shows |
|---|---|
| `01-pointer-on-the-pane.png` | Pointer inside the pane: four buttons, no tile. |
| `02-pointer-away.png` | Pointer outside the window: pixel-identical to 01. |
| `03-move-to-new-tab-no-crash.png` | New pane to the right, then move to a new tab, both clicked with the pointer on the pane. This is the path that used to recurse until the stack ran out, because hiding the hover row from inside its own `hide()` re-entered the handler. |

Driven under Xvfb :186 with an isolated profile, on a throwaway workspace; the repository's own
issues/ tree was never opened.
