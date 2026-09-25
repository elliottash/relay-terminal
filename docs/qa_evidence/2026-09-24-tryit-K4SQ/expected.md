# Expected (sealed until the person answers)

1. The staged window opens showing card **AA01** alone — the board's whole pane, **no list beside
   it** — reached by a card-link click (the app's `open` seam; the same `openBoardCard()` a `#ID`
   in chat or a notification calls). The board pane is ~1050px wide in the 2200px window, above
   `kCardSplitWidth` (900), so a by-hand open *would* split.
2. Clicking the **`#AA02` reference inside AA01's body** (an in-board link) swaps to AA02's page
   **still with no list** popping back in.
3. **Esc** once: the full card list comes back (the "Enter open / e edit / p plan…" key line).
4. Clicking a card row **by hand**: list and card side by side, the split that was there before.
