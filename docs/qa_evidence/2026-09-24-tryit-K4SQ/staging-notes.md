# Staging notes — #K4SQ, card links open the board on the card alone

- **Binary**: `build/relay` from the relay-terminal checkout at commit `e0d642c8df0c`.
- **Project**: `/tmp/claude-1000/tryit/k4sq` — a git repo with a small board (`.board/`, cards
  `AA01` inbox, `AA02` planned, `AA03` done; `AA01`'s body references `#AA02`, which renders as a
  clickable in-board link).
- **Isolation**: Xvfb on display `:187` (2200x1200, so the Switchboard pane lands above
  `kCardSplitWidth` = 900 and a by-hand open really splits), sandboxed `$HOME`,
  `RELAY_KEYRING=off`, `--clean-shell --fresh`, no model and no network.
- **The link click**: `scripts/relay-drive open AA01` — the app's named `open` seam, which calls
  `RelayWindow::openBoardCard`, the same function a `#ID` clicked in chat or a notification
  click goes through. There is no model-free clickable `#ID` on screen (chat needs a turn), so the
  mechanical click is made through that seam and the person judges what it produced; the in-board
  `#AA02` reference *is* on screen and clickable by hand.
- **Restaging**: run `stage.sh` again; it stops the previous instance first.
