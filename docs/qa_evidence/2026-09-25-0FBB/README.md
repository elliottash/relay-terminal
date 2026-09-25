# #0FBB evidence — claims chip before the pane title, `[n]` badge gone

Implementer's evidence (Claude, Fable 5.1 via Claude Code), 2026-09-25, from the shared checkout
build `build/relay` with the card's changes in the tree. A separate session verifies.

| Check | Command | Result |
|---|---|---|
| Chip logic | `ctest --test-dir build -R '^panes$'` | Passed |
| `Model::claimedBy` | `ctest --test-dir build -R '^board$'` | Passed |
| Theme rule | `ctest --test-dir build -R '^themeswitch$'` | Passed |
| Staged GUI | `machine-pass.sh` (Xvfb, throwaway HOME and project) | screenshots below |

`stage.sh` opens Relay on a throwaway project whose board holds three cards, then writes the one
pane's session token into two of them (K7Q2 last, so it is the latest). `machine-pass.sh` runs it
under an Xvfb and drives the pane: `open #K7Q2` attaches the tab to the project through the Board,
a first prompt configures the worker (the sandbox has no Claude login, so the turn errors, which is
fine), and ` #` in the prompt box loads the card index — the claims chip appears at that moment.

- `01-header-chip.png` — the header reads `#K7Q2 (2)` immediately before the title, and no `[n]`.
- `02-chip-menu.png` — a click on the chip lists both claims, latest first, with title and stage.
- `03-narrow-pane.png` — beside the Board, the chip stays whole and the title wraps to two lines.
- `04-full-window-menu.png` — the whole window behind 02.

Not shown in a picture: Tab focus and Enter/Space on the chip (a `QToolButton` with `TabFocus`;
Enter is handled in `ClaimsChip::keyPressEvent`), and the accessible name — read them in
`src/PaneUi.cpp` and `relay::panes::claimChipAccessibleName`.
