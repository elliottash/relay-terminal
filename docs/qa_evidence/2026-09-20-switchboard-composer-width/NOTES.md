# The Switchboard agent's prompt box fills the panel (79af8640, #8YQ9)

Owner, 2026-09-20: "the place where i type is that small thing at the bottom left. it should
fill the whole box, closer to the main terminal agent prompt box."

The composer shared one row with the model box, the microphone and Send. A `QHBoxLayout` hands
every widget its size hint before it distributes stretch, and the model box's hint is the row it
names, so at panel width the box to type in was squeezed down to a stub whose placeholder had
fallen back to "Ask…". It is now a row of its own with the chips on a strip under it, which is
the shape `src/Pane.h` gives a terminal pane's prompt box.

## Live run (implementer evidence, not a QA verdict)

`drive.sh` here: Xvfb, an isolated `HOME`/`XDG_*`/`TMPDIR` under a short path, `RELAY_KEYRING=off`,
a one-card fixture board, Ctrl+Shift+S for the Switchboard. The binary is the one
`scripts/land.py commit` built from the exact tree it landed
(`/tmp/claude-1000/land/boardcomposer/verify/build/relay`), so the shots are the committed code
and not this shared checkout's other in-flight edits — `build/relay` would not compile at the
time, on another session's half-written `SettingsPane::Options`.

- `01-switchboard.png` — the panel at rest beside a terminal pane. The box spans the panel and
  shows the longest placeholder, "Ask about the board — Enter sends, a second prompt queues";
  under it, right-aligned, `Follow Main — relay-main`, the microphone and Send. The left pane's
  prompt box has the same two-row shape.
- `02-typed.png` — typing into it (xdotool reached the window a few characters late; the box had
  focus from "rds duplicate each other?" on).

Covered by `tests/boardmodel_test.cpp`
`theModelBoxSitsInTheComposerRowWithTheMicAndTheContextChip`, which now asserts both rows and
their order and that the box is more than three quarters of the panel wide once shown.
