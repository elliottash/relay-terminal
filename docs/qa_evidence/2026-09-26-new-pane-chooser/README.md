# Ctrl+Alt+E: a new pane, and you choose what it is (#83YV)

Owner, 2026-09-26: "how about, ctrl+alt+E is new pane but you choose what it is".

`pane.newChooser` (Ctrl+Alt+E, the key #78BN freed) opens the Actions palette on a "New pane"
list: Shell, Python console, Stata console, File…, plus Local shell when the focused pane is on
a host. Typing filters the list and Enter runs the row's own action. Esc, or Backspace in the
empty box, closes the palette (`ActionPalette::openSubmenu`). The same list is "New pane…" in
the root palette. Making a Python or Stata console the slow way (its palette row, the pane menu)
shows the hint "Next time: Ctrl+Alt+E".

`drive-new-pane-chooser.sh` runs under Xvfb with an isolated profile, against the verify-slot
build of the landed tree:

- `01-chooser.png`: Ctrl+Alt+E on a fresh window, with the list open.
- `02-filtered.png`: `py` leaves "Python console".
- `03-python-console.png`: Enter made a Python console to the right (Jupyter console 6.6.3,
  "Python · ipython" chip). The profile had no Jupyter, so the first console also built Relay's
  managed venv.
- `04-esc-closed.png`: Ctrl+Alt+E, then Esc, closes the chooser.

Tests: `tests/actionpalette_test.cpp` `openSubmenuStartsInsideItAndEscCloses`, and
`tests/keymap_test.cpp` `newPaneChooserOwnsCtrlAltE`, which checks every preset and that nothing
conflicts.
