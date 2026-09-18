# Switchboard UX pass: implementer evidence (2026-09-17)

The owner's request: the Switchboard feels clunky; make it smooth before human QA. What changed is
summarised in `docs/SWITCHBOARD-DESIGN.md` section 4.5 (as built) and protocol section 19.1/19.3.

## How this was run

`cmake --build build`, then `build/relay --engine=relay --fresh` under **Xvfb `:171`**
(1700x1000, no window manager), window 1600x950, with its own `XDG_CONFIG_HOME` / `XDG_DATA_HOME` /
`XDG_RUNTIME_DIR` and `RELAY_KEYRING=off`, so no provider key was used (which is also how the
"Loading forever" bug showed up). Driven with `xdotool`, captured with `import -window <id>`. The
workspace is a throwaway git repo holding a **copy** of this repository's `issues/` tree (95 cards);
the repository's own cards were not touched.

`after-02-dragging.png` is a root-window grab: the drag image is its own top-level window, and a
window-id grab shows a black box where it is.

## Before (the code as it was)

| File | Shows |
|---|---|
| `before-01-stuck-loading-without-a-key` | Ctrl+Shift+S in a window with no provider key: "Loading the Switchboard…" pinned to the bottom, forever. The worker's `configure` failed on the key before it set up the board, and the pane only asked for the board after `configured`. |
| `before-02-board` | The board once loaded: the pane got a third of the window (the terminal header's long path label set a ~1000 px minimum), cards were bare text rows, columns cut off, tabs fighting the filter for one row, an unstyled two-line problems message. |
| `before-03-card-detail` | Enter: the detail squeezed the columns to a 60 px sliver; title said twice (header and H1 at browser size); a checkbox list that refused clicks; three nested scroll areas; labels unstyled (see "root cause" below). |
| `before-04-strip-chips` | The strip under the prompt box: the Switchboard and tasks chips 2 px shorter than the text chips beside them; the board glyph's columns run together at 14 px. |

## After

| File | Shows |
|---|---|
| `after-01-board-open` | Opens at half the window, loads without a key, the first card selected so the keys work at once. Two-row chrome, one-line problem linking to its file, painted cards, engraved column headers, key line. |
| `after-02-dragging` | A card being dragged: the drag image is the card; the target column tints and a line shows where it lands. |
| `after-03-moved-with-undo` | After the drop: the card is in Discussing and still selected; a floating notice says what moved, with Undo (Ctrl+Z). |
| `after-04-refused-move-explained` | Alt+Shift+Right four times: the card and the focus followed it column by column; the move into Needs QA was refused and the worker's reason is shown (before: nothing happened). Status badge "review" in the Waiting column. |
| `after-05-card-stacked-thread` | The card in a half-width pane takes the whole pane. One document: body, then the thread (events as muted lines, a comment made with Ctrl+Shift+Enter, a question). The question could not reach a model (no key): said on the card, the question kept. |
| `after-06-quick-add-burst` | `n`: the field opens in the focused column, stays open after Enter; the first card is already in. |
| `after-07-empty-tab` | An empty tab says so once. |
| `after-08-card-beside-columns` | Full width (pane moved to its own tab): the card sits beside the columns and follows the selection. |
| `after-09-bugs-tab-full-width` | Ctrl+PgDn to Bugs: the Needs QA column's cards measured and painted at the same width (they overlapped before the scrollbar fix). |
| `after-10-strip-chips` | Every strip chip 25 px tall; the new board glyph. |
| `after-11-light-theme` | Relay Light: the cards come from the theme tokens, so they follow the theme. |

## Root causes worth knowing

- `theme::polishWindow()` renames **every selectable `QLabel`** in a pane to `cwd`
  (`src/Theme.cpp`, the `TextSelectableByMouse` branch). That silently took the card detail's
  labels out of their stylesheet rules, and it does the same to the terminal header's `paneCwd`
  label today, so the `QLabel#paneCwd` rule never applies. The Switchboard avoids it (its labels
  are not selectable); the hook itself was left alone because changing it restyles the terminal
  header.
- QListView re-measures rows when the *list* is resized, not when its viewport narrows for a
  scrollbar. Cards measured from the viewport overlapped once a column grew a scrollbar; they are
  now measured from the list with the scrollbar's room always kept.

## Automated

- `tests/boardmodel_test.cpp`: 8 new cases (badges, title stripping, entry age, drop placement, a
  refused write shown and an accepted one undone, in-place refill, the open card re-reading only
  for its own changes, an ask failure reported on the card). 24/24.
- `tests/test_board_protocol.py`: the real worker answers `board_open` after a keyless `configure`.
- `ctest` in `build/`: all groups pass.
