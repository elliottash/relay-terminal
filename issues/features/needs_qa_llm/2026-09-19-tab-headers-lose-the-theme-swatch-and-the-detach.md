---
id: JDC5
type: work
status: needs-qa-llm
labels: [feature, gui, tabs]
implemented_by: glm/glm-5.3
rank: zzzzzzzzzzi
created: '2026-09-19'
source: pane 1, 2026-09-19
links: {plans: [], commits: [], evidence: [docs/qa_evidence/2026-09-19-tab-header-cleanup/], related: [], github: null}
---
# Tab headers lose the theme swatch and the ⧉ detach button

## Issue
remove the thermometer thing and the detach button from tab headers

## What changed
`src/RelayWindow.h`: `paintTabSwatches()` and its Paint-event interception in `eventFilter()`
are gone (the "thermometer" — the per-tab theme swatch at each tab's left edge, its accent
colour filling the bottom 34%), and so is the ⧉ `tabDetachButton` block in
`placeTabBarControls()`. `moveTabToNewWindow` keeps its palette item and tab context menu;
per-tab themes themselves are untouched. `docs/ARCHITECTURE.md` no longer mentions either.
Details and screenshots: `docs/qa_evidence/2026-09-19-tab-header-cleanup/README.md`.

## QA checklist
- [ ] `scripts/relay-build --target relay` succeeds (implementer saw it pass in 51 s).
- [ ] Under Xvfb with an isolated `XDG_CONFIG_HOME`: two tabs open, pointer on a tab — no
      vertical tube at the left edge of any tab (the swatch's `border_strong` outline leaves
      zero pixels in a scan of the tab row) and no ⧉ between the state icon and the title;
      `docs/qa_evidence/2026-09-19-tab-header-cleanup/after-xvfb-two-tabs-hover.png`.
- [ ] The close cross still dims on unhovered tabs and keeps its "Close tab"/"Close window"
      tooltip; clicking it still closes the tab (last tab offers close-window).
- [ ] An attached tab still shows its project chip in the left slot and clicking it still
      detaches; the chip leaves no gap when a tab has no project.
- [ ] With `theme/per_tab` on and two tabs, `/dark` in one tab still rethemes only that tab,
      switching tabs still switches the theme, and the tab bar paints normally (no leftover
      paint hook: a tab drag or rename repaints cleanly).
- [ ] `tab.moveToNewWindow` still works from the palette and the tab context menu.
- [ ] `ctest --test-dir build -j 8`: the only failures are the other sessions' `wordwrap` and
      `buttonfit` (their uncommitted `src/WordWrap.*` / `src/Theme.cpp` edits; neither test
      compiles `RelayWindow.h`). `scripts/test.sh`: 3413 backend tests OK.
