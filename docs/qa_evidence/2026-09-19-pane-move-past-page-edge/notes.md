# Move keys carry a pane past the page's edge — implementer evidence

Feature: `pane.moveLeft/Right/Up/Down` (Ctrl+Alt+arrows by default) used to answer "No pane in
that direction." when the focused pane was at the page's edge. Now the pane is carried past the
edge into a column (left/right) or row (up/down) of its own: appended to the root splitter when
it already runs that way, otherwise with the root wrapped. Implemented in `src/RelayWindow.h`
(`movePastPageEdge`, called from `moveActive` when `neighborOf` finds nothing), with the two
decisions in `src/PaneLayout.{h,cpp}` (`fillsTheEdge`, `sizesAfterEdgeDock`, unit-tested in
`tests/panelayout_test.cpp`) and descriptions in `src/Keymap.h`; `docs/ARCHITECTURE.md` updated.
The notice stays only for the tab's sole pane and a pane already filling that edge alone.

Run: `RELAY_QA_DISPLAY=:94 docs/qa_evidence/2026-09-19-pane-move-past-page-edge/drive.sh` (Xvfb +
xdotool + ImageMagick, isolated `XDG_CONFIG_HOME`, 1400x900). The focused pane reports its own
geometry (`stty size`, rows then columns) before and after each move; screenshots record the
layouts and the two notices.

## Results

Geometry, before -> after (rows columns):

| move | before | after | reading |
|---|---|---|---|
| Ctrl+Alt+Right, bottom of a stack in the rightmost column (the ask) | 13 45 | 40 43 | short stack pane -> full-height column of its own |
| Ctrl+Alt+Left, bottom of a stack in the leftmost column | 15 45 | 40 43 | same, mirrored |
| Ctrl+Alt+Up, left pane of a two-pane top row | 15 70 | 6 148 | half-width row pane -> full-width top row |
| Ctrl+Alt+Down, right pane of a two-pane bottom row | 15 70 | 6 148 | same, at the bottom |
| Ctrl+Alt+Right, a page that is one stack of two | 15 148 | 40 70 | root wrapped; full-height right column |

- The 40-row after-geometry is the page's full pane height and 148 its full width in this
  window, so "40 x" or "x 148" is "a region of its own" stated by the pane itself.
- Splitter arithmetic, machine-checked from the PNGs: in `03` the pre-move regions were
  ~910/~490 px, and `sizesAfterEdgeDock` predicts 606/328/466 (existing regions scaled, newcomer
  equal share) against dividers measured at ~585/~934 - the existing panes keep their relative
  sizes, the newcomer takes one equal share. (`06` is the mirror image.)
- `08` and `10` show full-width horizontal dividers only (no vertical ones): the extracted pane
  is a row spanning the page. `12` shows one divider at ~half width and no horizontal one.
- Notices, OCR'd from the screenshots: `04` "This pane already has that edge to itsel[f]" after
  the extracted pane pressed Ctrl+Alt+Right again; `13` "This pane is already the only pane in
  its tab." on a single-pane tab (wording shared with move-to-new-tab).
- `14-restored-after-relaunch`: after quitting with the pane alone, relaunch shows the same
  single pane - the shapes the move creates (a bare leaf directly under the root, a wrapped
  root) serialize and restore through the saved layout.
- `implementer-relay-stderr.log`: empty; no crash lines.

## Not covered here

- Dragging a pane off the page edge remains a non-drop (out of scope; the keyboard path is the
  feature). The Warp preset leaves the move keys unbound, as before.
- Full ctest during development had `buttonfit` failing on another session's in-flight theme
  edit (`data/theme/terminal.conf`, 8.5pt under the 9pt floor) and `board` failing only in the
  parallel full run (passes solo); neither touches this change's files. The `panes` suite
  (panelayout_test) is green with the two new slots.
