# Recently closed (card #RC7Z) — implementer's run, 2026-09-18

Xvfb, every XDG directory and TMPDIR isolated, `build/relay --clean-shell`, driven with xdotool.

| File | What it shows |
|---|---|
| `a-pane-reopened-in-place.png` | A split pane closed with Ctrl+W and reopened with Ctrl+Shift+Z: same slot, same width, its directory (`/usr`) and its text between the two rules. `closed.json` held one `pane` record (`sizes [624, 623]`, `slot 1`, mode 600) and its scrollback file survived the layout's prune. |
| `b-list-after-restart-unfolded.png` | After a SIGTERM quit and a plain restart: Sessions › Recently closed (opened with a test binding for `closed.list`) still lists the closed tab and the pane closed by typing `exit`, not the window the quit closed. Keyboard only: ↓ → unfolded both; the last lines of each pane's text are under it. |
| `c-tab-reopened-from-list.png` | Enter on the tab row: it reopened as a tab of the asking window (an item loaded from the file has no window of its own), with its text. |
| `d-close-window-names-the-real-key.png` | The close-window dialog names Ctrl+Shift+Z (it said Ctrl+Shift+W, which is *close pane*). |
| `e-renamed-tab-back-in-place.png` | `/rename-tab release`, close, Ctrl+Shift+Z: the tab is "release" again, at index 2 where it was. |
| `f-closed-window-reopened.png` | A second window closed while the first stayed open was recorded (`window`, geometry `[0, 0, 1260, 810]`) and reopened with its text. |
