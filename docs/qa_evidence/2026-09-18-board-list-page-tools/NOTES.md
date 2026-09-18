# The board's tools are the top of its list page (2026-09-18)

Implementer evidence for `#EVW1` (the filter and **+ New card** move out of the pane's header;
an open card puts **← Back to board** there instead) and `#T7BQ` (a checkbox per section, and
room for **Clean up**). Design: `docs/SWITCHBOARD-DESIGN.md` section 4.7.

These are the implementer's shots, not a QA verdict.

## How this was run

`cmake -S . -B <build> && cmake --build <build>`, then `build/relay --workspace <copy>` under this
session's own **Xvfb `:140`** (1700x1000, no window manager), window 1600x950 unless a shot says
otherwise, with its own `XDG_CONFIG_HOME` / `XDG_DATA_HOME` / `XDG_CACHE_HOME` /
`XDG_RUNTIME_DIR` and `RELAY_KEYRING=off`, so no provider key was used. Driven with `xdotool`,
captured with `import -window <id>`. `drive.sh` next to this file repeats it.

The workspace is a throwaway git repo holding a **copy** of this repository's `issues/` tree
(138 card files, 125 open at the time); the repository's own cards were never touched and nothing
in these shots was written back.

The pointer is parked in the title bar for every capture: left over a row it raises a tooltip,
which is its own override-redirect X window and comes out as a black box in a window grab.

## The shots

| File | Shows |
|---|---|
| `implementer-01-list-page-tools-wide` | The pane on open. Its first row is the list page's own: `125 open`, the filter box, **+ New card**, **Clean up**, with the pane's hover buttons keeping their room at the right of it. Under that, one ticked checkbox per section (INBOX … DONE) in the same uppercase mono as the section headers, then the problems line, then the rows. There is no separate header strip above any of it. |
| `implementer-02-a-section-unticked` | READY unticked: its header and all 14 of its cards are off the page, its box has gone grey, and the count reads `111 of 125 open` — the second number does not move, so nothing pretends the board shrank. |
| `implementer-03-clean-up-not-wired-yet` | **Clean up** clicked: "Board cleanup is not wired yet." on the board's own notice and in the window's status bar. The backend message does not exist yet (another session's work); `BoardView::requestCleanup()` is the one place to wire. |
| `implementer-04-open-card-back-to-board` | A card open in a 790 px pane, which stacks: the list and its tools are gone and the pane's header says **← Back to board** and nothing else. The hover buttons sit clear of it. |
| `implementer-05-wide-pane-card-beside-the-list` | The same card with the board in a tab of its own (1600 px): the card sits beside the list, and the list keeps its own tools and checkboxes, because they are the list's and not the window's. The header still carries the way back. |
| `implementer-06-narrow-pane-wrapping` | A ~415 px pane. The hover buttons take their room out of the top row for good, so **+ New card** and **Clean up** drop to a line of their own rather than eliding to a pair of identical "…", and the seven checkboxes wrap onto two lines. Nothing is clipped and the rows below are untouched. |
| `implementer-07-narrow-pane-open-card` | A card open in that same ~415 px pane: the whole list page including its tools is away, and the header is the way back. |

## Not shown

- **The unticked set surviving a restart.** `BoardView::hiddenSections()` /
  `setHiddenSections()` exist and mirror the folded set exactly, but the two lines in
  `src/main.cpp` that put them in the layout node were not written: another session is in that
  file. `#T7BQ`'s "Known gaps" has the exact change.
- **A shortcut hint for ← Back to board.** Clicking it calls `onHint("board.back", "Esc")`, which
  `tests/boardmodel_test.cpp` checks; the hint itself only appears when hints are on and the
  per-hint show limit has not been reached, so it is not in a shot.
