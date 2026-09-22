# #PNAV implementer evidence

Built Relay through `scripts/relay-build` as build `2026-09-21.21H.06`.

Targeted tests passed:

- `panetabnavigation`: focused helper behavior, nesting, wraparound, unavailable tabs and reserved widgets.
- `modelspane`: registered integration and reserved Alt+0 through Alt+9.
- `conversations`: Projects/Sessions/Globals integration remained green.

`drive.py` launched the built app under isolated Xvfb and drove only keyboard input after opening
the pane. Captures `01`–`04` show forward, reverse and wraparound selection across
Projects/Sessions/Globals. `05` shows that a writable Globals editor retained Tab. `06` opens the
Models Jobs tab using Tab; `07` shows Alt+3 leaves it unchanged. `08` shows Shift+Tab still turns
on Plan mode in the console.

