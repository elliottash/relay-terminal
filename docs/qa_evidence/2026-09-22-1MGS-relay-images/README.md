# #1MGS, relay side: inline images in a pane (2026-09-22)

`drive.sh` runs `build/relay` under Xvfb with an isolated profile and a loopback stub provider
(`stub-provider.py`) whose reply holds Markdown images, streamed in 7-character deltas.

- `reply.png`: `![the chart](chart.png)` (600x300 px, relative to the pane's cwd) is drawn as
  80 x 20 cells under its alt line. `![wide](~/project/wide.png)` mid-line ends the line before
  it, draws 1600x200 px as 80 x 5 cells, and the rest of the line starts on a row of its own. The
  `[a link](notes.md)` after it is still a link. `![logo](https://…)` is a link, never
  fetched. `![gone](nope.png)` is its alt text and the path. No escape body shows in the grid.
- `attachment.png`: `what does @chart.png show` prints a 6-row thumbnail two columns in under the
  prompt's echo. The stub is no vision model, so that turn is refused before the provider (the
  red lines): that refusal is Relay's own and not part of this card.

The engine side (kitty `t=f` in session/ImageProtocol) was already in the build that drew these.
