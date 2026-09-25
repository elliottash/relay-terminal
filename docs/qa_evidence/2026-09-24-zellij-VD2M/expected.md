# Expected (sealed — the card names none of this)

After `stage.sh` (machine pass, Xvfb):

1. Relay opens with one pane, and that pane is a zellij (`--session tryit`): its first-run
   tips overlay ("Zellij Tip") fills the pane until ESC. `zellij list-sessions` (outside
   Relay) shows `tryit`. A screenshot shows the tips overlay inside the Relay pane.
2. `palette.open` then typing "persistent" into `actionPaletteSearch` filters the Actions
   list to one new item: "Connect to host (persistent)…" with an mosh/zellij/tmux detail
   line mentioning surviving disconnects and Relay restarts.
3. Running that item opens the `sshHostPicker` dialog titled "Connect to SSH (persistent)"
   (placeholder mentioning that work survives disconnects), listing the seeded `filly-test`
   host. Cancel closes it; nothing connects.
4. `pane.focusLeft` still binds both "Alt+Left" and "Shift+Alt+Left"; the keymap test
   `programKeysGivePlainAltArrowsToTheProgram` encodes the give-way.

For the person (their display): zellij's Alt+arrows must move zellij's own panes (Alt+n makes
one) while Relay's pane focus inside the zellij pane answers Shift+Alt+arrows; the tips
overlay is dismissed by one ESC; the persistent picker reads sanely.
