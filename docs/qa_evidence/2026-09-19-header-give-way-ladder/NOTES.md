# The pane header's give-way ladder — implementer evidence (2026-09-19)

Owner's decision of 2026-09-19: the order in which a narrowing pane header gives way. The ladder is
`relay::panes::headerFit()` in `src/PaneLayout.{h,cpp}`, applied by `Pane::updateHeader()` through
`PaneChrome::measureHeader()` / `PaneChrome::applyHeaderFit()`; the rungs, the reverse path and the
"a function of the width alone" property are in `tests/panelayout_test.cpp` (`ctest -R '^panes$'`).

`drive.sh` is what took these: a private export of `main` plus this change, under Xvfb with an
isolated `HOME`, `XDG_*`, `TMPDIR` and `RELAY_KEYRING=off`. It takes control of the terminal
(Ctrl+H), runs `ssh … elliott@localhost sleep 300` so the state glyph, the state's word and the ssh
chip are all up, and then resizes the window to each width and crops the header row.

| Shot | Width | What the header shows |
| --- | --- | --- |
| `00-window-900.png` | 900 | the whole window, for context |
| `01-everything-on-900.png` | 900 | glyph · "Command running" · ⇄ elliott@localhost · title · directory |
| `02-directory-gone-640.png` | 640 | rung 1: the directory has gone; the title is still whole |
| `03-word-short-560.png` | 560 | rungs 2 and 3: the title is at its floor, the word is "Running" |
| `04-word-gone-480.png` | 480 | rung 3 finished: the glyph alone; the ssh chip is still whole |
| `05-ssh-host-only-420.png` | 420 | rung 4: the chip drops `elliott@` and keeps `localhost` |
| `06-ssh-host-elided-380.png` | 380 | rung 4's floor: `loc…` — a whole ellipsis, never half a letter |

Rung 5 (the usage meter collapsing to CPU alone) is not in these shots: the meter only appears when
the pane is costing something worth reading, and an idle `sleep` costs nothing. It is covered by
`andLastTheUsageChipKeepsOnlyTheCpu()` and by the chip's own `widthFor()`.

Before this change, at 380 px the *title* was the one elided ("relay-ter…") while the directory kept
its room ("…lay-terminal") — the order the owner reversed. The earlier baseline shots are at
`/tmp/claude-1000/r-gui2/hdr-*.png`.
