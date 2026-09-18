# Switchboard: columns become one sectioned list of rows (2026-09-18)

The owner's request, in two steps. First: with ~96 cards the Trello columns ran off the right edge
and wasted the vertical space, and a single-owner tracker reads better as rows — so make it one
scrolling list with a collapsible section per status. Then: **no tabs at all**, one view of
everything that is not done; `bug` and `feature` are labels like any other; done is a status with a
folded section at the bottom, not a place.

What was built is `docs/SWITCHBOARD-DESIGN.md` section 4.6 (which supersedes 4.2's columns, 4.4's
column keys and the tab row in section 3). `issues/board.yaml` and
`docs/SWITCHBOARD-FORMAT.md` were not touched: the config's `tabs:` are now only the category
folders a card's *file* lives in, and its `columns:`/`column_statuses:` are the sections.

## How this was run

`cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo && cmake --build build`, then
`build/relay --engine=relay --fresh` under this session's own **Xvfb `:182`** (1700x1000, no window
manager), window 1600x950 unless a shot says otherwise, with its own `XDG_CONFIG_HOME` /
`XDG_DATA_HOME` / `XDG_CACHE_HOME` / `XDG_RUNTIME_DIR` and `RELAY_KEYRING=off`, so no provider key
was used. Driven with `xdotool`, captured with `import -window <id>`. The workspace is a throwaway
git repo holding a **copy** of this repository's `issues/` tree (103 files, 92 open cards at the
start); the repository's own cards were never touched. The cards created and moved in these shots
(`#HFH3`, `#PAQC`) live only in that copy.

`09-dragging-the-landing-line` and `11-dragging-onto-a-section-header` are root-window grabs: the
drag image is its own top-level window, and a window-id grab shows a black box where it is.

## The shots

| File | Shows |
|---|---|
| `01-one-list-sections-and-counts` | The pane on open: one list, a header per status with its count, a status glyph per row, the title then `#ID`, and labels / `waiting:` / `☑ 0/7` / `✎ n` / age right-aligned. Done folded at the bottom (below Needs QA · 77). `92 open` beside the filter and in the pane's title. No tab row. |
| `02-left-folds-a-section` | Left folds Ready (`▸ READY 12`, count kept) and the selection steps to the nearest card still on screen (`#W5N2`). Right puts it back. |
| `03-filter-one-word` | `voice`: one match, its section shown, every other section out of the way, `1 shown`. |
| `04-filter-status-done` | `status:done`: the folded Done section unfolds itself while a filter is active, and the closed cards are there with `✓` and muted titles. Done is reachable without a tab. |
| `05-filter-folder-token` | `folder:changes` reaches the cards whose files live in `issues/changes/` (`folder:bugs`, the board.yaml id that names it, works too). |
| `06-filter-no-match` | "No card matches this filter. Esc clears it.", `0 shown`. |
| `07-quick-add-names-the-section` | `n` opens a field over the list that names the section the selection is in ("New card in Ready — Enter adds, Esc closes"). |
| `08-quick-add-created-and-still-open` | Enter created `#HFH3` in Ready, selected it, moved the count 93 → 94, and left the field open for the next card. Notice: "Created #HFH3 · Undo". |
| `09-dragging-the-landing-line` | A row dragged between two rows: the drag image is the row on the board's ground, and a 2 px accent line shows where it lands. |
| `10-dropped-with-undo` | The drop wrote the move: `#XZZB` is in Ready exactly where the line was, still selected, Inbox 1 → 0 and Ready 12 → 13, with "Moved #XZZB to Ready · Undo" floating over the bottom (and the "Next time: Alt+Shift+Arrows" hint). |
| `11-dragging-onto-a-section-header` | The same card aimed at the In progress header: the whole header lights up, and the card lands at the top of that section. (Behind it, "Undone: #XZZB is back as it was." from Ctrl+Z.) |
| `12-alt-shift-reorder` | Alt+Shift+Down twice: `#GDWE` moved two places down inside Ready, kept the selection and the focus, "Reordered #GDWE · Undo". |
| `13-refused-move-explained` | Alt+Shift+Right along the statuses; the move into Needs QA was refused and the worker's reason is shown ("Moving a card into a QA lane needs `evidence`…"). The card stayed in Waiting with the `review` badge and the `◐` glyph, and the selection followed it there. |
| `14-card-beside-the-list` | The board pane zoomed to the window: the card detail sits beside the list, which keeps its sections and its selected row. Board keys at the bottom. |
| `15-card-takes-a-narrow-pane` | A 1000 px window: below ~900 px of pane the card takes the whole pane and the key line switches to the card's keys (Esc goes back). |
| `16-rows-at-390px-badges-drop` | A 760 px window (≈390 px of list): titles elide, `#ID` stays, and the badges drop in order — the labels and most of the ages are gone, `waiting: o…` and `LLM QA` survive. Section headers keep their counts. |
| `17-relay-light` | Relay Light. The rows are painted from the theme tokens, so a theme switch carries them. |
| `18-relay-light-folded` | Relay Light with Ready folded. |

## Contrast, measured off the shots

WCAG relative luminance, sampled from the PNGs (`17-relay-light` / `01-one-list-…`):

| Pair | Relay Light | Relay Dark |
|---|---|---|
| A card title on the selected row's band | 13.2:1 | 11.9:1 |
| A section header's engraved label on the page | 5.7:1 | 6.0:1 |

## Worth knowing

- **A card whose `assignee` holds a sentence.** 77 of this repo's cards carry
  `assignee: implemented by Claude Opus 5 (Claude Code session), 2026-09-17` — a field misuse by
  an earlier agent, not a UI bug. The first pass let that badge eat the whole row. Rows now owe
  the title 45% of the width (80–280 px) before a badge may have anything, and no single badge may
  take more than a quarter of the row; the badge elides inside its pill. The cards themselves were
  left alone: they are the owner's tracker.
- **The worker's row was incomplete.** `board_tools._row` never sent `created`, `tasks_done`,
  `tasks_total`, `milestone`, `topic` or `implemented_by`, although protocol 19.2 promises them, so
  the age and `☑ done/total` badges had nothing to draw in the real app (only in tests, which build
  rows by hand). It now sends the full row; the backend tests still pass.
- **A dangling reference, caught by a test.** `BoardView::toggleSection` took a `const QString &`
  that every caller bound to a section id inside `m_rows` — which the rebuild it triggers replaces.
  It takes the id by value now. `boardmodel_test` segfaulted on it before the fix.
- **A "today" everywhere.** Every card in this repo was created on 2026-09-17, so the age badge
  reads `today` on all of them. `cardAge` is unit-tested across days, weeks, months and years.
- **Not built:** the unread dot, `e` edit, `l`/`a`, `?`, Shift+Enter "own pane", thinking/tool
  collapse in the thread and tickable tasks are still missing (as 4.5 already said). Dropping a
  card on a tab is gone with the tabs.
