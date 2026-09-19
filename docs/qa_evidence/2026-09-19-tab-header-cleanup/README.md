# Tab header cleanup: theme swatch and detach button removed

Owner ask (2026-09-19): "remove the thermometer thing and the detach button from tab headers".

- The "thermometer thing" was the per-tab **theme swatch**: `RelayWindow::paintTabSwatches()`
  painted a 6 px vertical tube at each tab's left edge — the theme's terminal ground with its
  accent colour filling the bottom 34%, outlined in `border_strong` — whenever per-tab themes
  were on and more than one tab was open. On the owner's live Dark Copper session that reads as
  an orange-filled thermometer (see `before-live-session.png`, tube at the left of both tabs).
- The **detach button** was the ⧉ `tabDetachButton` QToolButton `placeTabBarControls()` put in
  every tab's left box, glyph visible on the hovered tab, running `tab.moveToNewWindow`.

## What changed

`src/RelayWindow.h`:

- `paintTabSwatches()` deleted, its Paint-event interception in `eventFilter()` deleted, the
  `m_paintingTabSwatches` re-entrancy flag deleted. Per-tab themes themselves are untouched
  (`theme/per_tab`, `/light`, `/dark`, `/theme`, the saved `theme` field in the tab wrapper).
- The ⧉ block in `placeTabBarControls()` deleted (creation, tooltip, hover-glyph, enable state
  and the `tab.detach.mouse` hint). `moveTabToNewWindow` keeps its palette item and the tab
  context menu; the palette's project-detach item is a different action and stays.
- `tabLeftBox` now has the one caller and the one child it actually has left (the project chip
  of an attached tab, #916B); comments rewritten to say so. The Options row for
  `theme/per_tab` no longer promises a swatch.

`docs/ARCHITECTURE.md`: the Tabs bullet, the shortcut-hint trigger list, the project-chip
paragraph and the per-tab-themes paragraph no longer mention the ⧉ button or the swatch.

## Evidence

- `before-live-session.png` — the owner's live window (display :0) before the change: the tube
  with the accent fill at the left of each tab (orange in Dark Copper), pixel-sampled at
  (192,122,74) fill over (102,111,122) walls.
- `after-xvfb-two-tabs-hover.png` — `build/relay` (post-change, `scripts/relay-build --target
  relay`) under `Xvfb :99` with an isolated `XDG_CONFIG_HOME`, two tabs open (so the swatch's
  `count() > 1` gate was satisfied), pointer moved onto a tab (so the ⧉ hover glyph would have
  shown). A pixel scan of the tab row finds 0 pixels of the swatch's `border_strong` tube and
  no ⧉ between the state icon and the title; the only accent-coloured pixels left are the
  selected tab's `border-bottom` underline and text anti-aliasing fringes.

## Tests

- `ctest --test-dir build -j 8`: 61/63 pass. The two failures are not this change:
  - `wordwrap` — another session's uncommitted edits to `src/WordWrap.{h,cpp}` and
    `tests/wordwrap_test.cpp` (the reflow session; the same files also break
    `relay-wordwrap-tests_autogen`, the one target `scripts/relay-build` cannot build).
  - `buttonfit` — `stylesheetFontsStayAtOrAboveTheFloor()` fails in `setActiveTheme` for a
    builtin theme; `src/Theme.cpp` is modified by another session and the test never compiles
    `RelayWindow.h`.
- `scripts/test.sh`: 3413 backend tests, all OK.
