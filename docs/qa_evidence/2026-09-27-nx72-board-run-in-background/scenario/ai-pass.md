# The AI's mechanical pass — #NX72

Binary: scratch export of the working tree (fix applied; `relay` target built clean, exit 0).
Driver: `scripts/relay-drive` on a disposable Xvfb display and HOME (see `stage.sh`).
Fixture: `.board/` with one card TRY1, no plan (Run arms a confirmation on first press,
so the pass presses twice — that is the card page's normal flow, not the bug).

Measured results (screenshots and JSON in this directory):

| moment | pixels changed vs card-open | foreground panes |
|---|---|---|
| 0 ms after handover | 108,668 (confirmation strip toggling off, claim posting, toast) | 1 |
| 300 ms | 9,103 | 1 |
| 1500 ms | 78,479 (thread lines, toast, column move rendering) | 1 |

- `08-panes-after.json`: exactly one foreground pane, the board, cwd = staged workspace.
- `06-sections-after.json`: "owner claimed this card · assignee agent, Inbox → Executing,
  session f4ad8a48 · just now" and "Claimed (f4ad8a48) · working on it from a terminal pane".
- `07-notice.json`: "Claimed #TRY1 · Run".
- No sampled moment shows a right-half terminal split; a split pane would also have made
  `panes` list two foreground panes — it never did. The staged card file records
  `status: executing`, `assignee: agent`.

The changed-region bounding boxes are wide, short bands in the card page (text re-render);
a pane split would be a full-height right half (~800 px wide, full 1040 px tall) and never
appeared. The pre-fix behavior is described (sealed) in `expected.md`.
