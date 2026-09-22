# Opening new panes evenly — implementer evidence

Card: #EQM2

Implemented behavior: each terminal-pane opening through `RelayWindow::splitToward` queues a
whole-tab equalization after Qt settles the new splitter. The lower-level `insertBeside` path is
unchanged, so moving or dragging an existing pane continues to preserve hand-adjusted sizes.

Automated verification:

- `scripts/relay-build --target relay-panes-tests` — passed.
- `ctest --test-dir build -R '^panes$' --output-on-failure` — 1/1 passed.
- `scripts/relay-build --target relay` — passed.

The regression case `openingAPaneEvenlyRedistributesTheSplitter` begins with deliberately uneven
pane widths, inserts a fourth pane, applies the opening-path equalization arithmetic, and verifies
that every resulting share is equal within Qt's rounding tolerance.

A separate verifier should visually resize panes unevenly, open another pane with the keyboard or
pane-header button, and confirm the full tab becomes even while a subsequent pane move does not
reset sizes.
