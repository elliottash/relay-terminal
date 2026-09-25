# #TBRH — the Live strip on the Board's Cards tab (implementer evidence)

- `01-cards-tab-live-strip.png`: `BoardView` offscreen (`QT_QPA_PLATFORM=offscreen`, a throwaway
  `XDG_CONFIG_HOME`), grabbed by `BoardPaneTests::theLiveStripListsThisProjectsPanesAndTheirCards`
  with `RELAY_QA_SCREENSHOT` set. It shows two panes attached to the project. The first, `c522363d`,
  runs `claude-opus-5-5` with a turn in flight (✦) and holds `#K7Q2`. The second, `af0737e5`, is idle
  and holds nothing. A done card and a card whose `session` names no open pane draw no chip.
- `tests.txt`: the `boardpane` suite (20 cases, all pass), built by `land.py try` on the exact
  tree this card lands: tip plus only #TBRH's hunks. `land.py try --target relay` also built the
  `RelayWindow.h` wiring there.

Not captured: the strip in a live Relay window with real panes. The window side is the
`livePanes` lambda in `RelayWindow.h`, the same walk `feedProjects` does for the Projects page.
The card's `## Human QA` asks a person to check it once.
