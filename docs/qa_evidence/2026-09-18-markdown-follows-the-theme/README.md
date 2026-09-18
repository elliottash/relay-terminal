# The agent's Markdown follows the theme, scrollback included (owner report, 2026-09-18)

> "some of the text in beige mode is too light and not readable … same for dark mode"
> "(for dark mode the text is too dark against the background)"

The owner's screenshot measured **#e2e5eb** for the unreadable paragraphs on IBM Beige's warm
paper — the near-white the renderer shipped with. Both halves of the report are one cause: the
renderer wrote *absolute* colours, and an absolute colour is burnt into the scrollback, so prose
written under one theme stays that colour after a switch. Beige kept dark-theme near-white; dark
kept beige's near-black.

The renderer's default palette now names the terminal's own colours — `39` (default foreground),
the faint attribute, and the ANSI indices — which the engine resolves from the active theme every
time it paints.

`sgr-check.sh` prints exactly those SGRs into a pane under each theme; `switch-check.sh` prints
them under IBM Beige and then runs `/dark`.

| | |
|---|---|
| `01-beige.png` | prose dark on paper, `code` ochre, heading purple, faint grey, link blue |
| `02-copper.png` | the same five, light on charcoal: amber, violet, grey, blue |
| `03-scrollback-before-switch.png` | printed under IBM Beige |
| `04-scrollback-after-dark.png` | after `/dark`, **the same scrollback lines** in Dark Copper |

Both palettes are the ones `tests/theme_test.cpp` measures for AA on the grid, so the agent's
prose now inherits that contract instead of carrying its own colours past it.
